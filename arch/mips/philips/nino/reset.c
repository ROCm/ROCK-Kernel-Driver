/*
 *  linux/arch/mips/philips/nino/reset.c
 *
 *  Copyright (C) 2001 Steven J. Hill (sjhill@realitydiluted.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Generic restart, halt and power off functions for Philips Nino.
 */
#include <linux/init.h>
#include <asm/reboot.h>

void (*reset_vector)(void) = (void (*)(void)) 0xBFC00000;

void nino_machine_restart(char *command)
{
	reset_vector();
}

void nino_machine_halt(void)
{
	reset_vector();
}

void nino_machine_power_off(void)
{
	reset_vector();
}

void __init setup_nino_reset_vectors(void)
{
	_machine_restart = nino_machine_restart;
	_machine_halt = nino_machine_halt;
	_machine_power_off = nino_machine_power_off;
}
