/* 
 * linux/arch/sh/board/adx/setup.c
 *
 * Copyright (C) 2001 A&D Co., Ltd.
 *
 * I/O routine and setup routines for A&D ADX Board
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <asm/machvec.h>
#include <linux/module.h>

const char *get_system_type(void)
{
	return "A&D ADX";
}

void platform_setup(void)
{
}
