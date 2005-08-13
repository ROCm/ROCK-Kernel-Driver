/* 
 *   Creation Date: <2001/02/11 18:19:42 samuel>
 *   Time-stamp: <2003/07/27 18:58:35 samuel>
 *   
 *	<constants.h>
 *	
 *	Constants used both in the kernel module and in the emulator
 *   
 *   Copyright (C) 2001, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_CONSTANTS
#define _H_CONSTANTS

/* flags for _breakpoint_flags() */
#define BREAK_RFI		1	/* break at next rfi */
#define BREAK_SINGLE_STEP	2	/* singlestep */
#define BREAK_EA_PAGE		4	/* break when mdbg_break_ea is mapped */
#define BREAK_USER		8	/* break when MSR_PR is set */
#define BREAK_SINGLE_STEP_CONT	16	/* single step (but don't continue running) */

/* action for _tune_spr() */
#define kTuneSPR_Illegal	1	/* SPR is illegal */
#define kTuneSPR_Privileged	2	/* SPR is privileged */
#define kTuneSPR_Unprivileged	3	/* SPR is unprivileged */
#define kTuneSPR_ReadWrite	4	/* SPR is read-write */
#define kTuneSPR_ReadOnly	5	/* SPR is read-only */

#endif   /* _H_CONSTANTS */


