/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#ifndef _ASM_SN_SN1_BEDROCK_H
#define _ASM_SN_SN1_BEDROCK_H

/* The secret password; used to release protection */
#define HUB_PASSWORD		0x53474972756c6573ull

#define CHIPID_HUB		0x3012
#define CHIPID_ROUTER		0x3017

#define BEDROCK_REV_1_0		1
#define BEDROCK_REV_1_1		2

#define MAX_HUB_PATH		80

#include <linux/config.h>
#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
#include <asm/sn/arch.h>
#include <asm/sn/sn1/addrs.h>
#include <asm/sn/sn1/hubpi.h>
#include <asm/sn/sn1/hubmd.h>
#include <asm/sn/sn1/hubio.h>
#include <asm/sn/sn1/hubni.h>
#include <asm/sn/sn1/hublb.h>
#include <asm/sn/sn1/hubxb.h>
#include <asm/sn/sn1/hubpi_next.h>
#include <asm/sn/sn1/hubmd_next.h>
#include <asm/sn/sn1/hubio_next.h>
#include <asm/sn/sn1/hubni_next.h>
#include <asm/sn/sn1/hublb_next.h>
#include <asm/sn/sn1/hubxb_next.h>

#else /* ! CONFIG_SGI_IP35 || CONFIG_IA64_SGI_SN1 */

<< BOMB! CONFIG_SGI_IP35 is only defined for IP35 >>

#endif /* defined(CONFIG_SGI_IP35) */

/* Translation of uncached attributes */
#define	UATTR_HSPEC	0
#define	UATTR_IO	1
#define	UATTR_MSPEC	2
#define	UATTR_UNCAC	3

#if _LANGUAGE_ASSEMBLY

/*
 * Get nasid into register, r (uses at)
 */
#define GET_NASID_ASM(r)				\
	dli	r, LOCAL_HUB_ADDR(LB_REV_ID);	\
	ld	r, (r);					\
	and	r, LRI_NODEID_MASK;			\
	dsrl	r, LRI_NODEID_SHFT

#endif /* _LANGUAGE_ASSEMBLY */

#if _LANGUAGE_C

#include <asm/sn/xtalk/xwidget.h>

/* hub-as-widget iograph info, labelled by INFO_LBL_XWIDGET */
typedef struct v_hub_s *v_hub_t;
typedef uint64_t      rtc_time_t;

struct nodepda_s;
int hub_check_pci_equiv(void *addra, void *addrb);
void capture_hub_stats(cnodeid_t, struct nodepda_s *);
void init_hub_stats(cnodeid_t, struct nodepda_s *);

#endif /* _LANGUAGE_C */

#endif /* _ASM_SN_SN1_BEDROCK_H */
