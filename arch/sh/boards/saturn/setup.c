/* 
 * arch/sh/boards/saturn/setup.c
 *
 * Hardware support for the Sega Saturn.
 *
 * Copyright (c) 2002 Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */
#include <linux/kernel.h>
#include <linux/init.h>

const char *get_system_type(void)
{
	return "Sega Saturn";
}

int __init platform_setup(void)
{
	return 0;
}

