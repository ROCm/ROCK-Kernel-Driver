/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Cache operations for the cache instruction.
 *
 * (C) Copyright 1996, 1997, 1999 by Ralf Baechle
 * (C) Copyright 1999 Silicon Graphics, Inc.
 */
#ifndef	_ASM_R10KCACHEOPS_H
#define	_ASM_R10KCACHEOPS_H

/*
 * Cache Operations
 */
#define Index_Invalidate_I      0x00
#define Index_Writeback_Inv_D   0x01
					/* 0x02 is unused */
#define Index_Writeback_Inv_S	0x03
#define Index_Load_Tag_I	0x04
#define Index_Load_Tag_D	0x05
					/* 0x06 is unused */
#define Index_Load_Tag_S	0x07
#define Index_Store_Tag_I	0x08
#define Index_Store_Tag_D	0x09
					/* 0x0a is unused */
#define Index_Store_Tag_S	0x0b
					/* 0x0c - 0x0e are unused */
#define Hit_Invalidate_I	0x10
#define Hit_Invalidate_D	0x11
					/* 0x12 is unused */
#define Hit_Invalidate_S	0x13
#define Cache_Barrier		0x14
#define Hit_Writeback_Inv_D	0x15
					/* 0x16 is unused */
#define Hit_Writeback_Inv_S	0x17
#define Index_Load_Data_I	0x18
#define Index_Load_Data_D	0x19
					/* 0x1a is unused */
#define Index_Load_Data_S	0x1b
#define Index_Store_Data_I	0x1c
#define Index_Store_Data_D	0x1d
					/* 0x1e is unused */
#define Index_Store_Data_S	0x1f

#endif	/* _ASM_R10KCACHEOPS_H */
