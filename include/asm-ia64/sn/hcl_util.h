/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#ifndef _ASM_SN_HCL_UTIL_H
#define _ASM_SN_HCL_UTIL_H

extern char * dev_to_name(devfs_handle_t, char *, uint);
extern int device_master_set(devfs_handle_t, devfs_handle_t);
extern devfs_handle_t device_master_get(devfs_handle_t);
extern cnodeid_t master_node_get(devfs_handle_t);
extern cnodeid_t nodevertex_to_cnodeid(devfs_handle_t);
extern void mark_nodevertex_as_node(devfs_handle_t, cnodeid_t);
extern void device_info_set(devfs_handle_t, void *);
extern void *device_info_get(devfs_handle_t);


#endif _ASM_SN_HCL_UTIL_H
