/* 
 *   Creation Date: <2003/01/26 00:45:55 samuel>
 *   Time-stamp: <2003/01/27 01:26:25 samuel>
 *   
 *	<emu.h>
 *	
 *	Emulation of some assembly functions
 *   
 *   Copyright (C) 1998, 2000, 2001, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_EMU
#define _H_EMU

#include "mmu.h"

extern int 	do_mtsdr1( kernel_vars_t *kv, ulong value );
extern int 	do_mtbat( kernel_vars_t *kv, int sprnum, ulong value, int force );

extern int	alloc_emuaccel_slot( kernel_vars_t *kv, int inst_flags, int param, int inst_addr );
extern int	mapin_emuaccel_page( kernel_vars_t *kv, int mphys );


#endif   /* _H_EMU */
