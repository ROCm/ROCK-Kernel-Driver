/*
 * kernel/lvm-snap.h
 *
 * Copyright (C) 2001 Sistina Software
 *
 *
 * LVM driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * LVM driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

/*
 * Changelog
 *
 *    05/01/2001:Joe Thornber - Factored this file out of lvm.c
 *
 */

#ifndef LVM_SNAP_H
#define LVM_SNAP_H

/* external snapshot calls */
extern inline int lvm_get_blksize(kdev_t);
extern int lvm_snapshot_alloc(lv_t *);
extern void lvm_snapshot_fill_COW_page(vg_t *, lv_t *);
extern int lvm_snapshot_COW(kdev_t, ulong, ulong, ulong, lv_t *);
extern int lvm_snapshot_remap_block(kdev_t *, ulong *, ulong, lv_t *);
extern void lvm_snapshot_release(lv_t *); 
extern int lvm_write_COW_table_block(vg_t *, lv_t *);
extern inline void lvm_hash_link(lv_block_exception_t *, 
				 kdev_t, ulong, lv_t *);
extern int lvm_snapshot_alloc_hash_table(lv_t *);
extern void lvm_drop_snapshot(lv_t *, const char *);

#endif
