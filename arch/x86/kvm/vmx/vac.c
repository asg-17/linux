// SPDX-License-Identifier: GPL-2.0-only

#include <asm/percpu.h>
#include <linux/percpu-defs.h>

#include "vac.h"


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
