/*
 * kernel/lvm-snap.c
 *
 * Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *                    Heinz Mauelshagen, Sistina Software (persistent snapshots)
 *
 * LVM snapshot driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * LVM snapshot driver is distributed in the hope that it will be useful,
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

#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/smp_lock.h>
#include <linux/types.h>
#include <linux/iobuf.h>
#include <linux/lvm.h>


static char *lvm_snap_version __attribute__ ((unused)) = "LVM 0.9 snapshot code (13/11/2000)\n";

extern const char *const lvm_name;
extern int lvm_blocksizes[];

void lvm_snapshot_release(lv_t *);

uint lvm_pv_get_number(vg_t * vg, kdev_t rdev)
{
	uint p;

	for ( p = 0; p < vg->pv_max; p++)
	{
		if ( vg->pv[p] == NULL) continue;
		if ( vg->pv[p]->pv_dev == rdev) break;
	}

	return vg->pv[p]->pv_number;
}


#define hashfn(dev,block,mask,chunk_size) \
	((HASHDEV(dev)^((block)/(chunk_size))) & (mask))

static inline lv_block_exception_t *
lvm_find_exception_table(kdev_t org_dev, unsigned long org_start, lv_t * lv)
{
	struct list_head * hash_table = lv->lv_snapshot_hash_table, * next;
	unsigned long mask = lv->lv_snapshot_hash_mask;
	int chunk_size = lv->lv_chunk_size;
	lv_block_exception_t * ret;
	int i = 0;

	hash_table = &hash_table[hashfn(org_dev, org_start, mask, chunk_size)];
	ret = NULL;
	for (next = hash_table->next; next != hash_table; next = next->next)
	{
		lv_block_exception_t * exception;

		exception = list_entry(next, lv_block_exception_t, hash);
		if (exception->rsector_org == org_start &&
		    exception->rdev_org == org_dev)
		{
			if (i)
			{
				/* fun, isn't it? :) */
				list_del(next);
				list_add(next, hash_table);
			}
			ret = exception;
			break;
		}
		i++;
	}
	return ret;
}

inline void lvm_hash_link(lv_block_exception_t * exception,
			  kdev_t org_dev, unsigned long org_start,
			  lv_t * lv)
{
	struct list_head * hash_table = lv->lv_snapshot_hash_table;
	unsigned long mask = lv->lv_snapshot_hash_mask;
	int chunk_size = lv->lv_chunk_size;

	hash_table = &hash_table[hashfn(org_dev, org_start, mask, chunk_size)];
	list_add(&exception->hash, hash_table);
}

int lvm_snapshot_remap_block(kdev_t * org_dev, unsigned long * org_sector,
			     unsigned long pe_start, lv_t * lv)
{
	int ret;
	unsigned long pe_off, pe_adjustment, __org_start;
	kdev_t __org_dev;
	int chunk_size = lv->lv_chunk_size;
	lv_block_exception_t * exception;

	pe_off = pe_start % chunk_size;
	pe_adjustment = (*org_sector-pe_off) % chunk_size;
	__org_start = *org_sector - pe_adjustment;
	__org_dev = *org_dev;
	ret = 0;
	exception = lvm_find_exception_table(__org_dev, __org_start, lv);
	if (exception)
	{
		*org_dev = exception->rdev_new;
		*org_sector = exception->rsector_new + pe_adjustment;
		ret = 1;
	}
	return ret;
}

void lvm_drop_snapshot(lv_t * lv_snap, const char * reason)
{
	kdev_t last_dev;
	int i;

	/* no exception storage space available for this snapshot
	   or error on this snapshot --> release it */
	invalidate_buffers(lv_snap->lv_dev);

	for (i = last_dev = 0; i < lv_snap->lv_remap_ptr; i++) {
		if ( lv_snap->lv_block_exception[i].rdev_new != last_dev) {
			last_dev = lv_snap->lv_block_exception[i].rdev_new;
			invalidate_buffers(last_dev);
		}
	}

	lvm_snapshot_release(lv_snap);

	printk(KERN_INFO
	       "%s -- giving up to snapshot %s on %s due %s\n",
	       lvm_name, lv_snap->lv_snapshot_org->lv_name, lv_snap->lv_name,
	       reason);
}

static inline void lvm_snapshot_prepare_blocks(unsigned long * blocks,
					       unsigned long start,
					       int nr_sectors,
					       int blocksize)
{
	int i, sectors_per_block, nr_blocks;

	sectors_per_block = blocksize >> 9;
	nr_blocks = nr_sectors / sectors_per_block;
	start /= sectors_per_block;

	for (i = 0; i < nr_blocks; i++)
		blocks[i] = start++;
}

inline int lvm_get_blksize(kdev_t dev)
{
	int correct_size = BLOCK_SIZE, i, major;

	major = MAJOR(dev);
	if (blksize_size[major])
	{
		i = blksize_size[major][MINOR(dev)];
		if (i)
			correct_size = i;
	}
	return correct_size;
}

#ifdef DEBUG_SNAPSHOT
static inline void invalidate_snap_cache(unsigned long start, unsigned long nr,
					 kdev_t dev)
{
	struct buffer_head * bh;
	int sectors_per_block, i, blksize, minor;

	minor = MINOR(dev);
	blksize = lvm_blocksizes[minor];
	sectors_per_block = blksize >> 9;
	nr /= sectors_per_block;
	start /= sectors_per_block;

	for (i = 0; i < nr; i++)
	{
		bh = get_hash_table(dev, start++, blksize);
		if (bh)
			bforget(bh);
	}
}
#endif


void lvm_snapshot_fill_COW_page(vg_t * vg, lv_t * lv_snap)
{
	int 	id = 0, is = lv_snap->lv_remap_ptr;
	ulong	blksize_snap;
	lv_COW_table_disk_t * lv_COW_table =
	   ( lv_COW_table_disk_t *) page_address(lv_snap->lv_COW_table_page);

	if (is == 0) return;
	is--;
        blksize_snap = lvm_get_blksize(lv_snap->lv_block_exception[is].rdev_new);
        is -= is % (blksize_snap / sizeof(lv_COW_table_disk_t));

	memset(lv_COW_table, 0, blksize_snap);
	for ( ; is < lv_snap->lv_remap_ptr; is++, id++) {
		/* store new COW_table entry */
		lv_COW_table[id].pv_org_number = LVM_TO_DISK64(lvm_pv_get_number(vg, lv_snap->lv_block_exception[is].rdev_org));
		lv_COW_table[id].pv_org_rsector = LVM_TO_DISK64(lv_snap->lv_block_exception[is].rsector_org);
		lv_COW_table[id].pv_snap_number = LVM_TO_DISK64(lvm_pv_get_number(vg, lv_snap->lv_block_exception[is].rdev_new));
		lv_COW_table[id].pv_snap_rsector = LVM_TO_DISK64(lv_snap->lv_block_exception[is].rsector_new);
	}
}


/*
 * writes a COW exception table sector to disk (HM)
 *
 */

int lvm_write_COW_table_block(vg_t * vg,
			      lv_t * lv_snap)
{
	int blksize_snap;
	int end_of_table;
	int idx = lv_snap->lv_remap_ptr, idx_COW_table;
	int nr_pages_tmp;
	int length_tmp;
	ulong snap_pe_start, COW_table_sector_offset,
	      COW_entries_per_pe, COW_chunks_per_pe, COW_entries_per_block;
	ulong blocks[1];
	const char * reason;
	kdev_t snap_phys_dev;
	struct kiobuf * iobuf = lv_snap->lv_iobuf;
	struct page * page_tmp;
	lv_COW_table_disk_t * lv_COW_table =
	   ( lv_COW_table_disk_t *) page_address(lv_snap->lv_COW_table_page);

	idx--;

	COW_chunks_per_pe = LVM_GET_COW_TABLE_CHUNKS_PER_PE(vg, lv_snap);
	COW_entries_per_pe = LVM_GET_COW_TABLE_ENTRIES_PER_PE(vg, lv_snap);

	/* get physical addresse of destination chunk */
	snap_phys_dev = lv_snap->lv_block_exception[idx].rdev_new;
	snap_pe_start = lv_snap->lv_block_exception[idx - (idx % COW_entries_per_pe)].rsector_new - lv_snap->lv_chunk_size;

	blksize_snap = lvm_get_blksize(snap_phys_dev);

        COW_entries_per_block = blksize_snap / sizeof(lv_COW_table_disk_t);
        idx_COW_table = idx % COW_entries_per_pe % COW_entries_per_block;

	if ( idx_COW_table == 0) memset(lv_COW_table, 0, blksize_snap);

	/* sector offset into the on disk COW table */
	COW_table_sector_offset = (idx % COW_entries_per_pe) / (SECTOR_SIZE / sizeof(lv_COW_table_disk_t));

        /* COW table block to write next */
	blocks[0] = (snap_pe_start + COW_table_sector_offset) >> (blksize_snap >> 10);

	/* store new COW_table entry */
	lv_COW_table[idx_COW_table].pv_org_number = LVM_TO_DISK64(lvm_pv_get_number(vg, lv_snap->lv_block_exception[idx].rdev_org));
	lv_COW_table[idx_COW_table].pv_org_rsector = LVM_TO_DISK64(lv_snap->lv_block_exception[idx].rsector_org);
	lv_COW_table[idx_COW_table].pv_snap_number = LVM_TO_DISK64(lvm_pv_get_number(vg, snap_phys_dev));
	lv_COW_table[idx_COW_table].pv_snap_rsector = LVM_TO_DISK64(lv_snap->lv_block_exception[idx].rsector_new);

	length_tmp = iobuf->length;
	iobuf->length = blksize_snap;
	page_tmp = iobuf->maplist[0];
        iobuf->maplist[0] = lv_snap->lv_COW_table_page;
	nr_pages_tmp = iobuf->nr_pages;
	iobuf->nr_pages = 1;

	if (brw_kiovec(WRITE, 1, &iobuf, snap_phys_dev,
		       blocks, blksize_snap) != blksize_snap)
		goto fail_raw_write;


	/* initialization of next COW exception table block with zeroes */
	end_of_table = idx % COW_entries_per_pe == COW_entries_per_pe - 1;
	if (idx_COW_table % COW_entries_per_block == COW_entries_per_block - 1 || end_of_table)
	{
		/* don't go beyond the end */
		if (idx + 1 >= lv_snap->lv_remap_end) goto good_out;

		memset(lv_COW_table, 0, blksize_snap);

		if (end_of_table)
		{
			idx++;
			snap_phys_dev = lv_snap->lv_block_exception[idx].rdev_new;
			snap_pe_start = lv_snap->lv_block_exception[idx - (idx % COW_entries_per_pe)].rsector_new - lv_snap->lv_chunk_size;
			blksize_snap = lvm_get_blksize(snap_phys_dev);
			blocks[0] = snap_pe_start >> (blksize_snap >> 10);
		} else blocks[0]++;

		if (brw_kiovec(WRITE, 1, &iobuf, snap_phys_dev,
			       blocks, blksize_snap) != blksize_snap)
			goto fail_raw_write;
	}


 good_out:
	iobuf->length = length_tmp;
        iobuf->maplist[0] = page_tmp;
	iobuf->nr_pages = nr_pages_tmp;
	return 0;

	/* slow path */
 out:
	lvm_drop_snapshot(lv_snap, reason);
	return 1;

 fail_raw_write:
	reason = "write error";
	goto out;
}

/*
 * copy on write handler for one snapshot logical volume
 *
 * read the original blocks and store it/them on the new one(s).
 * if there is no exception storage space free any longer --> release snapshot.
 *
 * this routine gets called for each _first_ write to a physical chunk.
 */
int lvm_snapshot_COW(kdev_t org_phys_dev,
		     unsigned long org_phys_sector,
		     unsigned long org_pe_start,
		     unsigned long org_virt_sector,
		     lv_t * lv_snap)
{
	const char * reason;
	unsigned long org_start, snap_start, snap_phys_dev, virt_start, pe_off;
	int idx = lv_snap->lv_remap_ptr, chunk_size = lv_snap->lv_chunk_size;
	struct kiobuf * iobuf;
	unsigned long blocks[KIO_MAX_SECTORS];
	int blksize_snap, blksize_org, min_blksize, max_blksize;
	int max_sectors, nr_sectors;

	/* check if we are out of snapshot space */
	if (idx >= lv_snap->lv_remap_end)
		goto fail_out_of_space;

	/* calculate physical boundaries of source chunk */
	pe_off = org_pe_start % chunk_size;
	org_start = org_phys_sector - ((org_phys_sector-pe_off) % chunk_size);
	virt_start = org_virt_sector - (org_phys_sector - org_start);

	/* calculate physical boundaries of destination chunk */
	snap_phys_dev = lv_snap->lv_block_exception[idx].rdev_new;
	snap_start = lv_snap->lv_block_exception[idx].rsector_new;

#ifdef DEBUG_SNAPSHOT
	printk(KERN_INFO
	       "%s -- COW: "
	       "org %02d:%02d faulting %lu start %lu, "
	       "snap %02d:%02d start %lu, "
	       "size %d, pe_start %lu pe_off %lu, virt_sec %lu\n",
	       lvm_name,
	       MAJOR(org_phys_dev), MINOR(org_phys_dev), org_phys_sector,
	       org_start,
	       MAJOR(snap_phys_dev), MINOR(snap_phys_dev), snap_start,
	       chunk_size,
	       org_pe_start, pe_off,
	       org_virt_sector);
#endif

	iobuf = lv_snap->lv_iobuf;

	blksize_org = lvm_get_blksize(org_phys_dev);
	blksize_snap = lvm_get_blksize(snap_phys_dev);
	max_blksize = max(blksize_org, blksize_snap);
	min_blksize = min(blksize_org, blksize_snap);
	max_sectors = KIO_MAX_SECTORS * (min_blksize>>9);

	if (chunk_size % (max_blksize>>9))
		goto fail_blksize;

	while (chunk_size)
	{
		nr_sectors = min(chunk_size, max_sectors);
		chunk_size -= nr_sectors;

		iobuf->length = nr_sectors << 9;

		lvm_snapshot_prepare_blocks(blocks, org_start,
					    nr_sectors, blksize_org);
		if (brw_kiovec(READ, 1, &iobuf, org_phys_dev,
			       blocks, blksize_org) != (nr_sectors<<9))
			goto fail_raw_read;

		lvm_snapshot_prepare_blocks(blocks, snap_start,
					    nr_sectors, blksize_snap);
		if (brw_kiovec(WRITE, 1, &iobuf, snap_phys_dev,
			       blocks, blksize_snap) != (nr_sectors<<9))
			goto fail_raw_write;
	}

#ifdef DEBUG_SNAPSHOT
	/* invalidate the logical snapshot buffer cache */
	invalidate_snap_cache(virt_start, lv_snap->lv_chunk_size,
			      lv_snap->lv_dev);
#endif

	/* the original chunk is now stored on the snapshot volume
	   so update the execption table */
	lv_snap->lv_block_exception[idx].rdev_org = org_phys_dev;
	lv_snap->lv_block_exception[idx].rsector_org = org_start;

	lvm_hash_link(lv_snap->lv_block_exception + idx,
		      org_phys_dev, org_start, lv_snap);
	lv_snap->lv_remap_ptr = idx + 1;
	if (lv_snap->lv_snapshot_use_rate > 0) {
		if (lv_snap->lv_remap_ptr * 100 / lv_snap->lv_remap_end >= lv_snap->lv_snapshot_use_rate)
			wake_up_interruptible(&lv_snap->lv_snapshot_wait);
	}
	return 0;

	/* slow path */
 out:
	lvm_drop_snapshot(lv_snap, reason);
	return 1;

 fail_out_of_space:
	reason = "out of space";
	goto out;
 fail_raw_read:
	reason = "read error";
	goto out;
 fail_raw_write:
	reason = "write error";
	goto out;
 fail_blksize:
	reason = "blocksize error";
	goto out;
}

int lvm_snapshot_alloc_iobuf_pages(struct kiobuf * iobuf, int sectors)
{
	int bytes, nr_pages, err, i;

	bytes = sectors << 9;
	nr_pages = (bytes + ~PAGE_MASK) >> PAGE_SHIFT;
	err = expand_kiobuf(iobuf, nr_pages);
	if (err)
		goto out;

	err = -ENOMEM;
	iobuf->locked = 0;
	iobuf->nr_pages = 0;
	for (i = 0; i < nr_pages; i++)
	{
		struct page * page;

		page = alloc_page(GFP_KERNEL);
		if (!page)
			goto out;

		iobuf->maplist[i] = page;
		iobuf->nr_pages++;
	}
	iobuf->offset = 0;

	err = 0;
 out:
	return err;
}

static int calc_max_buckets(void)
{
	unsigned long mem;

	mem = num_physpages << PAGE_SHIFT;
	mem /= 100;
	mem *= 2;
	mem /= sizeof(struct list_head);

	return mem;
}

int lvm_snapshot_alloc_hash_table(lv_t * lv)
{
	int err;
	unsigned long buckets, max_buckets, size;
	struct list_head * hash;

	buckets = lv->lv_remap_end;
	max_buckets = calc_max_buckets();
	buckets = min(buckets, max_buckets);
	while (buckets & (buckets-1))
		buckets &= (buckets-1);

	size = buckets * sizeof(struct list_head);

	err = -ENOMEM;
	hash = vmalloc(size);
	lv->lv_snapshot_hash_table = hash;

	if (!hash)
		goto out;
	lv->lv_snapshot_hash_table_size = size;

	lv->lv_snapshot_hash_mask = buckets-1;
	while (buckets--)
		INIT_LIST_HEAD(hash+buckets);
	err = 0;
 out:
	return err;
}

int lvm_snapshot_alloc(lv_t * lv_snap)
{
	int err, blocksize, max_sectors;

	err = alloc_kiovec(1, &lv_snap->lv_iobuf);
	if (err)
		goto out;

	blocksize = lvm_blocksizes[MINOR(lv_snap->lv_dev)];
	max_sectors = KIO_MAX_SECTORS << (PAGE_SHIFT-9);

	err = lvm_snapshot_alloc_iobuf_pages(lv_snap->lv_iobuf, max_sectors);
	if (err)
		goto out_free_kiovec;

	err = lvm_snapshot_alloc_hash_table(lv_snap);
	if (err)
		goto out_free_kiovec;


		lv_snap->lv_COW_table_page = alloc_page(GFP_KERNEL);
		if (!lv_snap->lv_COW_table_page)
			goto out_free_kiovec;

 out:
	return err;

 out_free_kiovec:
	unmap_kiobuf(lv_snap->lv_iobuf);
	free_kiovec(1, &lv_snap->lv_iobuf);
	vfree(lv_snap->lv_snapshot_hash_table);
	lv_snap->lv_snapshot_hash_table = NULL;
	goto out;
}

void lvm_snapshot_release(lv_t * lv)
{
	if (lv->lv_block_exception)
	{
		vfree(lv->lv_block_exception);
		lv->lv_block_exception = NULL;
	}
	if (lv->lv_snapshot_hash_table)
	{
		vfree(lv->lv_snapshot_hash_table);
		lv->lv_snapshot_hash_table = NULL;
		lv->lv_snapshot_hash_table_size = 0;
	}
	if (lv->lv_iobuf)
	{
		unmap_kiobuf(lv->lv_iobuf);
		free_kiovec(1, &lv->lv_iobuf);
		lv->lv_iobuf = NULL;
	}
	if (lv->lv_COW_table_page)
	{
		free_page((ulong)lv->lv_COW_table_page);
		lv->lv_COW_table_page = NULL;
	}
}
