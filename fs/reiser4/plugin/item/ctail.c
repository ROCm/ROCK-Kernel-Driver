/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* ctails (aka "crypto tails") are items for cryptcompress objects */

/* DESCRIPTION:

Each cryptcompress object is stored on disk as a set of clusters sliced
into ctails.

Internal on-disk structure:

        HEADER   (1)  Here stored disk cluster shift
	BODY
*/

#include "../../forward.h"
#include "../../debug.h"
#include "../../dformat.h"
#include "../../kassign.h"
#include "../../key.h"
#include "../../coord.h"
#include "item.h"
#include "../node/node.h"
#include "../plugin.h"
#include "../object.h"
#include "../../znode.h"
#include "../../carry.h"
#include "../../tree.h"
#include "../../inode.h"
#include "../../super.h"
#include "../../context.h"
#include "../../page_cache.h"
#include "../../cluster.h"
#include "../../flush.h"
#include "../file/funcs.h"

#include <linux/swap.h>
#include <linux/fs.h>

/* return body of ctail item at @coord */
static ctail_item_format *
ctail_formatted_at(const coord_t * coord)
{
	assert("edward-60", coord != NULL);
	return item_body_by_coord(coord);
}

reiser4_internal __u8
cluster_shift_by_coord(const coord_t * coord)
{
	return d8tocpu(&ctail_formatted_at(coord)->cluster_shift);
}

static unsigned long
pg_by_coord(const coord_t * coord)
{
	reiser4_key  key;

	return get_key_offset(item_key_by_coord(coord, &key)) >> PAGE_CACHE_SHIFT;
}

reiser4_internal unsigned long
clust_by_coord(const coord_t * coord)
{
	return pg_by_coord(coord) >> cluster_shift_by_coord(coord);
}

#define cluster_key(key, coord) !(get_key_offset(key) & ~(~0ULL << cluster_shift_by_coord(coord) << PAGE_CACHE_SHIFT))

static char *
first_unit(coord_t * coord)
{
	/* FIXME: warning: pointer of type `void *' used in arithmetic */
	return (char *)item_body_by_coord(coord) + sizeof (ctail_item_format);
}

/* plugin->u.item.b.max_key_inside :
   tail_max_key_inside */

/* plugin->u.item.b.can_contain_key */
reiser4_internal int
can_contain_key_ctail(const coord_t *coord, const reiser4_key *key, const reiser4_item_data *data)
{
	reiser4_key item_key;

	if (item_plugin_by_coord(coord) != data->iplug)
		return 0;

	item_key_by_coord(coord, &item_key);
	if (get_key_locality(key) != get_key_locality(&item_key) ||
	    get_key_objectid(key) != get_key_objectid(&item_key))
		return 0;
	if (get_key_offset(&item_key) + nr_units_ctail(coord) != get_key_offset(key))
		return 0;
	if (cluster_key(key, coord))
		return 0;
	return 1;
}

/* plugin->u.item.b.mergeable
   c-tails of different clusters are not mergeable */
reiser4_internal int
mergeable_ctail(const coord_t * p1, const coord_t * p2)
{
	reiser4_key key1, key2;

	assert("edward-61", item_type_by_coord(p1) == UNIX_FILE_METADATA_ITEM_TYPE);
	assert("edward-62", item_id_by_coord(p1) == CTAIL_ID);

	if (item_id_by_coord(p2) != CTAIL_ID) {
		/* second item is of another type */
		return 0;
	}

	item_key_by_coord(p1, &key1);
	item_key_by_coord(p2, &key2);
	if (get_key_locality(&key1) != get_key_locality(&key2) ||
	    get_key_objectid(&key1) != get_key_objectid(&key2) ||
	    get_key_type(&key1) != get_key_type(&key2)) {
		/* items of different objects */
		return 0;
	    }
	if (get_key_offset(&key1) + nr_units_ctail(p1) != get_key_offset(&key2))
		/*  not adjacent items */
		return 0;
	if (cluster_key(&key2, p2))
		return 0;
	return 1;
}

/* plugin->u.item.b.nr_units */
reiser4_internal pos_in_node_t
nr_units_ctail(const coord_t * coord)
{
	return (item_length_by_coord(coord) - sizeof(ctail_formatted_at(coord)->cluster_shift));
}

/* plugin->u.item.b.estimate:
   estimate how much space is needed to insert/paste @data->length bytes
   into ctail at @coord */
reiser4_internal int
estimate_ctail(const coord_t * coord /* coord of item */,
	     const reiser4_item_data * data /* parameters for new item */)
{
	if (coord == NULL)
		/* insert */
		return (sizeof(ctail_item_format) + data->length);
	else
		/* paste */
		return data->length;
}

#if REISER4_DEBUG_OUTPUT
static unsigned
cluster_size_by_coord(const coord_t * coord)
{
	return (PAGE_CACHE_SIZE << cluster_shift_by_coord(coord));
}


/* ->print() method for this item plugin. */
reiser4_internal void
print_ctail(const char *prefix /* prefix to print */ ,
	  coord_t * coord /* coord of item to print */ )
{
	assert("edward-63", prefix != NULL);
	assert("edward-64", coord != NULL);

	if (item_length_by_coord(coord) < (int) sizeof (ctail_item_format))
		printk("%s: wrong size: %i < %i\n", prefix, item_length_by_coord(coord), sizeof (ctail_item_format));
	else
		printk("%s: disk cluster size: %i\n", prefix, cluster_size_by_coord(coord));
}
#endif

/* ->init() method for this item plugin. */
reiser4_internal int
init_ctail(coord_t * to /* coord of item */,
	   coord_t * from /* old_item */,
	   reiser4_item_data * data /* structure used for insertion */)
{
	int cluster_shift; /* cpu value to convert */

	if (data) {
		assert("edward-463", data->length > sizeof(ctail_item_format));

		cluster_shift = (int)(*((char *)(data->arg)));
		data->length -= sizeof(ctail_item_format);
	}
	else {
		assert("edward-464", from != NULL);

		cluster_shift = (int)(cluster_shift_by_coord(from));
	}
	cputod8(cluster_shift, &ctail_formatted_at(to)->cluster_shift);

	return 0;
}

/* plugin->u.item.b.lookup:
   NULL. (we are looking only for exact keys from item headers) */


/* plugin->u.item.b.check */

/* plugin->u.item.b.paste */
reiser4_internal int
paste_ctail(coord_t * coord, reiser4_item_data * data, carry_plugin_info * info UNUSED_ARG)
{
	unsigned old_nr_units;

	assert("edward-268", data->data != NULL);
	/* copy only from kernel space */
	assert("edward-66", data->user == 0);

	old_nr_units = item_length_by_coord(coord) - sizeof(ctail_item_format) - data->length;

	/* ctail items never get pasted in the middle */

	if (coord->unit_pos == 0 && coord->between == AT_UNIT) {

                /* paste at the beginning when create new item */
		assert("edward-450", item_length_by_coord(coord) == data->length + sizeof(ctail_item_format));
		assert("edward-451", old_nr_units == 0);
	}
	else if (coord->unit_pos == old_nr_units - 1 && coord->between == AFTER_UNIT) {

                /* paste at the end */
		coord->unit_pos++;
	}
	else
		impossible("edward-453", "bad paste position");

	xmemcpy(first_unit(coord) + coord->unit_pos, data->data, data->length);

	return 0;
}

/* plugin->u.item.b.fast_paste */

/* plugin->u.item.b.can_shift
   number of units is returned via return value, number of bytes via @size. For
   ctail items they coincide */
reiser4_internal int
can_shift_ctail(unsigned free_space, coord_t * source,
		znode * target, shift_direction direction UNUSED_ARG, unsigned *size, unsigned want)
{
	/* make sure that that we do not want to shift more than we have */
	assert("edward-68", want > 0 && want <= nr_units_ctail(source));

	*size = min(want, free_space);

	if (!target) {
		/* new item will be created */
		if (*size <= sizeof(ctail_item_format)) {
			*size = 0;
			return 0;
		}
		return *size - sizeof(ctail_item_format);
	}
	return *size;
}

/* plugin->u.item.b.copy_units */
reiser4_internal void
copy_units_ctail(coord_t * target, coord_t * source,
		unsigned from, unsigned count, shift_direction where_is_free_space, unsigned free_space UNUSED_ARG)
{
	/* make sure that item @target is expanded already */
	assert("edward-69", (unsigned) item_length_by_coord(target) >= count);
	assert("edward-70", free_space >= count);

	if (item_length_by_coord(target) == count) {
		/* new item has been created */
		assert("edward-465", count > sizeof(ctail_item_format));

		count--;
	}
	if (where_is_free_space == SHIFT_LEFT) {
		/* append item @target with @count first bytes of @source:
		   this restriction came from ordinary tails */
		assert("edward-71", from == 0);

		xmemcpy(first_unit(target) + nr_units_ctail(target) - count, first_unit(source), count);
	} else {
		/* target item is moved to right already */
		reiser4_key key;

		assert("edward-72", nr_units_ctail(source) == from + count);

		xmemcpy(first_unit(target), first_unit(source) + from, count);

		/* new units are inserted before first unit in an item,
		   therefore, we have to update item key */
		item_key_by_coord(source, &key);
		set_key_offset(&key, get_key_offset(&key) + from);

		node_plugin_by_node(target->node)->update_item_key(target, &key, 0 /*info */);
	}
}

/* plugin->u.item.b.create_hook */
/* plugin->u.item.b.kill_hook */
reiser4_internal int
kill_hook_ctail(const coord_t *coord, pos_in_node_t from, pos_in_node_t count, carry_kill_data *kdata)
{
	struct inode *inode;

	assert("edward-291", znode_is_write_locked(coord->node));

	inode = kdata->inode;
	if (inode) {
		reiser4_key key;
		item_key_by_coord(coord, &key);

		if (from == 0 && cluster_key(&key, coord)) {
			pgoff_t start = off_to_pg(get_key_offset(&key));
			pgoff_t end = off_to_pg(inode->i_size);
			truncate_cluster(inode, start, end - start + 1);
		}
	}
	return 0;
}

/* for shift_hook_ctail(),
   return true if the first disk cluster item has dirty child
*/
static int
ctail_squeezable (const coord_t *coord)
{
	int result;
	reiser4_key  key;
	jnode * child = NULL;

	assert("edward-477", coord != NULL);
	assert("edward-478", item_plugin_by_coord(coord) == item_plugin_by_id(CTAIL_ID));

	item_key_by_coord(coord, &key);
	child =  jlookup(current_tree, get_key_objectid(&key), pg_by_coord(coord));

	if (!child)
		return 0;
	LOCK_JNODE(child);
	if (jnode_is_dirty(child))
		result = 1;
	else
		result = 0;
	UNLOCK_JNODE(child);
	jput(child);
	return result;
}

/* plugin->u.item.b.shift_hook */
reiser4_internal int
shift_hook_ctail(const coord_t * item /* coord of item */ ,
		 unsigned from UNUSED_ARG /* start unit */ ,
		 unsigned count UNUSED_ARG /* stop unit */ ,
		 znode * old_node /* old parent */ )
{
	assert("edward-479", item != NULL);
	assert("edward-480", item->node != old_node);

	if (!znode_squeezable(old_node) || znode_squeezable(item->node))
		return 0;
	if (ctail_squeezable(item))
		znode_set_squeezable(item->node);
	return 0;
}

static int
cut_or_kill_ctail_units(coord_t * coord, pos_in_node_t from, pos_in_node_t to, int cut,
			void *p, reiser4_key * smallest_removed, reiser4_key *new_first)
{
	pos_in_node_t count; /* number of units to cut */
	char *item;

 	count = to - from + 1;
	item = item_body_by_coord(coord);

	/* When we cut from the end of item - we have nothing to do */
	assert("edward-73", count < nr_units_ctail(coord));
	assert("edward-74", ergo(from != 0, to == coord_last_unit_pos(coord)));

	if (smallest_removed) {
		/* store smallest key removed */
		item_key_by_coord(coord, smallest_removed);
		set_key_offset(smallest_removed, get_key_offset(smallest_removed) + from);
	}

	if (new_first) {
		assert("vs-1531", from == 0);

		item_key_by_coord(coord, new_first);
		set_key_offset(new_first, get_key_offset(new_first) + from + count);
	}

	if (!cut)
		kill_hook_ctail(coord, from, 0, (struct carry_kill_data *)p);

	if (from == 0) {
		if (count != nr_units_ctail(coord)) {
			/* part of item is removed, so move free space at the beginning
			   of the item and update item key */
			reiser4_key key;
			xmemcpy(item + to + 1, item, sizeof(ctail_item_format));
			item_key_by_coord(coord, &key);
			set_key_offset(&key, get_key_offset(&key) + count);
			node_plugin_by_node(coord->node)->update_item_key(coord, &key, 0 /*info */ );
		}
		else {
			impossible("vs-1532", "cut_units should not be called to cut evrything");
			/* whole item is cut, so more then amount of space occupied
			   by units got freed */
			count += sizeof(ctail_item_format);
		}
		if (REISER4_DEBUG)
			xmemset(item, 0, count);
	}
	else if (REISER4_DEBUG)
		xmemset(item + sizeof(ctail_item_format) + from, 0, count);
	return count;
}

/* plugin->u.item.b.cut_units */
reiser4_internal int
cut_units_ctail(coord_t *item, pos_in_node_t from, pos_in_node_t to,
		carry_cut_data *cdata, reiser4_key *smallest_removed, reiser4_key *new_first)
{
	return cut_or_kill_ctail_units(item, from, to, 1, NULL, smallest_removed, new_first);
}

/* plugin->u.item.b.kill_units */
reiser4_internal int
kill_units_ctail(coord_t *item, pos_in_node_t from, pos_in_node_t to,
		 struct carry_kill_data *kdata, reiser4_key *smallest_removed, reiser4_key *new_first)
{
	return cut_or_kill_ctail_units(item, from, to, 0, kdata, smallest_removed, new_first);
}

/* plugin->u.item.s.file.read */
reiser4_internal int
read_ctail(struct file *file UNUSED_ARG, flow_t *f, hint_t *hint)
{
	uf_coord_t *uf_coord;
	coord_t *coord;

	uf_coord = &hint->coord;
	coord = &uf_coord->base_coord;
	assert("edward-127", f->user == 0);
	assert("edward-128", f->data);
	assert("edward-129", coord && coord->node);
	assert("edward-130", coord_is_existing_unit(coord));
	assert("edward-132", znode_is_loaded(coord->node));

	/* start read only from the beginning of ctail */
	assert("edward-133", coord->unit_pos == 0);
	/* read only whole ctails */
	assert("edward-135", nr_units_ctail(coord) <= f->length);

	assert("edward-136", schedulable());

	memcpy(f->data, (char *)first_unit(coord), (size_t)nr_units_ctail(coord));

	mark_page_accessed(znode_page(coord->node));
	move_flow_forward(f, nr_units_ctail(coord));

	coord->item_pos ++;
	coord->between = BEFORE_ITEM;

	return 0;
}

/* this reads one cluster form disk,
   attaches buffer with decrypted and decompressed data */
reiser4_internal int
ctail_read_cluster (reiser4_cluster_t * clust, struct inode * inode, int write)
{
	int result;

	assert("edward-139", clust->buf == NULL);
	assert("edward-671", clust->hint != NULL);
	assert("edward-140", clust->stat != FAKE_CLUSTER);
	assert("edward-672", crc_inode_ok(inode));
	assert("edward-145", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));

	if (!hint_prev_cluster(clust)) {
		done_lh(clust->hint->coord.lh);
		unset_hint(clust->hint);
	}
	/* allocate temporary buffer of disk cluster size */

	clust->bsize = inode_scaled_offset(inode, fsize_to_count(clust, inode) +
					   max_crypto_overhead(inode));
	if (clust->bsize > inode_scaled_cluster_size(inode))
		clust->bsize = inode_scaled_cluster_size(inode);

	clust->buf = reiser4_kmalloc(clust->bsize, GFP_KERNEL);
	if (!clust->buf)
		return -ENOMEM;

	result = find_cluster(clust, inode, 1 /* read */, write);
	if (cbk_errored(result))
		goto out;

	assert("edward-673", znode_is_any_locked(clust->hint->coord.lh->node));

	result = inflate_cluster(clust, inode);
	if(result)
		goto out;
	return 0;
 out:
	put_cluster_data(clust);
	return result;
}

/* read one locked page */
reiser4_internal int
do_readpage_ctail(reiser4_cluster_t * clust, struct page *page)
{
	int ret;
	unsigned cloff;
	struct inode * inode;
	char * data;
	int release = 0;
	size_t pgcnt;

	assert("edward-212", PageLocked(page));

	if(PageUptodate(page))
		goto exit;

	inode = page->mapping->host;

	if (!cluster_is_uptodate(clust)) {
		clust->index = pg_to_clust(page->index, inode);
		unlock_page(page);
		ret = ctail_read_cluster(clust, inode, 0 /* do not write */);
		lock_page(page);
		if (ret)
			return ret;
		/* cluster was uptodated here, release it before exit */
		release = 1;
	}
	if(PageUptodate(page))
		/* races with another read/write */
		goto exit;
	if (clust->stat == FAKE_CLUSTER) {
		/* fill page by zeroes */
		char *kaddr = kmap_atomic(page, KM_USER0);

		assert("edward-119", clust->buf == NULL);

		memset(kaddr, 0, PAGE_CACHE_SIZE);
		flush_dcache_page(page);
		kunmap_atomic(kaddr, KM_USER0);
		SetPageUptodate(page);

		ON_TRACE(TRACE_CTAIL, " - hole, OK\n");
		return 0;
	}
	/* fill page by plain text from cluster handle */

	assert("edward-120", clust->len <= inode_cluster_size(inode));

        /* start page offset in the cluster */
	cloff = pg_to_off_to_cloff(page->index, inode);
	/* bytes in page */
	pgcnt = off_to_pgcount(inode->i_size, page->index);
	assert("edward-620", off_to_pgcount(inode->i_size, page->index) > 0);

	data = kmap(page);
	memcpy(data, clust->buf + cloff, pgcnt);
	memset(data + pgcnt, 0, (size_t)PAGE_CACHE_SIZE - pgcnt);
	kunmap(page);
	SetPageUptodate(page);
 exit:
	if (release)
		put_cluster_data(clust);
	return 0;
}

/* plugin->u.item.s.file.readpage */
reiser4_internal int readpage_ctail(void * vp, struct page * page)
{
	int result;
	hint_t hint;
	lock_handle lh;
	reiser4_cluster_t * clust = vp;

	assert("edward-114", clust != NULL);
	assert("edward-115", PageLocked(page));
	assert("edward-116", !PageUptodate(page));
	assert("edward-117", !jprivate(page) && !PagePrivate(page));
	assert("edward-118", page->mapping && page->mapping->host);

	clust->hint = &hint;
	init_lh(&lh);
	result = load_file_hint(clust->file, &hint, &lh);
	if (result)
		return result;

	result = do_readpage_ctail(clust, page);

	hint.coord.valid = 0;
	save_file_hint(clust->file, &hint);
	done_lh(&lh);
	put_cluster_data(clust);

	assert("edward-213", PageLocked(page));
	return result;
}

static int
ctail_read_page_cluster(reiser4_cluster_t * clust, struct inode * inode)
{
	int i;
	int result;
	assert("edward-779", clust != NULL);
	assert("edward-780", inode != NULL);

	set_nrpages_by_inode(clust, inode);

	result = grab_cluster_pages(inode, clust);
	if (result)
		return result;
	result = ctail_read_cluster(clust, inode, 0 /* read */);
	if (result)
		goto out;
	/* stream is attached at this point */
	assert("edward-781", cluster_is_uptodate(clust));

	for (i=0; i < clust->nr_pages; i++) {
		struct page * page = clust->pages[i];
		lock_page(page);
		do_readpage_ctail(clust, page);
		unlock_page(page);
	}
	release_cluster_buf(clust);
 out:
	release_cluster_pages(clust, 0);
	return result;
}

#define check_order(pages)                                                    \
assert("edward-214", ergo(!list_empty(pages) && pages->next != pages->prev,   \
       list_to_page(pages)->index < list_to_next_page(pages)->index))

/* plugin->s.file.writepage */

/* plugin->u.item.s.file.readpages
   populate an address space with some pages, and start reads against them.
   FIXME_EDWARD: this function should return errors
*/
reiser4_internal void
readpages_ctail(void *vp, struct address_space *mapping, struct list_head *pages)
{
	int ret = 0;
	hint_t hint;
	lock_handle lh;
	reiser4_cluster_t clust;
	struct page *page;
	struct pagevec lru_pvec;
	struct inode * inode = mapping->host;

	check_order(pages);
	pagevec_init(&lru_pvec, 0);
	reiser4_cluster_init(&clust);
	clust.file = vp;
	clust.hint = &hint;

	alloc_clust_pages(&clust, inode);
	init_lh(&lh);

	ret = load_file_hint(clust.file, &hint, &lh);
	if (ret)
		return;

	/* address_space-level file readahead doesn't know about
	   reiser4 page clustering, so we work around this fact */

	while (!list_empty(pages)) {
		page = list_to_page(pages);
		list_del(&page->lru);
		if (add_to_page_cache(page, mapping, page->index, GFP_KERNEL)) {
			page_cache_release(page);
			continue;
		}
		if (PageUptodate(page)) {
			unlock_page(page);
			continue;
		}
		unlock_page(page);
		clust.index = pg_to_clust(page->index, inode);
		ret = ctail_read_page_cluster(&clust, inode);
		if (ret)
			goto exit;
		assert("edward-782", !cluster_is_uptodate(&clust));

		lock_page(page);
		ret = do_readpage_ctail(&clust, page);
		if (!pagevec_add(&lru_pvec, page))
			__pagevec_lru_add(&lru_pvec);
		if (ret) {
			warning("edward-215", "do_readpage_ctail failed");
			unlock_page(page);
		exit:
			while (!list_empty(pages)) {
				struct page *victim;

				victim = list_to_page(pages);
				list_del(&victim->lru);
				page_cache_release(victim);
			}
			break;
		}
		unlock_page(page);
	}
	assert("edward-783", !cluster_is_uptodate(&clust));
	hint.coord.valid = 0;
	save_file_hint(clust.file, &hint);

	done_lh(&lh);
	/* free array */
	free_clust_pages(&clust);
	put_cluster_data(&clust);
	pagevec_lru_add(&lru_pvec);
	return;
}

/*
   plugin->u.item.s.file.append_key
*/
reiser4_internal reiser4_key *
append_key_ctail(const coord_t *coord, reiser4_key *key)
{
	return NULL;
}

/* key of the first item of the next cluster */
reiser4_internal reiser4_key *
append_cluster_key_ctail(const coord_t *coord, reiser4_key *key)
{
	item_key_by_coord(coord, key);
	set_key_offset(key, ((__u64)(clust_by_coord(coord)) + 1) << cluster_shift_by_coord(coord) << PAGE_CACHE_SHIFT);
	return key;
}

static int
insert_crc_flow(coord_t * coord, lock_handle * lh, flow_t * f, struct inode * inode)
{
	int result;
	carry_pool pool;
	carry_level lowest_level;
	carry_op *op;
	reiser4_item_data data;
	__u8 cluster_shift = inode_cluster_shift(inode);

	init_carry_pool(&pool);
	init_carry_level(&lowest_level, &pool);

	assert("edward-466", coord->between == AFTER_ITEM || coord->between == AFTER_UNIT ||
	       coord->between == BEFORE_ITEM);

	if (coord->between == AFTER_UNIT) {
		coord->unit_pos = 0;
		coord->between = AFTER_ITEM;
	}
	op = post_carry(&lowest_level, COP_INSERT_FLOW, coord->node, 0 /* operate directly on coord -> node */ );
	if (IS_ERR(op) || (op == NULL))
		return RETERR(op ? PTR_ERR(op) : -EIO);
	data.user = 0;
	data.iplug = item_plugin_by_id(CTAIL_ID);
	data.arg = &cluster_shift;

	data.length = 0;
	data.data = 0;

	op->u.insert_flow.flags = COPI_DONT_SHIFT_LEFT | COPI_DONT_SHIFT_RIGHT;
	op->u.insert_flow.insert_point = coord;
	op->u.insert_flow.flow = f;
	op->u.insert_flow.data = &data;
	op->u.insert_flow.new_nodes = 0;

	lowest_level.track_type = CARRY_TRACK_CHANGE;
	lowest_level.tracked = lh;

	ON_STATS(lowest_level.level_no = znode_get_level(coord->node));
	result = carry(&lowest_level, 0);
	done_carry_pool(&pool);

	return result;
}

static int
insert_crc_flow_in_place(coord_t * coord, lock_handle * lh, flow_t * f, struct inode * inode)
{
	int ret;
	coord_t point;
	lock_handle lock;

	assert("edward-674", f->length <= inode_scaled_cluster_size(inode));
	assert("edward-484", coord->between == AT_UNIT ||
	       coord->between == AFTER_UNIT || coord->between == AFTER_ITEM);

	coord_dup (&point, coord);

	if (coord->between == AT_UNIT) {
		coord_prev_item(&point);

		assert("edward-485", item_plugin_by_coord(&point) == item_plugin_by_id(CTAIL_ID));

		point.between = AFTER_ITEM;
	}

	init_lh (&lock);
	copy_lh(&lock, lh);

	ret = insert_crc_flow(&point, &lock, f, inode);
	done_lh(&lock);
	return ret;
}

/* overwrite tail citem or its part */
static int
overwrite_ctail(coord_t * coord, flow_t * f)
{
	unsigned count;

	assert("edward-269", f->user == 0);
	assert("edward-270", f->data != NULL);
	assert("edward-271", f->length > 0);
	assert("edward-272", coord_is_existing_unit(coord));
	assert("edward-273", coord->unit_pos == 0);
	assert("edward-274", znode_is_write_locked(coord->node));
	assert("edward-275", schedulable());
	assert("edward-467", item_plugin_by_coord(coord) == item_plugin_by_id(CTAIL_ID));

	count = nr_units_ctail(coord);

	if (count > f->length)
		count = f->length;
	xmemcpy(first_unit(coord), f->data, count);
	move_flow_forward(f, count);
	coord->unit_pos += count;
	return 0;
}

/* cut ctail item or its tail subset */
static int
cut_ctail(coord_t * coord)
{
	coord_t stop;

	assert("edward-435", coord->between == AT_UNIT &&
	       coord->item_pos < coord_num_items(coord) &&
	       coord->unit_pos <= coord_num_units(coord));

	if(coord->unit_pos == coord_num_units(coord)) {
		/* nothing to cut */
		return 0;
	}
	coord_dup(&stop, coord);
	stop.unit_pos = coord_last_unit_pos(coord);

	return cut_node_content(coord, &stop, NULL, NULL, NULL);
}

#define UNPREPPED_DCLUSTER_LEN 2

/* insert minimal disk cluster for unprepped page cluster */
int ctail_make_unprepped_cluster(reiser4_cluster_t * clust, struct inode * inode)
{
	char buf[UNPREPPED_DCLUSTER_LEN];
	flow_t f;
	int result;

	assert("edward-675", get_current_context()->grabbed_blocks == 0);
	assert("edward-676", znode_is_wlocked(clust->hint->coord.lh->node));

	grab_space_enable();
	result = reiser4_grab_space(estimate_insert_cluster(inode, 1 /*unprepped */), 0);
	if (result)
		return result;
	xmemset(buf, 0, UNPREPPED_DCLUSTER_LEN);

	flow_by_inode_cryptcompress(inode, buf, 0 /* kernel space */, UNPREPPED_DCLUSTER_LEN, clust_to_off(clust->index, inode), WRITE_OP, &f);

	result = insert_crc_flow(&clust->hint->coord.base_coord, clust->hint->coord.lh, &f, inode);
	all_grabbed2free();
	if (result)
		return result;

	assert("edward-677", reiser4_clustered_blocks(reiser4_get_current_sb()));
	assert("edward-678", znode_is_dirty(clust->hint->coord.base_coord.node));

	znode_set_squeezable(clust->hint->coord.base_coord.node);
	return result;
}

static ctail_squeeze_info_t * ctail_squeeze_data(flush_pos_t * pos)
{
	return &pos->sq->itm->u.ctail_info;
}

/* the following functions are used by flush item methods */
/* plugin->u.item.s.file.write ? */
reiser4_internal int
write_ctail(flush_pos_t * pos, crc_write_mode_t mode)
{
	int result;
	ctail_squeeze_info_t * info;

	assert("edward-468", pos != NULL);
	assert("edward-469", pos->sq != NULL);
	assert("edward-845", item_squeeze_data(pos) != NULL);

	info = ctail_squeeze_data(pos);

	switch (mode) {
	case CRC_FIRST_ITEM:
	case CRC_APPEND_ITEM:
		assert("edward-679", info->flow.data != NULL);
		result = insert_crc_flow_in_place(&pos->coord, &pos->lock, &info->flow, info->inode);
		break;
	case CRC_OVERWRITE_ITEM:
		overwrite_ctail(&pos->coord, &info->flow);
	case CRC_CUT_ITEM:
		result = cut_ctail(&pos->coord);
		break;
	default:
		result = RETERR(-EIO);
		impossible("edward-244", "wrong ctail write mode");
	}
	return result;
}

reiser4_internal item_plugin *
item_plugin_by_jnode(jnode * node)
{
	assert("edward-302", jnode_is_cluster_page(node));
	return (item_plugin_by_id(CTAIL_ID));
}

static jnode *
next_jnode_cluster(jnode * node, struct inode *inode, reiser4_cluster_t * clust)
{
	return jlookup(tree_by_inode(inode), get_inode_oid(inode), clust_to_pg(clust->index + 1, inode));
}

/* plugin->u.item.f.scan */
/* Check if the cluster node we started from is not presented by any items
   in the tree. If so, create the link by inserting prosessed cluster into
   the tree. Don't care about scan counter since leftward scanning will be
   continued from rightmost dirty node.
*/
reiser4_internal int scan_ctail(flush_scan * scan)
{
	int result;
	struct page * page;
	struct inode * inode;
	reiser4_cluster_t clust;
	flow_t f;
	jnode * node = scan->node;
	file_plugin * fplug;

	reiser4_cluster_init(&clust);

	assert("edward-227", scan->node != NULL);
	assert("edward-228", jnode_is_cluster_page(scan->node));
	assert("edward-639", znode_is_write_locked(scan->parent_lock.node));

	if (!znode_squeezable(scan->parent_lock.node)) {
		assert("edward-680", !jnode_is_dirty(scan->node));
		warning("edward-681", "cluster page is already processed");
		return -EAGAIN;
	}

	if (get_flush_scan_nstat(scan) == LINKED) {
		/* nothing to do */
		return 0;
	}

	jref(node);

	do {
		LOCK_JNODE(node);
		if (!(jnode_is_dirty(node) &&
		      (node->atom == ZJNODE(scan->parent_lock.node)->atom) &&
		      JF_ISSET(node, JNODE_NEW))) {
                        /* don't touch! */
			UNLOCK_JNODE(node);
			jput(node);
			break;
		}
		UNLOCK_JNODE(node);

		reiser4_cluster_init(&clust);

		page = jnode_page(node);

		assert("edward-229", page->mapping != NULL);
		assert("edward-230", page->mapping != NULL);
		assert("edward-231", page->mapping->host != NULL);

		inode = page->mapping->host;
		fplug = inode_file_plugin(inode);

		assert("edward-244", fplug == file_plugin_by_id(CRC_FILE_PLUGIN_ID));
		assert("edward-232", inode_get_flag(inode, REISER4_CLUSTER_KNOWN));
		assert("edward-233", scan->direction == LEFT_SIDE);

		clust.index = pg_to_clust(page->index, inode);

		/* remove jnode cluster from dirty list */
		result = flush_cluster_pages(&clust, inode);
		if (result)
			return result;
		result = deflate_cluster(NULL, &clust, inode);
		if (result)
			goto error;

		assert("edward-633", clust.len != 0);

		fplug->flow_by_inode(inode, clust.buf, 0, clust.len, clust_to_off(clust.index, inode), WRITE, &f);
		/* insert processed data */
		result = insert_crc_flow(&scan->parent_coord, /* insert point */
					 &scan->parent_lock, &f, inode);
		if (result)
			goto error;
		assert("edward-234", f.length == 0);
		JF_CLR(node, JNODE_NEW);
		release_cluster_buf(&clust);
		jput(node);
	}
	while ((node = next_jnode_cluster(node, inode, &clust)));

	/* now the child is linked to its parent,
	   set appropriate status */
	set_flush_scan_nstat(scan, LINKED);
	return 0;
 error:
	release_cluster_buf(&clust);
	return result;
}

/* If true, this function attaches children */
static int
should_attach_squeeze_idata(flush_pos_t * pos)
{
	int result;
	assert("edward-431", pos != NULL);
	assert("edward-432", pos->child == NULL);
	assert("edward-619", znode_is_write_locked(pos->coord.node));
	assert("edward-470", item_plugin_by_coord(&pos->coord) == item_plugin_by_id(CTAIL_ID));

	/* check for leftmost child */
	utmost_child_ctail(&pos->coord, LEFT_SIDE, &pos->child);

	if (!pos->child)
		return 0;
	LOCK_JNODE(pos->child);
	result = jnode_is_dirty(pos->child) &&
		pos->child->atom == ZJNODE(pos->coord.node)->atom;
	UNLOCK_JNODE(pos->child);
	if (!result && pos->child) {
		/* existing child isn't to attach, clear up this one */
		jput(pos->child);
		pos->child = NULL;
	}
	return result;
}

static int
alloc_squeeze_tfm_data(squeeze_info_t * sq)
{
	assert("edward-808", sq != NULL);
	assert("edward-809", sq->tfm == NULL);

	sq->tfm = reiser4_kmalloc(SQUEEZE_TFM_INFO_SIZE , GFP_KERNEL);
	if (!sq->tfm)
		return -ENOMEM;
	xmemset(sq->tfm, 0, SQUEEZE_TFM_INFO_SIZE);
	return 0;
}

static void
free_squeeze_tfm_data(squeeze_info_t * sq)
{
	reiser4_compression_id i;
	compression_plugin * cplug;

	assert("edward-810", sq != NULL);
	assert("edward-811", sq->tfm != NULL);

	for(i=0; i < LAST_COMPRESSION_ID; i++) {
		if (!sq->tfm[i])
			continue;
		cplug = compression_plugin_by_id(i);
		assert("edward-812", cplug->free != NULL);
		cplug->free(&sq->tfm[i], TFM_WRITE);
	}
	reiser4_kfree(sq->tfm);
	sq->tfm = NULL;
	return;
}

/* plugin->init_squeeze_data() */
static int
init_squeeze_data_ctail(squeeze_item_info_t * idata, struct inode * inode)
{
	ctail_squeeze_info_t * info;
	assert("edward-813", idata != NULL);
	assert("edward-814", inode != NULL);

	info = &idata->u.ctail_info;
	info->clust = reiser4_kmalloc(sizeof(*info->clust), GFP_KERNEL);
	if (!info->clust)
		return -ENOMEM;

	reiser4_cluster_init(info->clust);
	info->inode = inode;

	return 0;
}

/* plugin->free_squeeze_data() */
static void
free_squeeze_data_ctail(squeeze_item_info_t * idata)
{
	ctail_squeeze_info_t * info;
	assert("edward-815", idata != NULL);

	info = &idata->u.ctail_info;
	if (info->clust) {
		release_cluster_buf(info->clust);
		reiser4_kfree(info->clust);
	}
	return;
}

static int
alloc_item_squeeze_data(squeeze_info_t * sq)
{
	assert("edward-816", sq != NULL);
	assert("edward-817", sq->itm == NULL);

	sq->itm = reiser4_kmalloc(sizeof(*sq->itm), GFP_KERNEL);
	if (sq->itm == NULL)
		return -ENOMEM;
	return 0;
}

static void
free_item_squeeze_data(squeeze_info_t * sq)
{
	assert("edward-818", sq != NULL);
	assert("edward-819", sq->itm != NULL);
	assert("edward-820", sq->iplug != NULL);

	/* iplug->free(sq->idata); */
	free_squeeze_data_ctail(sq->itm);

	reiser4_kfree(sq->itm);
	sq->itm = NULL;
	return;
}

static int
alloc_squeeze_data(flush_pos_t * pos)
{
	assert("edward-821", pos != NULL);
	assert("edward-822", pos->sq == NULL);

	pos->sq = reiser4_kmalloc(sizeof(*pos->sq), GFP_KERNEL);
	if (!pos->sq)
		return -ENOMEM;
	xmemset(pos->sq, 0, sizeof(*pos->sq));
	return 0;
}

reiser4_internal void
free_squeeze_data(flush_pos_t * pos)
{
	squeeze_info_t * sq;

	assert("edward-823", pos != NULL);
	assert("edward-824", pos->sq != NULL);

	sq = pos->sq;
	if (sq->tfm)
		free_squeeze_tfm_data(sq);
	if (sq->itm)
		free_item_squeeze_data(sq);
	reiser4_kfree(pos->sq);
	pos->sq = NULL;
	return;
}

static int
init_item_squeeze_data(flush_pos_t * pos, struct inode * inode)
{
	squeeze_info_t * sq;

	assert("edward-825", pos != NULL);
	assert("edward-826", pos->sq != NULL);
	assert("edward-827", item_squeeze_data(pos) != NULL);
	assert("edward-828", inode != NULL);

	sq = pos->sq;

	xmemset(sq->itm, 0, sizeof(*sq->itm));

	/* iplug->init_squeeze_data() */
	return init_squeeze_data_ctail(sq->itm, inode);
}

/* create disk cluster info used by 'squeeze' phase of the flush squalloc() */
static int
attach_squeeze_idata(flush_pos_t * pos, struct inode * inode)
{
	int ret = 0;
	ctail_squeeze_info_t * info;
	reiser4_cluster_t *clust;
	file_plugin * fplug = inode_file_plugin(inode);
	compression_plugin * cplug = inode_compression_plugin(inode);

	assert("edward-248", pos != NULL);
	assert("edward-249", pos->child != NULL);
	assert("edward-251", inode != NULL);
	assert("edward-682", crc_inode_ok(inode));
	assert("edward-252", fplug == file_plugin_by_id(CRC_FILE_PLUGIN_ID));
	assert("edward-473", item_plugin_by_coord(&pos->coord) == item_plugin_by_id(CTAIL_ID));

	if (!pos->sq) {
		ret = alloc_squeeze_data(pos);
		if (ret)
			return ret;
	}
	if (!tfm_squeeze_data(pos) && cplug->alloc != NULL) {
		ret = alloc_squeeze_tfm_data(pos->sq);
		if (ret)
			goto exit;
	}
	if (cplug->alloc != NULL && *tfm_squeeze_idx(pos, cplug->h.id) == NULL) {
		ret = cplug->alloc(tfm_squeeze_idx(pos, cplug->h.id), TFM_WRITE);
		if (ret)
			goto exit;
	}
	assert("edward-829", pos->sq != NULL);
	assert("edward-250", item_squeeze_data(pos) == NULL);

	pos->sq->iplug = item_plugin_by_id(CTAIL_ID);

	ret = alloc_item_squeeze_data(pos->sq);
	if (ret)
		goto exit;
	ret = init_item_squeeze_data(pos, inode);
	if (ret)
		goto exit;
	info = ctail_squeeze_data(pos);
	clust = info->clust;
	clust->index = pg_to_clust(jnode_page(pos->child)->index, inode);

	ret = flush_cluster_pages(clust, inode);
	if (ret)
		goto exit;

	assert("edward-830", ergo(!tfm_squeeze_pos(pos, cplug->h.id), !cplug->alloc));

	ret = deflate_cluster(tfm_squeeze_pos(pos, cplug->h.id), clust, inode);
	if (ret)
		goto exit;

	inc_item_squeeze_count(pos);

	/* make flow by transformed stream */
	fplug->flow_by_inode(info->inode, clust->buf, 0/* kernel space */,
			     clust->len, clust_to_off(clust->index, inode),
			     WRITE_OP, &info->flow);
	jput(pos->child);

	assert("edward-683", crc_inode_ok(inode));
	return 0;
 exit:
	jput(pos->child);
	free_squeeze_data(pos);
	return ret;
}

/* clear up disk cluster info */
static void
detach_squeeze_idata(squeeze_info_t * sq)
{
	squeeze_item_info_t * idata;
	ctail_squeeze_info_t * info;
	struct inode * inode;

	assert("edward-253", sq != NULL);
	assert("edward-840", sq->itm != NULL);

	idata = sq->itm;
	info = &idata->u.ctail_info;

	assert("edward-254", info->clust != NULL);
	assert("edward-255", info->inode != NULL);

	inode = info->inode;

	assert("edward-841", atomic_read(&inode->i_count));
	assert("edward-256", info->clust->buf != NULL);

	atomic_dec(&inode->i_count);

	free_item_squeeze_data(sq);
	return;
}

/* plugin->u.item.f.utmost_child */

/* This function sets leftmost child for a first cluster item,
   if the child exists, and NULL in other cases.
   NOTE-EDWARD: Do not call this for RIGHT_SIDE */

reiser4_internal int
utmost_child_ctail(const coord_t * coord, sideof side, jnode ** child)
{
	reiser4_key key;

	item_key_by_coord(coord, &key);

	assert("edward-257", coord != NULL);
	assert("edward-258", child != NULL);
	assert("edward-259", side == LEFT_SIDE);
	assert("edward-260", item_plugin_by_coord(coord) == item_plugin_by_id(CTAIL_ID));

	if (!cluster_key(&key, coord))
		*child = NULL;
	else
		*child = jlookup(current_tree, get_key_objectid(item_key_by_coord(coord, &key)), pg_by_coord(coord));
	return 0;
}

/* plugin->u.item.f.squeeze */
/* write ctail in guessed mode */
reiser4_internal int
squeeze_ctail(flush_pos_t * pos)
{
	int result;
	ctail_squeeze_info_t * info = NULL;
	crc_write_mode_t mode = CRC_OVERWRITE_ITEM;

	assert("edward-261", pos != NULL);

	if (!pos->sq || !item_squeeze_data(pos)) {
		if (should_attach_squeeze_idata(pos)) {
			/* attach squeeze item info */
			struct inode * inode;

			assert("edward-264", pos->child != NULL);
			assert("edward-265", jnode_page(pos->child) != NULL);
			assert("edward-266", jnode_page(pos->child)->mapping != NULL);

			inode = jnode_page(pos->child)->mapping->host;

			assert("edward-267", inode != NULL);

			/* attach item squeeze info by child and put the last one */
			result = attach_squeeze_idata(pos, inode);
			pos->child = NULL;
			if (result != 0)
				return result;
			info = ctail_squeeze_data(pos);
		}
		else
			/* unsqueezable */
			return 0;
	}
	else {
		/* use old squeeze info */

		squeeze_item_info_t * idata = item_squeeze_data(pos);
		info = ctail_squeeze_data(pos);

		if (info->flow.length) {
			/* append or overwrite */
			if (idata->mergeable) {
				mode = CRC_OVERWRITE_ITEM;
				idata->mergeable = 0;
			}
			else
				mode = CRC_APPEND_ITEM;
		}
		else {
                        /* cut or invalidate */
			if (idata->mergeable) {
				mode = CRC_CUT_ITEM;
				idata->mergeable = 0;
			}
			else {
				detach_squeeze_idata(pos->sq);
				return RETERR(-E_REPEAT);
			}
		}
	}
	assert("edward-433", info != NULL);
	result = write_ctail(pos, mode);
	if (result) {
		detach_squeeze_idata(pos->sq);
		return result;
	}

	if (mode == CRC_APPEND_ITEM) {
		/* detach squeeze info */
		assert("edward-434", info->flow.length == 0);
		detach_squeeze_idata(pos->sq);
		return RETERR(-E_REPEAT);
	}
	return 0;
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
