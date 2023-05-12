// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include "vac.h"

#ifdef CONFIG_HAVE_KVM_VAC
MODULE_LICENSE("GPL");

static int __init vac_init(void)
{
	return 0;
}
module_init(vac_init);

static void __exit vac_exit(void)
{
}
module_exit(vac_exit);

#endif // CONFIG_HAVE_KVM_VAC
