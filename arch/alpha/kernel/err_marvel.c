/*
 *	linux/arch/alpha/kernel/err_marvel.c
 *
 *	Copyright (C) 2001 Jeff Wiedemeier (Compaq Computer Corporation)
 *
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/sched.h>

#include <asm/io.h>
#include <asm/console.h>
#include <asm/core_marvel.h>
#include <asm/hwrpb.h>
#include <asm/smp.h>

#include "err_impl.h"
#include "proto.h"


#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_MARVEL)

void
marvel_machine_check(u64 vector, u64 la_ptr, struct pt_regs *regs)
{
	struct el_subpacket *el_ptr = (struct el_subpacket *)la_ptr;

	/*
	 * Sync the processor
	 */
	mb();
	draina();

srm_printk("MARVEL MACHINE CHECK!!!\n");  /* HACK */
	el_process_subpacket(el_ptr);
srm_printk("...PROCESSED\n");		  /* HACK */

	switch(vector) {
	case SCB_Q_SYSEVENT:
		printk(KERN_CRIT "MARVEL SYSEVENT %ld\n", vector);
		break;
	case SCB_Q_SYSMCHK:
	case SCB_Q_SYSERR:
		printk(KERN_CRIT "MARVEL SYSMCHK/ERR %ld\n", vector);
		break;
	default:	/* don't know it - pass it up */
		return ev7_machine_check(vector, la_ptr, regs);
	}	

        /* 
	 * Release the logout frame 
	 */
	wrmces(0x7);
	mb();
}

void
marvel_register_error_handlers(void)
{
	ev7_register_error_handlers();
}
#endif /* CONFIG_ALPHA_GENERIC || CONFIG_ALPHA_MARVEL */
