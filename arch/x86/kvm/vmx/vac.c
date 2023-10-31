// SPDX-License-Identifier: GPL-2.0-only

#include <asm/percpu.h>
#include <asm/reboot.h>
#include <linux/percpu-defs.h>

#include "vac.h"
#include "vmx_ops.h"
#include "posted_intr.h"

/*
 * We maintain a per-CPU linked-list of VMCS loaded on that CPU. This is needed
 * when a CPU is brought down, and we need to VMCLEAR all VMCSs loaded on it.
 */
static DEFINE_PER_CPU(struct list_head, loaded_vmcss_on_cpu);

static DEFINE_PER_CPU(struct vmcs *, vmxarea);

DEFINE_PER_CPU(struct vmcs *, current_vmcs);

void vac_set_vmxarea(struct vmcs *vmcs, int cpu) {
	per_cpu(vmxarea, cpu) = vmcs;
}

struct vmcs *vac_get_vmxarea(int cpu) {
	return per_cpu(vmxarea, cpu);
}

static DECLARE_BITMAP(vmx_vpid_bitmap, VMX_NR_VPIDS);
static DEFINE_SPINLOCK(vmx_vpid_lock);

static bool __kvm_is_vmx_supported(void)
{
	int cpu = smp_processor_id();

	if (!(cpuid_ecx(1) & feature_bit(VMX))) {
		pr_err("VMX not supported by CPU %d\n", cpu);
		return false;
	}

	if (!this_cpu_has(X86_FEATURE_MSR_IA32_FEAT_CTL) ||
	    !this_cpu_has(X86_FEATURE_VMX)) {
		pr_err("VMX not enabled (by BIOS) in MSR_IA32_FEAT_CTL on CPU %d\n", cpu);
		return false;
	}

	return true;
}

bool kvm_is_vmx_supported(void)
{
	bool supported;

	migrate_disable();
	supported = __kvm_is_vmx_supported();
	migrate_enable();

	return supported;
}

int allocate_vpid(void)
{
        int vpid;

        if (!enable_vpid)
                return 0;
        spin_lock(&vmx_vpid_lock);
        vpid = find_first_zero_bit(vmx_vpid_bitmap, VMX_NR_VPIDS);
        if (vpid < VMX_NR_VPIDS)
                __set_bit(vpid, vmx_vpid_bitmap);
        else
                vpid = 0;
        spin_unlock(&vmx_vpid_lock);
        return vpid;
}

void free_vpid(int vpid)
{
        if (!enable_vpid || vpid == 0)
                return;
        spin_lock(&vmx_vpid_lock);
        __clear_bit(vpid, vmx_vpid_bitmap);
        spin_unlock(&vmx_vpid_lock);
}

void add_vmcs_to_loaded_vmcss_on_cpu(
		struct list_head *loaded_vmcss_on_cpu_link,
		int cpu)
{
	list_add(loaded_vmcss_on_cpu_link, &per_cpu(loaded_vmcss_on_cpu, cpu));
}

static void __loaded_vmcs_clear(void *arg)
{
	struct loaded_vmcs *loaded_vmcs = arg;
	int cpu = raw_smp_processor_id();

	if (loaded_vmcs->cpu != cpu)
		return; /* vcpu migration can race with cpu offline */
	if (per_cpu(current_vmcs, cpu) == loaded_vmcs->vmcs)
		per_cpu(current_vmcs, cpu) = NULL;

	vmcs_clear(loaded_vmcs->vmcs);
	if (loaded_vmcs->shadow_vmcs && loaded_vmcs->launched)
		vmcs_clear(loaded_vmcs->shadow_vmcs);

	list_del(&loaded_vmcs->loaded_vmcss_on_cpu_link);

	/*
	 * Ensure all writes to loaded_vmcs, including deleting it from its
	 * current percpu list, complete before setting loaded_vmcs->cpu to
	 * -1, otherwise a different cpu can see loaded_vmcs->cpu == -1 first
	 * and add loaded_vmcs to its percpu list before it's deleted from this
	 * cpu's list. Pairs with the smp_rmb() in vmx_vcpu_load_vmcs().
	 */
	smp_wmb();

	loaded_vmcs->cpu = -1;
	loaded_vmcs->launched = 0;
}

void loaded_vmcs_clear(struct loaded_vmcs *loaded_vmcs)
{
	int cpu = loaded_vmcs->cpu;

	if (cpu != -1)
		smp_call_function_single(cpu,
			 __loaded_vmcs_clear, loaded_vmcs, 1);

}

static int kvm_cpu_vmxon(u64 vmxon_pointer)
{
	u64 msr;

	cr4_set_bits(X86_CR4_VMXE);

	asm_volatile_goto("1: vmxon %[vmxon_pointer]\n\t"
			  _ASM_EXTABLE(1b, %l[fault])
			  : : [vmxon_pointer] "m"(vmxon_pointer)
			  : : fault);
	return 0;

fault:
	WARN_ONCE(1, "VMXON faulted, MSR_IA32_FEAT_CTL (0x3a) = 0x%llx\n",
		  rdmsrl_safe(MSR_IA32_FEAT_CTL, &msr) ? 0xdeadbeef : msr);
	cr4_clear_bits(X86_CR4_VMXE);

	return -EFAULT;
}

int vmx_hardware_enable(void)
{
	int cpu = raw_smp_processor_id();
	u64 phys_addr = __pa(vac_get_vmxarea(cpu));
	int r;

	if (cr4_read_shadow() & X86_CR4_VMXE)
		return -EBUSY;

	/*
	 * This can happen if we hot-added a CPU but failed to allocate
	 * VP assist page for it.
	 */
	if (kvm_is_using_evmcs() && !hv_get_vp_assist_page(cpu))
		return -EFAULT;

	intel_pt_handle_vmx(1);

	r = kvm_cpu_vmxon(phys_addr);
	if (r) {
		intel_pt_handle_vmx(0);
		return r;
	}

	if (enable_ept)
		ept_sync_global();

	return 0;
}

static void vmclear_local_loaded_vmcss(void)
{
	int cpu = raw_smp_processor_id();
	struct loaded_vmcs *v, *n;

	list_for_each_entry_safe(v, n, &per_cpu(loaded_vmcss_on_cpu, cpu),
				 loaded_vmcss_on_cpu_link)
		__loaded_vmcs_clear(v);
}

/*
 * Disable VMX and clear CR4.VMXE (even if VMXOFF faults)
 *
 * Note, VMXOFF causes a #UD if the CPU is !post-VMXON, but it's impossible to
 * atomically track post-VMXON state, e.g. this may be called in NMI context.
 * Eat all faults as all other faults on VMXOFF faults are mode related, i.e.
 * faults are guaranteed to be due to the !post-VMXON check unless the CPU is
 * magically in RM, VM86, compat mode, or at CPL>0.
 */
static int kvm_cpu_vmxoff(void)
{
	asm_volatile_goto("1: vmxoff\n\t"
			  _ASM_EXTABLE(1b, %l[fault])
			  ::: "cc", "memory" : fault);

	cr4_clear_bits(X86_CR4_VMXE);
	return 0;

fault:
	cr4_clear_bits(X86_CR4_VMXE);
	return -EIO;
}

static void vmx_emergency_disable(void)
{
	int cpu = raw_smp_processor_id();
	struct loaded_vmcs *v;

	kvm_rebooting = true;

	/*
	 * Note, CR4.VMXE can be _cleared_ in NMI context, but it can only be
	 * set in task context.  If this races with VMX is disabled by an NMI,
	 * VMCLEAR and VMXOFF may #UD, but KVM will eat those faults due to
	 * kvm_rebooting set.
	 */
	if (!(__read_cr4() & X86_CR4_VMXE))
		return;

	list_for_each_entry(v, &per_cpu(loaded_vmcss_on_cpu, cpu),
			    loaded_vmcss_on_cpu_link)
		vmcs_clear(v->vmcs);

	kvm_cpu_vmxoff();
}

void vmx_hardware_disable(void)
{
	vmclear_local_loaded_vmcss();

	if (kvm_cpu_vmxoff())
		kvm_spurious_fault();

	intel_pt_handle_vmx(0);
}

int __init vac_vmx_init(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		INIT_LIST_HEAD(&per_cpu(loaded_vmcss_on_cpu, cpu));

		pi_init_cpu(cpu);
	}

	cpu_emergency_register_virt_callback(vmx_emergency_disable);

	return 0;
}

void vac_vmx_exit(void)
{
	cpu_emergency_unregister_virt_callback(vmx_emergency_disable);
}
