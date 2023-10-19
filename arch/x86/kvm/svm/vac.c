// SPDX-License-Identifier: GPL-2.0-only

#include <linux/percpu-defs.h>

#include "vac.h"

DEFINE_PER_CPU(struct svm_cpu_data, svm_data);
