/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* This file contains page/cluster index translators and offset modulators
   See http://www.namesys.com/cryptcompress_design.html for details */

#if !defined( __FS_REISER4_CLUSTER_H__ )
#define __FS_REISER4_CLUSTER_H__

static inline loff_t min_count(loff_t a, loff_t b)
{
	return (a < b ? a : b);
}

static inline loff_t max_count(loff_t a, loff_t b)
{
	return (a > b ? a : b);
}

static inline int inode_cluster_shift (struct inode * inode)
{
	assert("edward-92", inode != NULL);
	assert("edward-93", reiser4_inode_data(inode) != NULL);

	return inode_cluster_plugin(inode)->shift;
}

static inline unsigned
cluster_nrpages_shift(struct inode * inode)
{
	return inode_cluster_shift(inode) - PAGE_CACHE_SHIFT;
}

/* cluster size in page units */
static inline unsigned cluster_nrpages (struct inode * inode)
{
	return 1U << cluster_nrpages_shift(inode);
}

static inline size_t inode_cluster_size (struct inode * inode)
{
	assert("edward-96", inode != NULL);

	return 1U << inode_cluster_shift(inode);
}

static inline unsigned long
pg_to_clust(unsigned long idx, struct inode * inode)
{
	return idx >> cluster_nrpages_shift(inode);
}

static inline unsigned long
clust_to_pg(unsigned long idx, struct inode * inode)
{
	return idx << cluster_nrpages_shift(inode);
}

static inline unsigned long
pg_to_clust_to_pg(unsigned long idx, struct inode * inode)
{
	return clust_to_pg(pg_to_clust(idx, inode), inode);
}

static inline unsigned long
off_to_pg(loff_t off)
{
	return (off >> PAGE_CACHE_SHIFT);
}

static inline loff_t
pg_to_off(unsigned long idx)
{
	return ((loff_t)(idx) << PAGE_CACHE_SHIFT);
}

static inline unsigned long
off_to_clust(loff_t off, struct inode * inode)
{
	return off >> inode_cluster_shift(inode);
}

static inline loff_t
clust_to_off(unsigned long idx, struct inode * inode)
{
	return (loff_t)idx << inode_cluster_shift(inode);
}

static inline unsigned long
count_to_nr(loff_t count, unsigned shift)
{
	return (count + (1UL << shift) - 1) >> shift;
}

/* number of pages occupied by @count bytes */
static inline unsigned long
count_to_nrpages(loff_t count)
{
	return count_to_nr(count, PAGE_CACHE_SHIFT);
}

/* number of clusters occupied by @count bytes */
static inline cloff_t
count_to_nrclust(loff_t count, struct inode * inode)
{
	return count_to_nr(count, inode_cluster_shift(inode));
}

/* number of clusters occupied by @count pages */
static inline cloff_t
pgcount_to_nrclust(pgoff_t count, struct inode * inode)
{
	return count_to_nr(count, cluster_nrpages_shift(inode));
}

static inline loff_t
off_to_clust_to_off(loff_t off, struct inode * inode)
{
	return clust_to_off(off_to_clust(off, inode), inode);
}

static inline unsigned long
off_to_clust_to_pg(loff_t off, struct inode * inode)
{
	return clust_to_pg(off_to_clust(off, inode), inode);
}

static inline unsigned
off_to_pgoff(loff_t off)
{
	return off & (PAGE_CACHE_SIZE - 1);
}

static inline unsigned
off_to_cloff(loff_t off, struct inode * inode)
{
	return off & ((loff_t)(inode_cluster_size(inode)) - 1);
}

static inline unsigned
pg_to_off_to_cloff(unsigned long idx, struct inode * inode)
{
	return off_to_cloff(pg_to_off(idx), inode);
}

/* if @size != 0, returns index of the page
   which contains the last byte of the file */
static inline pgoff_t
size_to_pg(loff_t size)
{
	return (size ? off_to_pg(size - 1) : 0);
}

/* minimal index of the page which doesn't contain
   file data */
static inline pgoff_t
size_to_next_pg(loff_t size)
{
	return (size ? off_to_pg(size - 1) + 1 : 0);
}

static inline unsigned
off_to_pgcount(loff_t off, unsigned long idx)
{
	if (idx > off_to_pg(off))
		return 0;
	if (idx < off_to_pg(off))
		return PAGE_CACHE_SIZE;
	return off_to_pgoff(off);
}

static inline unsigned
off_to_count(loff_t off, unsigned long idx, struct inode * inode)
{
	if (idx > off_to_clust(off, inode))
		return 0;
	if (idx < off_to_clust(off, inode))
		return inode_cluster_size(inode);
	return off_to_cloff(off, inode);
}

static inline unsigned
fsize_to_count(reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-288", clust != NULL);
	assert("edward-289", inode != NULL);

	return off_to_count(inode->i_size, clust->index, inode);
}

static inline void
reiser4_slide_init (reiser4_slide_t * win){
	assert("edward-1084", win != NULL);
	memset(win, 0, sizeof *win);
}

static inline void
reiser4_cluster_init (reiser4_cluster_t * clust, reiser4_slide_t * window){
	assert("edward-84", clust != NULL);
	memset(clust, 0, sizeof *clust);
	clust->dstat = INVAL_DISK_CLUSTER;
	clust->win = window;
}

static inline int
dclust_get_extension(hint_t * hint)
{
	return hint->ext_coord.extension.ctail.shift;
}

static inline void
dclust_set_extension(hint_t * hint)
{
	assert("edward-1270", item_id_by_coord(&hint->ext_coord.coord) == CTAIL_ID);
	hint->ext_coord.extension.ctail.shift = cluster_shift_by_coord(&hint->ext_coord.coord);
}

static inline int
hint_is_unprepped_dclust(hint_t * hint)
{
	return dclust_get_extension(hint) == (int)UCTAIL_SHIFT;
}

static inline void
coord_set_between_clusters(coord_t * coord)
{
#if REISER4_DEBUG
	int result;
	result = zload(coord->node);
	assert("edward-1296", !result);
#endif
	if (!coord_is_between_items(coord)) {
		coord->between = AFTER_ITEM;
		coord->unit_pos = 0;
	}
#if REISER4_DEBUG
	zrelse(coord->node);
#endif
}

int inflate_cluster(reiser4_cluster_t *, struct inode *);
int find_cluster(reiser4_cluster_t *, struct inode *, int read, int write);
void forget_cluster_pages(struct page ** page, int nrpages);
int flush_cluster_pages(reiser4_cluster_t *, jnode *, struct inode *);
int deflate_cluster(reiser4_cluster_t *, struct inode *);
void truncate_page_cluster(struct inode * inode, cloff_t start);
void set_hint_cluster(struct inode * inode, hint_t * hint, unsigned long index, znode_lock_mode mode);
void invalidate_hint_cluster(reiser4_cluster_t * clust);
int get_disk_cluster_locked(reiser4_cluster_t * clust, struct inode * inode, znode_lock_mode lock_mode);
void reset_cluster_params(reiser4_cluster_t * clust);
int prepare_page_cluster(struct inode *inode, reiser4_cluster_t *clust, int capture);
void release_cluster_pages_nocapture(reiser4_cluster_t *);
void put_cluster_handle(reiser4_cluster_t * clust, tfm_action act);
int grab_tfm_stream(struct inode * inode, tfm_cluster_t * tc, tfm_action act, tfm_stream_id id);
int tfm_cluster_is_uptodate (tfm_cluster_t * tc);
void tfm_cluster_set_uptodate (tfm_cluster_t * tc);
void tfm_cluster_clr_uptodate (tfm_cluster_t * tc);
unsigned long clust_by_coord(const coord_t * coord, struct inode * inode);

/* move cluster handle to the target position
   specified by the page of index @pgidx
*/
static inline void
move_cluster_forward(reiser4_cluster_t * clust, struct inode * inode,
		     pgoff_t pgidx, int * progress)
{
	assert("edward-1297", clust != NULL);
	assert("edward-1298", inode != NULL);

	reset_cluster_params(clust);
	if (*progress &&
	    /* Hole in the indices. Hint became invalid and can not be
	       used by find_cluster_item() even if seal/node versions
	       will coincide */
	    pg_to_clust(pgidx, inode) != clust->index + 1) {
		unset_hint(clust->hint);
		invalidate_hint_cluster(clust);
	}
	*progress = 1;
	clust->index = pg_to_clust(pgidx, inode);
}

static inline int
alloc_clust_pages(reiser4_cluster_t * clust, struct inode * inode )
{
	assert("edward-791", clust != NULL);
	assert("edward-792", inode != NULL);
	clust->pages = reiser4_kmalloc(sizeof(*clust->pages) << inode_cluster_shift(inode), GFP_KERNEL);
	if (!clust->pages)
		return -ENOMEM;
	return 0;
}

static inline void
free_clust_pages(reiser4_cluster_t * clust)
{
	reiser4_kfree(clust->pages);
}

#endif /* __FS_REISER4_CLUSTER_H__ */


/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
