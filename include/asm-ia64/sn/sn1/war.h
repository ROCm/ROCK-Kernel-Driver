/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_SN1_WAR_H
#define _ASM_SN_SN1_WAR_H

/****************************************************************************
 * Support macros and defitions for hardware workarounds in		    *
 * early chip versions.                                                     *
 ****************************************************************************/

/*
 * This is the bitmap of runtime-switched workarounds.
 */
typedef short warbits_t;

extern int warbits_override;

#endif /*  _ASM_SN_SN1_WAR_H */
