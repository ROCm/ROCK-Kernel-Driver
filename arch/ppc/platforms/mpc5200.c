/*
 * arch/ppc/platforms/mpc5200.c
 *
 * OCP Definitions for the boards based on MPC5200 processor. Contains
 * definitions for every common peripherals. (Mostly all but PSCs)
 * 
 * Maintainer : Sylvain Munaut <tnt@246tNt.com>
 *
 * Copyright 2004 Sylvain Munaut <tnt@246tNt.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <asm/ocp.h>
#include <asm/mpc52xx.h>

/* Here is the core_ocp struct.
 * With all the devices common to all board. Even if port multiplexing is
 * not setup for them (if the user don't want them, just don't select the
 * config option). The potentially conflicting devices (like PSCs) goes in
 * board specific file.
 */
struct ocp_def core_ocp[] = {
	{	/* Terminating entry */
		.vendor		= OCP_VENDOR_INVALID
	}
};
