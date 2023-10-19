// SPDX-License-Identifier: GPL-2.0-only

#ifndef ARCH_X86_KVM_SVM_VAC_H
#define ARCH_X86_KVM_SVM_VAC_H

#include "../vac.h"

struct svm_cpu_data {
	u64 asid_generation;
	u32 max_asid;
	u32 next_asid;
	u32 min_asid;

	struct page *save_area;
	unsigned long save_area_pa;

	struct vmcb *current_vmcb;

	/* index = sev_asid, value = vmcb pointer */
	struct vmcb **sev_vmcbs;
};

#endif // ARCH_X86_KVM_SVM_VAC_H
