/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

#include "forward.h"
#include "tree.h"
#include "tree_walk.h"
#include "super.h"
#include "inode.h"
#include "key.h"
#include "znode.h"

#include <linux/swap.h>	/* for totalram_pages */

reiser4_internal void init_ra_info(ra_info_t * rai)
{
	rai->key_to_stop = *min_key();
}

/* global formatted node readahead parameter. It can be set by mount option -o readahead:NUM:1 */
static inline int ra_adjacent_only(int flags)
{
	return flags & RA_ADJACENT_ONLY;
}

/* this is used by formatted_readahead to decide whether read for right neighbor of node is to be issued. It returns 1
   if right neighbor's first key is less or equal to readahead's stop key */
static int
should_readahead_neighbor(znode *node, ra_info_t *info)
{
	return (UNDER_RW(dk, ZJNODE(node)->tree, read,
			 keyle(znode_get_rd_key(node), &info->key_to_stop)));
}

#define LOW_MEM_PERCENTAGE (5)

static int
low_on_memory(void)
{
	unsigned int freepages;

	freepages = nr_free_pages();
	return freepages < (totalram_pages * LOW_MEM_PERCENTAGE / 100);
}

/* start read for @node and for a few of its right neighbors */
reiser4_internal void
formatted_readahead(znode *node, ra_info_t *info)
{
	ra_params_t *ra_params;
	znode *cur;
	int i;
	int grn_flags;
	lock_handle next_lh;

	/* do nothing if node block number has not been assigned to node (which means it is still in cache). */
	if (blocknr_is_fake(znode_get_block(node)))
		return;

	ra_params = get_current_super_ra_params();

	if (znode_page(node) == NULL)
		jstartio(ZJNODE(node));

	if (znode_get_level(node) != LEAF_LEVEL)
		return;

	/* don't waste memory for read-ahead when low on memory */
	if (low_on_memory())
		return;

	write_current_logf(READAHEAD_LOG, "...readahead\n");

	/* We can have locked nodes on upper tree levels, in this situation lock
	   priorities do not help to resolve deadlocks, we have to use TRY_LOCK
	   here. */
	grn_flags = (GN_CAN_USE_UPPER_LEVELS | GN_TRY_LOCK);

	i = 0;
	cur = zref(node);
	init_lh(&next_lh);
	while (i < ra_params->max) {
		const reiser4_block_nr *nextblk;

		if (!should_readahead_neighbor(cur, info))
			break;

		if (reiser4_get_right_neighbor(&next_lh, cur, ZNODE_READ_LOCK, grn_flags))
			break;

		if (JF_ISSET(ZJNODE(next_lh.node), JNODE_EFLUSH)) {
			/* emergency flushed znode is encountered. That means we are low on memory. Do not readahead
			   then */
			break;
		}

		nextblk = znode_get_block(next_lh.node);
		if (blocknr_is_fake(nextblk) ||
		    (ra_adjacent_only(ra_params->flags) && *nextblk != *znode_get_block(cur) + 1)) {
			break;
		}

		zput(cur);
		cur = zref(next_lh.node);
		done_lh(&next_lh);
		if (znode_page(cur) == NULL)
			jstartio(ZJNODE(cur));
		else
			/* Do not scan read-ahead window if pages already
			 * allocated (and i/o already started). */
			break;

		i ++;
	}
	zput(cur);
	done_lh(&next_lh);

	write_current_logf(READAHEAD_LOG, "...readahead exits\n");
}

static inline loff_t get_max_readahead(struct reiser4_file_ra_state *ra)
{
	/* NOTE: ra->max_window_size is initialized in
	 * reiser4_get_file_fsdata(). */
	return ra->max_window_size;
}

static inline loff_t get_min_readahead(struct reiser4_file_ra_state *ra)
{
	return VM_MIN_READAHEAD * 1024;
}


/* Start read for the given window. */
static loff_t do_reiser4_file_readahead (struct inode * inode, loff_t offset, loff_t size)
{
	reiser4_tree * tree = current_tree;
	reiser4_inode * object;
	reiser4_key start_key;
	reiser4_key stop_key;

	lock_handle lock;
	lock_handle next_lock;

	coord_t coord;
	tap_t tap;

	loff_t result;

	assert("zam-994", lock_stack_isclean(get_current_lock_stack()));

	object = reiser4_inode_data(inode);
	key_by_inode_unix_file(inode, offset, &start_key);
	key_by_inode_unix_file(inode, offset + size, &stop_key);

	init_lh(&lock);
	init_lh(&next_lock);

	/* Stop on twig level */
	result = coord_by_key(
		current_tree, &start_key, &coord, &lock, ZNODE_READ_LOCK,
		FIND_EXACT, TWIG_LEVEL, TWIG_LEVEL, 0, NULL);
	if (result < 0)
		goto error;
	if (result != CBK_COORD_FOUND) {
		result = 0;
		goto error;
	}

	tap_init(&tap, &coord, &lock, ZNODE_WRITE_LOCK);
	result = tap_load(&tap);
	if (result)
		goto error0;

	/* Advance coord to right (even across node boundaries) while coord key
	 * less than stop_key.  */
	while (1) {
		reiser4_key key;
		znode * child;
		reiser4_block_nr blk;

		/* Currently this read-ahead is for formatted nodes only */
		if (!item_is_internal(&coord))
			break;

		item_key_by_coord(&coord, &key);
		if (keyge(&key, &stop_key))
			break;

		result = item_utmost_child_real_block(&coord, LEFT_SIDE, &blk);
		if (result || blk == 0)
			break;

		child = zget(tree, &blk, lock.node, LEAF_LEVEL, GFP_KERNEL);

		if (IS_ERR(child)) {
			result = PTR_ERR(child);
			break;
		}

		/* If znode's page is present that usually means that i/o was
		 * already started for the page. */
		if (znode_page(child) == NULL) {
			result = jstartio(ZJNODE(child));
			if (result) {
				zput(child);
				break;
			}
		}
		zput(child);

		/* Advance coord by one unit ... */
		result = coord_next_unit(&coord);
		if (result == 0)
			continue;

		/* ... and continue on the right neighbor if needed. */
		result = reiser4_get_right_neighbor (
			&next_lock, lock.node, ZNODE_READ_LOCK,
			GN_CAN_USE_UPPER_LEVELS);
		if (result)
			break;

		if (znode_page(next_lock.node) == NULL) {
			loff_t end_offset;

			result = jstartio(ZJNODE(next_lock.node));
			if (result)
				break;

			read_lock_dk(tree);
			end_offset = get_key_offset(znode_get_ld_key(next_lock.node));
			read_unlock_dk(tree);

			result = end_offset - offset;
			break;
		}

		result = tap_move(&tap, &next_lock);
		if (result)
			break;

		done_lh(&next_lock);
		coord_init_first_unit(&coord, lock.node);
	}

	if (! result || result == -E_NO_NEIGHBOR)
		result = size;
 error0:
	tap_done(&tap);
 error:
	done_lh(&lock);
	done_lh(&next_lock);
	return result;
}

typedef unsigned long long int ull_t;
#define PRINTK(...) noop
/* This is derived from the linux original read-ahead code (mm/readahead.c), and
 * cannot be licensed from Namesys in its current state.  */
int reiser4_file_readahead (struct file * file, loff_t offset, size_t size)
{
	loff_t min;
	loff_t max;
	loff_t orig_next_size;
	loff_t actual;
	struct reiser4_file_ra_state * ra;
	struct inode * inode = file->f_dentry->d_inode;

	assert ("zam-995", inode != NULL);

	PRINTK ("R/A REQ: off=%llu, size=%llu\n", (ull_t)offset, (ull_t)size);
	ra = &reiser4_get_file_fsdata(file)->ra;

	max = get_max_readahead(ra);
	if (max == 0)
		goto out;

	min = get_min_readahead(ra);
	orig_next_size = ra->next_size;

	if (!ra->slow_start) {
		ra->slow_start = 1;
		/*
		 * Special case - first read from first page.
		 * We'll assume it's a whole-file read, and
		 * grow the window fast.
		 */
		ra->next_size = max / 2;
		goto do_io;

	}

	/*
	 * Is this request outside the current window?
	 */
	if (offset < ra->start || offset > (ra->start + ra->size)) {
		/* R/A miss. */

		/* next r/a window size is shrunk by fixed offset and enlarged
		 * by 2 * size of read request.  This makes r/a window smaller
		 * for small unordered requests and larger for big read
		 * requests.  */
		ra->next_size += -2 * PAGE_CACHE_SIZE + 2 * size ;
		if (ra->next_size < 0)
			ra->next_size = 0;
do_io:
		ra->start = offset;
		ra->size = size + orig_next_size;
		actual = do_reiser4_file_readahead(inode, offset, ra->size);
		if (actual > 0)
			ra->size = actual;

		ra->ahead_start = ra->start + ra->size;
		ra->ahead_size = ra->next_size;

		actual =  do_reiser4_file_readahead(inode, ra->ahead_start, ra->ahead_size);
		if (actual > 0)
			ra->ahead_size = actual;

		PRINTK ("R/A MISS: cur = [%llu, +%llu[, ahead = [%llu, +%llu[\n",
			(ull_t)ra->start, (ull_t)ra->size,
			(ull_t)ra->ahead_start, (ull_t)ra->ahead_size);
	} else {
		/* R/A hit. */

		/* Enlarge r/a window size. */
		ra->next_size += 2 * size;
		if (ra->next_size > max)
			ra->next_size = max;

		PRINTK("R/A HIT\n");
		while (offset + size >= ra->ahead_start) {
			ra->start = ra->ahead_start;
			ra->size = ra->ahead_size;

			ra->ahead_start = ra->start + ra->size;
			ra->ahead_size = ra->next_size;

			actual = do_reiser4_file_readahead(
				inode, ra->ahead_start, ra->ahead_size);
			if (actual > 0) {
				ra->ahead_size = actual;
			}

			PRINTK ("R/A ADVANCE: cur = [%llu, +%llu[, ahead = [%llu, +%llu[\n",
				(ull_t)ra->start, (ull_t)ra->size,
				(ull_t)ra->ahead_start, (ull_t)ra->ahead_size);

		}
	}

out:
	return 0;
}

reiser4_internal void
reiser4_readdir_readahead_init(struct inode *dir, tap_t *tap)
{
	reiser4_key *stop_key;

	assert("nikita-3542", dir != NULL);
	assert("nikita-3543", tap != NULL);

	stop_key = &tap->ra_info.key_to_stop;
	/* initialize readdir readahead information: include into readahead
	 * stat data of all files of the directory */
	set_key_locality(stop_key, get_inode_oid(dir));
	set_key_type(stop_key, KEY_SD_MINOR);
	set_key_ordering(stop_key, get_key_ordering(max_key()));
	set_key_objectid(stop_key, get_key_objectid(max_key()));
	set_key_offset(stop_key, get_key_offset(max_key()));
}

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 80
   End:
*/
