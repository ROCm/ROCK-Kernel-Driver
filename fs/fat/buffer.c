/*
 * linux/fs/fat/buffer.c
 *
 *
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/msdos_fs.h>
#include <linux/fat_cvf.h>

#if 0
#  define PRINTK(x) printk x
#else
#  define PRINTK(x)
#endif

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
	return bread (sb->s_dev, block, sb->s_blocksize);
}

struct buffer_head *default_fat_getblk(struct super_block *sb, int block)
{
	return getblk (sb->s_dev, block, sb->s_blocksize);
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
	mark_buffer_uptodate(bh, val);
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

struct buffer_head *bigblock_fat_bread(struct super_block *sb, int block)
{
	unsigned int hardsect = get_hardsect_size(sb->s_dev);
	int rblock, roffset;
	struct buffer_head *real, *dummy;

	if (hardsect <= sb->s_blocksize)
		BUG();

	dummy = NULL;
	rblock = block / (hardsect / sb->s_blocksize);
	roffset = (block % (hardsect / sb->s_blocksize)) * sb->s_blocksize;
	real = bread(sb->s_dev, rblock, hardsect);
	if (real != NULL) {
		dummy = kmalloc(sizeof(struct buffer_head), GFP_KERNEL);
		if (dummy != NULL) {
			memset(dummy, 0, sizeof(*dummy));
			dummy->b_data = real->b_data + roffset;
			dummy->b_next = real;
		} else
			brelse(real);
	}

	return dummy;
}

void bigblock_fat_brelse(struct super_block *sb, struct buffer_head *bh)
{
	brelse(bh->b_next);
	kfree(bh);
}

void bigblock_fat_mark_buffer_dirty(struct super_block *sb, struct buffer_head *bh)
{
	mark_buffer_dirty(bh->b_next);
}

void bigblock_fat_set_uptodate(struct super_block *sb, struct buffer_head *bh,
			       int val)
{
	mark_buffer_uptodate(bh->b_next, val);
}

int bigblock_fat_is_uptodate(struct super_block *sb, struct buffer_head *bh)
{
	return buffer_uptodate(bh->b_next);
}

void bigblock_fat_ll_rw_block (struct super_block *sb, int opr, int nbreq,
			       struct buffer_head *bh[32])
{
	struct buffer_head *tmp[32];
	int i;

	for (i = 0; i < nbreq; i++)
		tmp[i] = bh[i]->b_next;
	ll_rw_block(opr, nbreq, tmp);
}
