/* $Id: ip22-berr.c,v 1.1 2000/01/21 22:34:03 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1999, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 by Silicon Graphics
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include <asm/paccess.h>
#include <asm/addrspace.h>
#include <asm/ptrace.h>

extern asmlinkage void handle_ibe(void);
extern asmlinkage void handle_dbe(void);

extern const struct exception_table_entry __start___dbe_table[];
extern const struct exception_table_entry __stop___dbe_table[];

static inline unsigned long
search_one_table(const struct exception_table_entry *first,
                 const struct exception_table_entry *last,
                 unsigned long value)
{
	while (first <= last) {
		const struct exception_table_entry *mid;
		long diff;

		mid = (last - first) / 2 + first;
		diff = mid->insn - value;
		if (diff == 0)
			return mid->nextinsn;
		else if (diff < 0)
			first = mid+1;
		else
			last = mid-1;
	}
	return 0;
}

static inline unsigned long
search_dbe_table(unsigned long addr)
{
	unsigned long ret;

	/* There is only the kernel to search.  */
	ret = search_one_table(__start___dbe_table, __stop___dbe_table-1, addr);
	if (ret) return ret;

	return 0;
}

void do_ibe(struct pt_regs *regs)
{
	printk("Got ibe at 0x%lx\n", regs->cp0_epc);
	show_regs(regs);
	dump_tlb_addr(regs->cp0_epc);
	force_sig(SIGBUS, current);
	while(1);
}

void do_dbe(struct pt_regs *regs)
{
	unsigned long fixup;

	fixup = search_dbe_table(regs->cp0_epc);
	if (fixup) {
		long new_epc;

		new_epc = fixup_exception(dpf_reg, fixup, regs->cp0_epc);
		regs->cp0_epc = new_epc;
		return;
	}

	printk("Got dbe at 0x%lx\n", regs->cp0_epc);
	show_regs(regs);
	dump_tlb_all();
	while(1);
	force_sig(SIGBUS, current);
}

void __init
bus_error_init(void)
{
	int dummy;

	set_except_vector(6, handle_ibe);
	set_except_vector(7, handle_dbe);

	/* At this time nothing uses the DBE protection mechanism on the
	   Indy, so this here is needed to make the kernel link.  */
	get_dbe(dummy, (int *)KSEG0);
}
