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
#include "xfs_bmap_btree.h"
#include "xfs_bit.h"
#include "xfs_rw.h"

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

/* Export XFS symbols used by xfsidbg */
EXPORT_SYMBOL(xfs_next_bit);
EXPORT_SYMBOL(xfs_contig_bits);
EXPORT_SYMBOL(xfs_bmbt_get_all);
#if ARCH_CONVERT != ARCH_NOCONVERT
EXPORT_SYMBOL(xfs_bmbt_disk_get_all);
#endif
