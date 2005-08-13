/* 
 *   Creation Date: <2003/01/25 14:57:36 samuel>
 *   Time-stamp: <2003/01/27 23:11:29 samuel>
 *   
 *	<emuaccel.h>
 *	
 *	Acceleration of the emulation of certain privileged instructions
 *   
 *   Copyright (C) 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_EMUACCEL
#define _H_EMUACCEL

/* OSI_AllocInstAccelSlot( EMULATE_xxx + source_reg, NIP ) */

#define EMUACCEL_MAPIN_PAGE	0
#define EMUACCEL_MTSRR0		(1 << 5)
#define EMUACCEL_MTSRR1		(2 << 5)
#define EMUACCEL_MTSPRG0	(3 << 5)
#define EMUACCEL_MTSPRG1	(4 << 5)
#define EMUACCEL_MTSPRG2	(5 << 5)
#define EMUACCEL_MTSPRG3	(6 << 5)
#define EMUACCEL_MTMSR		(7 << 5)
#define EMUACCEL_RFI		(8 << 5)
#define EMUACCEL_UPDATE_DEC	(9 << 5)		/* update xDEC */
#define EMUACCEL_MTSR		((10 << 5) | EMUACCEL_HAS_PARAM)
#define EMUACCEL_NOP		(11 << 5) 
#define EMUACCEL_MTHID0		(12 << 5) 

#define EMUACCEL_HAS_PARAM	(1 << 10)
#define EMUACCEL_INST_MASK	0xffe0
#define EMUACCEL_DESTREG_MASK	0x1f


#endif   /* _H_EMUACCEL */
