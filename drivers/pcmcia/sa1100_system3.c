/*
 * drivers/pcmcia/sa1100_system3.c
 *
 * PT Diagital Board PCMCIA specific routines
 *
 * Copyright (C) 2001 Stefan Eletzhofer <stefan.eletzhofer@eletztrick.de>
 *
 * $Id: sa1100_system3.c,v 1.1.4.2 2002/02/25 13:56:45 seletz Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * $Log: sa1100_system3.c,v $
 * Revision 1.1.4.2  2002/02/25 13:56:45  seletz
 * - more cleanups
 * - setup interrupts for CF card only ATM
 *
 * Revision 1.1.4.1  2002/02/14 02:23:27  seletz
 * - 2.5.2-rmk6 PCMCIA changes
 *
 * Revision 1.1.2.1  2002/02/13 23:49:33  seletz
 * - added from 2.4.16-rmk2
 * - cleanups
 *
 *
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/hardware/sa1111.h>

#include "sa1100_generic.h"
#include "sa1111_generic.h"

#define DEBUG 0

#ifdef DEBUG
#	define DPRINTK( x, args... )	printk( "%s: line %d: "x, __FUNCTION__, __LINE__, ## args  );
#else
#	define DPRINTK( x, args... )	/* nix */
#endif

int system3_pcmcia_init(struct pcmcia_init *init)
{
	/* Don't need no CD and BVD* interrupts */
	return 2;
}

int system3_pcmcia_shutdown(void)
{
	return 0;
}

int system3_pcmcia_configure_socket(const struct pcmcia_configure *conf)
{
	if (!conf)
		return -1;

	/* only CF ATM */
	if (conf->sock == 0)
		return -1;

	return sa1111_pcmcia_configure_socket( conf );
}

static int system3_pcmcia_socket_state(struct pcmcia_state_array
		*state)
{
	unsigned long	status		= 0;

	if(state->size<2) return -1;

	memset(state->state, 0,
			(state->size)*sizeof(struct pcmcia_state));

	status=PCSR;

#if 0 /* PCMCIA socket not yet connected */
	state->state[0].detect = status & PCSR_S0_DETECT ? 0 : 1;
	state->state[0].ready  = status & PCSR_S0_READY  ? 1 : 0;
	state->state[0].bvd1   = status & PCSR_S0_BVD1   ? 1 : 0;
	state->state[0].bvd2   = 1;
	state->state[0].wrprot = status & PCSR_S0_WP     ? 1 : 0;
	state->state[0].vs_3v  = 1;
	state->state[0].vs_Xv  = 0;
#endif

	state->state[1].detect = status & PCSR_S1_DETECT ? 0 : 1;
	state->state[1].ready  = status & PCSR_S1_READY  ? 1 : 0;
	state->state[1].bvd1   = status & PCSR_S1_BVD1   ? 1 : 0;
	state->state[1].bvd2   = 1;
	state->state[1].wrprot = status & PCSR_S1_WP     ? 1 : 0;
	state->state[1].vs_3v  = 1;
	state->state[1].vs_Xv  = 0;

	DPRINTK( "PCSR=0x%08lx, S1_RDY_nIREQ=%d\n", status,
			state->state[1].ready );

	return 1;
}

struct pcmcia_low_level system3_pcmcia_ops = {
	init:				system3_pcmcia_init,
	shutdown:			system3_pcmcia_shutdown,
	socket_state:		system3_pcmcia_socket_state,
	get_irq_info:		sa1111_pcmcia_get_irq_info,
	configure_socket:	system3_pcmcia_configure_socket,

	socket_init:		sa1111_pcmcia_socket_init,
	socket_suspend:		sa1111_pcmcia_socket_suspend,
};

int __init pcmcia_system3_init(void)
{
	int ret = -ENODEV;

	if (machine_is_pt_system3())
		ret = sa1100_register_pcmcia(&system3_pcmcia_ops);

	return ret;
}

void __exit pcmcia_system3_exit(void)
{
	sa1100_unregister_pcmcia(&system3_pcmcia_ops);
}
