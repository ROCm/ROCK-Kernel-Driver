/*
 *   (C) Copyright IBM Corp. 2002, 2004
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * linux/drivers/md/dm-bbr.c
 *
 * Bad-block-relocation (BBR) target for device-mapper.
 *
 * The BBR target is designed to remap I/O write failures to another safe
 * location on disk. Note that most disk drives have BBR built into them,
 * this means that our software BBR will be only activated when all hardware
 * BBR replacement sectors have been used.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/mempool.h>
#include <linux/workqueue.h>
#include <linux/vmalloc.h>

#include "dm.h"
#include "dm-bio-list.h"
#include "dm-bio-record.h"
#include "dm-bbr.h"
#include "dm-io.h"

#define SECTOR_SIZE (1 << SECTOR_SHIFT)

static struct workqueue_struct *dm_bbr_wq = NULL;
static void bbr_remap_handler(void *data);
static kmem_cache_t *bbr_remap_cache;
static kmem_cache_t *bbr_io_cache;
static mempool_t *bbr_io_pool;

/**
 * bbr_binary_tree_destroy
 *
 * Destroy the binary tree.
 **/
static void bbr_binary_tree_destroy(struct bbr_runtime_remap *root)
{
	struct bbr_runtime_remap **link = NULL;
	struct bbr_runtime_remap *node = root;

	while (node) {
		if (node->left) {
			link = &(node->left);
			node = node->left;
			continue;
		}
		if (node->right) {
			link = &(node->right);
			node = node->right;
			continue;
		}

		kmem_cache_free(bbr_remap_cache, node);
		if (node == root) {
			/* If root is deleted, we're done. */
			break;
		}

		/* Back to root. */
		node = root;
		*link = NULL;
	}
}

static void bbr_free_remap(struct bbr_private *bbr_id)
{
	spin_lock_irq(&bbr_id->remap_root_lock);
	bbr_binary_tree_destroy(bbr_id->remap_root);
	bbr_id->remap_root = NULL;
	spin_unlock_irq(&bbr_id->remap_root_lock);
}

static struct bbr_private *bbr_alloc_private(void)
{
	struct bbr_private *bbr_id;

	bbr_id = kmalloc(sizeof(*bbr_id), GFP_KERNEL);
	if (bbr_id) {
		memset(bbr_id, 0, sizeof(*bbr_id));
		INIT_WORK(&bbr_id->remap_work, bbr_remap_handler, bbr_id);
		bbr_id->remap_root_lock = SPIN_LOCK_UNLOCKED;
		bbr_id->remap_ios_lock = SPIN_LOCK_UNLOCKED;
		bbr_id->in_use_replacement_blks = (atomic_t)ATOMIC_INIT(0);
	}

	return bbr_id;
}

static void bbr_free_private(struct bbr_private *bbr_id)
{
	if (bbr_id->bbr_table) {
		vfree(bbr_id->bbr_table);
	}
	bbr_free_remap(bbr_id);
	kfree(bbr_id);
}

static u32 crc_table[256];
static u32 crc_table_built = 0;

static void build_crc_table(void)
{
	u32 i, j, crc;

	for (i = 0; i <= 255; i++) {
		crc = i;
		for (j = 8; j > 0; j--) {
			if (crc & 1)
				crc = (crc >> 1) ^ CRC_POLYNOMIAL;
			else
				crc >>= 1;
		}
		crc_table[i] = crc;
	}
	crc_table_built = 1;
}

static u32 calculate_crc(u32 crc, void *buffer, u32 buffersize)
{
	unsigned char *current_byte;
	u32 temp1, temp2, i;

	current_byte = (unsigned char *) buffer;
	/* Make sure the crc table is available */
	if (!crc_table_built)
		build_crc_table();
	/* Process each byte in the buffer. */
	for (i = 0; i < buffersize; i++) {
		temp1 = (crc >> 8) & 0x00FFFFFF;
		temp2 = crc_table[(crc ^ (u32) * current_byte) &
				  (u32) 0xff];
		current_byte++;
		crc = temp1 ^ temp2;
	}
	return crc;
}

/**
 * le_bbr_table_sector_to_cpu
 *
 * Convert bbr meta data from on-disk (LE) format
 * to the native cpu endian format.
 **/
static void le_bbr_table_sector_to_cpu(struct bbr_table *p)
{
	int i;
	p->signature		= le32_to_cpup(&p->signature);
	p->crc			= le32_to_cpup(&p->crc);
	p->sequence_number	= le32_to_cpup(&p->sequence_number);
	p->in_use_cnt		= le32_to_cpup(&p->in_use_cnt);
	for (i = 0; i < BBR_ENTRIES_PER_SECT; i++) {
		p->entries[i].bad_sect =
			le64_to_cpup(&p->entries[i].bad_sect);
		p->entries[i].replacement_sect =
			le64_to_cpup(&p->entries[i].replacement_sect);
	}
}

/**
 * cpu_bbr_table_sector_to_le
 *
 * Convert bbr meta data from cpu endian format to on-disk (LE) format
 **/
static void cpu_bbr_table_sector_to_le(struct bbr_table *p,
				       struct bbr_table *le)
{
	int i;
	le->signature		= cpu_to_le32p(&p->signature);
	le->crc			= cpu_to_le32p(&p->crc);
	le->sequence_number	= cpu_to_le32p(&p->sequence_number);
	le->in_use_cnt		= cpu_to_le32p(&p->in_use_cnt);
	for (i = 0; i < BBR_ENTRIES_PER_SECT; i++) {
		le->entries[i].bad_sect =
			cpu_to_le64p(&p->entries[i].bad_sect);
		le->entries[i].replacement_sect =
			cpu_to_le64p(&p->entries[i].replacement_sect);
	}
}

/**
 * validate_bbr_table_sector
 *
 * Check the specified BBR table sector for a valid signature and CRC. If it's
 * valid, endian-convert the table sector.
 **/
static int validate_bbr_table_sector(struct bbr_table *p)
{
	int rc = 0;
	int org_crc, final_crc;

	if (le32_to_cpup(&p->signature) != BBR_TABLE_SIGNATURE) {
		DMERR("dm-bbr: BBR table signature doesn't match!");
		DMERR("dm-bbr: Found 0x%x. Expecting 0x%x",
		      le32_to_cpup(&p->signature), BBR_TABLE_SIGNATURE);
		rc = -EINVAL;
		goto out;
	}

	if (!p->crc) {
		DMERR("dm-bbr: BBR table sector has no CRC!");
		rc = -EINVAL;
		goto out;
	}

	org_crc = le32_to_cpup(&p->crc);
	p->crc = 0;
	final_crc = calculate_crc(INITIAL_CRC, (void *)p, sizeof(*p));
	if (final_crc != org_crc) {
		DMERR("dm-bbr: CRC failed!");
		DMERR("dm-bbr: Found 0x%x. Expecting 0x%x",
		      org_crc, final_crc);
		rc = -EINVAL;
		goto out;
	}

	p->crc = cpu_to_le32p(&org_crc);
	le_bbr_table_sector_to_cpu(p);

out:
	return rc;
}

/**
 * bbr_binary_tree_insert
 *
 * Insert a node into the binary tree.
 **/
static void bbr_binary_tree_insert(struct bbr_runtime_remap **root,
				   struct bbr_runtime_remap *newnode)
{
	struct bbr_runtime_remap **node = root;
	while (node && *node) {
		if (newnode->remap.bad_sect > (*node)->remap.bad_sect) {
			node = &((*node)->right);
		} else {
			node = &((*node)->left);
		}
	}

	newnode->left = newnode->right = NULL;
	*node = newnode;
}

/**
 * bbr_binary_search
 *
 * Search for a node that contains bad_sect == lsn.
 **/
static struct bbr_runtime_remap *bbr_binary_search(
	struct bbr_runtime_remap *root,
	u64 lsn)
{
	struct bbr_runtime_remap *node = root;
	while (node) {
		if (node->remap.bad_sect == lsn) {
			break;
		}
		if (lsn > node->remap.bad_sect) {
			node = node->right;
		} else {
			node = node->left;
		}
	}
	return node;
}

/**
 * bbr_insert_remap_entry
 *
 * Create a new remap entry and add it to the binary tree for this node.
 **/
static int bbr_insert_remap_entry(struct bbr_private *bbr_id,
				  struct bbr_table_entry *new_bbr_entry)
{
	struct bbr_runtime_remap *newnode;

	newnode = kmem_cache_alloc(bbr_remap_cache, GFP_NOIO);
	if (!newnode) {
		DMERR("dm-bbr: Could not allocate from remap cache!");
		return -ENOMEM;
	}
	newnode->remap.bad_sect  = new_bbr_entry->bad_sect;
	newnode->remap.replacement_sect = new_bbr_entry->replacement_sect;
	spin_lock_irq(&bbr_id->remap_root_lock);
	bbr_binary_tree_insert(&bbr_id->remap_root, newnode);
	spin_unlock_irq(&bbr_id->remap_root_lock);
	return 0;
}

/**
 * bbr_table_to_remap_list
 *
 * The on-disk bbr table is sorted by the replacement sector LBA. In order to
 * improve run time performance, the in memory remap list must be sorted by
 * the bad sector LBA. This function is called at discovery time to initialize
 * the remap list. This function assumes that at least one copy of meta data
 * is valid.
 **/
static u32 bbr_table_to_remap_list(struct bbr_private *bbr_id)
{
	u32 in_use_blks = 0;
	int i, j;
	struct bbr_table *p;

	for (i = 0, p = bbr_id->bbr_table;
	     i < bbr_id->nr_sects_bbr_table;
	     i++, p++) {
		if (!p->in_use_cnt) {
			break;
		}
		in_use_blks += p->in_use_cnt;
		for (j = 0; j < p->in_use_cnt; j++) {
			bbr_insert_remap_entry(bbr_id, &p->entries[j]);
		}
	}
	if (in_use_blks) {
		char b[32];
		DMWARN("dm-bbr: There are %u BBR entries for device %s",
		       in_use_blks, format_dev_t(b, bbr_id->dev->bdev->bd_dev));
	}

	return in_use_blks;
}

/**
 * bbr_search_remap_entry
 *
 * Search remap entry for the specified sector. If found, return a pointer to
 * the table entry. Otherwise, return NULL.
 **/
static struct bbr_table_entry *bbr_search_remap_entry(
	struct bbr_private *bbr_id,
	u64 lsn)
{
	struct bbr_runtime_remap *p;

	spin_lock_irq(&bbr_id->remap_root_lock);
	p = bbr_binary_search(bbr_id->remap_root, lsn);
	spin_unlock_irq(&bbr_id->remap_root_lock);
	if (p) {
		return (&p->remap);
	} else {
		return NULL;
	}
}

/**
 * bbr_remap
 *
 * If *lsn is in the remap table, return TRUE and modify *lsn,
 * else, return FALSE.
 **/
static inline int bbr_remap(struct bbr_private *bbr_id,
			    u64 *lsn)
{
	struct bbr_table_entry *e;

	if (atomic_read(&bbr_id->in_use_replacement_blks)) {
		e = bbr_search_remap_entry(bbr_id, *lsn);
		if (e) {
			*lsn = e->replacement_sect;
			return 1;
		}
	}
	return 0;
}

/**
 * bbr_remap_probe
 *
 * If any of the sectors in the range [lsn, lsn+nr_sects] are in the remap
 * table return TRUE, Else, return FALSE.
 **/
static inline int bbr_remap_probe(struct bbr_private *bbr_id,
				  u64 lsn, u64 nr_sects)
{
	u64 tmp, cnt;

	if (atomic_read(&bbr_id->in_use_replacement_blks)) {
		for (cnt = 0, tmp = lsn;
		     cnt < nr_sects;
		     cnt += bbr_id->blksize_in_sects, tmp = lsn + cnt) {
			if (bbr_remap(bbr_id,&tmp)) {
				return 1;
			}
		}
	}
	return 0;
}

/**
 * bbr_setup
 *
 * Read the remap tables from disk and set up the initial remap tree.
 **/
static int bbr_setup(struct bbr_private *bbr_id)
{
	struct bbr_table *table = bbr_id->bbr_table;
	struct io_region job;
	unsigned long error;
	int i, rc = 0;

	job.bdev = bbr_id->dev->bdev;
	job.count = 1;

	/* Read and verify each BBR table sector individually. */
	for (i = 0; i < bbr_id->nr_sects_bbr_table; i++, table++) {
		job.sector = bbr_id->lba_table1 + i;
		rc = dm_io_sync_vm(1, &job, READ, table, &error);
		if (rc && bbr_id->lba_table2) {
			job.sector = bbr_id->lba_table2 + i;
			rc = dm_io_sync_vm(1, &job, READ, table, &error);
		}
		if (rc) {
			goto out;
		}

		rc = validate_bbr_table_sector(table);
		if (rc) {
			goto out;
		}
	}
	atomic_set(&bbr_id->in_use_replacement_blks,
		   bbr_table_to_remap_list(bbr_id));

out:
	if (rc) {
		DMERR("dm-bbr: error during device setup: %d", rc);
	}
	return rc;
}

/**
 * bbr_io_remap_error
 * @bbr_id:		Private data for the BBR node.
 * @rw:			READ or WRITE.
 * @starting_lsn:	Starting sector of request to remap.
 * @count:		Number of sectors in the request.
 * @page:		Page containing the data for the request.
 * @offset:		Byte-offset of the data within the page.
 *
 * For the requested range, try to write each sector individually. For each
 * sector that fails, find the next available remap location and write the
 * data to that new location. Then update the table and write both copies
 * of the table to disk. Finally, update the in-memory mapping and do any
 * other necessary bookkeeping.
 **/
static int bbr_io_remap_error(struct bbr_private *bbr_id,
			      int rw,
			      u64 starting_lsn,
			      u64 count,
			      struct page *page,
			      unsigned int offset)
{
	struct bbr_table *bbr_table;
	struct io_region job;
	struct page_list pl;
	unsigned long table_sector_index;
	unsigned long table_sector_offset;
	unsigned long index;
	unsigned long error;
	u64 lsn, new_lsn;
	char b[32];
	int rc;

	job.bdev = bbr_id->dev->bdev;
	job.count = 1;
	pl.page = page;
	pl.next = NULL;

	/* For each sector in the request. */
	for (lsn = 0; lsn < count; lsn++, offset += SECTOR_SIZE) {
		job.sector = starting_lsn + lsn;
		rc = dm_io_sync(1, &job, rw, &pl, offset, &error);
		while (rc) {
			/* Find the next available relocation sector. */
			new_lsn = atomic_read(&bbr_id->in_use_replacement_blks);
			if (new_lsn >= bbr_id->nr_replacement_blks) {
				/* No more replacement sectors available. */
				return -EIO;
			}
			new_lsn += bbr_id->start_replacement_sect;

			/* Write the data to its new location. */
			DMWARN("dm-bbr: device %s: Trying to remap bad sector "PFU64" to sector "PFU64,
			       format_dev_t(b, bbr_id->dev->bdev->bd_dev),
			       starting_lsn + lsn, new_lsn);
			job.sector = new_lsn;
			rc = dm_io_sync(1, &job, rw, &pl, offset, &error);
			if (rc) {
				/* This replacement sector is bad.
				 * Try the next one.
				 */
				DMERR("dm-bbr: device %s: replacement sector "PFU64" is bad. Skipping.",
				      format_dev_t(b, bbr_id->dev->bdev->bd_dev), new_lsn);
				atomic_inc(&bbr_id->in_use_replacement_blks);
				continue;
			}

			/* Add this new entry to the on-disk table. */
			table_sector_index = new_lsn -
					     bbr_id->start_replacement_sect;
			table_sector_offset = table_sector_index /
					      BBR_ENTRIES_PER_SECT;
			index = table_sector_index % BBR_ENTRIES_PER_SECT;

			bbr_table = &bbr_id->bbr_table[table_sector_offset];
			bbr_table->entries[index].bad_sect = starting_lsn + lsn;
			bbr_table->entries[index].replacement_sect = new_lsn;
			bbr_table->in_use_cnt++;
			bbr_table->sequence_number++;
			bbr_table->crc = 0;
			bbr_table->crc = calculate_crc(INITIAL_CRC,
						       bbr_table,
						       sizeof(struct bbr_table));

			/* Write the table to disk. */
			cpu_bbr_table_sector_to_le(bbr_table, bbr_table);
			if (bbr_id->lba_table1) {
				job.sector = bbr_id->lba_table1 + table_sector_offset;
				rc = dm_io_sync_vm(1, &job, WRITE, bbr_table, &error);
			}
			if (bbr_id->lba_table2) {
				job.sector = bbr_id->lba_table2 + table_sector_offset;
				rc |= dm_io_sync_vm(1, &job, WRITE, bbr_table, &error);
			}
			le_bbr_table_sector_to_cpu(bbr_table);

			if (rc) {
				/* Error writing one of the tables to disk. */
				DMERR("dm-bbr: device %s: error updating BBR tables on disk.",
				      format_dev_t(b, bbr_id->dev->bdev->bd_dev));
				return rc;
			}

			/* Insert a new entry in the remapping binary-tree. */
			rc = bbr_insert_remap_entry(bbr_id,
						    &bbr_table->entries[index]);
			if (rc) {
				DMERR("dm-bbr: device %s: error adding new entry to remap tree.",
				      format_dev_t(b, bbr_id->dev->bdev->bd_dev));
				return rc;
			}

			atomic_inc(&bbr_id->in_use_replacement_blks);
		}
	}

	return 0;
}

/**
 * bbr_io_process_request
 *
 * For each sector in this request, check if the sector has already
 * been remapped. If so, process all previous sectors in the request,
 * followed by the remapped sector. Then reset the starting lsn and
 * count, and keep going with the rest of the request as if it were
 * a whole new request. If any of the sync_io's return an error,
 * call the remapper to relocate the bad sector(s).
 *
 * 2.5 Note: When switching over to bio's for the I/O path, we have made
 * the assumption that the I/O request described by the bio is one
 * virtually contiguous piece of memory (even though the bio vector
 * describes it using a series of physical page addresses).
 **/
static int bbr_io_process_request(struct bbr_private *bbr_id,
				  struct bio *bio)
{
	struct io_region job;
	u64 starting_lsn = bio->bi_sector;
	u64 count, lsn, remapped_lsn;
	struct page_list pl;
	unsigned int offset;
	unsigned long error;
	int i, rw = bio_data_dir(bio);
	int rc = 0;

	job.bdev = bbr_id->dev->bdev;
	pl.next = NULL;

	/* Each bio can contain multiple vectors, each with a different page.
	 * Treat each vector as a separate request.
	 */
	/* KMC: Is this the right way to walk the bvec list? */
	for (i = 0;
	     i < bio->bi_vcnt;
	     i++, bio->bi_idx++, starting_lsn += count) {

		/* Bvec info: number of sectors, page,
		 * and byte-offset within page.
		 */
		count = bio_iovec(bio)->bv_len >> SECTOR_SHIFT;
		pl.page = bio_iovec(bio)->bv_page;
		offset = bio_iovec(bio)->bv_offset;

		/* For each sector in this bvec, check if the sector has
		 * already been remapped. If so, process all previous sectors
		 * in this request, followed by the remapped sector. Then reset
		 * the starting lsn and count and keep going with the rest of
		 * the request as if it were a whole new request.
		 */
		for (lsn = 0; lsn < count; lsn++) {
			remapped_lsn = starting_lsn + lsn;
			rc = bbr_remap(bbr_id, &remapped_lsn);
			if (!rc) {
				/* This sector is fine. */
				continue;
			}

			/* Process all sectors in the request up to this one. */
			if (lsn > 0) {
				job.sector = starting_lsn;
				job.count = lsn;
				rc = dm_io_sync(1, &job, rw, &pl,
						offset, &error);
				if (rc) {
					/* If this I/O failed, then one of the
					 * sectors in this request needs to be
					 * relocated.
					 */
					rc = bbr_io_remap_error(bbr_id, rw,
								starting_lsn,
								lsn, pl.page,
								offset);
					if (rc) {
						/* KMC: Return? Or continue to next bvec? */
						return rc;
					}
				}
				offset += (lsn << SECTOR_SHIFT);
			}
	
			/* Process the remapped sector. */
			job.sector = remapped_lsn;
			job.count = 1;
			rc = dm_io_sync(1, &job, rw, &pl, offset, &error);
			if (rc) {
				/* BUGBUG - Need more processing if this caused
				 * an error. If this I/O failed, then the
				 * existing remap is now bad, and we need to
				 * find a new remap. Can't use
				 * bbr_io_remap_error(), because the existing
				 * map entry needs to be changed, not added
				 * again, and the original table entry also
				 * needs to be changed.
				 */
				return rc;
			}

			starting_lsn	+= (lsn + 1);
			count		-= (lsn + 1);
			lsn		= -1;
			offset		+= SECTOR_SIZE;
		}

		/* Check for any remaining sectors after the last split. This
		 * could potentially be the whole request, but that should be a
		 * rare case because requests should only be processed by the
		 * thread if we know an error occurred or they contained one or
		 * more remapped sectors.
		 */
		if (count) {
			job.sector = starting_lsn;
			job.count = count;
			rc = dm_io_sync(1, &job, rw, &pl, offset, &error);
			if (rc) {
				/* If this I/O failed, then one of the sectors
				 * in this request needs to be relocated.
				 */
				rc = bbr_io_remap_error(bbr_id, rw, starting_lsn,
							count, pl.page, offset);
				if (rc) {
					/* KMC: Return? Or continue to next bvec? */
					return rc;
				}
			}
		}
	}

	return 0;
}

static void bbr_io_process_requests(struct bbr_private *bbr_id,
				    struct bio *bio)
{
	struct bio *next;
	int rc;

	while (bio) {
		next = bio->bi_next;
		bio->bi_next = NULL;

		rc = bbr_io_process_request(bbr_id, bio);

		bio_endio(bio, bio->bi_size, rc);

		bio = next;
	}
}

/**
 * bbr_remap_handler
 *
 * This is the handler for the bbr work-queue.
 *
 * I/O requests should only be sent to this handler if we know that:
 * a) the request contains at least one remapped sector.
 *   or
 * b) the request caused an error on the normal I/O path.
 *
 * This function uses synchronous I/O, so sending a request to this
 * thread that doesn't need special processing will cause severe
 * performance degredation.
 **/
static void bbr_remap_handler(void *data)
{
	struct bbr_private *bbr_id = data;
	struct bio *bio;
	unsigned long flags;

	spin_lock_irqsave(&bbr_id->remap_ios_lock, flags);
	bio = bio_list_get(&bbr_id->remap_ios);
	spin_unlock_irqrestore(&bbr_id->remap_ios_lock, flags);

	bbr_io_process_requests(bbr_id, bio);
}

/**
 * bbr_endio
 *
 * This is the callback for normal write requests. Check for an error
 * during the I/O, and send to the thread for processing if necessary.
 **/
static int bbr_endio(struct dm_target *ti, struct bio *bio,
		     int error, union map_info *map_context)
{
	struct bbr_private *bbr_id = ti->private;
	struct dm_bio_details *bbr_io = map_context->ptr;

	if (error && bbr_io) {
		unsigned long flags;
		char b[32];

		dm_bio_restore(bbr_io, bio);
		map_context->ptr = NULL;

		DMERR("dm-bbr: device %s: I/O failure on sector %lu. "
		      "Scheduling for retry.",
		      format_dev_t(b, bbr_id->dev->bdev->bd_dev),
		      (unsigned long)bio->bi_sector);

		spin_lock_irqsave(&bbr_id->remap_ios_lock, flags);
		bio_list_add(&bbr_id->remap_ios, bio);
		spin_unlock_irqrestore(&bbr_id->remap_ios_lock, flags);

		queue_work(dm_bbr_wq, &bbr_id->remap_work);

		error = 1;
	}

	if (bbr_io)
		mempool_free(bbr_io, bbr_io_pool);

	return error;
}

/**
 * Construct a bbr mapping
 **/
static int bbr_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct bbr_private *bbr_id;
	unsigned long block_size;
	char *end;
	int rc = -EINVAL;

	if (argc != 8) {
		ti->error = "dm-bbr requires exactly 8 arguments: "
			    "device offset table1_lsn table2_lsn table_size start_replacement nr_replacement_blks block_size";
		goto out1;
	}

	bbr_id = bbr_alloc_private();
	if (!bbr_id) {
		ti->error = "dm-bbr: Error allocating bbr private data.";
		goto out1;
	}

	bbr_id->offset = simple_strtoull(argv[1], &end, 10);
	bbr_id->lba_table1 = simple_strtoull(argv[2], &end, 10);
	bbr_id->lba_table2 = simple_strtoull(argv[3], &end, 10);
	bbr_id->nr_sects_bbr_table = simple_strtoull(argv[4], &end, 10);
	bbr_id->start_replacement_sect = simple_strtoull(argv[5], &end, 10);
	bbr_id->nr_replacement_blks = simple_strtoull(argv[6], &end, 10);
	block_size = simple_strtoul(argv[7], &end, 10);
	bbr_id->blksize_in_sects = (block_size >> SECTOR_SHIFT);

	bbr_id->bbr_table = vmalloc(bbr_id->nr_sects_bbr_table << SECTOR_SHIFT);
	if (!bbr_id->bbr_table) {
		ti->error = "dm-bbr: Error allocating bbr table.";
		goto out2;
	}

	if (dm_get_device(ti, argv[0], 0, ti->len,
			  dm_table_get_mode(ti->table), &bbr_id->dev)) {
		ti->error = "dm-bbr: Device lookup failed";
		goto out2;
	}

	rc = bbr_setup(bbr_id);
	if (rc) {
		ti->error = "dm-bbr: Device setup failed";
		goto out3;
	}

	ti->private = bbr_id;
	return 0;

out3:
	dm_put_device(ti, bbr_id->dev);
out2:
	bbr_free_private(bbr_id);
out1:
	return rc;
}

static void bbr_dtr(struct dm_target *ti)
{
	struct bbr_private *bbr_id = ti->private;

	dm_put_device(ti, bbr_id->dev);
	bbr_free_private(bbr_id);
}

static int bbr_map(struct dm_target *ti, struct bio *bio,
		   union map_info *map_context)
{
	struct bbr_private *bbr_id = ti->private;
	struct dm_bio_details *bbr_io;
	unsigned long flags;
	int rc = 1;

	bio->bi_sector += bbr_id->offset;

	if (atomic_read(&bbr_id->in_use_replacement_blks) == 0 ||
	    !bbr_remap_probe(bbr_id, bio->bi_sector, bio_sectors(bio))) {
		/* No existing remaps or this request doesn't
		 * contain any remapped sectors.
		 */
		bio->bi_bdev = bbr_id->dev->bdev;

		bbr_io = mempool_alloc(bbr_io_pool, GFP_NOIO);
		dm_bio_record(bbr_io, bio);
		map_context->ptr = bbr_io;
	} else {
		/* This request has at least one remapped sector.
		 * Give it to the work-queue for processing.
		 */
		map_context->ptr = NULL;
		spin_lock_irqsave(&bbr_id->remap_ios_lock, flags);
		bio_list_add(&bbr_id->remap_ios, bio);
		spin_unlock_irqrestore(&bbr_id->remap_ios_lock, flags);

		queue_work(dm_bbr_wq, &bbr_id->remap_work);
		rc = 0;
	}

	return rc;
}

static int bbr_status(struct dm_target *ti, status_type_t type,
		      char *result, unsigned int maxlen)
{
	struct bbr_private *bbr_id = ti->private;
	char b[BDEVNAME_SIZE];

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		snprintf(result, maxlen, "%s "PFU64" "PFU64" "PFU64" "PFU64" "PFU64" "PFU64" %u",
			 format_dev_t(b, bbr_id->dev->bdev->bd_dev),
			 bbr_id->offset, bbr_id->lba_table1, bbr_id->lba_table2,
			 bbr_id->nr_sects_bbr_table,
			 bbr_id->start_replacement_sect,
			 bbr_id->nr_replacement_blks,
			 bbr_id->blksize_in_sects << SECTOR_SHIFT);
		 break;
	}
	return 0;
}

static struct target_type bbr_target = {
	.name	= "bbr",
	.version= {1, 0, 1},
	.module	= THIS_MODULE,
	.ctr	= bbr_ctr,
	.dtr	= bbr_dtr,
	.map	= bbr_map,
	.end_io	= bbr_endio,
	.status	= bbr_status,
};

int __init dm_bbr_init(void)
{
	int rc;

	rc = dm_register_target(&bbr_target);
	if (rc) {
		DMERR("dm-bbr: error registering target.");
		goto err1;
	}

	bbr_remap_cache = kmem_cache_create("bbr-remap",
					    sizeof(struct bbr_runtime_remap),
					    0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!bbr_remap_cache) {
		DMERR("dm-bbr: error creating remap cache.");
		rc = ENOMEM;
		goto err2;
	}

	bbr_io_cache = kmem_cache_create("bbr-io", sizeof(struct dm_bio_details),
					 0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!bbr_io_cache) {
		DMERR("dm-bbr: error creating io cache.");
		rc = ENOMEM;
		goto err3;
	}

	bbr_io_pool = mempool_create(256, mempool_alloc_slab,
				     mempool_free_slab, bbr_io_cache);
	if (!bbr_io_pool) {
		DMERR("dm-bbr: error creating io mempool.");
		rc = ENOMEM;
		goto err4;
	}

	dm_bbr_wq = create_workqueue("dm-bbr");
	if (!dm_bbr_wq) {
		DMERR("dm-bbr: error creating work-queue.");
		rc = ENOMEM;
		goto err5;
	}

	rc = dm_io_get(1);
	if (rc) {
		DMERR("dm-bbr: error initializing I/O service.");
		goto err6;
	}

	return 0;

err6:
	destroy_workqueue(dm_bbr_wq);
err5:
	mempool_destroy(bbr_io_pool);
err4:
	kmem_cache_destroy(bbr_io_cache);
err3:
	kmem_cache_destroy(bbr_remap_cache);
err2:
	dm_unregister_target(&bbr_target);
err1:
	return rc;
}

void __exit dm_bbr_exit(void)
{
	dm_io_put(1);
	destroy_workqueue(dm_bbr_wq);
	mempool_destroy(bbr_io_pool);
	kmem_cache_destroy(bbr_io_cache);
	kmem_cache_destroy(bbr_remap_cache);
	dm_unregister_target(&bbr_target);
}

module_init(dm_bbr_init);
module_exit(dm_bbr_exit);
MODULE_LICENSE("GPL");
