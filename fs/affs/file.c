/*
 *  linux/fs/affs/file.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1992  Eric Youngdale Modified for ISO 9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 *
 *  affs regular file handling primitives
 */

#define DEBUG 0
#include <asm/div64.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/sched.h>
#include <linux/affs_fs.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/stat.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <linux/dirent.h>
#include <linux/fs.h>
#include <linux/amigaffs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#if PAGE_SIZE < 4096
#error PAGE_SIZE must be at least 4096
#endif

static struct buffer_head *affs_getblock(struct inode *inode, s32 block);
static ssize_t affs_file_read_ofs(struct file *filp, char *buf, size_t count, loff_t *ppos);
static ssize_t affs_file_write(struct file *filp, const char *buf, size_t count, loff_t *ppos);
static ssize_t affs_file_write_ofs(struct file *filp, const char *buf, size_t cnt, loff_t *ppos);
static int alloc_ext_cache(struct inode *inode);

struct file_operations affs_file_operations = {
	read:		generic_file_read,
	write:		affs_file_write,
	mmap:		generic_file_mmap,
	fsync:		file_fsync,
};

struct inode_operations affs_file_inode_operations = {
	truncate:	affs_truncate,
	setattr:	affs_notify_change,
};

struct file_operations affs_file_operations_ofs = {
	read:		affs_file_read_ofs,
	write:		affs_file_write_ofs,
	fsync:		file_fsync,
};

#define AFFS_ISINDEX(x)	((x < 129) ||				\
			 (x < 512 && (x & 1) == 0) ||		\
			 (x < 1024 && (x & 3) == 0) ||		\
			 (x < 2048 && (x & 15) == 0) ||		\
			 (x < 4096 && (x & 63) == 0) ||		\
			 (x < 20480 && (x & 255) == 0) ||	\
			 (x < 36864 && (x & 511) == 0))

/* The keys of the extension blocks are stored in a 512-entry
 * deep cache. In order to save memory, not every key of later
 * extension blocks is stored - the larger the file gets, the
 * bigger the holes in between.
 */

static int
seqnum_to_index(int seqnum)
{
	/* All of the first 127 keys are stored */
	if (seqnum < 128)
		return seqnum;
	seqnum -= 128;

	/* Of the next 384 keys, every 2nd is kept */
	if (seqnum < (192 * 2))
		return 128 + (seqnum >> 1);
	seqnum -= 192 * 2;
	
	/* Every 4th of the next 512 */
	if (seqnum < (128 * 4))
		return 128 + 192 + (seqnum >> 2);
	seqnum -= 128 * 4;

	/* Every 16th of the next 1024 */
	if (seqnum < (64 * 16))
		return 128 + 192 + 128 + (seqnum >> 4);
	seqnum -= 64 * 16;

	/* Every 64th of the next 2048 */
	if (seqnum < (32 * 64))
		return 128 + 192 + 128 + 64 + (seqnum >> 6);
	seqnum -= 32 * 64;

	/* Every 256th of the next 16384 */
	if (seqnum < (64 * 256))
		return 128 + 192 + 128 + 64 + 32 + (seqnum >> 8);
	seqnum -= 64 * 256;

	/* Every 512th upto 36479 (1.3 GB with 512 byte blocks).
	 * Seeking to positions behind this will get slower
	 * than dead snails nailed to the ground. But if
	 * someone uses files that large with 512-byte blocks,
	 * he or she deserves no better.
	 */
	
	if (seqnum > (31 * 512))
		seqnum = 31 * 512;
	return 128 + 192 + 128 + 64 + 32 + 64 + (seqnum >> 9);
}

/* Now the other way round: Calculate the sequence
 * number of an extension block of a key at the
 * given index in the cache.
 */

static int
index_to_seqnum(int index)
{
	if (index < 128)
		return index;
	index -= 128;
	if (index < 192)
		return 128 + (index << 1);
	index -= 192;
	if (index < 128)
		return 128 + 192 * 2 + (index << 2);
	index -= 128;
	if (index < 64)
		return 128 + 192 * 2 + 128 * 4 + (index << 4);
	index -= 64;
	if (index < 32)
		return 128 + 192 * 2 + 128 * 4 + 64 * 16 + (index << 6);
	index -= 32;
	if (index < 64)
		return 128 + 192 * 2 + 128 * 4 + 64 * 16 + 32 * 64 + (index << 8);
	index -= 64;
	return 128 + 192 * 2 + 128 * 4 + 64 * 16 + 32 * 64 + 64 * 256 + (index << 9);
}

static s32 __inline__
calc_key(struct inode *inode, int *ext)
{
	int		  index;
	struct key_cache *kc;

	for (index = 0; index < 4; index++) {
		kc = &inode->u.affs_i.i_ec->kc[index];
		if (kc->kc_last == -1)
			continue;	/* don't look in cache if invalid. */
		if (*ext == kc->kc_this_seq) {
			return kc->kc_this_key;
		} else if (*ext == kc->kc_this_seq + 1) {
			if (kc->kc_next_key)
				return kc->kc_next_key;
			else {
				(*ext)--;
				return kc->kc_this_key;
			}
		}
	}
	index = seqnum_to_index(*ext);
	if (index > inode->u.affs_i.i_ec->max_ext)
		index = inode->u.affs_i.i_ec->max_ext;
	*ext = index_to_seqnum(index);
	return inode->u.affs_i.i_ec->ec[index];
}

int
affs_bmap(struct inode *inode, int block)
{
	struct buffer_head	*bh;
	s32			 key, nkey;
	s32			 ptype, stype;
	int			 ext;
	int			 index;
	int			 keycount;
	struct key_cache	*kc;
	struct key_cache	*tkc;
	struct timeval		 tv;
	s32			*keyp;
	int			 i;

	pr_debug("AFFS: bmap(%lu,%d)\n",inode->i_ino,block);

	lock_kernel();
	if (block < 0) {
		affs_error(inode->i_sb,"bmap","Block < 0");
		goto out_fail;
	}
	if (!inode->u.affs_i.i_ec) {
		if (alloc_ext_cache(inode)) {
			goto out_fail;
		}
	}

	/* Try to find the requested key in the cache.
	 * In order to speed this up as much as possible,
	 * the cache line lookup is done in a separate
	 * step.
	 */

	for (i = 0; i < 4; i++) {
		tkc = &inode->u.affs_i.i_ec->kc[i];
		/* Look in any cache if the key is there */
		if (block <= tkc->kc_last && block >= tkc->kc_first) {
			unlock_kernel();
			return tkc->kc_keys[block - tkc->kc_first];
		}
	}
	kc = NULL;
	tv = xtime;
	for (i = 0; i < 4; i++) {
		tkc = &inode->u.affs_i.i_ec->kc[i];
		if (tkc->kc_lru_time.tv_sec > tv.tv_sec)
			continue;
		if (tkc->kc_lru_time.tv_sec < tv.tv_sec ||
		    tkc->kc_lru_time.tv_usec < tv.tv_usec) {
			kc = tkc;
			tv = tkc->kc_lru_time;
		}
	}
	if (!kc)	/* Really shouldn't happen */
		kc = tkc;
	kc->kc_lru_time = xtime;
	keyp            = kc->kc_keys;
	kc->kc_first    = block;
	kc->kc_last     = -1;
	keycount        = AFFS_KCSIZE;

	/* Calculate sequence number of the extension block where the
	 * number of the requested block is stored. 0 means it's in
	 * the file header.
	 */

	ext    = block / AFFS_I2HSIZE(inode);
	key    = calc_key(inode,&ext);
	block -= ext * AFFS_I2HSIZE(inode);

	for (;;) {
		bh = affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode));
		if (!bh)
			goto out_fail;

		index = seqnum_to_index(ext);
		if (index > inode->u.affs_i.i_ec->max_ext &&
		    (affs_checksum_block(AFFS_I2BSIZE(inode),bh->b_data,&ptype,&stype) ||
		     (ptype != T_SHORT && ptype != T_LIST) || stype != ST_FILE)) {
			affs_brelse(bh);
			goto out_fail;
		}
		nkey = be32_to_cpu(FILE_END(bh->b_data,inode)->extension);
		if (block < AFFS_I2HSIZE(inode)) {
			/* Fill cache as much as possible */
			if (keycount) {
				kc->kc_first = ext * AFFS_I2HSIZE(inode) + block;
				keycount     = keycount < AFFS_I2HSIZE(inode) - block ? keycount :
						AFFS_I2HSIZE(inode) - block;
				for (i = 0; i < keycount; i++)
					kc->kc_keys[i] = be32_to_cpu(AFFS_BLOCK(bh->b_data,inode,block + i));
				kc->kc_last = kc->kc_first + i - 1;
			}
			break;
		}
		block -= AFFS_I2HSIZE(inode);
		affs_brelse(bh);
		ext++;
		if (index > inode->u.affs_i.i_ec->max_ext && AFFS_ISINDEX(ext)) {
			inode->u.affs_i.i_ec->ec[index] = nkey;
			inode->u.affs_i.i_ec->max_ext   = index;
		}
		key = nkey;
	}
	kc->kc_this_key = key;
	kc->kc_this_seq = ext;
	kc->kc_next_key = nkey;
	key = be32_to_cpu(AFFS_BLOCK(bh->b_data,inode,block));
	affs_brelse(bh);
out:
	unlock_kernel();
	return key;

out_fail:
	key=0;
	goto out;
}


static int affs_get_block(struct inode *inode, long block, struct buffer_head *bh_result, int create)
{
	int err, phys=0, new=0;

	if (!create) {
		phys = affs_bmap(inode, block);
		if (phys) {
			bh_result->b_dev = inode->i_dev;
			bh_result->b_blocknr = phys;
			bh_result->b_state |= (1UL << BH_Mapped);
		}
		return 0;
	}

	err = -EIO;
	lock_kernel();
	if (block < 0)
		goto abort_negative;

	if (affs_getblock(inode, block)==NULL) {
		err = -EIO;
		goto abort;
	}

	bh_result->b_dev = inode->i_dev;
	bh_result->b_blocknr = phys;
	bh_result->b_state |= (1UL << BH_Mapped);
	if (new)
		bh_result->b_state |= (1UL << BH_New);
	
abort:
	unlock_kernel();
	return err;

abort_negative:
	affs_error(inode->i_sb,"affs_get_block","Block < 0");
	goto abort;

}
		
static int affs_writepage(struct page *page)
{
	return block_write_full_page(page,affs_get_block);
}
static int affs_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page,affs_get_block);
}
static int affs_prepare_write(struct file *file, struct page *page, unsigned from, unsigned to)
{
	return cont_prepare_write(page,from,to,affs_get_block,
		&page->mapping->host->u.affs_i.mmu_private);
}
static int _affs_bmap(struct address_space *mapping, long block)
{
	return generic_block_bmap(mapping,block,affs_get_block);
}
struct address_space_operations affs_aops = {
	readpage: affs_readpage,
	writepage: affs_writepage,
	sync_page: block_sync_page,
	prepare_write: affs_prepare_write,
	commit_write: generic_commit_write,
	bmap: _affs_bmap
};

/* With the affs, getting a random block from a file is not
 * a simple business. Since this fs does not allow holes,
 * it may be necessary to allocate all the missing blocks
 * in between, as well as some new extension blocks. The OFS
 * is even worse: All data blocks contain pointers to the
 * next ones, so you have to fix [n-1] after allocating [n].
 * What a mess.
 */

static struct buffer_head * affs_getblock(struct inode *inode, s32 block)
{
	struct super_block	*sb = inode->i_sb;
	int			 ofs = sb->u.affs_sb.s_flags & SF_OFS;
	int			 ext = block / AFFS_I2HSIZE(inode);
	struct buffer_head	*bh, *ebh, *pbh = NULL;
	struct key_cache	*kc;
	s32			 key, nkey;
	int			 cf, j, pt;
	int			 index;
	int			 err;

	pr_debug("AFFS: getblock(%lu,%d)\n",inode->i_ino,block);

	key    = calc_key(inode,&ext);
	block -= ext * AFFS_I2HSIZE(inode);
	pt     = ext ? T_LIST : T_SHORT;

	/* Key refers now to the last known extension block,
	 * ext is its sequence number (if 0, key refers to the
	 * header block), and block is the block number relative
	 * to the first block stored in that extension block.
	 */
	for (;;) {	/* Loop over header block and extension blocks */
		struct file_front *fdp;

		bh = affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode));
		if (!bh)
			goto out_fail;
		fdp = (struct file_front *) bh->b_data;
		err = affs_checksum_block(AFFS_I2BSIZE(inode),bh->b_data,&cf,&j);
		if (err || cf != pt || j != ST_FILE) {
		    	affs_error(sb, "getblock",
				"Block %d is not a valid %s", key,
				pt == T_SHORT ? "file header" : "ext block");
			goto out_free_bh;
		}
		j  = be32_to_cpu(((struct file_front *)bh->b_data)->block_count);
		for (cf = 0; j < AFFS_I2HSIZE(inode) && j <= block; j++) {
			if (ofs && !pbh && inode->u.affs_i.i_lastblock >= 0) {
				if (j > 0) {
					s32 k = AFFS_BLOCK(bh->b_data, inode, j - 1);
					pbh = affs_bread(inode->i_dev,
							be32_to_cpu(k),
							AFFS_I2BSIZE(inode));
				} else
					pbh = affs_getblock(inode,inode->u.affs_i.i_lastblock);
				if (!pbh) {
					affs_error(sb,"getblock", "Cannot get last block in file");
					break;
				}
			}
			nkey = affs_new_data(inode);
			if (!nkey)
				break;
			inode->u.affs_i.i_lastblock++;
			if (AFFS_BLOCK(bh->b_data,inode,j)) {
				affs_warning(sb,"getblock","Block already allocated");
				affs_free_block(sb,nkey);
				continue;
			}
			AFFS_BLOCK(bh->b_data,inode,j) = cpu_to_be32(nkey);
			if (ofs) {
				ebh = affs_bread(inode->i_dev,nkey,AFFS_I2BSIZE(inode));
				if (!ebh) {
					affs_error(sb,"getblock", "Cannot get block %d",nkey);
					affs_free_block(sb,nkey);
					AFFS_BLOCK(bh->b_data,inode,j) = 0;
					break;
				}
				DATA_FRONT(ebh)->primary_type    = cpu_to_be32(T_DATA);
				DATA_FRONT(ebh)->header_key      = cpu_to_be32(inode->i_ino);
				DATA_FRONT(ebh)->sequence_number = cpu_to_be32(inode->u.affs_i.i_lastblock + 1);
				affs_fix_checksum(AFFS_I2BSIZE(inode), ebh->b_data, 5);
				mark_buffer_dirty(ebh);
				if (pbh) {
					DATA_FRONT(pbh)->data_size = cpu_to_be32(AFFS_I2BSIZE(inode) - 24);
					DATA_FRONT(pbh)->next_data = cpu_to_be32(nkey);
					affs_fix_checksum(AFFS_I2BSIZE(inode),pbh->b_data,5);
					mark_buffer_dirty(pbh);
					affs_brelse(pbh);
				}
				pbh = ebh;
			}
			cf = 1;
		}
		/* N.B. May need to release pbh after here */

		if (cf) {
			if (pt == T_SHORT)
				fdp->first_data = AFFS_BLOCK(bh->b_data,inode,0);
			fdp->block_count = cpu_to_be32(j);
			affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
			mark_buffer_dirty(bh);
		}

		if (block < j) {
			if (pbh)
				affs_brelse(pbh);
			break;
		}
		if (j < AFFS_I2HSIZE(inode)) {
			/* N.B. What about pbh here? */
			goto out_free_bh;
		}

		block -= AFFS_I2HSIZE(inode);
		key    = be32_to_cpu(FILE_END(bh->b_data,inode)->extension);
		if (!key) {
			key = affs_new_header(inode);
			if (!key)
				goto out_free_bh;
			ebh = affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode));
			if (!ebh) {
				/* N.B. must free bh here */
				goto out_free_block;
			}
			((struct file_front *)ebh->b_data)->primary_type = cpu_to_be32(T_LIST);
			((struct file_front *)ebh->b_data)->own_key      = cpu_to_be32(key);
			FILE_END(ebh->b_data,inode)->secondary_type      = cpu_to_be32(ST_FILE);
			FILE_END(ebh->b_data,inode)->parent              = cpu_to_be32(inode->i_ino);
			affs_fix_checksum(AFFS_I2BSIZE(inode),ebh->b_data,5);
			mark_buffer_dirty(ebh);
			FILE_END(bh->b_data,inode)->extension = cpu_to_be32(key);
			affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
			mark_buffer_dirty(bh);
			affs_brelse(bh);
			bh = ebh;
		}
		pt = T_LIST;
		ext++;
		index = seqnum_to_index(ext);
		if (index > inode->u.affs_i.i_ec->max_ext &&
		    AFFS_ISINDEX(ext)) {
			inode->u.affs_i.i_ec->ec[index] = key;
			inode->u.affs_i.i_ec->max_ext   = index;
		}
		affs_brelse(bh);
	}

	/* Invalidate key cache */
	for (j = 0; j < 4; j++) {
		kc = &inode->u.affs_i.i_ec->kc[j];
		kc->kc_last = -1;
	}
	key = be32_to_cpu(AFFS_BLOCK(bh->b_data,inode,block));
	affs_brelse(bh);
	if (!key)
		goto out_fail;

	bh = affs_bread(inode->i_dev, key, AFFS_I2BSIZE(inode));
	return bh;

out_free_block:
	affs_free_block(sb, key);
out_free_bh:
	affs_brelse(bh);
out_fail:
	return NULL;
}

static ssize_t
affs_file_read_ofs(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	struct inode		*inode = filp->f_dentry->d_inode;
	char			*start;
	ssize_t			 left, offset, size, sector;
	ssize_t			 blocksize;
	struct buffer_head	*bh;
	void			*data;
	loff_t		tmp;

	pr_debug("AFFS: file_read_ofs(ino=%lu,pos=%lu,%d)\n",inode->i_ino,
		 (unsigned long)*ppos,count);

	if (!inode) {
		affs_error(inode->i_sb,"file_read_ofs","Inode = NULL");
		return -EINVAL;
	}
	blocksize = AFFS_I2BSIZE(inode) - 24;
	if (!(S_ISREG(inode->i_mode))) {
		pr_debug("AFFS: file_read: mode = %07o",inode->i_mode);
		return -EINVAL;
	}
	if (*ppos >= inode->i_size || count <= 0)
		return 0;

	start = buf;
	for (;;) {
		left = MIN (inode->i_size - *ppos,count - (buf - start));
		if (!left)
			break;
		tmp = *ppos;
		do_div(tmp, blocksize);
		sector = affs_bmap(inode, tmp);
		if (!sector)
			break;
		tmp = *ppos;
		offset = do_div(tmp, blocksize);
		bh = affs_bread(inode->i_dev,sector,AFFS_I2BSIZE(inode));
		if (!bh)
			break;
		data = bh->b_data + 24;
		size = MIN(blocksize - offset,left);
		*ppos += size;
		copy_to_user(buf,data + offset,size);
		buf += size;
		affs_brelse(bh);
	}
	if (start == buf)
		return -EIO;
	return buf - start;
}

static ssize_t
affs_file_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	ssize_t retval;

	retval = generic_file_write (file, buf, count, ppos);
	if (retval >0) {
		struct inode *inode = file->f_dentry->d_inode;
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		mark_inode_dirty(inode);
	}
	return retval;
}

static ssize_t
affs_file_write_ofs(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	ssize_t retval;

	retval = generic_file_write (file, buf, count, ppos);
	if (retval >0) {
		struct inode *inode = file->f_dentry->d_inode;
		inode->i_ctime = inode->i_mtime = CURRENT_TIME;
		mark_inode_dirty(inode);
	}
	return retval;
}

/* Free any preallocated blocks. */

void
affs_free_prealloc(struct inode *inode)
{
	struct super_block	*sb = inode->i_sb;
	struct affs_zone	*zone;
	int block;

	pr_debug("AFFS: free_prealloc(ino=%lu)\n", inode->i_ino);

	while (inode->u.affs_i.i_pa_cnt) {	
		block = inode->u.affs_i.i_data[inode->u.affs_i.i_pa_next++];
		inode->u.affs_i.i_pa_next &= AFFS_MAX_PREALLOC - 1;
		inode->u.affs_i.i_pa_cnt--;
		affs_free_block(sb, block);
	}
	if (inode->u.affs_i.i_zone) {
		zone = &sb->u.affs_sb.s_zones[inode->u.affs_i.i_zone];
		if (zone->z_ino == inode->i_ino)
			zone->z_ino = 0;
	}
}

/* Truncate (or enlarge) a file to the requested size. */

void
affs_truncate(struct inode *inode)
{
	struct buffer_head	*bh = NULL;
	int	 first;			/* First block to be thrown away	*/
	int	 block;
	s32	 key;
	s32	*keyp;
	s32	 ekey;
	s32	 ptype, stype;
	int	 freethis;
	int	 net_blocksize;
	int	 blocksize = AFFS_I2BSIZE(inode);
	int	 rem;
	int	 ext;
	loff_t tmp;

	pr_debug("AFFS: truncate(inode=%ld,size=%lu)\n",inode->i_ino,inode->i_size);

	net_blocksize = blocksize - ((inode->i_sb->u.affs_sb.s_flags & SF_OFS) ? 24 : 0);
	first = inode->i_size + net_blocksize -1;
	do_div (first, net_blocksize);
	if (inode->u.affs_i.i_lastblock < first - 1) {
		/* There has to be at least one new block to be allocated */
		if (!inode->u.affs_i.i_ec && alloc_ext_cache(inode)) {
			/* XXX Fine! No way to indicate an error. */
			return /* -ENOSPC */;
		}
		bh = affs_getblock(inode,first - 1);
		if (!bh) {
			affs_warning(inode->i_sb,"truncate","Cannot extend file");
			inode->i_size = net_blocksize * (inode->u.affs_i.i_lastblock + 1);
		} else if (inode->i_sb->u.affs_sb.s_flags & SF_OFS) {
			tmp = inode->i_size;
			rem = do_div(tmp, net_blocksize);
			DATA_FRONT(bh)->data_size = cpu_to_be32(rem ? rem : net_blocksize);
			affs_fix_checksum(blocksize,bh->b_data,5);
			mark_buffer_dirty(bh);
		}
		goto out_truncate;
	}
	ekey = inode->i_ino;
	ext  = 0;

	/* Free all blocks starting at 'first' and all then-empty
	 * extension blocks. Do not free the header block, though.
	 */
	while (ekey) {
		if (!(bh = affs_bread(inode->i_dev,ekey,blocksize))) {
			affs_error(inode->i_sb,"truncate","Cannot read block %d",ekey);
			goto out_truncate;
		}
		if (affs_checksum_block(blocksize,bh->b_data,&ptype,&stype)) {
			affs_error(inode->i_sb,"truncate","Checksum error in header/ext block %d",
				   ekey);
			goto out_truncate;
		}
		if (stype != ST_FILE || (ptype != T_SHORT && ptype != T_LIST)) {
			affs_error(inode->i_sb,"truncate",
				   "Bad block (key=%d, ptype=%d, stype=%d)",ekey,ptype,stype);
			goto out_truncate;
		}
		/* Do we have to free this extension block after
		 * freeing the data blocks pointed to?
		 */
		freethis = first == 0 && ekey != inode->i_ino;

		/* Free the data blocks. 'first' is relative to this
		 * extension block and may well lie behind this block.
		 */
		for (block = first; block < AFFS_I2HSIZE(inode); block++) {
			keyp = &AFFS_BLOCK(bh->b_data,inode,block);
			key  = be32_to_cpu(*keyp);
			if (key) {
				*keyp = 0;
				affs_free_block(inode->i_sb,key);
			} else
				break;
		}
		keyp = &GET_END_PTR(struct file_end,bh->b_data,blocksize)->extension;
		key  = be32_to_cpu(*keyp);

		/* If 'first' is in this block or is the first
		 * in the next one, this will be the last in
		 * the list, thus we have to adjust the count
		 * and zero the pointer to the next ext block.
		 */
		if (first <= AFFS_I2HSIZE(inode)) {
			((struct file_front *)bh->b_data)->block_count = cpu_to_be32(first);
			first = 0;
			*keyp = 0;
			affs_fix_checksum(blocksize,bh->b_data,5);
			mark_buffer_dirty(bh);
		} else
			first -= AFFS_I2HSIZE(inode);
		affs_brelse(bh);
		bh = NULL;
		if (freethis)			/* Don't bother fixing checksum */
			affs_free_block(inode->i_sb,ekey);
		ekey = key;
	}
	block = inode->i_size + net_blocksize - 1;
	do_div (block, net_blocksize);
	block--;
	inode->u.affs_i.i_lastblock = block;

	/* If the file is not truncated to a block boundary,
	 * the partial block after the EOF must be zeroed
	 * so it cannot become accessible again.
	 */

	tmp = inode->i_size;
	rem = do_div(tmp, net_blocksize);
	if (rem) {
		if ((inode->i_sb->u.affs_sb.s_flags & SF_OFS)) 
			rem += 24;
		pr_debug("AFFS: Zeroing from offset %d in block %d\n",rem,block);
		bh = affs_getblock(inode,block);
		if (bh) {
			memset(bh->b_data + rem,0,blocksize - rem);
			if ((inode->i_sb->u.affs_sb.s_flags & SF_OFS)) {
				((struct data_front *)bh->b_data)->data_size = cpu_to_be32(rem);
				((struct data_front *)bh->b_data)->next_data = 0;
				affs_fix_checksum(blocksize,bh->b_data,5);
			}
			mark_buffer_dirty(bh);
		} else 
			affs_error(inode->i_sb,"truncate","Cannot read block %d",block);
	}

out_truncate:
	affs_brelse(bh);
	/* Invalidate cache */
	if (inode->u.affs_i.i_ec) {
		inode->u.affs_i.i_ec->max_ext = 0;
		for (key = 0; key < 4; key++) {
			inode->u.affs_i.i_ec->kc[key].kc_next_key = 0;
			inode->u.affs_i.i_ec->kc[key].kc_last     = -1;
		}
	}
	mark_inode_dirty(inode);
}

/*
 * Called only when we need to allocate the extension cache.
 */

static int
alloc_ext_cache(struct inode *inode)
{
	s32	 key;
	int	 i;
	unsigned long cache_page;
	int      error = 0;

	pr_debug("AFFS: alloc_ext_cache(ino=%lu)\n",inode->i_ino);

	cache_page = get_free_page(GFP_KERNEL);
	/*
	 * Check whether somebody else allocated it for us ...
	 */
	if (inode->u.affs_i.i_ec)
		goto out_free;
	if (!cache_page)
		goto out_error;

	inode->u.affs_i.i_ec = (struct ext_cache *) cache_page;
	/* We only have to initialize non-zero values.
	 * get_free_page() zeroed the page already.
	 */
	key = inode->i_ino;
	inode->u.affs_i.i_ec->ec[0] = key;
	for (i = 0; i < 4; i++) {
		inode->u.affs_i.i_ec->kc[i].kc_this_key = key;
		inode->u.affs_i.i_ec->kc[i].kc_last     = -1;
	}
out:
	return error;

out_free:
	if (cache_page)
		free_page(cache_page);
	goto out;

out_error:
	affs_error(inode->i_sb,"alloc_ext_cache","Cache allocation failed");
	error = -ENOMEM;
	goto out;
}
