/* $Id: setup_od.c,v 1.1 2000/06/14 09:35:59 stuart_menefy Exp $
 *
 * arch/sh/kernel/setup_od.c
 *
 * Copyright (C) 2000  Stuart Menefy
 *
 * STMicroelectronics Overdrive Support.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>

/*
 * Initialize the board
 */
int __init setup_od(void)
{
	/* Enable RS232 receive buffers */
	volatile int* p = (volatile int*)0xa3000000;

#if defined(CONFIG_SH_ORION)
	*p=1;
#elif defined(CONFIG_SH_OVERDRIVE)
	*p=0x1e;
#else
#error Illegal configuration
#endif

	printk(KERN_INFO "STMicroelectronics Overdrive Setup...done\n");
	return 0;
}

module_init(setup_od);
