/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_SN1_HUBXB_NEXT_H
#define _ASM_SN_SN1_HUBXB_NEXT_H

/* XB_FIRST_ERROR fe_source field encoding */
#define XVE_SOURCE_POQ0 0xf	/* 1111 */
#define XVE_SOURCE_PIQ0 0xe	/* 1110 */
#define XVE_SOURCE_POQ1 0xd	/* 1101 */
#define XVE_SOURCE_PIQ1 0xc	/* 1100 */
#define XVE_SOURCE_MP0  0xb	/* 1011 */
#define XVE_SOURCE_MP1  0xa	/* 1010 */
#define XVE_SOURCE_MMQ  0x9	/* 1001 */
#define XVE_SOURCE_MIQ  0x8	/* 1000 */
#define XVE_SOURCE_NOQ  0x7	/* 0111 */
#define XVE_SOURCE_NIQ  0x6	/* 0110 */
#define XVE_SOURCE_IOQ  0x5	/* 0101 */
#define XVE_SOURCE_IIQ  0x4	/* 0100 */
#define XVE_SOURCE_LOQ  0x3	/* 0011 */
#define XVE_SOURCE_LIQ  0x2	/* 0010 */

/* XB_PARMS fields */
#define XBP_RESET_DEFAULTS	0x0008000080000021LL

#endif	/* _ASM_SN_SN1_HUBXB_NEXT_H */
