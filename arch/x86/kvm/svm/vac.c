// SPDX-License-Identifier: GPL-2.0-only

#include <asm/reboot.h>
#include <asm/svm.h>
#include <linux/percpu-defs.h>

#include "svm_ops.h"
#include "vac.h"

DEFINE_PER_CPU(struct svm_cpu_data, svm_data);
EXPORT_SYMBOL_GPL(svm_data);

unsigned int max_sev_asid;
EXPORT_SYMBOL_GPL(max_sev_asid);

static bool __kvm_is_svm_supported(void)
{
	int cpu = smp_processor_id();
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	u64 vm_cr;

	if (c->x86_vendor != X86_VENDOR_AMD &&
	    c->x86_vendor != X86_VENDOR_HYGON) {
		pr_err("CPU %d isn't AMD or Hygon\n", cpu);
		return false;
	}

	if (!cpu_has(c, X86_FEATURE_SVM)) {
		pr_err("SVM not supported by CPU %d\n", cpu);
		return false;
	}

	if (cc_platform_has(CC_ATTR_GUEST_MEM_ENCRYPT)) {
		pr_info("KVM is unsupported when running as an SEV guest\n");
		return false;
	}

	rdmsrl(MSR_VM_CR, vm_cr);
	if (vm_cr & (1 << SVM_VM_CR_SVM_DISABLE)) {
		pr_err("SVM disabled (by BIOS) in MSR_VM_CR on CPU %d\n", cpu);
		return false;
	}

	return true;
}

bool kvm_is_svm_supported(void)
{
	bool supported;

	migrate_disable();
	supported = __kvm_is_svm_supported();
	migrate_enable();

	return supported;
}
EXPORT_SYMBOL_GPL(kvm_is_svm_supported);

static inline void kvm_cpu_svm_disable(void)
{
	uint64_t efer;

	wrmsrl(MSR_VM_HSAVE_PA, 0);
	rdmsrl(MSR_EFER, efer);
	if (efer & EFER_SVME) {
		/*
		 * Force GIF=1 prior to disabling SVM, e.g. to ensure INIT and
		 * NMI aren't blocked.
		 */
		stgi();
		wrmsrl(MSR_EFER, efer & ~EFER_SVME);
	}
}

static void svm_emergency_disable(void)
{
	kvm_rebooting = true;

	kvm_cpu_svm_disable();
}

void svm_hardware_disable(void)
{
	/* Make sure we clean up behind us */
	// TODO: Fix everything TSC
	 if (tsc_scaling)
		// __svm_write_tsc_multiplier(SVM_TSC_RATIO_DEFAULT);

	kvm_cpu_svm_disable();

	amd_pmu_disable_virt();
}
EXPORT_SYMBOL_GPL(svm_hardware_disable);

int svm_hardware_enable(void)
{

	struct svm_cpu_data *sd;
	uint64_t efer;
	int me = raw_smp_processor_id();

	rdmsrl(MSR_EFER, efer);
	if (efer & EFER_SVME)
		return -EBUSY;

	sd = per_cpu_ptr(&svm_data, me);
	sd->asid_generation = 1;
	sd->max_asid = cpuid_ebx(SVM_CPUID_FUNC) - 1;
	sd->next_asid = sd->max_asid + 1;
	sd->min_asid = max_sev_asid + 1;

	wrmsrl(MSR_EFER, efer | EFER_SVME);

	wrmsrl(MSR_VM_HSAVE_PA, sd->save_area_pa);

	if (static_cpu_has(X86_FEATURE_TSCRATEMSR)) {
		/*
		 * Set the default value, even if we don't use TSC scaling
		 * to avoid having stale value in the msr
		 */
		// TODO: Fix everything TSC
		// __svm_write_tsc_multiplier(SVM_TSC_RATIO_DEFAULT);
	}


	/*
	 * Get OSVW bits.
	 *
	 * Note that it is possible to have a system with mixed processor
	 * revisions and therefore different OSVW bits. If bits are not the same
	 * on different processors then choose the worst case (i.e. if erratum
	 * is present on one processor and not on another then assume that the
	 * erratum is present everywhere).
	 */
	if (cpu_has(&boot_cpu_data, X86_FEATURE_OSVW)) {
		uint64_t len, status = 0;
		int err;

		len = native_read_msr_safe(MSR_AMD64_OSVW_ID_LENGTH, &err);
		if (!err)
			status = native_read_msr_safe(MSR_AMD64_OSVW_STATUS,
						      &err);

		if (err)
			osvw_status = osvw_len = 0;
		else {
			if (len < osvw_len)
				osvw_len = len;
			osvw_status |= status;
			osvw_status &= (1ULL << osvw_len) - 1;
		}
	} else
		osvw_status = osvw_len = 0;

	amd_pmu_enable_virt();

	return 0;
}
EXPORT_SYMBOL_GPL(svm_hardware_enable);

int __init vac_svm_init(void)
{
	cpu_emergency_register_virt_callback(svm_emergency_disable);

	return 0;
}

void __exit vac_svm_exit(void)
{
	cpu_emergency_unregister_virt_callback(svm_emergency_disable);
}
