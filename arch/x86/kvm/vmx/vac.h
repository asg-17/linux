// SPDX-License-Identifier: GPL-2.0-only

#include <asm/vmx.h>

#include "../vac.h"
#include "vmcs.h"

void vac_set_vmxarea(struct vmcs *vmcs, int cpu);

struct vmcs *vac_get_vmxarea(int cpu);
int allocate_vpid(void);
void free_vpid(int);
