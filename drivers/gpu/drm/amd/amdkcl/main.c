/* SPDX-License-Identifier: MIT */
#include <linux/kernel.h>
#include <linux/module.h>

int __init amdkcl_init(void)
{
	return 0;
}
module_init(amdkcl_init);

void __exit amdkcl_exit(void)
{
}

module_exit(amdkcl_exit);

MODULE_AUTHOR("AMD linux driver team");
MODULE_DESCRIPTION("Module for OS kernel compatible layer");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
