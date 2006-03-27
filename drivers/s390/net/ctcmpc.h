/*
 * CTC / ESCON network driver, mpc interface.
 *
 * Copyright (C) 2003 IBM United States, IBM Corporation
 * Author(s): Belinda Thompson (belindat@us.ibm.com)
 *            Andy Richter (richtera@us.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef _CTCMPC_H_
#define _CTCMPC_H_

#include "ctcmpcfsm.h"

typedef struct sk_buff sk_buff;
typedef void (*callbacktypei2)(int,int);	      /* void (*void)(int,int) */
typedef void (*callbacktypei3)(int,int,int);	  /* void (*void)(int,int,int) */

/*  port_number is the mpc device 0,1,2 etc mpc2 is port_number 2 */
/*  passive open  Just wait for XID2 exchange */
/*  ctc_mpc_alloc channel(port_number,
                                void(*callback)(port_number,max_write_size)) */
extern  int ctc_mpc_alloc_channel(int,callbacktypei2);
/* active open  Alloc then send XID2 */
/*  ctc_mpc_establish_connectivity(port_number ,
                              void(callback*)(port_number,rc,max_write_size)) */
extern void ctc_mpc_establish_connectivity(int,callbacktypei3);
extern void ctc_mpc_dealloc_ch(int);
extern void ctc_mpc_flow_control(int,int);

#endif
