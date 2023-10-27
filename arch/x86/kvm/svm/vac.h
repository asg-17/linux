// SPDX-License-Identifier: GPL-2.0-only

#ifndef ARCH_X86_KVM_SVM_VAC_H
#define ARCH_X86_KVM_SVM_VAC_H

#include "../vac.h"
#include "svm_data.h"

static int tsc_scaling = true;

/*
 * Set osvw_len to higher value when updated Revision Guides
 * are published and we know what the new status bits are
 */
static uint64_t osvw_len = 4, osvw_status;

int svm_hardware_enable(void);
void svm_hardware_disable(void);

#endif // ARCH_X86_KVM_SVM_VAC_H
