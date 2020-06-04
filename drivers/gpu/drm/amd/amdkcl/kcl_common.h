/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_COMMON_H
#define AMDKCL_COMMON_H

/*
 * kallsyms_lookup_name has been exported in version 2.6.33
 */
#include <linux/version.h>
#include <linux/kallsyms.h>
#include <linux/bug.h>

static inline void *amdkcl_fp_setup(const char *symbol, void *fp_stup)
{
	unsigned long addr;
	void *fp = NULL;

	addr = kallsyms_lookup_name(symbol);
	if (addr == 0) {
		fp = fp_stup;
		if (fp != NULL)
			printk_once(KERN_WARNING "Warning: fail to get symbol %s, replace it with kcl stub\n", symbol);
		else {
			printk_once(KERN_ERR "Error: fail to get symbol %s\n", symbol);
			BUG();
		}
	} else {
		fp = (void *)addr;
	}

	return fp;
}

/*
 * create dummy func
 */
#define amdkcl_dummy_symbol(name, ret_type, ret, ...) \
ret_type name(__VA_ARGS__) \
{ \
	pr_warn_once("%s is not supported\n", #name); \
	ret ;\
} \
EXPORT_SYMBOL(name);

#endif
