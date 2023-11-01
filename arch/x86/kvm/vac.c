// SPDX-License-Identifier: GPL-2.0-only

#include "vac.h"
#include <asm/msr.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");

extern bool kvm_rebooting;

u32 __read_mostly kvm_uret_msrs_list[KVM_MAX_NR_USER_RETURN_MSRS];
struct kvm_user_return_msrs __percpu *user_return_msrs;
EXPORT_SYMBOL_GPL(user_return_msrs);

u32 __read_mostly kvm_nr_uret_msrs;
EXPORT_SYMBOL_GPL(kvm_nr_uret_msrs);

static void kvm_on_user_return(struct user_return_notifier *urn)
{
	unsigned int slot;
	struct kvm_user_return_msrs *msrs
		= container_of(urn, struct kvm_user_return_msrs, urn);
	struct kvm_user_return_msr_values *values;
	unsigned long flags;

	/*
	 * Disabling irqs at this point since the following code could be
	 * interrupted and executed through kvm_arch_hardware_disable()
	 */
	local_irq_save(flags);
	if (msrs->registered) {
		msrs->registered = false;
		user_return_notifier_unregister(urn);
	}
	local_irq_restore(flags);
	for (slot = 0; slot < kvm_nr_uret_msrs; ++slot) {
		values = &msrs->values[slot];
		if (values->host != values->curr) {
			wrmsrl(kvm_uret_msrs_list[slot], values->host);
			values->curr = values->host;
		}
	}
}

static void kvm_user_return_msr_cpu_online(void)
{
	unsigned int cpu = smp_processor_id();
	struct kvm_user_return_msrs *msrs = per_cpu_ptr(user_return_msrs, cpu);
	u64 value;
	int i;

	for (i = 0; i < kvm_nr_uret_msrs; ++i) {
		rdmsrl_safe(kvm_uret_msrs_list[i], &value);
		msrs->values[i].host = value;
		msrs->values[i].curr = value;
	}
}

static void drop_user_return_notifiers(void)
{
	unsigned int cpu = smp_processor_id();
	struct kvm_user_return_msrs *msrs = per_cpu_ptr(user_return_msrs, cpu);

	if (msrs->registered)
		kvm_on_user_return(&msrs->urn);
}

static int kvm_probe_user_return_msr(u32 msr)
{
	u64 val;
	int ret;

	preempt_disable();
	ret = rdmsrl_safe(msr, &val);
	if (ret)
		goto out;
	ret = wrmsrl_safe(msr, val);
out:
	preempt_enable();
	return ret;
}

int kvm_add_user_return_msr(u32 msr)
{
	BUG_ON(kvm_nr_uret_msrs >= KVM_MAX_NR_USER_RETURN_MSRS);

	if (kvm_probe_user_return_msr(msr))
		return -1;
	if (kvm_find_user_return_msr(msr) != -1)
		return -1;

	kvm_uret_msrs_list[kvm_nr_uret_msrs] = msr;
	return kvm_nr_uret_msrs++;
}
EXPORT_SYMBOL_GPL(kvm_add_user_return_msr);

int kvm_find_user_return_msr(u32 msr)
{
	int i;

	for (i = 0; i < kvm_nr_uret_msrs; ++i) {
		if (kvm_uret_msrs_list[i] == msr)
			return i;
	}
	return -1;
}
EXPORT_SYMBOL_GPL(kvm_find_user_return_msr);

int kvm_set_user_return_msr(unsigned int slot, u64 value, u64 mask)
{
	unsigned int cpu = smp_processor_id();
	struct kvm_user_return_msrs *msrs = per_cpu_ptr(user_return_msrs, cpu);
	int err;

	value = (value & mask) | (msrs->values[slot].host & ~mask);
	if (value == msrs->values[slot].curr)
		return 0;
	err = wrmsrl_safe(kvm_uret_msrs_list[slot], value);
	if (err)
		return 1;

	msrs->values[slot].curr = value;
	if (!msrs->registered) {
		msrs->urn.on_user_return = kvm_on_user_return;
		user_return_notifier_register(&msrs->urn);
		msrs->registered = true;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_user_return_msr);

int kvm_arch_hardware_enable(void)
{
	int ret = -EIO;

	kvm_user_return_msr_cpu_online();

	if (kvm_is_vmx_supported())
		ret = vmx_hardware_enable();
	else if (kvm_is_svm_supported())
		ret = svm_hardware_enable();
	if (ret != 0)
		return ret;

	// TODO: Handle unstable TSC

	return 0;
}

void kvm_arch_hardware_disable(void)
{
	if (kvm_is_vmx_supported())
		vmx_hardware_disable();
	else if (kvm_is_svm_supported())
		svm_hardware_disable();
	drop_user_return_notifiers();
}

/*
 * Handle a fault on a hardware virtualization (VMX or SVM) instruction.
 *
 * Hardware virtualization extension instructions may fault if a reboot turns
 * off virtualization while processes are running.  Usually after catching the
 * fault we just panic; during reboot instead the instruction is ignored.
 */
noinstr void kvm_spurious_fault(void)
{
	/* Fault while not rebooting.  We want the trace. */
	BUG_ON(!kvm_rebooting);
}
EXPORT_SYMBOL_GPL(kvm_spurious_fault);

int __init vac_init(void)
{
	int r;
	r = vac_vmx_init();
	return r;
}
module_init(vac_init);

void __exit vac_exit(void)
{
	vac_vmx_exit();
}
module_exit(vac_exit);
