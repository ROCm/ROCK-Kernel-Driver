/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_SN1_HUBLB_NEXT_H
#define _ASM_SN_SN1_HUBLB_NEXT_H

/**********************************************************************

 This contains some mask and shift values for LB defined as required
 for compatibility.

 **********************************************************************/

#define LRI_SYSTEM_SIZE_SHFT        46
#define LRI_SYSTEM_SIZE_MASK        (UINT64_CAST 0x3 << LRI_SYSTEM_SIZE_SHFT)
#define LRI_NODEID_SHFT        32
#define LRI_NODEID_MASK        (UINT64_CAST 0xff << LRI_NODEID_SHFT)/* Node ID    */
#define LRI_CHIPID_SHFT		12
#define LRI_CHIPID_MASK		(UINT64_CAST 0xffff << LRI_CHIPID_SHFT) /* should be 0x3012 */
#define LRI_REV_SHFT        28
#define LRI_REV_MASK        (UINT64_CAST 0xf << LRI_REV_SHFT)/* Chip revision    */

/* Values for LRI_SYSTEM_SIZE */
#define SYSTEM_SIZE_INVALID	0x3
#define SYSTEM_SIZE_NMODE	0x2
#define SYSTEM_SIZE_COARSE 	0x1
#define SYSTEM_SIZE_SMALL	0x0

/* In fine mode, each node is a region.  In coarse mode, there are
 * 2 nodes per region.  In N-mode, there are 4 nodes per region. */
#define NASID_TO_FINEREG_SHFT   0
#define NASID_TO_COARSEREG_SHFT 1
#define NASID_TO_NMODEREG_SHFT  2

#define LR_LOCALRESET               (UINT64_CAST 1)
/*
 * LB_VECTOR_PARMS mask and shift definitions.
 * TYPE may be any of the first four PIOTYPEs defined under NI_VECTOR_STATUS.
 */

#define LVP_BUSY		(UINT64_CAST 1 << 63)
#define LVP_PIOID_SHFT          40
#define LVP_PIOID_MASK          (UINT64_CAST 0x7ff << 40)
#define LVP_WRITEID_SHFT        32
#define LVP_WRITEID_MASK        (UINT64_CAST 0xff << 32)
#define LVP_ADDRESS_MASK        (UINT64_CAST 0xfffff8)   /* Bits 23:3        */
#define LVP_TYPE_SHFT           0
#define LVP_TYPE_MASK           (UINT64_CAST 0x3)

/* LB_VECTOR_STATUS mask and shift definitions */

#define LVS_VALID               (UINT64_CAST 1 << 63)
#define LVS_OVERRUN             (UINT64_CAST 1 << 62)
#define LVS_TARGET_SHFT         51
#define LVS_TARGET_MASK         (UINT64_CAST 0x7ff << 51)
#define LVS_PIOID_SHFT          40
#define LVS_PIOID_MASK          (UINT64_CAST 0x7ff << 40)
#define LVS_WRITEID_SHFT        32
#define LVS_WRITEID_MASK        (UINT64_CAST 0xff << 32)
#define LVS_ADDRESS_MASK        (UINT64_CAST 0xfffff8)   /* Bits 23:3     */
#define LVS_TYPE_SHFT           0
#define LVS_TYPE_MASK           (UINT64_CAST 0x7)
#define LVS_ERROR_MASK          (UINT64_CAST 0x4)  /* bit set means error */

/* LB_RT_LOCAL_CTRL mask and shift definitions */

#define LRLC_USE_INT_SHFT       32
#define LRLC_USE_INT_MASK       (UINT64_CAST 1 << 32)
#define LRLC_USE_INT            (UINT64_CAST 1 << 32)
#define LRLC_GCLK_SHFT          28
#define LRLC_GCLK_MASK          (UINT64_CAST 1 << 28)
#define LRLC_GCLK               (UINT64_CAST 1 << 28)
#define LRLC_GCLK_COUNT_SHFT    16
#define LRLC_GCLK_COUNT_MASK    (UINT64_CAST 0x3ff << 16)
#define LRLC_MAX_COUNT_SHFT     4
#define LRLC_MAX_COUNT_MASK     (UINT64_CAST 0x3ff << 4)
#define LRLC_GCLK_EN_SHFT       0
#define LRLC_GCLK_EN_MASK       (UINT64_CAST 1)
#define LRLC_GCLK_EN            (UINT64_CAST 1)

/* LB_NODES_ABSENT mask and shift definitions */
#define LNA_VALID_SHFT		15
#define LNA_VALID_MASK		(UINT64_CAST 1 << LNA_VALID_SHFT)
#define LNA_VALID		(UINT64_CAST 1 << LNA_VALID_SHFT)
#define LNA_NODE_SHFT		0
#define LNA_NODE_MASK		(UINT64_CAST 0xff << LNA_NODE_SHFT)

/* LB_NODES_ABSENT has 4 identical sub-registers, on 16-bit boundaries */
#define LNA_ENTRY_SHFT		16
#define LNA_MAX_ENTRIES		4
#define LNA_ADD(_reg, _n)	((_reg) = (_reg) << LNA_ENTRY_SHFT | \
				 	LNA_VALID | (_n) << LNA_NODE_SHFT)

#define  PIOTYPE_READ           0       /* VECTOR_PARMS and VECTOR_STATUS   */
#define  PIOTYPE_WRITE          1       /* VECTOR_PARMS and VECTOR_STATUS   */
#define  PIOTYPE_UNDEFINED      2       /* VECTOR_PARMS and VECTOR_STATUS   */
/* XXX IP35 doesn't support vector exchange:  scr. regs. do locks directly */
#define  PIOTYPE_EXCHANGE       3       /* VECTOR_PARMS and VECTOR_STATUS   */
#define  PIOTYPE_ADDR_ERR       4       /* VECTOR_STATUS only               */
#define  PIOTYPE_CMD_ERR        5       /* VECTOR_STATUS only               */
#define  PIOTYPE_PROT_ERR       6       /* VECTOR_STATUS only               */
#define  PIOTYPE_UNKNOWN        7       /* VECTOR_STATUS only               */

#endif	/* _ASM_SN_SN1_HUBLB_NEXT_H */
