/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_XTALK_XSWITCH_H
#define _ASM_SN_XTALK_XSWITCH_H

/*
 * xswitch.h - controls the format of the data
 * provided by xswitch verticies back to the
 * xtalk bus providers.
 */

#if LANGUAGE_C

typedef struct xswitch_info_s *xswitch_info_t;

typedef int
                        xswitch_reset_link_f(devfs_handle_t xconn);

typedef struct xswitch_provider_s {
    xswitch_reset_link_f   *reset_link;
} xswitch_provider_t;

extern void             xswitch_provider_register(devfs_handle_t sw_vhdl, xswitch_provider_t * xsw_fns);

xswitch_reset_link_f    xswitch_reset_link;

extern xswitch_info_t   xswitch_info_new(devfs_handle_t vhdl);

extern void             xswitch_info_link_is_ok(xswitch_info_t xswitch_info,
						xwidgetnum_t port);
extern void             xswitch_info_vhdl_set(xswitch_info_t xswitch_info,
					      xwidgetnum_t port,
					      devfs_handle_t xwidget);
extern void             xswitch_info_master_assignment_set(xswitch_info_t xswitch_info,
						       xwidgetnum_t port,
					       devfs_handle_t master_vhdl);

extern xswitch_info_t   xswitch_info_get(devfs_handle_t vhdl);

extern int              xswitch_info_link_ok(xswitch_info_t xswitch_info,
					     xwidgetnum_t port);
extern devfs_handle_t     xswitch_info_vhdl_get(xswitch_info_t xswitch_info,
					      xwidgetnum_t port);
extern devfs_handle_t     xswitch_info_master_assignment_get(xswitch_info_t xswitch_info,
						      xwidgetnum_t port);

extern int		xswitch_id_get(devfs_handle_t vhdl);
extern void		xswitch_id_set(devfs_handle_t vhdl,int xbow_num);

#endif				/* LANGUAGE_C */

#endif				/* _ASM_SN_XTALK_XSWITCH_H */
