/*
 * Copyright (C) 1996 Universidade de Lisboa
 * 
 * Written by Pedro Roque Marques (roque@di.fc.ul.pt)
 *
 * This software may be used and distributed according to the terms of 
 * the GNU Public License, incorporated herein by reference.
 */

/*        
 *        PCBIT-D module support
 */

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/tqueue.h>
#include <linux/skbuff.h>

#include <linux/isdnif.h>
#include "pcbit.h"

static int mem[MAX_PCBIT_CARDS] = {0, };
static int irq[MAX_PCBIT_CARDS] = {0, };

static int num_boards;
struct pcbit_dev * dev_pcbit[MAX_PCBIT_CARDS] = {0, 0, 0, 0};

int init_module(void);
void cleanup_module(void);

extern void pcbit_terminate(int board);
extern int pcbit_init_dev(int board, int mem_base, int irq);

#ifdef MODULE
MODULE_PARM(mem, "1-" __MODULE_STRING(MAX_PCBIT_CARDS) "i");
MODULE_PARM(irq, "1-" __MODULE_STRING(MAX_PCBIT_CARDS) "i");
#define pcbit_init init_module
#endif

int pcbit_init(void)
{
	int board;

	num_boards = 0;

	printk(KERN_NOTICE 
	       "PCBIT-D device driver v 0.5-fjpc0 19991204 - "
	       "Copyright (C) 1996 Universidade de Lisboa\n");

	if (mem[0] || irq[0]) 
	{
		for (board=0; board < MAX_PCBIT_CARDS && mem[board] && irq[board]; board++)
		{
			if (!mem[board])
				mem[board] = 0xD0000;
			if (!irq[board])
				irq[board] = 5;
			
			if (pcbit_init_dev(board, mem[board], irq[board]) == 0)
				num_boards++;
		
			else 
			{
				printk(KERN_WARNING 
				       "pcbit_init failed for dev %d", 
				       board + 1);
				return -EIO;
			}
		}
	}

	/* Hardcoded default settings detection */

	if (!num_boards)
	{
		printk(KERN_INFO 
		       "Trying to detect board using default settings\n");
		if (pcbit_init_dev(0, 0xD0000, 5) == 0)
			num_boards++;
		else
			return -EIO;
	}

	/* No symbols to export, hide all symbols */
	EXPORT_NO_SYMBOLS;

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	int board;

	for (board = 0; board < num_boards; board++)
		pcbit_terminate(board);
	printk(KERN_NOTICE 
	       "PCBIT-D module unloaded\n");
}

#else
#define MAX_PARA	(MAX_PCBIT_CARDS * 2)
#include <linux/init.h>
static int __init pcbit_setup(char *line)
{
	int i, j, argc;
	char *str;
	int ints[MAX_PARA+1];

	str = get_options(line, MAX_PARA, ints);
	argc = ints[0];
	i = 0;
	j = 1;

	while (argc && (i<MAX_PCBIT_CARDS)) {

		if (argc) {
			mem[i]	= ints[j];
			j++; argc--;
		}
		
		if (argc) {
			irq[i]	= ints[j];
			j++; argc--;
		}

		i++;
	}
	return(1);
}
__setup("pcbit=", pcbit_setup);
#endif



