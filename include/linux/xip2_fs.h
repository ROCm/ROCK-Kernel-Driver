/*
 *  linux/include/linux/xip2_fs.h, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */

#ifndef __XIP2FS_H
#define __XIP2FS_H
#include <linux/ext2_fs.h>
#include <linux/xip2_fs_sb.h>
#ifdef __KERNEL__
static inline struct xip2_sb_info * XIP2_SB (struct super_block *sb)
{
	return (struct xip2_sb_info *) sb->s_fs_info;
}
#else
#define XIP2_SB(sb)	(sb)
#endif
#define XIP2_ADDR_PER_BLOCK_BITS(s)	(XIP2_SB(s)->s_addr_per_block_bits)
#define XIP2_INODE_SIZE(s)		(XIP2_SB(s)->s_inode_size)
#define XIP2_FIRST_INO(s)		(XIP2_SB(s)->s_first_ino)
#define XIP2_BLOCKS_PER_GROUP(s)	(XIP2_SB(s)->s_blocks_per_group)
#define XIP2_DESC_PER_BLOCK(s)		(XIP2_SB(s)->s_desc_per_block)
#define XIP2_INODES_PER_GROUP(s)	(XIP2_SB(s)->s_inodes_per_group)
#undef test_opt
#define test_opt(sb, opt)		(XIP2_SB(sb)->s_mount_opt & \
					 EXT2_MOUNT_##opt)
#define XIP2_HAS_COMPAT_FEATURE(sb,mask)			\
	( XIP2_SB(sb)->s_es->s_feature_compat & cpu_to_le32(mask) )
#define XIP2_HAS_RO_COMPAT_FEATURE(sb,mask)			\
	( XIP2_SB(sb)->s_es->s_feature_ro_compat & cpu_to_le32(mask) )
#define XIP2_HAS_INCOMPAT_FEATURE(sb,mask)			\
	( XIP2_SB(sb)->s_es->s_feature_incompat & cpu_to_le32(mask) )
#define xip2_test_bit			ext2_test_bit
#define xip2_clear_bit_atomic		ext2_clear_bit_atomic
#define xip2_find_next_zero_bit		ext2_find_next_zero_bit
#define xip2_find_first_zero_bit	ext2_find_first_zero_bit
#define xip2_set_bit_atomic		ext2_set_bit_atomic
#endif
