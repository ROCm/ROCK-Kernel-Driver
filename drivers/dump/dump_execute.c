/*
 * The file has the common/generic dump execution code 
 *
 * Started: Oct 2002 -  Suparna Bhattacharya <suparna@in.ibm.com>
 * 	Split and rewrote high level dump execute code to make use 
 * 	of dump method interfaces.
 *
 * Derived from original code in dump_base.c created by 
 * 	Matt Robinson <yakker@sourceforge.net>)
 * 	
 * Copyright (C) 1999 - 2002 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001 - 2002 Matt D. Robinson.  All rights reserved.
 * Copyright (C) 2002 International Business Machines Corp. 
 *
 * Assumes dumper and dump config settings are in place
 * (invokes corresponding dumper specific routines as applicable)
 *
 * This code is released under version 2 of the GNU GPL.
 */
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/dump.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include "dump_methods.h"

struct notifier_block *dump_notifier_list; /* dump started/ended callback */

extern int panic_timeout;

/* Dump progress indicator */
void 
dump_speedo(int i)
{
	static const char twiddle[4] =  { '|', '\\', '-', '/' };
	printk("%c\b", twiddle[i&3]);
}

/* Make the device ready and write out the header */
int dump_begin(void)
{
	int err = 0;

	/* dump_dev = dump_config.dumper->dev; */
	dumper_reset();
	if ((err = dump_dev_silence())) {
		/* quiesce failed, can't risk continuing */
		/* Todo/Future: switch to alternate dump scheme if possible */
		printk("dump silence dev failed ! error %d\n", err);
		return err;
	}

	pr_debug("Writing dump header\n");
	if ((err = dump_update_header())) {
		printk("dump update header failed ! error %d\n", err);
		dump_dev_resume();
		return err;
	}

	dump_config.dumper->curr_offset = DUMP_BUFFER_SIZE;

	return 0;
}

/* 
 * Write the dump terminator, a final header update and let go of 
 * exclusive use of the device for dump.
 */
int dump_complete(void)
{
	int ret = 0;

	if (dump_config.level != DUMP_LEVEL_HEADER) {
		if ((ret = dump_update_end_marker())) {
			printk("dump update end marker error %d\n", ret);
		}
		if ((ret = dump_update_header())) {
			printk("dump update header error %d\n", ret);
		}
	}
	ret = dump_dev_resume();

	if ((panic_timeout > 0) && (!(dump_config.flags & (DUMP_FLAGS_SOFTBOOT | DUMP_FLAGS_NONDISRUPT)))) {
		printk(KERN_EMERG "Rebooting in %d seconds..",panic_timeout);
#ifdef CONFIG_SMP
		smp_send_stop();
#endif
		mdelay(panic_timeout * 1000);
		machine_restart(NULL);
	}

	return ret;
}

/* Saves all dump data */
int dump_execute_savedump(void)
{
	int ret = 0, err = 0;

	if ((ret = dump_begin()))  {
		return ret;
	}

	if (dump_config.level != DUMP_LEVEL_HEADER) { 
		ret = dump_sequencer();
	}
	if ((err = dump_complete())) {
		printk("Dump complete failed. Error %d\n", err);
	}

	return ret;
}

extern void dump_calc_bootmap_pages(void);

/* Does all the real work:  Capture and save state */
int dump_generic_execute(const char *panic_str, const struct pt_regs *regs)
{
	int ret = 0;

	if ((ret = dump_configure_header(panic_str, regs))) {
		printk("dump config header failed ! error %d\n", ret);
		return ret;	
	}

	dump_calc_bootmap_pages();
	/* tell interested parties that a dump is about to start */
	notifier_call_chain(&dump_notifier_list, DUMP_BEGIN, 
		&dump_config.dump_device);

	if (dump_config.level != DUMP_LEVEL_NONE)
		ret = dump_execute_savedump();

	pr_debug("dumped %ld blocks of %d bytes each\n", 
		dump_config.dumper->count, DUMP_BUFFER_SIZE);
	
	/* tell interested parties that a dump has completed */
	notifier_call_chain(&dump_notifier_list, DUMP_END, 
		&dump_config.dump_device);

	return ret;
}
