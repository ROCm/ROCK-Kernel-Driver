/* SPDX-License-Identifier: MIT */
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/printk.h>
#include <linux/bug.h>

static unsigned long _kcl_kallsyms_lookup_name(const char *name)
{
	unsigned long addr = 0;
#ifndef HAVE_KALLSYMS_LOOKUP_NAME
	struct kprobe kp;
	int r;

	memset(&kp, 0, sizeof(kp));
	kp.symbol_name = name;
	r = register_kprobe(&kp);
	if (!r) {
		addr = (unsigned long)kp.addr;
		unregister_kprobe(&kp);
	}
#else
	addr = kallsyms_lookup_name(name);
#endif

	return addr;
}

void *amdkcl_fp_setup(const char *symbol, void *dummy)
{
       unsigned long addr;
       void *fp = dummy;

       addr = _kcl_kallsyms_lookup_name(symbol);
       if (addr == 0) {
	       if (fp)
		       pr_warn("Warning: fail to get symbol %s, replace it with kcl stub\n", symbol);
	       else {
		       pr_err("Error: fail to get symbol %s, abort...\n", symbol);
		       BUG();
	       }
       } else {
	       fp = (void *)addr;
       }

       return fp;
}

