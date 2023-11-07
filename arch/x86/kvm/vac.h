/* SPDX-License-Identifier: GPL-2.0 */

#ifndef ARCH_X86_KVM_VAC_H
#define ARCH_X86_KVM_VAC_H

#include <linux/user-return-notifier.h>

int __init vac_init(void);
void vac_exit(void);

#ifdef CONFIG_KVM_INTEL
int __init vac_vmx_init(void);
void vac_vmx_exit(void);
#else
int __init vac_vmx_init(void)
{
	return 0;
}
void vac_vmx_exit(void) {}
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

int kvm_alloc_user_return_msrs(void);
void kvm_free_user_return_msrs(void);
int kvm_add_user_return_msr(u32 msr);
int kvm_find_user_return_msr(u32 msr);
int kvm_set_user_return_msr(unsigned int slot, u64 value, u64 mask);
void kvm_on_user_return(struct user_return_notifier *urn);
void kvm_user_return_msr_cpu_online(void);
void drop_user_return_notifiers(void);

static inline bool kvm_is_supported_user_return_msr(u32 msr)
{
	return kvm_find_user_return_msr(msr) >= 0;
}

#endif // ARCH_X86_KVM_VAC_H
