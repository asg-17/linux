// SPDX-License-Identifier: GPL-2.0-only

#ifndef ARCH_X86_KVM_VMX_VAC_H
#define ARCH_X86_KVM_VMX_VAC_H

#include <asm/vmx.h>

#include "../vac.h"
#include "vmcs.h"

void vac_set_vmxarea(struct vmcs *vmcs, int cpu);

struct vmcs *vac_get_vmxarea(int cpu);
int allocate_vpid(void);
void free_vpid(int);
void add_vmcs_to_loaded_vmcss_on_cpu(
		struct list_head *loaded_vmcss_on_cpu_link,
		int cpu);
void loaded_vmcs_clear(struct loaded_vmcs *loaded_vmcs);
int vmx_hardware_enable(void);
void vmx_hardware_disable(void);

#endif // ARCH_X86_KVM_VMX_VAC_H
