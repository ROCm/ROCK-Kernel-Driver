/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#if !defined( __FS_REISER4_CTAIL_H__ )
#define __FS_REISER4_CTAIL_H__

/* cryptcompress object item. See ctail.c for description. */
#include "../cryptcompress.h"

#include <linux/pagevec.h>

typedef struct ctail_item_format {
	/* cluster shift */
	d8 cluster_shift;
	/* ctail body */
	d8 body[0];
} __attribute__((packed)) ctail_item_format;

/* for flush squeeze */
typedef struct ctail_squeeze_info {
	struct inode * inode;
	reiser4_cluster_t * clust;
	flow_t flow;
} ctail_squeeze_info_t;

#define CTAIL_MIN_BODY_SIZE MIN_CRYPTO_BLOCKSIZE

#define list_to_page(head) (list_entry((head)->prev, struct page, lru))
#define list_to_next_page(head) (list_entry((head)->prev->prev, struct page, lru))

struct cut_list;

/* plugin->item.b.* */
int can_contain_key_ctail(const coord_t *, const reiser4_key *, const reiser4_item_data *);
int mergeable_ctail(const coord_t * p1, const coord_t * p2);
pos_in_node_t nr_units_ctail(const coord_t * coord);
int estimate_ctail(const coord_t * coord, const reiser4_item_data * data);
void print_ctail(const char *prefix, coord_t * coord);
lookup_result lookup_ctail(const reiser4_key *, lookup_bias, coord_t *);

int paste_ctail(coord_t * coord, reiser4_item_data * data, carry_plugin_info * info UNUSED_ARG);
int init_ctail(coord_t *, coord_t *, reiser4_item_data *);
int can_shift_ctail(unsigned free_space, coord_t * coord,
		  znode * target, shift_direction pend, unsigned *size, unsigned want);
void copy_units_ctail(coord_t * target, coord_t * source,
		    unsigned from, unsigned count, shift_direction where_is_free_space, unsigned free_space);
int cut_units_ctail(coord_t *coord, pos_in_node_t from, pos_in_node_t to,
		    carry_cut_data *, reiser4_key * smallest_removed, reiser4_key *new_first);
int kill_units_ctail(coord_t * coord, pos_in_node_t from, pos_in_node_t to,
		     carry_kill_data *, reiser4_key * smallest_removed, reiser4_key *new_first);

/*int check_check(const coord_t * coord, const char **error);*/

/* plugin->u.item.s.* */
int read_ctail(struct file *, flow_t *, hint_t *);
int readpage_ctail(void *, struct page *);
void readpages_ctail(void *, struct address_space *, struct list_head *);
reiser4_key *append_key_ctail(const coord_t *, reiser4_key *);
int kill_hook_ctail(const coord_t *, pos_in_node_t, pos_in_node_t, carry_kill_data *);
int shift_hook_ctail(const coord_t *, unsigned, unsigned, znode *);

/* plugin->u.item.f */
int utmost_child_ctail(const coord_t *, sideof, jnode **);
int scan_ctail(flush_scan *);
int squeeze_ctail(flush_pos_t *);
item_plugin * item_plugin_by_jnode(jnode *);

crypto_stat_t * inode_crypto_stat(struct inode *);

void reiser4_cluster_init(reiser4_cluster_t *);
void put_cluster_data(reiser4_cluster_t *);
int cluster_is_uptodate (reiser4_cluster_t *);
void release_cluster_buf(reiser4_cluster_t *);
size_t inode_scaled_cluster_size(struct inode *);
loff_t inode_scaled_offset (struct inode *, const loff_t);
unsigned max_crypto_overhead(struct inode *);

int inflate_cluster(reiser4_cluster_t *, struct inode *);
int find_cluster_item(hint_t * hint, const reiser4_key *key,
		      znode_lock_mode lock_mode, ra_info_t *ra_info,
		      lookup_bias bias);
int page_of_cluster(struct page *, reiser4_cluster_t *, struct inode *);
int find_cluster(reiser4_cluster_t *, struct inode *, int read, int write);
int flush_cluster_pages(reiser4_cluster_t *, struct inode *);
int deflate_cluster(tfm_info_t, reiser4_cluster_t *, struct inode *);
void truncate_cluster(struct inode * inode, pgoff_t start, long count);
int hint_prev_cluster(reiser4_cluster_t * clust);
void set_nrpages_by_inode(reiser4_cluster_t * clust, struct inode * inode);
int grab_cluster_pages(struct inode * inode, reiser4_cluster_t * clust);
void release_cluster_pages(reiser4_cluster_t * clust, int from);

#endif /* __FS_REISER4_CTAIL_H__ */

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
