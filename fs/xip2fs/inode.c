/*
 *  linux/fs/xip2fs/inode.c, Version 1
 *
 * (C) Copyright IBM Corp. 2002,2004
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 * derived from second extended filesystem (ext2)
 */


#include <linux/smp_lock.h>
#include <linux/time.h>
#include <linux/highuid.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/module.h>
#include <linux/mpage.h>
#include <asm/processor.h>
#include "xip2.h"
#include "acl.h"

#include <linux/pagevec.h>

MODULE_AUTHOR("Remy Card and others");
MODULE_DESCRIPTION("Second Extended Filesystem");
MODULE_LICENSE("GPL");

/*
 * Test whether an inode is a fast symlink.
 */
static inline int xip2_inode_is_fast_symlink(struct inode *inode)
{
	int ea_blocks = XIP2_I(inode)->i_file_acl ?
		(inode->i_sb->s_blocksize >> 9) : 0;

	return (S_ISLNK(inode->i_mode) &&
		inode->i_blocks - ea_blocks == 0);
}

typedef struct {
	u32	*p;
	u32	key;
	void *block_ptr;
} Indirect;

static inline void add_chain(Indirect *p, void *block_ptr, u32 *v)
{
	p->key = *(p->p = v);
	p->block_ptr = block_ptr;
}

static inline int verify_chain(Indirect *from, Indirect *to)
{
	while (from <= to && from->key == *from->p)
		from++;
	return (from > to);
}

/**
 *	xip2_block_to_path - parse the block number into array of offsets
 *	@inode: inode in question (we are only interested in its superblock)
 *	@i_block: block number to be parsed
 *	@offsets: array to store the offsets in
 *      @boundary: set this non-zero if the referred-to block is likely to be
 *             followed (on disk) by an indirect block.
 *	To store the locations of file's data ext2 uses a data structure common
 *	for UNIX filesystems - tree of pointers anchored in the inode, with
 *	data blocks at leaves and indirect blocks in intermediate nodes.
 *	This function translates the block number into path in that tree -
 *	return value is the path length and @offsets[n] is the offset of
 *	pointer to (n+1)th node in the nth one. If @block is out of range
 *	(negative or too large) warning is printed and zero returned.
 *
 *	Note: function doesn't find node addresses, so no IO is needed. All
 *	we need to know is the capacity of indirect blocks (taken from the
 *	inode->i_sb).
 */

/*
 * Portability note: the last comparison (check that we fit into triple
 * indirect block) is spelled differently, because otherwise on an
 * architecture with 32-bit longs and 8Kb pages we might get into trouble
 * if our filesystem had 8Kb blocks. We might use long long, but that would
 * kill us on x86. Oh, well, at least the sign propagation does not matter -
 * i_block would have to be negative in the very beginning, so we would not
 * get there at all.
 */

static int xip2_block_to_path(struct inode *inode,
			long i_block, int offsets[4])
{
	int ptrs = EXT2_ADDR_PER_BLOCK(inode->i_sb);
	int ptrs_bits = XIP2_ADDR_PER_BLOCK_BITS(inode->i_sb);
	const long direct_blocks = EXT2_NDIR_BLOCKS,
		indirect_blocks = ptrs,
		double_blocks = (1 << (ptrs_bits * 2));
	int n = 0;

	if (i_block < 0) {
		xip2_warning (inode->i_sb, "xip2_block_to_path", "block < 0");
	} else if (i_block < direct_blocks) {
		offsets[n++] = i_block;
	} else if ( (i_block -= direct_blocks) < indirect_blocks) {
		offsets[n++] = EXT2_IND_BLOCK;
		offsets[n++] = i_block;
	} else if ((i_block -= indirect_blocks) < double_blocks) {
		offsets[n++] = EXT2_DIND_BLOCK;
		offsets[n++] = i_block >> ptrs_bits;
		offsets[n++] = i_block & (ptrs - 1);
	} else if (((i_block -= double_blocks) >> (ptrs_bits * 2)) < ptrs) {
		offsets[n++] = EXT2_TIND_BLOCK;
		offsets[n++] = i_block >> (ptrs_bits * 2);
		offsets[n++] = (i_block >> ptrs_bits) & (ptrs - 1);
		offsets[n++] = i_block & (ptrs - 1);
	} else {
		xip2_warning (inode->i_sb, "xip2_block_to_path", "block > big");
	}
	return n;
}

/**
 *	xip2_get_branch - read the chain of indirect blocks leading to data
 *	@inode: inode in question
 *	@depth: depth of the chain (1 - direct pointer, etc.)
 *	@offsets: offsets of pointers in inode/indirect blocks
 *	@chain: place to store the result
 *	@err: here we store the error value
 *
 *	Function fills the array of triples <key, p, bh> and returns %NULL
 *	if everything went OK or the pointer to the last filled triple
 *	(incomplete one) otherwise. Upon the return chain[i].key contains
 *	the number of (i+1)-th block in the chain (as it is stored in memory,
 *	i.e. little-endian 32-bit), chain[i].p contains the address of that
 *	number (it points into struct inode for i==0 and into the bh->b_data
 *	for i>0) and chain[i].bh points to the buffer_head of i-th indirect
 *	block for i>0 and NULL for i==0. In other words, it holds the block
 *	numbers of the chain, addresses they were taken from (and where we can
 *	verify that chain did not change) and buffer_heads hosting these
 *	numbers.
 *
 *	Function stops when it stumbles upon zero pointer (absent block)
 *		(pointer to last triple returned, *@err == 0)
 *	or when it gets an IO error reading an indirect block
 *		(ditto, *@err == -EIO)
 *	or when it notices that chain had been changed while it was reading
 *		(ditto, *@err == -EAGAIN)
 *	or when it reads all @depth-1 indirect blocks successfully and finds
 *	the whole chain, all way to the data (returns %NULL, *err == 0).
 */
static Indirect *xip2_get_branch(struct inode *inode,
				 int depth,
				 int *offsets,
				 Indirect chain[4],
				 int *err)
{
	struct super_block *sb = inode->i_sb;
	Indirect *p = chain;
	void *block_ptr;

	*err = 0;
	/* i_data is not going away, no lock needed */
	add_chain (chain, NULL, XIP2_I(inode)->i_data + *offsets);
	if (!p->key)
		goto no_block;
	while (--depth) {
		block_ptr = xip2_sb_bread(sb, le32_to_cpu(p->key));
		if (!block_ptr)
			goto failure;
		read_lock(&XIP2_I(inode)->i_meta_lock);
		if (!verify_chain(chain, p))
			goto changed;
		add_chain(++p, block_ptr, (u32*)block_ptr + *++offsets);
		read_unlock(&XIP2_I(inode)->i_meta_lock);
		if (!p->key)
			goto no_block;
	}
	return NULL;

changed:
	read_unlock(&XIP2_I(inode)->i_meta_lock);
	*err = -EAGAIN;
	goto no_block;
failure:
	*err = -EIO;
no_block:
	return p;
}

/*
 * Allocation strategy is simple: if we have to allocate something, we will
 * have to go the whole way to leaf. So let's do it before attaching anything
 * to tree, set linkage between the newborn blocks, write them if sync is
 * required, recheck the path, free and repeat if check fails, otherwise
 * set the last missing link (that will protect us from any truncate-generated
 * removals - all blocks on the path are immune now) and possibly force the
 * write on the parent block.
 * That has a nice additional property: no special recovery from the failed
 * allocations is needed - we simply release blocks and do not touch anything
 * reachable from inode.
 */

int xip2_get_block(struct inode *inode, unsigned long iblock,
		   sector_t *blockno_result, int create)
{
	int err = -EIO;
	int offsets[4];
	Indirect chain[4];
	Indirect *partial;
	int depth = xip2_block_to_path(inode, iblock, offsets);

	/* Initialize blockno_result to illegal value */
#ifdef CONFIG_LBD
	*blockno_result = (~0ULL);
#else
	*blockno_result = (~0UL);
#endif

	if (depth == 0)
		goto out;

	partial = xip2_get_branch(inode, depth, offsets, chain, &err);

	/* Simplest case - block found, no allocation needed */
	if (!partial) {
		*blockno_result = le32_to_cpu(chain[depth-1].key);
		/* Clean up and exit */
		partial = chain+depth-1; /* the whole chain */
		goto cleanup;
	}

	/* Next simple case - plain lookup or failed read of indirect block */
	if (!create || err) {
cleanup:
		while (partial > chain) {
			partial--;
		}
out:
		return err;
	}
	xip2_warning (inode->i_sb, "xip2_get_block", "allocation of a block "
			"would be needed");
	return -EROFS;
}

static int xip2_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	unsigned long iblock;
	sector_t blockno;
	void* block_ptr;
	int err;

	printk("XIP2-error: xip2_readpage was called, "
		"stack trace will follow\n");
	dump_stack();

	if (!PageLocked(page))
		PAGE_BUG(page);
	iblock = page->index << (PAGE_CACHE_SHIFT - inode->i_blkbits);

	err=xip2_get_block(inode, iblock, &blockno, 0);
	if (err)
		return err;
	block_ptr = xip2_sb_bread(inode->i_sb, blockno);
	if (!block_ptr)
		return -EIO;
	memcpy (page_address(page),block_ptr,PAGE_SIZE);
	SetPageUptodate(page);
	unlock_page(page);
	return 0;
}

static int
xip2_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	unsigned page_idx;
	int rc;
	struct pagevec lru_pvec;
	
	printk("XIP2-error: xip2_readpages was called, "
		"stack trace will follow\n");
	dump_stack();


	pagevec_init(&lru_pvec, 0);
	for (page_idx = 0; page_idx < nr_pages; page_idx++) {
		struct page *page = list_entry (pages->prev, struct page, lru);
		
		prefetchw(&page->flags);
		list_del (&page->lru);
		if (!add_to_page_cache(page, mapping,
					page->index, GFP_KERNEL)) {

			page->mapping = mapping;
			rc = xip2_readpage (file, page);
			if (rc)
				return rc;
			if (!pagevec_add(&lru_pvec, page))
				__pagevec_lru_add(&lru_pvec);
		} else {
			page_cache_release(page);
		}
	}
	pagevec_lru_add(&lru_pvec);
	return 0;
}

static sector_t xip2_bmap(struct address_space *mapping, sector_t block)
{
	sector_t tmp;
	struct inode *inode = mapping->host;
	xip2_get_block(inode, block, &tmp, 0);
#ifdef CONFIG_LBD
	if (tmp == (~0ULL))
#else
	if (tmp == (~0UL))
#endif
		tmp = 0; //sparse block case
	return tmp;
}

struct address_space_operations xip2_aops = {
	.readpage	= xip2_readpage,
	.readpages	= xip2_readpages,
	.bmap		= xip2_bmap,
};

struct address_space_operations xip2_nobh_aops = {
	.readpage	= xip2_readpage,
	.readpages	= xip2_readpages,
	.bmap		= xip2_bmap,
};

/*
 * Probably it should be a library function... search for first non-zero word
 * or memcmp with zero_page, whatever is better for particular architecture.
 * Linus?
 */
static inline int all_zeroes(u32 *p, u32 *q)
{
	while (p < q)
		if (*p++)
			return 0;
	return 1;
}

static struct ext2_inode *xip2_get_inode(struct super_block *sb, ino_t ino,
					void **p)
{
	void * block_ptr;
	unsigned long block_group;
	unsigned long block;
	unsigned long offset;
	struct ext2_group_desc * gdp;

	*p = NULL;
	if ((ino != EXT2_ROOT_INO && ino < XIP2_FIRST_INO(sb)) ||
	    ino > le32_to_cpu(XIP2_SB(sb)->s_es->s_inodes_count))
		goto Einval;

	block_group = (ino - 1) / XIP2_INODES_PER_GROUP(sb);
	gdp = xip2_get_group_desc(sb, block_group, &block_ptr);
	if (!gdp)
		goto Egdp;
	/*
	 * Figure out the offset within the block group inode table
	 */
	offset = ((ino - 1) % XIP2_INODES_PER_GROUP(sb)) * XIP2_INODE_SIZE(sb);
	block = le32_to_cpu(gdp->bg_inode_table) +
		(offset >> EXT2_BLOCK_SIZE_BITS(sb));
	if (!(block_ptr = xip2_sb_bread(sb, block)))
		goto Eio;

	*p = block_ptr;
	offset &= (EXT2_BLOCK_SIZE(sb) - 1);
	return (struct ext2_inode *) (block_ptr + offset);

Einval:
	xip2_error(sb, "xip2_get_inode", "bad inode number: %lu",
		   (unsigned long) ino);
	return ERR_PTR(-EINVAL);
Eio:
	xip2_error(sb, "xip2_get_inode",
		   "unable to read inode block - inode=%lu, block=%lu",
		   (unsigned long) ino, block);
Egdp:
	return ERR_PTR(-EIO);
}

void xip2_set_inode_flags(struct inode *inode)
{
	unsigned int flags = XIP2_I(inode)->i_flags;

	inode->i_flags &= ~(S_SYNC|S_APPEND|S_IMMUTABLE|S_NOATIME|S_DIRSYNC);
	if (flags & EXT2_SYNC_FL)
		inode->i_flags |= S_SYNC;
	if (flags & EXT2_APPEND_FL)
		inode->i_flags |= S_APPEND;
	if (flags & EXT2_IMMUTABLE_FL)
		inode->i_flags |= S_IMMUTABLE;
	if (flags & EXT2_NOATIME_FL)
		inode->i_flags |= S_NOATIME;
	if (flags & EXT2_DIRSYNC_FL)
		inode->i_flags |= S_DIRSYNC;
}

void xip2_read_inode (struct inode * inode)
{
	struct xip2_inode_info *ei = XIP2_I(inode);
	ino_t ino = inode->i_ino;
	void *block_ptr;
	struct ext2_inode *raw_inode = xip2_get_inode(inode->i_sb, ino,
						      &block_ptr);
	int n;

#ifdef CONFIG_XIP2_FS_POSIX_ACL
	ei->i_acl = XIP2_ACL_NOT_CACHED;
	ei->i_default_acl = XIP2_ACL_NOT_CACHED;
#endif
	if (IS_ERR(raw_inode))
		goto bad_inode;

	inode->i_mode = le16_to_cpu(raw_inode->i_mode);
	inode->i_uid = (uid_t)le16_to_cpu(raw_inode->i_uid_low);
	inode->i_gid = (gid_t)le16_to_cpu(raw_inode->i_gid_low);
	if (!(test_opt (inode->i_sb, NO_UID32))) {
		inode->i_uid |= le16_to_cpu(raw_inode->i_uid_high) << 16;
		inode->i_gid |= le16_to_cpu(raw_inode->i_gid_high) << 16;
	}
	inode->i_nlink = le16_to_cpu(raw_inode->i_links_count);
	inode->i_size = le32_to_cpu(raw_inode->i_size);
	inode->i_atime.tv_sec = le32_to_cpu(raw_inode->i_atime);
	inode->i_ctime.tv_sec = le32_to_cpu(raw_inode->i_ctime);
	inode->i_mtime.tv_sec = le32_to_cpu(raw_inode->i_mtime);
	inode->i_atime.tv_nsec = inode->i_mtime.tv_nsec =
				 inode->i_ctime.tv_nsec = 0;
	ei->i_dtime = le32_to_cpu(raw_inode->i_dtime);
	/* We now have enough fields to check if the inode was active or not.
	 * This is needed because nfsd might try to access dead inodes
	 * the test is that same one that e2fsck uses
	 * NeilBrown 1999oct15
	 */
	if (inode->i_nlink == 0 && (inode->i_mode == 0 || ei->i_dtime)) {
		/* this inode is deleted */
		goto bad_inode;
	}
	/* This is the optimal IO size (for stat), not the fs block size */
	inode->i_blksize = PAGE_SIZE;
	inode->i_blocks = le32_to_cpu(raw_inode->i_blocks);
	ei->i_flags = le32_to_cpu(raw_inode->i_flags);
	ei->i_faddr = le32_to_cpu(raw_inode->i_faddr);
	ei->i_frag_no = raw_inode->i_frag;
	ei->i_frag_size = raw_inode->i_fsize;
	ei->i_file_acl = le32_to_cpu(raw_inode->i_file_acl);
	ei->i_dir_acl = 0;
	if (S_ISREG(inode->i_mode))
		inode->i_size |= ((__u64)le32_to_cpu(raw_inode->i_size_high))
				 << 32;
	else
		ei->i_dir_acl = le32_to_cpu(raw_inode->i_dir_acl);
	ei->i_dtime = 0;
	inode->i_generation = le32_to_cpu(raw_inode->i_generation);
	ei->i_state = 0;
	ei->i_next_alloc_block = 0;
	ei->i_next_alloc_goal = 0;
	ei->i_prealloc_count = 0;
	ei->i_block_group = (ino - 1) / XIP2_INODES_PER_GROUP(inode->i_sb);
	ei->i_dir_start_lookup = 0;

	/*
	 * NOTE! The in-memory inode i_data array is in little-endian order
	 * even on big-endian machines: we do NOT byteswap the block numbers!
	 */
	for (n = 0; n < EXT2_N_BLOCKS; n++)
		ei->i_data[n] = raw_inode->i_block[n];

	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &xip2_file_inode_operations;
		inode->i_fop = &xip2_file_operations;
		if (test_opt(inode->i_sb, NOBH))
			inode->i_mapping->a_ops = &xip2_nobh_aops;
		else
			inode->i_mapping->a_ops = &xip2_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &xip2_dir_inode_operations;
		inode->i_fop = &xip2_dir_operations;
		if (test_opt(inode->i_sb, NOBH))
			inode->i_mapping->a_ops = &xip2_nobh_aops;
		else
			inode->i_mapping->a_ops = &xip2_aops;
	} else if (S_ISLNK(inode->i_mode)) {
		if (xip2_inode_is_fast_symlink(inode))
			inode->i_op = &xip2_fast_symlink_inode_operations;
		else {
			inode->i_op = &xip2_symlink_inode_operations;
			if (test_opt(inode->i_sb, NOBH))
				inode->i_mapping->a_ops = &xip2_nobh_aops;
			else
				inode->i_mapping->a_ops = &xip2_aops;
		}
	} else {
		inode->i_op = &xip2_special_inode_operations;
		if (raw_inode->i_block[0])
			init_special_inode(inode, inode->i_mode,
			   old_decode_dev(le32_to_cpu(raw_inode->i_block[0])));
		else 
			init_special_inode(inode, inode->i_mode,
			   new_decode_dev(le32_to_cpu(raw_inode->i_block[1])));
	}
	xip2_set_inode_flags(inode);
	return;
	
bad_inode:
	make_bad_inode(inode);
	return;
}
