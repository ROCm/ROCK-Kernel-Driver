/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2001 Silicon Graphics, Inc.
 */
#ifndef _ASM_IA64_SN_SNCONFIG_H
#define _ASM_IA64_SN_SNCONFIG_H

#include <linux/config.h>

#if defined(CONFIG_IA64_SGI_SN1)
#include <asm/sn/sn1/ip27config.h>
#elif defined(CONFIG_IA64_SGI_SN2)
#endif

#endif /* _ASM_IA64_SN_SNCONFIG_H */
