/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2001 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_SLOTNUM_H
#define _ASM_IA64_SN_SLOTNUM_H

#include <linux/config.h>

typedef	unsigned char slotid_t;

#if defined (CONFIG_IA64_SGI_SN1)
#include <asm/sn/sn1/slotnum.h>
#elif defined (CONFIG_IA64_SGI_SN2)
#include <asm/sn/sn2/slotnum.h>
#else

#error <<BOMB! slotnum defined only for SN0 and SN1 >>

#endif /* !CONFIG_IA64_SGI_SN1 */

#endif /* _ASM_IA64_SN_SLOTNUM_H */
