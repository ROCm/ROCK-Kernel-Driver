#
# Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of version 2 of the GNU General Public License as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it would be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# Further, this software is distributed without any warranty that it is
# free of the rightful claim of any third person regarding infringement
# or the like.  Any license provided herein, whether implied or
# otherwise, applies only to this software file.  Patent licenses, if
# any, provided herein do not apply to combinations of this program with
# other software, or any other product whatsoever.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write the Free Software Foundation, Inc., 59
# Temple Place - Suite 330, Boston MA 02111-1307, USA.
#
# Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
# Mountain View, CA  94043, or:
#
# http://www.sgi.com
#
# For further information regarding this notice, see:
#
# http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
#

EXTRA_CFLAGS +=	 -Ifs/xfs -funsigned-char

ifeq ($(CONFIG_XFS_DEBUG),y)
	EXTRA_CFLAGS += -g -DSTATIC="" -DDEBUG -DXFSDEBUG
endif
ifeq ($(CONFIG_PAGEBUF_DEBUG),y)
	EXTRA_CFLAGS += -DPAGEBUF_TRACE
endif

obj-$(CONFIG_XFS_FS)		+= xfs.o


xfs-$(CONFIG_XFS_QUOTA)		+= $(addprefix quota/, \
				   xfs_dquot.o \
				   xfs_dquot_item.o \
				   xfs_trans_dquot.o \
				   xfs_qm_syscalls.o \
				   xfs_qm_bhv.o \
				   xfs_qm.o)

ifeq ($(CONFIG_XFS_QUOTA),y)
xfs-$(CONFIG_PROC_FS)		+= quota/xfs_qm_stats.o
endif

xfs-$(CONFIG_XFS_RT)		+= xfs_rtalloc.o
xfs-$(CONFIG_XFS_POSIX_ACL)	+= xfs_acl.o
xfs-$(CONFIG_XFS_POSIX_CAP)	+= xfs_cap.o
xfs-$(CONFIG_XFS_POSIX_MAC)	+= xfs_mac.o
xfs-$(CONFIG_PROC_FS)		+= linux/xfs_stats.o
xfs-$(CONFIG_SYSCTL)		+= linux/xfs_sysctl.o


xfs-y				+= xfs_alloc.o \
				   xfs_alloc_btree.o \
				   xfs_attr.o \
				   xfs_attr_leaf.o \
				   xfs_bit.o \
				   xfs_bmap.o \
				   xfs_bmap_btree.o \
				   xfs_btree.o \
				   xfs_buf_item.o \
				   xfs_da_btree.o \
				   xfs_dir.o \
				   xfs_dir2.o \
				   xfs_dir2_block.o \
				   xfs_dir2_data.o \
				   xfs_dir2_leaf.o \
				   xfs_dir2_node.o \
				   xfs_dir2_sf.o \
				   xfs_dir2_trace.o \
				   xfs_dir_leaf.o \
				   xfs_error.o \
				   xfs_extfree_item.o \
				   xfs_fsops.o \
				   xfs_ialloc.o \
				   xfs_ialloc_btree.o \
				   xfs_iget.o \
				   xfs_inode.o \
				   xfs_inode_item.o \
				   xfs_iocore.o \
				   xfs_itable.o \
				   xfs_dfrag.o \
				   xfs_log.o \
				   xfs_log_recover.o \
				   xfs_macros.o \
				   xfs_mount.o \
				   xfs_rename.o \
				   xfs_trans.o \
				   xfs_trans_ail.o \
				   xfs_trans_buf.o \
				   xfs_trans_extfree.o \
				   xfs_trans_inode.o \
				   xfs_trans_item.o \
				   xfs_utils.o \
				   xfs_vfsops.o \
				   xfs_vnodeops.o \
				   xfs_rw.o

# Objects in pagebuf/
xfs-y				+= $(addprefix pagebuf/, \
				   page_buf.o \
				   page_buf_locking.o)

# Objects in linux/
xfs-y				+= $(addprefix linux/, \
				   xfs_aops.o \
				   xfs_behavior.o \
				   xfs_file.o \
				   xfs_fs_subr.o \
				   xfs_globals.o \
				   xfs_ioctl.o \
				   xfs_iomap.o \
				   xfs_iops.o \
				   xfs_lrw.o \
				   xfs_super.o \
				   xfs_vfs.o \
				   xfs_vnode.o)

# Objects in support/
xfs-y				+= $(addprefix support/, \
				   debug.o \
				   ktrace.o \
				   move.o \
				   mrlock.o \
				   qsort.o \
				   uuid.o)

# Quota and DMAPI stubs
xfs-y				+= xfs_dmops.o \
				   xfs_qmops.o

# If both xfs and kdb modules are built in then xfsidbg is built in.  If xfs is
# a module and kdb modules are being compiled then xfsidbg must be a module, to
# follow xfs.  If xfs is built in then xfsidbg tracks the kdb module state.
# This must come after the main xfs code so xfs initialises before xfsidbg.
# KAO
ifneq ($(CONFIG_KDB_MODULES),)
  ifeq ($(CONFIG_XFS_FS),y)
    obj-$(CONFIG_KDB_MODULES)	+= xfsidbg.o
  else
    obj-$(CONFIG_XFS_FS)	+= xfsidbg.o
  endif
endif

CFLAGS_xfsidbg.o += -Iarch/$(ARCH)/kdb
