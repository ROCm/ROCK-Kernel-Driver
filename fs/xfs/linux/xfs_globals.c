/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

/*
 * This file contains globals needed by XFS that were normally defined
 * somewhere else in IRIX.
 */

#include "xfs.h"

#include "xfs_fs.h"
#include "xfs_buf.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_clnt.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_imap.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_bit.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_da_btree.h"
#include "xfs_dir_leaf.h"
#include "xfs_dir2_data.h"
#include "xfs_dir2_leaf.h"
#include "xfs_dir2_block.h"
#include "xfs_dir2_node.h"
#include "xfs_dir2_trace.h"
#include "xfs_acl.h"
#include "xfs_cap.h"
#include "xfs_mac.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_inode_item.h"
#include "xfs_buf_item.h"
#include "xfs_extfree_item.h"
#include "xfs_log_priv.h"
#include "xfs_trans_priv.h"
#include "xfs_trans_space.h"
#include "xfs_utils.h"

/*
 * System memory size - used to scale certain data structures in XFS.
 */
unsigned long xfs_physmem;

/*
 * Tunable XFS parameters.  xfs_params is required even when CONFIG_SYSCTL=n,
 * other XFS code uses these values.
 */

xfs_param_t xfs_params = {
			  /*	MIN	DFLT	MAX	*/
	.restrict_chown	= {	0,	1,	1	},
	.sgid_inherit	= {	0,	0,	1	},
	.symlink_mode	= {	0,	0,	1	},
	.panic_mask	= {	0,	0,	127	},
	.error_level	= {	0,	3,	11	},
	.sync_interval	= {	HZ,	30*HZ,	60*HZ	},
	.stats_clear	= {	0,	0,	1	},
	.inherit_sync	= {	0,	1,	1	},
	.inherit_nodump	= {	0,	1,	1	},
	.inherit_noatim = {	0,	1,	1	},
};

/*
 * Global system credential structure.
 */
cred_t sys_cred_val, *sys_cred = &sys_cred_val;

/*
 * Export symbols used for XFS debugging
 */
EXPORT_SYMBOL(xfs_next_bit);
EXPORT_SYMBOL(xfs_contig_bits);
EXPORT_SYMBOL(xfs_bmbt_get_all);
#if ARCH_CONVERT != ARCH_NOCONVERT
EXPORT_SYMBOL(xfs_bmbt_disk_get_all);
#endif

/*
 * Export symbols used for XFS tracing
 */
#ifdef XFS_ALLOC_TRACE
EXPORT_SYMBOL(xfs_alloc_trace_buf);
#endif
#ifdef XFS_BMAP_TRACE
EXPORT_SYMBOL(xfs_bmap_trace_buf);
#endif
#ifdef XFS_BMBT_TRACE
EXPORT_SYMBOL(xfs_bmbt_trace_buf);
#endif
#ifdef XFS_ATTR_TRACE
EXPORT_SYMBOL(xfs_attr_trace_buf);
#endif  
#ifdef XFS_DIR2_TRACE
EXPORT_SYMBOL(xfs_dir2_trace_buf);
#endif   
#ifdef XFS_DIR_TRACE
EXPORT_SYMBOL(xfs_dir_trace_buf);
#endif
