/*
 * Kernel exception handling table support.  Derived from arch/alpha/mm/extable.c.
 *
 * Copyright (C) 1998, 1999, 2001-2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>

#include <asm/uaccess.h>
#include <asm/module.h>

const struct exception_table_entry *
search_extable (const struct exception_table_entry *first,
		const struct exception_table_entry *last,
		unsigned long ip)
{
	const struct exception_table_entry *mid;
	unsigned long mid_ip;
	long diff;

        while (first <= last) {
		mid = &first[(last - first)/2];
		mid_ip = (u64) &mid->addr + mid->addr;
		diff = mid_ip - ip;
                if (diff == 0)
                        return mid;
                else if (diff < 0)
                        first = mid + 1;
                else
                        last = mid - 1;
        }
        return 0;
}

void
handle_exception (struct pt_regs *regs, const struct exception_table_entry *e)
{
	long fix = (u64) &e->cont + e->cont;

	regs->r8 = -EFAULT;
	if (fix & 4)
		regs->r9 = 0;
	regs->cr_iip = fix & ~0xf;
	ia64_psr(regs)->ri = fix & 0x3;		/* set continuation slot number */
}
