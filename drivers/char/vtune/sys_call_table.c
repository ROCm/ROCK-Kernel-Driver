/*
 *  sys_call_table.c
 *
 *  Copyright (C) 2003-2004 Intel Corporation
 *  Maintainer - Juan Villacis <juan.villacis@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
/*
 * ===========================================================================
 *
 *	File: sys_call_table.c
 *
 *	Description: scan for sys_call_table symbol and return pointer to it
 *	             if found (null otherwise)
 *
 *	Author(s): Juan Villacis, George Artz, Intel Corp.
 *
 *	System: VTune(TM) Performance Analyzer Driver Kit for Linux*
 *
 * ===========================================================================
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <asm/unistd.h>

#include "vtdef.h"

#ifdef USE_SYSCALLTABLE_ADDRESS
/*
 * use_sys_call_table_address() : returns address of sys_call_table obtained
 *                                by one of the following commands:
 *
 *              "grep sys_call_table /boot/System.map-2.x.y"
 *      or,
 *              "nm /boot/vmlinux-2.x.y | grep sys_call_table"
 *
 * the address used below is expected to be of the form "0xNNNNNN"
 */
void *use_sys_call_table_address(void)
{
  void *sys_call_table = 0;
  unsigned long address = USE_SYSCALLTABLE_ADDRESS;

  if (address>0x1)
    sys_call_table = (void *)address;

  return (sys_call_table);
}
#endif /* USE_SYSCALLTABLE_ADDRESS */

/*
 * find_sys_call_table_symbol() : if not exported, then scan for sys_call_table
 *                                symbol and returns pointer to it if found
 *                                (NULL otherwise)
 */
void *find_sys_call_table_symbol(int verbose)
{
  void *sys_call_table = 0;

#ifdef USE_SYSCALLTABLE_ADDRESS
  if (! sys_call_table)
    sys_call_table = use_sys_call_table_address();
#endif

  if (sys_call_table) 
  {
    if (verbose)
      VDK_PRINT("sys_call_table symbol found at address 0x%lx\n",(long)sys_call_table);
  }
  else
  {
    if (verbose)
      VDK_PRINT_WARNING("failed to find address of sys_call_table!\n");
  }

  return (sys_call_table);
}
