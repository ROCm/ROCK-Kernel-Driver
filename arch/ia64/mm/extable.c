/*
 * Kernel exception handling table support.  Derived from arch/alpha/mm/extable.c.
 *
 * Copyright (C) 1998, 1999, 2001 Hewlett-Packard Co
 * Copyright (C) 1998, 1999, 2001 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>

extern const struct exception_table_entry __start___ex_table[];
extern const struct exception_table_entry __stop___ex_table[];

static inline const struct exception_table_entry *
search_one_table (const struct exception_table_entry *first,
		  const struct exception_table_entry *last,
		  signed long value)
{
	/* Abort early if the search value is out of range.  */
	if (value != (signed int)value)
		return 0;

        while (first <= last) {
		const struct exception_table_entry *mid;
		long diff;
		/*
		 * We know that first and last are both kernel virtual
		 * pointers (region 7) so first+last will cause an
		 * overflow.  We fix that by calling __va() on the
		 * result, which will ensure that the top two bits get
		 * set again.
		 */
		mid = (void *) __va((((__u64) first + (__u64) last)/2/sizeof(*mid))*sizeof(*mid));
		diff = mid->addr - value;
                if (diff == 0)
                        return mid;
                else if (diff < 0)
                        first = mid+1;
                else
                        last = mid-1;
        }
        return 0;
}

#ifndef CONFIG_MODULE
register unsigned long main_gp __asm__("gp");
#endif

struct exception_fixup
search_exception_table (unsigned long addr)
{
	const struct exception_table_entry *entry;
	struct exception_fixup fix = { 0 };

#ifndef CONFIG_MODULE
	/* There is only the kernel to search.  */
	entry = search_one_table(__start___ex_table, __stop___ex_table - 1, addr - main_gp);
	if (entry)
		fix.cont = entry->cont + main_gp;
	return fix;
#else
	struct exception_table_entry *ret;
	/* The kernel is the last "module" -- no need to treat it special. */
	struct module *mp;

	for (mp = module_list; mp ; mp = mp->next) {
		if (!mp->ex_table_start)
			continue;
		entry = search_one_table(mp->ex_table_start, mp->ex_table_end - 1, addr - mp->gp);
		if (entry) {
			fix.cont = entry->cont + mp->gp;
			return fix;
		}
	}
#endif
	return fix;
}

void
handle_exception (struct pt_regs *regs, struct exception_fixup fix)
{
	regs->r8 = -EFAULT;
	if (fix.cont & 4)
		regs->r9 = 0;
	regs->cr_iip = (long) fix.cont & ~0xf;
	ia64_psr(regs)->ri = fix.cont & 0x3;		/* set continuation slot number */
}
