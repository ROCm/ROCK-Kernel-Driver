/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996, 1999, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 by Silicon Graphics
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/module.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/sn0/hub.h>
#include <asm/uaccess.h>

extern void dump_tlb_addr(unsigned long addr);
extern void dump_tlb_all(void);

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

extern spinlock_t modlist_lock;

static inline unsigned long
search_dbe_table(unsigned long addr)
{
	unsigned long ret;

#ifndef CONFIG_MODULES
	/* There is only the kernel to search.  */
	ret = search_one_table(__start___dbe_table, __stop___dbe_table-1, addr);
	return ret;
#else
	unsigned long flags;

	/* The kernel is the last "module" -- no need to treat it special.  */
	struct module *mp;
	struct archdata *ap;

	spin_lock_irqsave(&modlist_lock, flags);
	for (mp = module_list; mp != NULL; mp = mp->next) {
		if (!mod_member_present(mp, archdata_end) ||
        	    !mod_archdata_member_present(mp, struct archdata,
						 dbe_table_end))
			continue;
		ap = (struct archdata *)(mod->archdata_start);

		if (ap->dbe_table_start == NULL ||
		    !(mp->flags & (MOD_RUNNING | MOD_INITIALIZING)))
			continue;
		ret = search_one_table(ap->dbe_table_start,
				       ap->dbe_table_end - 1, addr);
		if (ret)
			break;
	}
	spin_unlock_irqrestore(&modlist_lock, flags);
	return ret;
#endif
}

void do_ibe(struct pt_regs *regs)
{
	printk("Got ibe at 0x%lx\n", regs->cp0_epc);
	show_regs(regs);
	dump_tlb_addr(regs->cp0_epc);
	force_sig(SIGBUS, current);
	while(1);
}

static void dump_hub_information(unsigned long errst0, unsigned long errst1)
{
	static char *err_type[2][8] = {
		{ NULL, "Uncached Partial Read PRERR", "DERR", "Read Timeout",
		  NULL, NULL, NULL, NULL },
		{ "WERR", "Uncached Partial Write", "PWERR", "Write Timeout",
		  NULL, NULL, NULL, NULL }
	};
	int wrb = errst1 & PI_ERR_ST1_WRBRRB_MASK;

	if (!(errst0 & PI_ERR_ST0_VALID_MASK)) {
		printk("Hub does not contain valid error information\n");
		return;
	}

	
	printk("Hub has valid error information:\n");
	if (errst0 & PI_ERR_ST0_OVERRUN_MASK)
		printk("Overrun is set.  Error stack may contain additional "
		       "information.\n");
	printk("Hub error address is %08lx\n",
	       (errst0 & PI_ERR_ST0_ADDR_MASK) >> (PI_ERR_ST0_ADDR_SHFT - 3));
	printk("Incoming message command 0x%lx\n",
	       (errst0 & PI_ERR_ST0_CMD_MASK) >> PI_ERR_ST0_CMD_SHFT);
	printk("Supplemental field of incoming message is 0x%lx\n",
	       (errst0 & PI_ERR_ST0_SUPPL_MASK) >> PI_ERR_ST0_SUPPL_SHFT);
	printk("T5 Rn (for RRB only) is 0x%lx\n",
	       (errst0 & PI_ERR_ST0_REQNUM_MASK) >> PI_ERR_ST0_REQNUM_SHFT);
	printk("Error type is %s\n", err_type[wrb]
	       [(errst0 & PI_ERR_ST0_TYPE_MASK) >> PI_ERR_ST0_TYPE_SHFT]
		? : "invalid");
}

void do_dbe(struct pt_regs *regs)
{
	unsigned long fixup, errst0, errst1;
	int cpu = LOCAL_HUB_L(PI_CPU_NUM);

	fixup = search_dbe_table(regs->cp0_epc);
	if (fixup) {
		long new_epc;

		new_epc = fixup_exception(dpf_reg, fixup, regs->cp0_epc);
		regs->cp0_epc = new_epc;
		return;
	}

	printk("Slice %c got dbe at 0x%lx\n", 'A' + cpu, regs->cp0_epc);
	printk("Hub information:\n");
	printk("ERR_INT_PEND = 0x%06lx\n", LOCAL_HUB_L(PI_ERR_INT_PEND));
	errst0 = LOCAL_HUB_L(cpu ? PI_ERR_STATUS0_B : PI_ERR_STATUS0_A);
	errst1 = LOCAL_HUB_L(cpu ? PI_ERR_STATUS1_B : PI_ERR_STATUS1_A);
	dump_hub_information(errst0, errst1);
	show_regs(regs);
	dump_tlb_all();
	while(1);
	force_sig(SIGBUS, current);
}

void __init
bus_error_init(void)
{
	/* XXX Initialize all the Hub & Bridge error handling here.  */
	int cpu = LOCAL_HUB_L(PI_CPU_NUM);
	int cpuoff = cpu << 8;

	set_except_vector(6, handle_ibe);
	set_except_vector(7, handle_dbe);

	LOCAL_HUB_S(PI_ERR_INT_PEND,
	            cpu ? PI_ERR_CLEAR_ALL_B : PI_ERR_CLEAR_ALL_A);
	LOCAL_HUB_S(PI_ERR_INT_MASK_A + cpuoff, 0);
	LOCAL_HUB_S(PI_ERR_STACK_ADDR_A + cpuoff, 0);
	LOCAL_HUB_S(PI_ERR_STACK_SIZE, 0);	/* Disable error stack */
	LOCAL_HUB_S(PI_SYSAD_ERRCHK_EN, PI_SYSAD_CHECK_ALL);
}
