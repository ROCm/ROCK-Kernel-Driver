/*
 * linux/fs/fat/buffer.c
 *
 *
 */

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/fat_cvf.h>

struct buffer_head *fat_bread(struct super_block *sb, int block)
{
	return MSDOS_SB(sb)->cvf_format->cvf_bread(sb,block);
}
struct buffer_head *fat_getblk(struct super_block *sb, int block)
{
	return MSDOS_SB(sb)->cvf_format->cvf_getblk(sb,block);
}
void fat_brelse (struct super_block *sb, struct buffer_head *bh)
{
	if (bh) 
		MSDOS_SB(sb)->cvf_format->cvf_brelse(sb,bh);
}
void fat_mark_buffer_dirty (
	struct super_block *sb,
	struct buffer_head *bh)
{
	MSDOS_SB(sb)->cvf_format->cvf_mark_buffer_dirty(sb,bh);
}
void fat_set_uptodate (
	struct super_block *sb,
	struct buffer_head *bh,
	int val)
{
	MSDOS_SB(sb)->cvf_format->cvf_set_uptodate(sb,bh,val);
}
int fat_is_uptodate(struct super_block *sb, struct buffer_head *bh)
{
	return MSDOS_SB(sb)->cvf_format->cvf_is_uptodate(sb,bh);
}
void fat_ll_rw_block (
	struct super_block *sb,
	int opr,
	int nbreq,
	struct buffer_head *bh[32])
{
	MSDOS_SB(sb)->cvf_format->cvf_ll_rw_block(sb,opr,nbreq,bh);
}

struct buffer_head *default_fat_bread(struct super_block *sb, int block)
{
	return sb_bread(sb, block);
}

struct buffer_head *default_fat_getblk(struct super_block *sb, int block)
{
	return sb_getblk(sb, block);
}

void default_fat_brelse(struct super_block *sb, struct buffer_head *bh)
{
	brelse (bh);
}

void default_fat_mark_buffer_dirty (
	struct super_block *sb,
	struct buffer_head *bh)
{
	mark_buffer_dirty (bh);
}

void default_fat_set_uptodate (
	struct super_block *sb,
	struct buffer_head *bh,
	int val)
{
	if (val)
		set_buffer_uptodate(bh);
	else
		clear_buffer_uptodate(bh);
}

int default_fat_is_uptodate (struct super_block *sb, struct buffer_head *bh)
{
	return buffer_uptodate(bh);
}

void default_fat_ll_rw_block (
	struct super_block *sb,
	int opr,
	int nbreq,
	struct buffer_head *bh[32])
{
	ll_rw_block(opr,nbreq,bh);
}
