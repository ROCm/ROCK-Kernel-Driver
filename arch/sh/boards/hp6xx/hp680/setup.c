/*
 * linux/arch/sh/boards/hp6xx/hp680/setup.c
 *
 * Copyright (C) 2002 Andriy Skulysh
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Setup code for an HP680  (internal peripherials only)
 */

#include <linux/config.h>
#include <linux/init.h>
#include <asm/hd64461/hd64461.h>

const char *get_system_type(void)
{
	return "HP680";
}

int __init platform_setup(void)
{
	__set_io_port_base(CONFIG_HD64461_IOBASE - HD64461_STBCR);

	return 0;
}

