/* 
 *   arch/ppc/kernel/mol.h
 *   
 *	<mol.h>
 *	
 *	Mac-on-Linux hook macros
 *	<http://www.maconlinux.org>
 *   
 *   Copyright (C) 2000 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _PPC_KERNEL_MOL
#define _PPC_KERNEL_MOL

#include <linux/config.h>

#ifdef CONFIG_MOL
#define MOL_INTERFACE_VERSION		3

#define MOL_HOOK(hook_num)					\
	lwz	r0,(mol_interface + 4 * hook_num + 4)@l(0); 	\
	cmpwi	cr1,r0,0; 					\
	beq+	cr1,777f; 					\
	mtctr	r0; 						\
	bctrl; 							\
777:	lwz	r0,GPR0(r21)

#define MOL_HOOK_RESTORE(hook_num)				\
	mfcr	r2;						\
	MOL_HOOK(hook_num);					\
	mtcrf	0x80,r2;					\
	lwz	r2,_CTR(r21);					\
	mtctr	r2;						\
	lwz	r2,GPR2(r21)

#define MOL_HOOK_MMU(hook_num, scr)				\
	lis	scr,(mol_interface + 4 * hook_num + 4)@ha;	\
	lwz	scr,(mol_interface + 4 * hook_num + 4)@l(scr);	\
	cmpwi	cr1,scr,0;					\
	beq+	cr1,778f;					\
	mtctr	scr;						\
	bctrl;							\
778:

#define MOL_HOOK_TLBMISS(hook_num)				\
	lwz	r0,(mol_interface + 4 * hook_num + 4)@l(0);	\
	cmpwi	r0,0;						\
	beq+	779f;						\
	mflr	r3;						\
	mtlr	r0;						\
	blrl;							\
	mtlr	r3;						\
779:

#else
#define MOL_HOOK(num)
#define MOL_HOOK_RESTORE(num)
#define MOL_HOOK_MMU(num, scr)
#define MOL_HOOK_TLBMISS(num)
#endif


#endif   /* _PPC_KERNEL_MOL */
