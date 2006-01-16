/*
 * Copyright (c) 2004 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_ialloc.h"
#include "xfs_btree.h"
#include "xfs_qm.h"

EXPORT_SYMBOL(xfs_Gqm);
EXPORT_SYMBOL(xfs_qm_dqattach);
EXPORT_SYMBOL(xfs_qm_dqpurge_all);
EXPORT_SYMBOL(xfs_qm_dqrele_all_inodes);
EXPORT_SYMBOL(xfs_qm_quotacheck);
EXPORT_SYMBOL(xfs_trans_reserve_quota_bydquots);

MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION("SGI XFS quota management subsystem");
MODULE_LICENSE("GPL");
