/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_HUBSPC_H
#define _ASM_SN_HUBSPC_H

typedef enum {
        HUBSPC_REFCOUNTERS,
	HUBSPC_PROM
} hubspc_subdevice_t;


/*
 * Reference Counters
 */

extern int refcounters_attach(devfs_handle_t hub);

#endif /* _ASM_SN_HUBSPC_H */        
