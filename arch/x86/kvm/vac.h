/* SPDX-License-Identifier: GPL-2.0 */

#ifndef ARCH_X86_KVM_VAC_H
#define ARCH_X86_KVM_VAC_H

#include <linux/user-return-notifier.h>

void kvm_spurious_fault(void);

#ifdef CONFIG_KVM_INTEL
bool kvm_is_vmx_supported(void);
int __init vac_vmx_init(void);
void vac_vmx_exit(void);
int vmx_hardware_enable(void);
void vmx_hardware_disable(void);
#else
bool kvm_is_vmx_supported(void) { return false }
int __init vac_vmx_init(void)
{
	return 0;
}
void vac_vmx_exit(void) {}
#endif

#ifdef CONFIG_KVM_AMD
bool kvm_is_svm_supported(void);
int __init vac_svm_init(void);
void vac_svm_exit(void);
int svm_hardware_enable(void);
void svm_hardware_disable(void);
#else
bool kvm_is_svm_supported(void) { return false }
int __init vac_svm_init(void)
{
	return 0;
}
void vac_svm_exit(void) {}
#endif


/*
 * Restoring the host value for MSRs that are only consumed when running in
 * usermode, e.g. SYSCALL MSRs and TSC_AUX, can be deferred until the CPU
 * returns to userspace, i.e. the kernel can run with the guest's value.
 */
#define KVM_MAX_NR_USER_RETURN_MSRS 16

struct kvm_user_return_msrs {
	struct user_return_notifier urn;
	bool registered;
	struct kvm_user_return_msr_values {
		u64 host;
		u64 curr;
	} values[KVM_MAX_NR_USER_RETURN_MSRS];
};

extern u32 __read_mostly kvm_nr_uret_msrs;

int kvm_add_user_return_msr(u32 msr);
int kvm_find_user_return_msr(u32 msr);
int kvm_set_user_return_msr(unsigned int slot, u64 value, u64 mask);
void kvm_on_user_return(struct user_return_notifier *urn);

static inline bool kvm_is_supported_user_return_msr(u32 msr)
{
	return kvm_find_user_return_msr(msr) >= 0;
}

#endif // ARCH_X86_KVM_VAC_H
