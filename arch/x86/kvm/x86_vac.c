/* SPDX-License-Identifier: GPL-2.0 */

#include "x86_vac.h"
#include <asm/msr.h>

struct vac_x86_ops vac_x86_ops __read_mostly;
#define VAC_X86_OP(func)					     \
	DEFINE_STATIC_CALL_NULL(vac_x86_##func,			     \
				*(((struct vac_x86_ops *)0)->func));
#define VAC_X86_OP_OPTIONAL VAC_X86_OP
#define VAC_X86_OP_OPTIONAL_RET0 VAC_X86_OP
#include "vac-x86-ops.h"

#ifdef KVM_VAC

static int __init vac_init(void)
{
	x86_vac_init();
	return 0;
}
module_init(vac_init);

static void __exit vac_exit(void)
{
	/*
	 * If module_init() is implemented, module_exit() must also be
	 * implemented to allow module unload.
	 */
}
module_exit(vac_exit);

#endif // KVM_VAC
 
u32 __read_mostly kvm_nr_uret_msrs;
EXPORT_SYMBOL_GPL(kvm_nr_uret_msrs);

static void kvm_on_user_return(struct user_return_notifier *urn)
{
	unsigned slot;
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

int kvm_set_user_return_msr(unsigned slot, u64 value, u64 mask)
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

void drop_user_return_notifiers(void)
{
	unsigned int cpu = smp_processor_id();
	struct kvm_user_return_msrs *msrs = per_cpu_ptr(user_return_msrs, cpu);

	if (msrs->registered)
		kvm_on_user_return(&msrs->urn);
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

int kvm_arch_hardware_enable(void)
{
	int ret;

	kvm_user_return_msr_cpu_online();

	ret = static_call(vac_x86_hardware_enable)();
	if (ret != 0)
		return ret;

	return 0;
}

void kvm_arch_hardware_disable(void)
{
	static_call(vac_x86_hardware_disable)();
	drop_user_return_notifiers();
}

void x86_vac_init(void)
{
#define __VAC_X86_OP(func) \
	static_call_update(vac_x86_##func, vac_x86_ops.func);
#define VAC_X86_OP(func) \
	WARN_ON(!vac_x86_ops.func); __VAC_X86_OP(func)
#define VAC_X86_OP_OPTIONAL __VAC_X86_OP
#define VAC_X86_OP_OPTIONAL_RET0(func) \
	static_call_update(vac_x86_##func, (void *)vac_x86_ops.func ? : \
					   (void *)__static_call_return0);
#include "vac-x86-ops.h"
#undef __VAC_X86_OP
}
