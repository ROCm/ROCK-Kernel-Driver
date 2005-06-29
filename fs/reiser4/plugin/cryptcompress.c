/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README

This file contains all cluster operations and methods of the reiser4
cryptcompress object plugin (see http://www.namesys.com/cryptcompress_design.html
for details).

Cryptcompress specific fields of reiser4 inode/stat-data:

                 Incore inode                               Disk stat-data
********************************************************************************************
* data structure       *         field        * data structure       *          field      *
********************************************************************************************
* plugin_set           *file plugin id        * reiser4_plugin_stat  *file plugin id       *
*                      *crypto plugin id      *                      *crypto plugin id     *
*                      *digest plugin id      *                      *digest plugin id     *
*                      *compression plugin id *                      *compression plugin id*
********************************************************************************************
* crypto_stat_t        *      keysize         * reiser4_crypto_stat  *      keysize        *
*                      *      keyid           *                      *      keyid          *
********************************************************************************************
* cluster_stat_t       *      cluster_shift   * reiser4_cluster_stat *      cluster_shift  *
********************************************************************************************
* cryptcompress_info_t *      crypto_tfm      *                      *                     *
********************************************************************************************
*/
#include "../debug.h"
#include "../inode.h"
#include "../jnode.h"
#include "../tree.h"
#include "../page_cache.h"
#include "../readahead.h"
#include "../forward.h"
#include "../super.h"
#include "../context.h"
#include "../cluster.h"
#include "../seal.h"
#include "../vfs_ops.h"
#include "plugin.h"
#include "object.h"
#include "../tree_walk.h"
#include "file/funcs.h"

#include <asm/scatterlist.h>
#include <linux/writeback.h>
#include <linux/pagemap.h>
#include <linux/crypto.h>
#include <linux/swap.h>
#include <linux/hardirq.h>
#include <linux/pagevec.h>

int do_readpage_ctail(reiser4_cluster_t *, struct page * page);
int ctail_read_cluster (reiser4_cluster_t *, struct inode *, int);
reiser4_key * append_cluster_key_ctail(const coord_t *, reiser4_key *);
int setattr_reserve(reiser4_tree *);
int writepage_ctail(struct page *);
int update_file_size(struct inode * inode, reiser4_key * key, int update_sd);
int cut_file_items(struct inode *inode, loff_t new_size, int update_sd, loff_t cur_size,
		   int (*update_actor)(struct inode *, reiser4_key *, int));
int delete_object(struct inode *inode, int mode);
int ctail_insert_unprepped_cluster(reiser4_cluster_t * clust, struct inode * inode);
int hint_is_set(const hint_t *hint);
reiser4_plugin * get_default_plugin(pset_member memb);
void inode_check_scale_nolock(struct inode * inode, __u64 old, __u64 new);

/* get cryptcompress specific portion of inode */
reiser4_internal cryptcompress_info_t *
cryptcompress_inode_data(const struct inode * inode)
{
	return &reiser4_inode_data(inode)->file_plugin_data.cryptcompress_info;
}

/* plugin->u.file.init_inode_data */
reiser4_internal void
init_inode_data_cryptcompress(struct inode *inode,
			      reiser4_object_create_data *crd, int create)
{
	cryptcompress_info_t * data;

	data = cryptcompress_inode_data(inode);
	assert("edward-685", data != NULL);

	memset(data, 0, sizeof (*data));

	init_rwsem(&data->lock);
	init_inode_ordering(inode, crd, create);
}

#if REISER4_DEBUG
static int
crc_generic_check_ok(void)
{
	return MIN_CRYPTO_BLOCKSIZE == DC_CHECKSUM_SIZE << 1;
}

reiser4_internal int
crc_inode_ok(struct inode * inode)
{
	cryptcompress_info_t * data = cryptcompress_inode_data(inode);

	if (cluster_shift_ok(inode_cluster_shift(inode)) &&
	    (data->tfm[CRYPTO_TFM] == NULL) &&
	    (data->tfm[DIGEST_TFM] == NULL))
		return 1;
	assert("edward-686", 0);
	return 0;
}
#endif

static int
check_cryptcompress(struct inode * inode)
{
	int result = 0;

	assert("edward-1307", inode_compression_plugin(inode) != NULL);

	if (inode_cluster_size(inode) < PAGE_CACHE_SIZE) {
		warning("edward-1331",
			"%s clusters are unsupported",
			inode_cluster_plugin(inode)->h.label);
		return RETERR(-EINVAL);
	}
	if (inode_compression_plugin(inode)->init)
		result = inode_compression_plugin(inode)->init();
	return result;
}

static crypto_stat_t * inode_crypto_stat (struct inode * inode)
{
	assert("edward-90", inode != NULL);
	assert("edward-91", reiser4_inode_data(inode) != NULL);

	return (cryptcompress_inode_data(inode)->crypt);
}

/* NOTE-EDWARD: Do not use crypto without digest */
static int
alloc_crypto_tfm(struct inode * inode, struct inode * parent)
{
	int result;
	crypto_plugin * cplug = inode_crypto_plugin(parent);
	digest_plugin * dplug = inode_digest_plugin(parent);

	assert("edward-414", dplug != NULL);
	assert("edward-415", cplug != NULL);

	result = dplug->alloc(inode);
	if (result)
		return result;
	result = cplug->alloc(inode);
	if (result) {
		dplug->free(inode);
		return result;
	}
	return 0;
}

static void
free_crypto_tfm(struct inode * inode)
{
	reiser4_inode * info;

	assert("edward-410", inode != NULL);

	info = reiser4_inode_data(inode);

	if (!inode_get_crypto(inode))
		return;

	assert("edward-411", inode_crypto_plugin(inode));
	assert("edward-763", inode_digest_plugin(inode));

	inode_crypto_plugin(inode)->free(inode);
	inode_digest_plugin(inode)->free(inode);
}

static int
attach_crypto_stat(struct inode * inode, crypto_data_t * data)
{
	__u8 * txt;

	crypto_stat_t * stat;
	struct scatterlist sg;
	struct crypto_tfm * dtfm;

	assert("edward-690", inode_get_crypto(inode));
	assert("edward-766", inode_get_digest(inode));

	dtfm =  inode_get_digest(inode);

	stat = reiser4_kmalloc(sizeof(*stat), GFP_KERNEL);
	if (!stat)
		return -ENOMEM;

	stat->keyid = reiser4_kmalloc((size_t)crypto_tfm_alg_digestsize(dtfm), GFP_KERNEL);
	if (!stat->keyid) {
		reiser4_kfree(stat);
		return -ENOMEM;
	}
	txt = reiser4_kmalloc(data->keyid_size, GFP_KERNEL);
	if (!txt) {
		reiser4_kfree(stat->keyid);
		reiser4_kfree(stat);
		return -ENOMEM;
	}
	memcpy(txt, data->keyid, data->keyid_size);
	sg.page = virt_to_page (txt);
	sg.offset = offset_in_page (txt);
	sg.length = data->keyid_size;

	crypto_digest_init (dtfm);
	crypto_digest_update (dtfm, &sg, 1);
	crypto_digest_final (dtfm, stat->keyid);

	cryptcompress_inode_data(inode)->crypt = stat;
	reiser4_kfree(txt);

	return 0;
}

static void
detach_crypto_stat(struct inode * object)
{
	crypto_stat_t * stat;

	stat = inode_crypto_stat(object);

	assert("edward-691", crc_inode_ok(object));

	if (!inode_get_crypto(object))
		return;

	assert("edward-412", stat != NULL);

	reiser4_kfree(stat->keyid);
	reiser4_kfree(stat);
}

/*  1) fill crypto specific part of inode
    2) set inode crypto stat which is supposed to be saved in stat-data */
static int
inode_set_crypto(struct inode * object, struct inode * parent,
		 crypto_data_t * data)
{
	int result;
	struct crypto_tfm * tfm;
	crypto_plugin * cplug;
	digest_plugin * dplug;
	reiser4_inode * info = reiser4_inode_data(object);

	cplug = inode_crypto_plugin(parent);
	dplug = inode_digest_plugin(parent);

	plugin_set_crypto(&info->pset, cplug);
	plugin_set_digest(&info->pset, dplug);

	result = alloc_crypto_tfm(object, parent);
	if (!result)
		return result;

	if (!inode_get_crypto(object))
		/* nothing to do anymore */
		return 0;

	assert("edward-414", dplug != NULL);
	assert("edward-415", cplug != NULL);
	assert("edward-417", data->key!= NULL);
	assert("edward-88", data->keyid != NULL);
	assert("edward-83", data->keyid_size != 0);
	assert("edward-89", data->keysize != 0);

	tfm = inode_get_tfm(object, CRYPTO_TFM);
	assert("edward-695", tfm != NULL);

	result = cplug->setkey(tfm, data->key, data->keysize);
	if (result) {
		free_crypto_tfm(object);
		return result;
	}
	assert ("edward-34", !inode_get_flag(object, REISER4_SECRET_KEY_INSTALLED));
	inode_set_flag(object, REISER4_SECRET_KEY_INSTALLED);

	info->extmask |= (1 << CRYPTO_STAT);

	result = attach_crypto_stat(object, data);
	if (result)
		goto error;

	info->plugin_mask |= (1 << PSET_CRYPTO) | (1 << PSET_DIGEST);

	return 0;
 error:
	free_crypto_tfm(object);
	inode_clr_flag(object, REISER4_SECRET_KEY_INSTALLED);
	return result;
}

static int
inode_set_compression(struct inode * object, struct inode * parent)
{
	int result = 0;
	compression_plugin * cplug;
	reiser4_inode * info = reiser4_inode_data(object);

	cplug = inode_compression_plugin(parent);

	if (cplug->init != NULL) {
		result = cplug->init();
		if (result)
			return result;
	}
	plugin_set_compression(&info->pset, cplug);
	info->plugin_mask |= (1 << PSET_COMPRESSION);

	return 0;
}

static void
inode_set_compression_mode(struct inode * object, struct inode * parent)
{
	compression_mode_plugin * mplug;
	reiser4_inode * info = reiser4_inode_data(object);

	mplug = inode_compression_mode_plugin(parent);

	plugin_set_compression_mode(&info->pset, mplug);
	info->plugin_mask |= (1 << PSET_COMPRESSION_MODE);
	return;
}

static int
inode_set_cluster(struct inode * object, struct inode * parent)
{
	reiser4_inode * info;
	cluster_plugin * cplug;

	assert("edward-696", object != NULL);

	info = reiser4_inode_data(object);
	cplug = inode_cluster_plugin(parent);

	if (cplug->shift < PAGE_CACHE_SHIFT) {
		warning("edward-1320",
			"Can not support cluster size %p",
			cplug->h.label);
		return RETERR(-EINVAL);
	}
	plugin_set_cluster(&info->pset, cplug);

	info->plugin_mask |= (1 << PSET_CLUSTER);
	return 0;
}

/* plugin->create() method for crypto-compressed files

. install plugins
. attach crypto info if specified
. attach compression info if specified
. attach cluster info
*/
reiser4_internal int
create_cryptcompress(struct inode *object, struct inode *parent,
		     reiser4_object_create_data * data)
{
	int result;
	reiser4_inode * info;

	assert("edward-23", object != NULL);
	assert("edward-24", parent != NULL);
	assert("edward-30", data != NULL);
	assert("edward-26", inode_get_flag(object, REISER4_NO_SD));
	assert("edward-27", data->id == CRC_FILE_PLUGIN_ID);
	assert("edward-1170", crc_generic_check_ok());

	info = reiser4_inode_data(object);

	assert("edward-29", info != NULL);

	/* set file bit */
	info->plugin_mask |= (1 << PSET_FILE);

	/* set crypto */
	result = inode_set_crypto(object, parent, data->crypto);
	if (result)
		goto error;
	/* set compression */
	result = inode_set_compression(object, parent);
	if (result)
		goto error;
	inode_set_compression_mode(object, parent);

	/* set cluster info */
	result = inode_set_cluster(object, parent);
	if (result)
		goto error;
	/* set plugin mask */
	info->extmask |= (1 << PLUGIN_STAT);

	/* save everything in disk stat-data */
	result = write_sd_by_inode_common(object);
	if (!result)
		return 0;
	/* save() method failed, release attached crypto info */
	inode_clr_flag(object, REISER4_CRYPTO_STAT_LOADED);
 error:
	free_crypto_tfm(object);
	detach_crypto_stat(object);
	inode_clr_flag(object, REISER4_SECRET_KEY_INSTALLED);
	return result;
}

reiser4_internal int open_cryptcompress(struct inode * inode, struct file * file)
{
	/* FIXME-EDWARD: should be powered by key management */
	assert("edward-698", inode_file_plugin(inode) == file_plugin_by_id(CRC_FILE_PLUGIN_ID));
	return 0;
}

/* plugin->destroy_inode() */
reiser4_internal void
destroy_inode_cryptcompress(struct inode * inode)
{
	assert("edward-802", inode_file_plugin(inode) == file_plugin_by_id(CRC_FILE_PLUGIN_ID));
	assert("edward-803", !is_bad_inode(inode) && is_inode_loaded(inode));

	free_crypto_tfm(inode);
	if (inode_get_flag(inode, REISER4_CRYPTO_STAT_LOADED))
		detach_crypto_stat(inode);

	inode_clr_flag(inode, REISER4_CRYPTO_STAT_LOADED);
	inode_clr_flag(inode, REISER4_SECRET_KEY_INSTALLED);
}

/* returns translated offset */
static loff_t inode_scaled_offset (struct inode * inode,
				   const loff_t src_off /* input offset */)
{
	assert("edward-97", inode != NULL);

	if (!inode_get_crypto(inode) || src_off == get_key_offset(max_key()))
		return src_off;

	return inode_crypto_plugin(inode)->scale(inode, crypto_blocksize(inode), src_off);
}

/* returns disk cluster size */
reiser4_internal size_t
inode_scaled_cluster_size (struct inode * inode)
{
	assert("edward-110", inode != NULL);

	return inode_scaled_offset(inode, inode_cluster_size(inode));
}

static int
new_cluster(reiser4_cluster_t * clust, struct inode * inode)
{
	return (clust_to_off(clust->index, inode) >= inode->i_size);
}

/* set number of cluster pages */
static void
set_cluster_nrpages(reiser4_cluster_t * clust, struct inode * inode)
{
	reiser4_slide_t * win;

	assert("edward-180", clust != NULL);
	assert("edward-1040", inode != NULL);

	win = clust->win;
	if (!win) {
		/* FIXME-EDWARD: i_size should be protected */
		clust->nr_pages = count_to_nrpages(fsize_to_count(clust, inode));
		return;
	}
	assert("edward-1176", clust->op != PCL_UNKNOWN);
	assert("edward-1064", win->off + win->count + win->delta != 0);

	if (win->stat == HOLE_WINDOW &&
	    win->off == 0 &&
	    win->count == inode_cluster_size(inode)) {
		/* special case: we start write hole from fake cluster */
		clust->nr_pages = 0;
		return;
	}
	clust->nr_pages =
		count_to_nrpages(max_count(win->off + win->count + win->delta,
					   fsize_to_count(clust, inode)));
	return;
}

/* plugin->key_by_inode() */
/* see plugin/plugin.h for details */
reiser4_internal int
key_by_inode_cryptcompress(struct inode *inode, loff_t off, reiser4_key * key)
{
	loff_t clust_off;

	assert("edward-64", inode != 0);
	//	assert("edward-112", ergo(off != get_key_offset(max_key()), !off_to_cloff(off, inode)));
	/* don't come here with other offsets */

	clust_off = (off == get_key_offset(max_key()) ? get_key_offset(max_key()) : off_to_clust_to_off(off, inode));

	key_by_inode_and_offset_common(inode, 0, key);
	set_key_offset(key, (__u64) (!inode_crypto_stat(inode) ? clust_off : inode_scaled_offset(inode, clust_off)));
	return 0;
}

/* plugin->flow_by_inode */
reiser4_internal int
flow_by_inode_cryptcompress(struct inode *inode /* file to build flow for */ ,
			    char *buf /* user level buffer */ ,
			    int user	/* 1 if @buf is of user space, 0 - if it is
					   kernel space */ ,
			    loff_t size /* buffer size */ ,
			    loff_t off /* offset to start io from */ ,
			    rw_op op /* READ or WRITE */ ,
			    flow_t * f /* resulting flow */)
{
	assert("edward-436", f != NULL);
	assert("edward-149", inode != NULL);
	assert("edward-150", inode_file_plugin(inode) != NULL);
	assert("edward-151", inode_file_plugin(inode)->key_by_inode == key_by_inode_cryptcompress);


	f->length = size;
	f->data = buf;
	f->user = user;
	f->op = op;

	if (op == WRITE_OP && user == 1)
		return 0;
	return key_by_inode_cryptcompress(inode, off, &f->key);
}

static int
crc_hint_validate(hint_t *hint, const reiser4_key *key, znode_lock_mode lock_mode)
{
	coord_t * coord;

	assert("edward-704", hint != NULL);
	assert("edward-1089", !hint->ext_coord.valid);
	assert("edward-706", hint->ext_coord.lh->owner == NULL);

	coord = &hint->ext_coord.coord;

	if (!hint || !hint_is_set(hint) || hint->mode != lock_mode)
		/* hint either not set or set by different operation */
		return RETERR(-E_REPEAT);

	if (get_key_offset(key) != hint->offset)
		/* hint is set for different key */
		return RETERR(-E_REPEAT);

	assert("edward-707", schedulable());

	return seal_validate(&hint->seal, &hint->ext_coord.coord,
			     key, hint->ext_coord.lh,
			     lock_mode,
			     ZNODE_LOCK_LOPRI);
}

static int
__reserve4cluster(struct inode * inode, reiser4_cluster_t * clust)
{
	int result = 0;

	assert("edward-965", schedulable());
	assert("edward-439", inode != NULL);
	assert("edward-440", clust != NULL);
	assert("edward-441", clust->pages != NULL);
	assert("edward-1261", get_current_context()->grabbed_blocks == 0);

	if (clust->nr_pages == 0) {
		assert("edward-1152", clust->win != NULL);
		assert("edward-1153", clust->win->stat == HOLE_WINDOW);
		/* don't reserve space for fake disk clusteer */
		return 0;
	}
	assert("edward-442", jprivate(clust->pages[0]) != NULL);

	result = reiser4_grab_space_force(/* for prepped disk cluster */
					  estimate_insert_cluster(inode, 0) +
					  /* for unprepped disk cluster */
					  estimate_insert_cluster(inode, 1),
					  BA_CAN_COMMIT);
	if (result)
		return result;
	clust->reserved = 1;
	grabbed2cluster_reserved(estimate_insert_cluster(inode, 0) +
				 estimate_insert_cluster(inode, 1));
#if REISER4_DEBUG
	clust->reserved_prepped = estimate_insert_cluster(inode, 0);
	clust->reserved_unprepped = estimate_insert_cluster(inode, 1);
#endif
	assert("edward-1262", get_current_context()->grabbed_blocks == 0);
	return 0;
}

#if REISER4_TRACE
#define reserve4cluster(inode, clust, msg)    __reserve4cluster(inode, clust)
#else
#define reserve4cluster(inode, clust, msg)    __reserve4cluster(inode, clust)
#endif

static void
free_reserved4cluster(struct inode * inode, reiser4_cluster_t * clust, int count)
{
	assert("edward-967", clust->reserved == 1);

	cluster_reserved2free(count);
	clust->reserved = 0;
}
#if REISER4_DEBUG
static int
eq_to_ldk(znode *node, const reiser4_key *key)
{
	return UNDER_RW(dk, current_tree, read, keyeq(key, znode_get_ld_key(node)));
}
#endif

/* The core search procedure.
   If returned value is not cbk_errored, current znode is locked */
static int
find_cluster_item(hint_t * hint,
		  const reiser4_key *key,   /* key of the item we are
					       looking for */
		  znode_lock_mode lock_mode /* which lock */,
		  ra_info_t *ra_info,
		  lookup_bias bias,
		  __u32 flags)
{
	int result;
	reiser4_key ikey;
	coord_t * coord = &hint->ext_coord.coord;
	coord_t orig = *coord;

	assert("edward-152", hint != NULL);

	if (hint->ext_coord.valid == 0) {
		result = crc_hint_validate(hint, key, lock_mode);
		if (result == -E_REPEAT)
			goto traverse_tree;
		else if (result) {
			assert("edward-1216", 0);
			return result;
		}
		hint->ext_coord.valid = 1;
	}
	assert("edward-709", znode_is_any_locked(coord->node));

	/* In-place lookup is going here, it means we just need to
	   check if next item of the @coord match to the @keyhint) */

	if (equal_to_rdk(coord->node, key)) {
		result = goto_right_neighbor(coord, hint->ext_coord.lh);
		if (result == -E_NO_NEIGHBOR) {
			assert("edward-1217", 0);
			return RETERR(-EIO);
		}
		if (result)
			return result;
		assert("edward-1218", eq_to_ldk(coord->node, key));
	}
	else {
		coord->item_pos++;
		coord->unit_pos = 0;
		coord->between = AT_UNIT;
	}
	result = zload(coord->node);
	if (result)
		return result;
	assert("edward-1219", !node_is_empty(coord->node));

	if (!coord_is_existing_item(coord)) {
		zrelse(coord->node);
		goto not_found;
	}
	item_key_by_coord(coord, &ikey);
	zrelse(coord->node);
	if (!keyeq(key, &ikey))
		goto not_found;
	return CBK_COORD_FOUND;

 not_found:
	assert("edward-1220", coord->item_pos > 0);
	//coord->item_pos--;
	/* roll back */
	*coord = orig;
	ON_DEBUG(coord_update_v(coord));
	return CBK_COORD_NOTFOUND;

 traverse_tree:
	assert("edward-713", hint->ext_coord.lh->owner == NULL);
	assert("edward-714", schedulable());

	unset_hint(hint);
	coord_init_zero(coord);
	result = coord_by_key(current_tree, key, coord, hint->ext_coord.lh,
			      lock_mode, bias, LEAF_LEVEL, LEAF_LEVEL,
			      CBK_UNIQUE | flags, ra_info);
	if (cbk_errored(result))
		return result;
	hint->ext_coord.valid = 1;
	return result;
}

/* FIXME-EDWARD */
#if 0
/* This represent reiser4 crypto alignment policy.
   Returns the size > 0 of aligning overhead, if we should align/cut,
   returns 0, if we shouldn't (alignment assumes appending an overhead of the size > 0) */
static int
crypto_overhead(size_t len /* advised length */,
		reiser4_cluster_t * clust,
		struct inode * inode, rw_op rw)
{
	size_t size = 0;
	int result = 0;
	int oh;

	assert("edward-486", clust != 0);

	if (!inode_get_crypto(inode) || !inode_crypto_plugin(inode)->align_stream)
		return 0;
	if (!len)
		size = clust->len;

	assert("edward-615", size != 0);
	assert("edward-489", crypto_blocksize(inode) != 0);

	switch (rw) {
	case WRITE_OP: /* align */
		assert("edward-488", size <= inode_cluster_size(inode));

		oh = size % crypto_blocksize(inode);

		if (!oh && size == fsize_to_count(clust, inode))
			/* cluster don't need alignment and didn't get compressed */
			return 0;
		result = (crypto_blocksize(inode) - oh);
		break;
	case READ_OP: /* cut */
		assert("edward-490", size <= inode_scaled_cluster_size(inode));
		if (size >= inode_scaled_offset(inode, fsize_to_count(clust, inode)))
			/* cluster didn't get aligned */
			return 0;
		assert("edward-491", tfm_stream_data(clust, OUTPUT_STREAM) != NULL);
		assert("edward-900", 0);
		/* FIXME-EDWARD: the stuff above */
		result = *(tfm_stream_data(clust, OUTPUT_STREAM) + size - 1);
		break;
	default:
		impossible("edward-493", "bad option for getting alignment");
	}
	return result;
}
#endif

/* the following two functions are to evaluate results
   of compression transform */
static unsigned
max_crypto_overhead(struct inode * inode)
{
	if (!inode_get_crypto(inode) || !inode_crypto_plugin(inode)->align_stream)
		return 0;
	return crypto_blocksize(inode);
}

static int
deflate_overhead(struct inode * inode)
{
	return (inode_compression_plugin(inode)->checksum ? DC_CHECKSUM_SIZE : 0);
}

/* to estimate size of allocating transform stream */
static unsigned
deflate_overrun(struct inode * inode, int in_len)
{
	return (inode_compression_plugin(inode)->overrun != NULL ?
		inode_compression_plugin(inode)->overrun(in_len) :
		0);
}

/* The following two functions represent reiser4 compression policy */
static int
try_compress(tfm_cluster_t * tc, cloff_t index, struct inode * inode)
{
	compression_plugin * cplug = inode_compression_plugin(inode);
	compression_mode_plugin * mplug = inode_compression_mode_plugin(inode);

	assert("edward-1321", tc->len != 0);
	assert("edward-1322", cplug != NULL);
	assert("edward-1323", mplug != NULL);

	return (cplug->compress != NULL) &&
		(mplug->should_deflate != NULL ? mplug->should_deflate(index) : 1) &&
		(cplug->min_size_deflate != NULL ?  tc->len >= cplug->min_size_deflate() : 1);
}

static int
try_encrypt(struct inode * inode)
{
	return inode_get_crypto(inode) != NULL;
}

/* Evaluation results of compression transform. */
static int
save_compressed(int old_size, int new_size, struct inode * inode)
{
	return (new_size + deflate_overhead(inode) + max_crypto_overhead(inode) < old_size);
}

/* Guess result of the evaluation above */
static int
need_inflate(reiser4_cluster_t * clust, struct inode * inode,
	     int encrypted /* is cluster encrypted */)
{
	tfm_cluster_t * tc = &clust->tc;

	assert("edward-142", tc != 0);
	assert("edward-143", inode != NULL);

	return tc->len <
		(encrypted ?
		 inode_scaled_offset(inode, fsize_to_count(clust, inode)) :
		 fsize_to_count(clust, inode));
}

/* append checksum at the end of input transform stream
   and increase its length */
static void
dc_set_checksum(compression_plugin * cplug, tfm_cluster_t * tc)
{
	__u32 checksum;

	assert("edward-1309", tc != NULL);
	assert("edward-1310", tc->len > 0);
	assert("edward-1311", cplug->checksum != NULL);

	checksum = cplug->checksum(tfm_stream_data(tc, OUTPUT_STREAM), tc->len);
	cputod32(checksum, (d32 *)(tfm_stream_data(tc, OUTPUT_STREAM) + tc->len));
	tc->len += (int)DC_CHECKSUM_SIZE;
}

/* returns 0 if checksums coincide, otherwise returns 1,
   increase the length of input transform stream */
static int
dc_check_checksum(compression_plugin * cplug, tfm_cluster_t * tc)
{
	assert("edward-1312", tc != NULL);
	assert("edward-1313", tc->len > (int)DC_CHECKSUM_SIZE);
	assert("edward-1314", cplug->checksum != NULL);

	if (cplug->checksum(tfm_stream_data(tc, INPUT_STREAM),  tc->len - (int)DC_CHECKSUM_SIZE) !=
	    d32tocpu((d32 *)(tfm_stream_data(tc, INPUT_STREAM) + tc->len - (int)DC_CHECKSUM_SIZE))) {
		warning("edward-156", "bad disk cluster checksum %d, (should be %d)\n",
			(int)d32tocpu((d32 *)(tfm_stream_data(tc, INPUT_STREAM) + tc->len - (int)DC_CHECKSUM_SIZE)),
			(int)cplug->checksum(tfm_stream_data(tc, INPUT_STREAM),  tc->len - (int)DC_CHECKSUM_SIZE));
		return 1;
	}
	tc->len -= (int)DC_CHECKSUM_SIZE;
	return 0;
}

reiser4_internal int
grab_tfm_stream(struct inode * inode, tfm_cluster_t * tc,
		tfm_action act, tfm_stream_id id)
{
	size_t size = inode_scaled_cluster_size(inode);

	assert("edward-901", tc != NULL);
	assert("edward-1027", inode_compression_plugin(inode) != NULL);

	if (act == TFM_WRITE)
		size += deflate_overrun(inode, inode_cluster_size(inode));

	if (!tfm_stream(tc, id) && id == INPUT_STREAM)
		alternate_streams(tc);
	if (!tfm_stream(tc, id))
		return alloc_tfm_stream(tc, size, id);

	assert("edward-902", tfm_stream_is_set(tc, id));

	if (tfm_stream_size(tc, id) < size)
		return realloc_tfm_stream(tc, size, id);
	return 0;
}

/* Common deflate cluster manager */
reiser4_internal int
deflate_cluster(reiser4_cluster_t * clust, struct inode * inode)
{
	int result = 0;
	int transformed = 0;
	tfm_cluster_t * tc = &clust->tc;

	assert("edward-401", inode != NULL);
	assert("edward-903", tfm_stream_is_set(tc, INPUT_STREAM));
	assert("edward-498", !tfm_cluster_is_uptodate(tc));

	if (try_compress(tc, clust->index, inode)) {
		/* try to compress, discard bad results */
		__u32 dst_len;
		compression_plugin * cplug = inode_compression_plugin(inode);
		compression_mode_plugin * mplug =
			inode_compression_mode_plugin(inode);
		assert("edward-602", cplug != NULL);

		result = grab_tfm_stream(inode, tc, TFM_WRITE, OUTPUT_STREAM);
		if (result)
			return result;
		dst_len = tfm_stream_size(tc, OUTPUT_STREAM);
		cplug->compress(get_coa(tc, cplug->h.id),
				tfm_stream_data(tc, INPUT_STREAM), tc->len,
				tfm_stream_data(tc, OUTPUT_STREAM), &dst_len);

		/* make sure we didn't overwrite extra bytes */
		assert("edward-603", dst_len <= tfm_stream_size(tc, OUTPUT_STREAM));

		/* evaluate results of compression transform */
		if (save_compressed(tc->len, dst_len, inode)) {
			/* good result, accept */
			tc->len = dst_len;
			if (cplug->checksum != NULL)
				dc_set_checksum(cplug, tc);
			transformed = 1;
			if (mplug->save_deflate != NULL)
				mplug->save_deflate(inode);
		}
		else {
			/* bad result, discard */
#if REISER4_DEBUG
			warning("edward-1309",
				"incompressible data: inode %llu, cluster %lu",
				(unsigned long long)get_inode_oid(inode), clust->index);
#endif
			if (mplug->discard_deflate != NULL) {
				result = mplug->discard_deflate(inode, clust->index);
				if (result)
					return result;
			}
		}
	}
	if (try_encrypt(inode)) {
		crypto_plugin * cplug;
		/* FIXME-EDWARD */
		assert("edward-904", 0);

		cplug = inode_crypto_plugin(inode);
		if (transformed)
			alternate_streams(tc);
		result = grab_tfm_stream(inode, tc, TFM_WRITE, OUTPUT_STREAM);
		if (result)
			return result;
		/* FIXME: set src_len, dst_len, encrypt */
		transformed = 1;
	}
	if (!transformed)
		alternate_streams(tc);
	return result;
}

/* Common inflate cluster manager.
   Is used in readpage() or readpages() methods of
   cryptcompress object plugins. */
reiser4_internal int
inflate_cluster(reiser4_cluster_t * clust, struct inode * inode)
{
	int result = 0;
	int transformed = 0;

	tfm_cluster_t * tc = &clust->tc;

	assert("edward-905", inode != NULL);
	assert("edward-1178", clust->dstat == PREP_DISK_CLUSTER);
	assert("edward-906", tfm_stream_is_set(&clust->tc, INPUT_STREAM));
	assert("edward-907", !tfm_cluster_is_uptodate(tc));

	if (inode_get_crypto(inode) != NULL) {
		crypto_plugin * cplug;

		/* FIXME-EDWARD: isn't supported yet */
		assert("edward-908", 0);
		cplug = inode_crypto_plugin(inode);
		assert("edward-617", cplug != NULL);

		result = grab_tfm_stream(inode, tc, TFM_READ, OUTPUT_STREAM);
		if (result)
			return result;
		assert("edward-909", tfm_cluster_is_set(tc));

		/* set src_len, dst_len and decrypt */
		/* tc->len = dst_len */

		transformed = 1;
	}
	if (need_inflate(clust, inode, 0)) {
		unsigned dst_len = inode_cluster_size(inode);
		compression_plugin * cplug = inode_compression_plugin(inode);

		if(transformed)
			alternate_streams(tc);

		result = grab_tfm_stream(inode, tc, TFM_READ, OUTPUT_STREAM);
		if (result)
			return result;
		assert("edward-1305", cplug->decompress != NULL);
		assert("edward-910", tfm_cluster_is_set(tc));

		/* Check compression checksum for possible IO errors.

		   End-of-cluster format created before encryption:

		   data
		   checksum           (4)   Indicates presence of compression
		                            infrastructure, should be private.
		                            Can be absent.
		   crypto_overhead          Created by ->align() method of crypto-plugin,
		                            Can be absent.

		   Crypto overhead format:

		   data
		   tail_size           (1)   size of aligning tail,
		                             1 <= tail_size <= blksize
		*/
		if (cplug->checksum != NULL) {
			result = dc_check_checksum(cplug, tc);
			if (result)
				return RETERR(-EIO);
		}
		/* decompress cluster */
		cplug->decompress(get_coa(tc, cplug->h.id),
				  tfm_stream_data(tc, INPUT_STREAM), tc->len,
				  tfm_stream_data(tc, OUTPUT_STREAM), &dst_len);

		/* check length */
		tc->len = dst_len;
		assert("edward-157", dst_len == fsize_to_count(clust, inode));
		transformed = 1;
	}
	if (!transformed)
		alternate_streams(tc);
	return result;
}

/* plugin->read() :
 * generic_file_read()
 * All key offsets don't make sense in traditional unix semantics unless they
 * represent the beginning of clusters, so the only thing we can do is start
 * right from mapping to the address space (this is precisely what filemap
 * generic method does) */
/* plugin->readpage() */
reiser4_internal int
readpage_cryptcompress(void *vp, struct page *page)
{
	reiser4_cluster_t clust;
	struct file * file;
	item_plugin * iplug;
	int result;

	assert("edward-88", PageLocked(page));
	assert("edward-89", page->mapping && page->mapping->host);

	result = check_cryptcompress(page->mapping->host);
	if (result)
		return result;
	file = vp;
	if (file)
		assert("edward-113", page->mapping == file->f_dentry->d_inode->i_mapping);

	if (PageUptodate(page)) {
		printk("readpage_cryptcompress: page became already uptodate\n");
		unlock_page(page);
		return 0;
	}
	reiser4_cluster_init(&clust, 0);
	clust.file = file;
	iplug = item_plugin_by_id(CTAIL_ID);
	if (!iplug->s.file.readpage) {
		put_cluster_handle(&clust, TFM_READ);
		return -EINVAL;
	}
	result = iplug->s.file.readpage(&clust, page);

	assert("edward-64", ergo(result == 0, (PageLocked(page) || PageUptodate(page))));
	/* if page has jnode - that jnode is mapped
	   assert("edward-65", ergo(result == 0 && PagePrivate(page),
	   jnode_mapped(jprivate(page))));
	*/
	put_cluster_handle(&clust, TFM_READ);
	return result;
}

/* plugin->readpages() */
reiser4_internal void
readpages_cryptcompress(struct file *file, struct address_space *mapping,
			struct list_head *pages)
{
	file_plugin *fplug;
	item_plugin *iplug;

	assert("edward-1112", mapping != NULL);
	assert("edward-1113", mapping->host != NULL);

	if (check_cryptcompress(mapping->host))
		return;
	fplug = inode_file_plugin(mapping->host);

	assert("edward-1114", fplug == file_plugin_by_id(CRC_FILE_PLUGIN_ID));

	iplug = item_plugin_by_id(CTAIL_ID);

	iplug->s.file.readpages(file, mapping, pages);

	return;
}

/* how much pages will be captured */
static int
cluster_nrpages_to_capture(reiser4_cluster_t * clust)
{
	switch (clust->op) {
	case PCL_APPEND:
		return clust->nr_pages;
	case PCL_TRUNCATE:
		assert("edward-1179", clust->win != NULL);
		return count_to_nrpages(clust->win->off + clust->win->count);
	default:
		impossible("edward-1180","bad page cluster option");
		return 0;
	}
}

static void
set_cluster_pages_dirty(reiser4_cluster_t * clust)
{
	int i;
	struct page * pg;
	int nrpages = cluster_nrpages_to_capture(clust);

	for (i=0; i < nrpages; i++) {

		pg = clust->pages[i];

		assert("edward-968", pg != NULL);

		lock_page(pg);

		assert("edward-1065", PageUptodate(pg));

		set_page_dirty_internal(pg, 0);

		if (!PageReferenced(pg))
			SetPageReferenced(pg);
		mark_page_accessed(pg);

		unlock_page(pg);
	}
}

static void
clear_cluster_pages_dirty(reiser4_cluster_t * clust)
{
	int i;
	assert("edward-1275", clust != NULL);

	for (i = 0; i < clust->nr_pages; i++) {
		assert("edward-1276", clust->pages[i] != NULL);

		lock_page(clust->pages[i]);
		if (!PageDirty(clust->pages[i])) {
			warning("edward-985", "Page of index %lu (inode %llu)"
				" is not dirty\n", clust->pages[i]->index,
				(unsigned long long)get_inode_oid(clust->pages[i]->mapping->host));
		}
		else {
			assert("edward-1277", PageUptodate(clust->pages[i]));
			reiser4_clear_page_dirty(clust->pages[i]);
		}
		unlock_page(clust->pages[i]);
	}
}

/* update i_size by window */
static void
inode_set_new_size(reiser4_cluster_t * clust, struct inode * inode)
{
	loff_t size;
	reiser4_slide_t * win;

	assert("edward-1181", clust != NULL);
	assert("edward-1182", inode != NULL);

	win = clust->win;
	assert("edward-1183", win != NULL);

	size = clust_to_off(clust->index, inode) + win->off;

	switch (clust->op) {
	case PCL_APPEND:
		if (size + win->count <= inode->i_size)
			/* overwrite only */
			return;
		size += win->count;
		break;
	case PCL_TRUNCATE:
		break;
	default:
		impossible("edward-1184", "bad page cluster option");
		break;
	}
	inode_check_scale_nolock(inode, inode->i_size, size);
	inode->i_size = size;
	return;
}

/* . reserve space for a disk cluster if its jnode is not dirty;
   . update set of pages referenced by this jnode
   . update jnode's counter of referenced pages (excluding first one)
*/
static void
make_cluster_jnode_dirty_locked(reiser4_cluster_t * clust, jnode * node,
				loff_t * old_isize, struct inode * inode)
{
	int i;
	int old_refcnt;
	int new_refcnt;

	assert("edward-221", node != NULL);
	assert("edward-971", clust->reserved == 1);
	assert("edward-1028", spin_jnode_is_locked(node));
	assert("edward-972", node->page_count < cluster_nrpages(inode));
	assert("edward-1263", clust->reserved_prepped ==  estimate_insert_cluster(inode, 0));
	assert("edward-1264", clust->reserved_unprepped == 0);


	if (jnode_is_dirty(node)) {
		/* there are >= 1 pages already referenced by this jnode */
		assert("edward-973", count_to_nrpages(off_to_count(*old_isize, clust->index, inode)));
		old_refcnt = count_to_nrpages(off_to_count(*old_isize, clust->index, inode)) - 1;
		/* space for the disk cluster is already reserved */

		free_reserved4cluster(inode, clust, estimate_insert_cluster(inode, 0));
	}
	else {
		/* there is only one page referenced by this jnode */
		assert("edward-1043", node->page_count == 0);
		old_refcnt = 0;
		jnode_make_dirty_locked(node);
		clust->reserved = 0;
	}
#if REISER4_DEBUG
	clust->reserved_prepped -=  estimate_insert_cluster(inode, 0);
#endif
	new_refcnt = cluster_nrpages_to_capture(clust) - 1;

	/* get rid of duplicated references */
	for (i = 0; i <= old_refcnt; i++) {
		assert("edward-975", clust->pages[i]);
		assert("edward-976", old_refcnt < inode_cluster_size(inode));
		assert("edward-1185", PageUptodate(clust->pages[i]));

		page_cache_release(clust->pages[i]);
	}
	/* truncate old references */
	if (new_refcnt < old_refcnt) {
		assert("edward-1186", clust->op == PCL_TRUNCATE);
		for (i = new_refcnt + 1; i <= old_refcnt; i++) {
			assert("edward-1187", clust->pages[i]);
			assert("edward-1188", PageUptodate(clust->pages[i]));

			page_cache_release(clust->pages[i]);
		}
	}
#if REISER4_DEBUG
	node->page_count = new_refcnt;
#endif
	return;
}

/* This is the interface to capture page cluster.
   All the cluster pages contain dependent modifications
   and should be committed at the same time */
static int
try_capture_cluster(reiser4_cluster_t * clust, struct inode * inode)
{
	int result = 0;
	loff_t old_size = inode->i_size;
	jnode * node;

	assert("edward-1029", clust != NULL);
	assert("edward-1030", clust->reserved == 1);
	assert("edward-1031", clust->nr_pages != 0);
	assert("edward-1032", clust->pages != NULL);
	assert("edward-1033", clust->pages[0] != NULL);

	node = jprivate(clust->pages[0]);

	assert("edward-1035", node != NULL);

	LOCK_JNODE(node);
	if (clust->win)
		inode_set_new_size(clust, inode);

	result = try_capture(node, ZNODE_WRITE_LOCK, 0, 0);
	if (result)
		goto exit;
	make_cluster_jnode_dirty_locked(clust, node, &old_size, inode);
 exit:
	assert("edward-1034", !result);
	UNLOCK_JNODE(node);
	jput(node);
	return result;
}

/* Collect unlocked cluster pages and jnode */
static int
grab_cluster_pages_jnode(struct inode * inode, reiser4_cluster_t * clust)
{
	int i;
	int result = 0;
	jnode * node = NULL;

	assert("edward-182", clust != NULL);
	assert("edward-183", clust->pages != NULL);
	assert("edward-184", clust->nr_pages <= cluster_nrpages(inode));

	if (clust->nr_pages == 0)
		return 0;

	for (i = 0; i < clust->nr_pages; i++) {

		assert("edward-1044", clust->pages[i] == NULL);

		clust->pages[i] = grab_cache_page(inode->i_mapping, clust_to_pg(clust->index, inode) + i);
		if (!clust->pages[i]) {
			result = RETERR(-ENOMEM);
			break;
		}
		if (i == 0) {
			node = jnode_of_page(clust->pages[i]);
			unlock_page(clust->pages[i]);
			if (IS_ERR(node)) {
				result = PTR_ERR(node);
				break;
			}
			assert("edward-919", node);
			continue;
		}
		unlock_page(clust->pages[i]);
	}
	if (result) {
		while(i)
			page_cache_release(clust->pages[--i]);
		if (node && !IS_ERR(node))
			jput(node);
		return result;
	}
	assert("edward-920", jprivate(clust->pages[0]));
	LOCK_JNODE(node);
	JF_SET(node, JNODE_CLUSTER_PAGE);
	UNLOCK_JNODE(node);
	return 0;
}

/* collect unlocked cluster pages */
static int
grab_cluster_pages(struct inode * inode, reiser4_cluster_t * clust)
{
	int i;
	int result = 0;

	assert("edward-787", clust != NULL);
	assert("edward-788", clust->pages != NULL);
	assert("edward-789", clust->nr_pages != 0);
	assert("edward-790", clust->nr_pages <= cluster_nrpages(inode));

	for (i = 0; i < clust->nr_pages; i++) {
		clust->pages[i] = grab_cache_page(inode->i_mapping, clust_to_pg(clust->index, inode) + i);
		if (!clust->pages[i]) {
			result = RETERR(-ENOMEM);
			break;
		}
		unlock_page(clust->pages[i]);
	}
	if (result)
		while(i)
			page_cache_release(clust->pages[--i]);
	return result;
}

/* put cluster pages */
static void
release_cluster_pages(reiser4_cluster_t * clust, int from)
{
	int i;

	assert("edward-447", clust != NULL);
	assert("edward-448", from <= clust->nr_pages);

	for (i = from; i < clust->nr_pages; i++) {

		assert("edward-449", clust->pages[i] != NULL);

		page_cache_release(clust->pages[i]);
	}
}

static void
release_cluster_pages_capture(reiser4_cluster_t * clust)
{
	assert("edward-1278", clust != NULL);
	assert("edward-1279", clust->nr_pages != 0);

	return release_cluster_pages(clust, 1);
}

reiser4_internal void
release_cluster_pages_nocapture(reiser4_cluster_t * clust)
{
	return release_cluster_pages(clust, 0);
}

static void
release_cluster_pages_and_jnode(reiser4_cluster_t * clust)
{
	jnode * node;

	assert("edward-445", clust != NULL);
	assert("edward-922", clust->pages != NULL);
	assert("edward-446", clust->pages[0] != NULL);

	node = jprivate(clust->pages[0]);

	assert("edward-447", node != NULL);

	release_cluster_pages(clust, 0);

	jput(node);
}

#if REISER4_DEBUG
static int
window_ok(reiser4_slide_t * win, struct inode * inode)
{
	assert ("edward-1115", win != NULL);
	assert ("edward-1116", ergo(win->delta, win->stat == HOLE_WINDOW));

	return (win->off != inode_cluster_size(inode)) &&
		(win->off + win->count + win->delta <= inode_cluster_size(inode));
}

static int
cluster_ok(reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-279", clust != NULL);

	if (!clust->pages)
		return 0;
	return (clust->win ? window_ok(clust->win, inode) : 1);
}
#endif

/* guess next window stat */
static inline window_stat
next_window_stat(reiser4_slide_t * win)
{
	assert ("edward-1130", win != NULL);
	return ((win->stat == HOLE_WINDOW && win->delta == 0) ?
		HOLE_WINDOW : DATA_WINDOW);
}

/* guess next cluster index and window params */
static void
update_cluster(struct inode * inode, reiser4_cluster_t * clust, loff_t file_off, loff_t to_file)
{
	reiser4_slide_t * win;

	assert ("edward-185", clust != NULL);
	assert ("edward-438", clust->pages != NULL);
	assert ("edward-281", cluster_ok(clust, inode));

	win = clust->win;
	if (!win)
		return;

	switch (win->stat) {
	case DATA_WINDOW:
		/* increment window position */
		clust->index++;
		win->stat = DATA_WINDOW;
		win->off = 0;
		win->count = min_count(inode_cluster_size(inode), to_file);
		break;
	case HOLE_WINDOW:
		switch(next_window_stat(win)) {
		case HOLE_WINDOW:
			/* set window to fit the offset we start write from */
			clust->index = off_to_clust(file_off, inode);
			win->stat = HOLE_WINDOW;
			win->off = 0;
			win->count = off_to_cloff(file_off, inode);
			win->delta = min_count(inode_cluster_size(inode) - win->count, to_file);
			break;
		case DATA_WINDOW:
			/* do not move the window, just change its state,
			   off+count+delta=inv */
			win->stat = DATA_WINDOW;
			win->off = win->off + win->count;
			win->count = win->delta;
			win->delta = 0;
			break;
		default:
			impossible ("edward-282", "wrong next window state");
		}
		break;
	default:
		impossible ("edward-283", "wrong current window state");
	}
	assert ("edward-1068", cluster_ok(clust, inode));
}

static int
update_sd_cryptcompress(struct inode *inode)
{
	int result = 0;

	assert("edward-978", schedulable());
	assert("edward-1265", get_current_context()->grabbed_blocks == 0);

	result = reiser4_grab_space_force(/* one for stat data update */
					  estimate_update_common(inode),
					  BA_CAN_COMMIT);
	assert("edward-979", !result);
	if (result)
		return result;
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
	result = reiser4_update_sd(inode);

	all_grabbed2free();
	return result;
}

static void
uncapture_cluster_jnode(jnode *node)
{
	txn_atom *atom;

	assert("edward-1023", spin_jnode_is_locked(node));

	/*jnode_make_clean(node);*/
	atom = jnode_get_atom(node);
	if (atom == NULL) {
		assert("jmacd-7111", !jnode_is_dirty(node));
		UNLOCK_JNODE (node);
		return;
	}

	uncapture_block(node);
	UNLOCK_ATOM(atom);
	jput(node);
}

reiser4_internal void
forget_cluster_pages(struct page ** pages, int nr)
{
	int i;
	for (i = 0; i < nr; i++) {

		assert("edward-1045", pages[i] != NULL);
		page_cache_release(pages[i]);
	}
}

/* Prepare input stream for transform operations.
   Try to do it in one step. Return -E_REPEAT when it is
   impossible because of races with concurrent processes.
*/
reiser4_internal int
flush_cluster_pages(reiser4_cluster_t * clust, jnode * node,
		    struct inode * inode)
{
	int result = 0;
	int i;
	int nr_pages = 0;
	tfm_cluster_t * tc = &clust->tc;

	assert("edward-980", node != NULL);
	assert("edward-236", inode != NULL);
	assert("edward-237", clust != NULL);
	assert("edward-240", !clust->win);
	assert("edward-241", schedulable());
	assert("edward-718", crc_inode_ok(inode));

	LOCK_JNODE(node);

	if (!jnode_is_dirty(node)) {

		assert("edward-981", node->page_count == 0);
		warning("edward-982", "flush_cluster_pages: jnode is not dirty "
			"clust %lu, inode %llu\n",
			clust->index, (unsigned long long)get_inode_oid(inode));

		/* race with another flush */
		UNLOCK_JNODE(node);
		return RETERR(-E_REPEAT);
	}
	tc->len = fsize_to_count(clust, inode);
	clust->nr_pages = count_to_nrpages(tc->len);

	assert("edward-983", clust->nr_pages == node->page_count + 1);
#if REISER4_DEBUG
	node->page_count = 0;
#endif
	cluster_reserved2grabbed(estimate_insert_cluster(inode, 0));
	uncapture_cluster_jnode(node);

	/* Try to create input stream for the found size (tc->len).
	   Starting from this point the page cluster can be modified
	   (truncated, appended) by concurrent processes, so we need
	   to worry if the constructed stream is valid */

	assert("edward-1224", schedulable());

	result = grab_tfm_stream(inode, tc, TFM_WRITE, INPUT_STREAM);
	if (result)
		return result;

	nr_pages = find_get_pages(inode->i_mapping, clust_to_pg(clust->index, inode),
				clust->nr_pages, clust->pages);

	if (nr_pages != clust->nr_pages) {
		/* the page cluster get truncated, try again */
		assert("edward-1280", nr_pages < clust->nr_pages);
		warning("edward-1281", "Page cluster of index %lu (inode %llu)"
			" get truncated from %u to %u pages\n",
			clust->index,
			(unsigned long long)get_inode_oid(inode),
			clust->nr_pages,
			nr_pages);
		forget_cluster_pages(clust->pages, nr_pages);
		return RETERR(-E_REPEAT);
	}
	for (i = 0; i < clust->nr_pages; i++){
		char * data;

		assert("edward-242", clust->pages[i] != NULL);

		if (clust->pages[i]->index != clust_to_pg(clust->index, inode) + i) {
			/* holes in the indices of found group of pages:
			   page cluster get truncated, transform impossible */
			warning("edward-1282",
				"Hole in the indices: "
				"Page %d in the cluster of index %lu "
				"(inode %llu) has index %lu\n",
				i, clust->index,
				(unsigned long long)get_inode_oid(inode),
				clust->pages[i]->index);

			forget_cluster_pages(clust->pages, nr_pages);
			result = RETERR(-E_REPEAT);
			goto finish;
		}
		if (!PageUptodate(clust->pages[i])) {
			/* page cluster get truncated, transform impossible */
			assert("edward-1283", !PageDirty(clust->pages[i]));
			warning("edward-1284",
				"Page of index %lu (inode %llu) "
				"is not uptodate\n", clust->pages[i]->index,
				(unsigned long long)get_inode_oid(inode));

			forget_cluster_pages(clust->pages, nr_pages);
			result = RETERR(-E_REPEAT);
			goto finish;
		}
		/* ok with this page, flush it to the input stream */
		lock_page(clust->pages[i]);
		data = kmap(clust->pages[i]);

		assert("edward-986", off_to_pgcount(tc->len, i) != 0);

		memcpy(tfm_stream_data(tc, INPUT_STREAM) + pg_to_off(i),
		       data, off_to_pgcount(tc->len, i));
		kunmap(clust->pages[i]);
		unlock_page(clust->pages[i]);
	}
	/* input stream is ready for transform */

	clear_cluster_pages_dirty(clust);
 finish:
	release_cluster_pages_capture(clust);
	return result;
}

/* set hint for the cluster of the index @index */
reiser4_internal void
set_hint_cluster(struct inode * inode, hint_t * hint,
		 cloff_t index, znode_lock_mode mode)
{
	reiser4_key key;
	assert("edward-722", crc_inode_ok(inode));
	assert("edward-723", inode_file_plugin(inode) == file_plugin_by_id(CRC_FILE_PLUGIN_ID));

	inode_file_plugin(inode)->key_by_inode(inode, clust_to_off(index, inode), &key);

	seal_init(&hint->seal, &hint->ext_coord.coord, &key);
	hint->offset = get_key_offset(&key);
	hint->mode = mode;
}

reiser4_internal void
invalidate_hint_cluster(reiser4_cluster_t * clust)
{
	assert("edward-1291", clust != NULL);
	assert("edward-1292", clust->hint != NULL);

	done_lh(clust->hint->ext_coord.lh);
	clust->hint->ext_coord.valid = 0;
}

static void
put_hint_cluster(reiser4_cluster_t * clust, struct inode * inode,
		 znode_lock_mode mode)
{
	assert("edward-1286", clust != NULL);
	assert("edward-1287", clust->hint != NULL);

	set_hint_cluster(inode, clust->hint, clust->index + 1, mode);
	invalidate_hint_cluster(clust);
}

static int
balance_dirty_page_cluster(reiser4_cluster_t * clust, struct inode * inode,
			   loff_t off, loff_t to_file)
{
	int result;

	assert("edward-724", inode != NULL);
	assert("edward-725", crc_inode_ok(inode));
	assert("edward-1272", get_current_context()->grabbed_blocks == 0);

	/* set next window params */
	update_cluster(inode, clust, off, to_file);

	result = update_sd_cryptcompress(inode);
	assert("edward-988", !result);
	if (result)
		return result;
	assert("edward-726", clust->hint->ext_coord.lh->owner == NULL);

	reiser4_throttle_write(inode);
	all_grabbed2free();
	return 0;
}

/* set zeroes to the cluster, update it, and maybe, try to capture its pages */
static int
write_hole(struct inode *inode, reiser4_cluster_t * clust, loff_t file_off, loff_t to_file)
{
	char * data;
	int result = 0;
	unsigned cl_off, cl_count = 0;
	unsigned to_pg, pg_off;
	reiser4_slide_t * win;

	assert ("edward-190", clust != NULL);
	assert ("edward-1069", clust->win != NULL);
	assert ("edward-191", inode != NULL);
	assert ("edward-727", crc_inode_ok(inode));
	assert ("edward-1171", clust->dstat != INVAL_DISK_CLUSTER);
	assert ("edward-1154",
		ergo(clust->dstat != FAKE_DISK_CLUSTER, clust->reserved == 1));

	win = clust->win;

	assert ("edward-1070", win != NULL);
	assert ("edward-201", win->stat == HOLE_WINDOW);
	assert ("edward-192", cluster_ok(clust, inode));

	if (win->off == 0 && win->count == inode_cluster_size(inode)) {
		/* the hole will be represented by fake disk cluster */
		update_cluster(inode, clust, file_off, to_file);
		return 0;
	}
	cl_count = win->count; /* number of zeroes to write */
	cl_off = win->off;
	pg_off = off_to_pgoff(win->off);

	while (cl_count) {
		struct page * page;
		page = clust->pages[off_to_pg(cl_off)];

		assert ("edward-284", page != NULL);

		to_pg = min_count(PAGE_CACHE_SIZE - pg_off, cl_count);
		lock_page(page);
		data = kmap_atomic(page, KM_USER0);
		memset(data + pg_off, 0, to_pg);
		flush_dcache_page(page);
		kunmap_atomic(data, KM_USER0);
		SetPageUptodate(page);
		unlock_page(page);

		cl_off += to_pg;
		cl_count -= to_pg;
		pg_off = 0;
	}
	if (!win->delta) {
		/* only zeroes, try to capture */

		set_cluster_pages_dirty(clust);
		result = try_capture_cluster(clust, inode);
		if (result)
			return result;
		put_hint_cluster(clust, inode, ZNODE_WRITE_LOCK);
		result = balance_dirty_page_cluster(clust, inode, file_off, to_file);
	}
	else
		update_cluster(inode, clust, file_off, to_file);
	return result;
}

/*
  The main disk search procedure for cryptcompress plugins, which
  . scans all items of disk cluster
  . maybe reads each one (if @read != 0)
  . maybe makes its znode dirty  (if @write != 0)

  NOTE-EDWARD: Callers should handle the case when disk cluster
  is incomplete (-EIO)
*/
reiser4_internal int
find_cluster(reiser4_cluster_t * clust,
	     struct inode * inode,
	     int read,
	     int write)
{
	flow_t f;
	hint_t * hint;
	int result;
	unsigned long cl_idx;
	ra_info_t ra_info;
	file_plugin * fplug;
	item_plugin * iplug;
	tfm_cluster_t * tc;

#if REISER4_DEBUG
	reiser4_context *ctx;
	ctx = get_current_context();
#endif
	assert("edward-138", clust != NULL);
	assert("edward-728", clust->hint != NULL);
	assert("edward-225", read || write);
	assert("edward-226", schedulable());
	assert("edward-137", inode != NULL);
	assert("edward-729", crc_inode_ok(inode));
	assert("edward-474", get_current_context()->grabbed_blocks == 0);

	hint = clust->hint;
	cl_idx = clust->index;
	fplug = inode_file_plugin(inode);

	tc = &clust->tc;

	assert("edward-462", !tfm_cluster_is_uptodate(tc));
	assert("edward-461", ergo(read, tfm_stream_is_set(tc, INPUT_STREAM)));

	/* set key of the first disk cluster item */
	fplug->flow_by_inode(inode,
			     (read ? tfm_stream_data(tc, INPUT_STREAM) : 0),
			     0 /* kernel space */,
			     inode_scaled_cluster_size(inode),
			     clust_to_off(cl_idx, inode), READ_OP, &f);
	if (write) {
		/* reserve for flush to make dirty all the leaf nodes
		   which contain disk cluster */
		result = reiser4_grab_space_force(estimate_disk_cluster(inode), BA_CAN_COMMIT);
		assert("edward-990", !result);
		if (result)
			goto out2;
	}

	ra_info.key_to_stop = f.key;
	set_key_offset(&ra_info.key_to_stop, get_key_offset(max_key()));

	while (f.length) {
		result = find_cluster_item(hint,
					   &f.key,
					   (write ? ZNODE_WRITE_LOCK : ZNODE_READ_LOCK),
					   NULL,
					   FIND_EXACT,
					   (write ? CBK_FOR_INSERT : 0));
		switch (result) {
		case CBK_COORD_NOTFOUND:
			if (inode_scaled_offset(inode, clust_to_off(cl_idx, inode)) == get_key_offset(&f.key)) {
				/* first item not found, this is treated
				   as disk cluster is absent */
				clust->dstat = FAKE_DISK_CLUSTER;
				result = 0;
				goto out2;
			}
			/* we are outside the cluster, stop search here */
			assert("edward-146", f.length != inode_scaled_cluster_size(inode));
			goto ok;
		case CBK_COORD_FOUND:
			assert("edward-148", hint->ext_coord.coord.between == AT_UNIT);
			assert("edward-460", hint->ext_coord.coord.unit_pos == 0);

			coord_clear_iplug(&hint->ext_coord.coord);
			result = zload_ra(hint->ext_coord.coord.node, &ra_info);
			if (unlikely(result))
				goto out2;
			iplug = item_plugin_by_coord(&hint->ext_coord.coord);
			assert("edward-147",
			       item_id_by_coord(&hint->ext_coord.coord) == CTAIL_ID);

			result = iplug->s.file.read(NULL, &f, hint);
			if (result)
				goto out;
			if (write) {
				znode_make_dirty(hint->ext_coord.coord.node);
				znode_set_convertible(hint->ext_coord.coord.node);
			}
			zrelse(hint->ext_coord.coord.node);
			break;
		default:
			goto out2;
		}
	}
 ok:
	/* at least one item was found  */
	/* NOTE-EDWARD: Callers should handle the case when disk cluster is incomplete (-EIO) */
	tc->len = inode_scaled_cluster_size(inode) - f.length;
	assert("edward-1196", tc->len > 0);

	if (hint_is_unprepped_dclust(clust->hint))
		clust->dstat = UNPR_DISK_CLUSTER;
	else
		clust->dstat = PREP_DISK_CLUSTER;
	all_grabbed2free();
	return 0;
 out:
	zrelse(hint->ext_coord.coord.node);
 out2:
	all_grabbed2free();
	return result;
}

reiser4_internal int
get_disk_cluster_locked(reiser4_cluster_t * clust, struct inode * inode,
			znode_lock_mode lock_mode)
{
	reiser4_key key;
	ra_info_t ra_info;

	assert("edward-730", schedulable());
	assert("edward-731", clust != NULL);
	assert("edward-732", inode != NULL);

	if (clust->hint->ext_coord.valid) {
		assert("edward-1293", clust->dstat != INVAL_DISK_CLUSTER);
		assert("edward-1294", znode_is_write_locked(clust->hint->ext_coord.lh->node));
		/* already have a valid locked position */
		return (clust->dstat == FAKE_DISK_CLUSTER ? CBK_COORD_NOTFOUND : CBK_COORD_FOUND);
	}
	key_by_inode_cryptcompress(inode, clust_to_off(clust->index, inode), &key);
	ra_info.key_to_stop = key;
	set_key_offset(&ra_info.key_to_stop, get_key_offset(max_key()));

	return find_cluster_item(clust->hint, &key, lock_mode, NULL, FIND_EXACT, CBK_FOR_INSERT);
}

/* Read needed cluster pages before modifying.
   If success, @clust->hint contains locked position in the tree.
   Also:
   . find and set disk cluster state
   . make disk cluster dirty if its state is not FAKE_DISK_CLUSTER.
*/
static int
read_some_cluster_pages(struct inode * inode, reiser4_cluster_t * clust)
{
	int i;
	int result = 0;
	item_plugin * iplug;
	reiser4_slide_t * win = clust->win;

	iplug = item_plugin_by_id(CTAIL_ID);

	assert("edward-733", get_current_context()->grabbed_blocks == 0);
	assert("edward-924", !tfm_cluster_is_uptodate(&clust->tc));

#if REISER4_DEBUG
	if (clust->nr_pages == 0) {
		/* start write hole from fake disk cluster */
		assert("edward-1117", win != NULL);
		assert("edward-1118", win->stat == HOLE_WINDOW);
		assert("edward-1119", new_cluster(clust, inode));
	}
#endif
	if (new_cluster(clust, inode)) {
		/*
		   new page cluster is about to be written, nothing to read,
		*/
		assert("edward-734", schedulable());
		assert("edward-735", clust->hint->ext_coord.lh->owner == NULL);

		clust->dstat = FAKE_DISK_CLUSTER;
		return 0;
	}
	/*
	  Here we should search for disk cluster to figure out its real state.
	  Also there is one more important reason to do disk search: we need
	  to make disk cluster _dirty_ if it exists
	*/

	/* if windows is specified, read the only pages
	   that will be modified partially */

	for (i = 0; i < clust->nr_pages; i++) {
		struct page * pg = clust->pages[i];

		lock_page(pg);
		if (PageUptodate(pg)) {
			unlock_page(pg);
			continue;
		}
		unlock_page(pg);

		if (win &&
		    i >= count_to_nrpages(win->off) &&
		    i < off_to_pg(win->off + win->count + win->delta))
			/* page will be completely overwritten */
			continue;
		if (win && (i == clust->nr_pages - 1) &&
		    /* the last page is
		       partially modified,
		       not uptodate .. */
		    (count_to_nrpages(inode->i_size) <= pg->index)) {
			/* .. and appended,
			   so set zeroes to the rest */
			char * data;
			int offset;
			lock_page(pg);
			data = kmap_atomic(pg, KM_USER0);

			assert("edward-1260",
			       count_to_nrpages(win->off + win->count + win->delta) - 1 == i);

			offset = off_to_pgoff(win->off + win->count + win->delta);
			memset(data + offset, 0, PAGE_CACHE_SIZE - offset);
			flush_dcache_page(pg);
			kunmap_atomic(data, KM_USER0);
			unlock_page(pg);
			/* still not uptodate */
			break;
		}
		if (!tfm_cluster_is_uptodate(&clust->tc)) {
			result = ctail_read_cluster(clust, inode, 1 /* write */);
			assert("edward-992", !result);
			if (result)
				goto out;
			assert("edward-925", tfm_cluster_is_uptodate(&clust->tc));
		}
		lock_page(pg);
		result =  do_readpage_ctail(clust, pg);
		unlock_page(pg);
		assert("edward-993", !result);
		if (result) {
			impossible("edward-219", "do_readpage_ctail returned crap");
			goto out;
		}
	}
	if (!tfm_cluster_is_uptodate(&clust->tc)) {
		/* disk cluster unclaimed, but we need to make its znodes dirty
		   to make flush update convert its content */
		result = find_cluster(clust, inode, 0 /* do not read */, 1 /*write */);
		assert("edward-994", !cbk_errored(result));
		if (!cbk_errored(result))
			result = 0;
	}
 out:
	tfm_cluster_clr_uptodate(&clust->tc);
	return result;
}

static int
should_create_unprepped_cluster(reiser4_cluster_t * clust, struct inode * inode)
{
	assert("edward-737", clust != NULL);

	switch (clust->dstat) {
	case PREP_DISK_CLUSTER:
	case UNPR_DISK_CLUSTER:
		return 0;
	case FAKE_DISK_CLUSTER:
		if (clust->win &&
		    clust->win->stat == HOLE_WINDOW &&
		    clust->nr_pages == 0) {
			assert("edward-1172", new_cluster(clust, inode));
			return 0;
		}
		return 1;
	default:
		impossible("edward-1173", "bad disk cluster state");
		return 0;
	}
}

static int
crc_make_unprepped_cluster (reiser4_cluster_t * clust, struct inode * inode)
{
	int result;

	assert("edward-1123", schedulable());
	assert("edward-737", clust != NULL);
	assert("edward-738", inode != NULL);
	assert("edward-739", crc_inode_ok(inode));
	assert("edward-1053", clust->hint != NULL);
	assert("edward-1266", get_current_context()->grabbed_blocks == 0);

	if (clust->reserved){
		cluster_reserved2grabbed(estimate_insert_cluster(inode, 1));
#if REISER4_DEBUG
		assert("edward-1267", clust->reserved_unprepped == estimate_insert_cluster(inode, 1));
		clust->reserved_unprepped -= estimate_insert_cluster(inode, 1);
#endif
	}
	if (!should_create_unprepped_cluster(clust, inode)) {
		all_grabbed2free();
		return 0;
	} else {
		assert("edward-1268", clust->reserved == 1);
	}
	result = ctail_insert_unprepped_cluster(clust, inode);
	all_grabbed2free();
	if (result)
		return result;

	assert("edward-743", crc_inode_ok(inode));
	assert("edward-1269", get_current_context()->grabbed_blocks == 0);
	assert("edward-744", znode_is_write_locked(clust->hint->ext_coord.lh->node));

	clust->dstat = UNPR_DISK_CLUSTER;
	return 0;
}

#if REISER4_DEBUG
static int
jnode_truncate_ok(struct inode *inode, cloff_t index)
{
	jnode * node;
	node = jlookup(current_tree, get_inode_oid(inode), clust_to_pg(index, inode));
	if (node) {
		warning("edward-1315", "jnode %p is untruncated\n", node);
		jput(node);
	}
	return (node == NULL);
}

static int
jnodes_truncate_ok(struct inode * inode, cloff_t start)
{
	int result;
	jnode * node;
	reiser4_inode *info = reiser4_inode_data(inode);
	reiser4_tree * tree = tree_by_inode(inode);

	RLOCK_TREE(tree);

	result = radix_tree_gang_lookup(jnode_tree_by_reiser4_inode(info), (void **)&node,
					clust_to_pg(start, inode), 1);
	RUNLOCK_TREE(tree);
	if (result)
		warning("edward-1332", "Untruncated jnode %p\n", node);
	return !result;
}

#endif

/* Collect unlocked cluster pages and jnode (the last is in the
   case when the page cluster will be modified and captured) */
reiser4_internal int
prepare_page_cluster(struct inode *inode, reiser4_cluster_t *clust, int capture)
{
	assert("edward-177", inode != NULL);
	assert("edward-741", crc_inode_ok(inode));
	assert("edward-740", clust->pages != NULL);

	set_cluster_nrpages(clust, inode);
	reset_cluster_pgset(clust, cluster_nrpages(inode));
	return (capture ?
		grab_cluster_pages_jnode(inode, clust) :
		grab_cluster_pages      (inode, clust));
}

/* Truncate all the pages and jnode bound with the cluster of index @index */
reiser4_internal void
truncate_page_cluster(struct inode *inode, cloff_t index)
{
	int i;
	int found = 0;
	int nr_pages;
	jnode * node;
	struct page * pages[MAX_CLUSTER_NRPAGES];

	node = jlookup(current_tree, get_inode_oid(inode), clust_to_pg(index, inode));
	/* jnode is absent, just drop pages which can not
	   acquire jnode because of exclusive access */
	if (!node) {
		truncate_inode_pages_range(inode->i_mapping,
					   clust_to_off(index, inode),
					   clust_to_off(index, inode) + inode_cluster_size(inode) - 1);
		return;
	}
	/* jnode is present and may be dirty, if so, put
	   all the cluster pages except the first one */
	nr_pages = count_to_nrpages(off_to_count(inode->i_size, index, inode));

	found = find_get_pages(inode->i_mapping, clust_to_pg(index, inode),
			       nr_pages, pages);

	LOCK_JNODE(node);
	if (jnode_is_dirty(node)) {
		/* jnode is dirty => space for disk cluster
		   conversion grabbed */
		cluster_reserved2grabbed(estimate_insert_cluster(inode, 0));
		grabbed2free(get_current_context(),
			     get_current_super_private(),
			     estimate_insert_cluster(inode, 0));

		assert("edward-1198", found == nr_pages);
		/* This will clear dirty bit so concurrent flush
		   won't start to convert the disk cluster */
		assert("edward-1199", PageUptodate(pages[0]));
		uncapture_cluster_jnode(node);

		for (i = 1; i < nr_pages ; i++) {
			assert("edward-1200", PageUptodate(pages[i]));

			page_cache_release(pages[i]);
		}
	}
	else
		UNLOCK_JNODE(node);
	/* now drop pages and jnode */
	/* FIXME-EDWARD: Use truncate_complete_page in the loop above instead */

	jput(node);
	forget_cluster_pages(pages, found);
	truncate_inode_pages_range(inode->i_mapping,
				   clust_to_off(index, inode),
				   clust_to_off(index, inode) + inode_cluster_size(inode) - 1);
	assert("edward-1201", jnode_truncate_ok(inode, index));
	return;
}

/* Prepare cluster handle before write and(or) capture. This function
   is called by all the clients which modify page cluster and(or) put
   it into a transaction (file_write, truncate, writepages, etc..)

   . grab cluster pages;
   . reserve disk space;
   . maybe read pages from disk and set the disk cluster dirty;
   . maybe write hole;
   . maybe create 'unprepped' disk cluster if the last one is fake
     (isn't represenred by any items on disk)
*/

static int
prepare_cluster(struct inode *inode,
		loff_t file_off /* write position in the file */,
		loff_t to_file, /* bytes of users data to write to the file */
		reiser4_cluster_t *clust,
		page_cluster_op op)

{
	int result = 0;
	reiser4_slide_t * win = clust->win;

	assert("edward-1273", get_current_context()->grabbed_blocks == 0);
	reset_cluster_params(clust);
#if REISER4_DEBUG
	clust->ctx = get_current_context();
#endif
	assert("edward-1190", op != PCL_UNKNOWN);

	clust->op = op;

	result = prepare_page_cluster(inode, clust, 1);
	if (result)
		return result;
	result = reserve4cluster(inode, clust, msg);
	if (result)
		goto err1;
	result = read_some_cluster_pages(inode, clust);
	if (result) {
		free_reserved4cluster(inode,
				      clust,
				      estimate_insert_cluster(inode, 0) +
				      estimate_insert_cluster(inode, 1));
		goto err1;
	}
	assert("edward-1124", clust->dstat != INVAL_DISK_CLUSTER);

	result = crc_make_unprepped_cluster(clust, inode);
	if (result)
		goto err2;
	if (win && win->stat == HOLE_WINDOW) {
 		result = write_hole(inode, clust, file_off, to_file);
 		if (result)
			goto err2;
	}
	return 0;
 err2:
	free_reserved4cluster(inode,
			      clust,
			      estimate_insert_cluster(inode, 0));
 err1:
	page_cache_release(clust->pages[0]);
	release_cluster_pages_and_jnode(clust);
	assert("edward-1125", 0);
	return result;
}

/* set window by two offsets */
static void
set_window(reiser4_cluster_t * clust, reiser4_slide_t * win,
	   struct inode * inode, loff_t o1, loff_t o2)
{
	assert("edward-295", clust != NULL);
	assert("edward-296", inode != NULL);
	assert("edward-1071", win != NULL);
	assert("edward-297", o1 <= o2);

	clust->index = off_to_clust(o1, inode);

	win->off = off_to_cloff(o1, inode);
	win->count = min_count(inode_cluster_size(inode) - win->off, o2 - o1);
	win->delta = 0;

	clust->win = win;
}

static int
set_cluster_params(struct inode * inode, reiser4_cluster_t * clust,
		   reiser4_slide_t * win, flow_t * f, loff_t file_off)
{
	int result;

	assert("edward-197", clust != NULL);
	assert("edward-1072", win != NULL);
	assert("edward-198", inode != NULL);

	result = alloc_cluster_pgset(clust, cluster_nrpages(inode));
	if (result)
		return result;

	if (file_off > inode->i_size) {
		/* Uhmm, hole in crypto-file... */
		loff_t hole_size;
		hole_size = file_off - inode->i_size;

		set_window(clust, win, inode, inode->i_size, file_off);
		win->stat = HOLE_WINDOW;
		if (win->off + hole_size < inode_cluster_size(inode))
			/* there is also user's data to append to the hole */
			win->delta = min_count(inode_cluster_size(inode) - (win->off + win->count), f->length);
		return 0;
	}
	set_window(clust, win, inode, file_off, file_off + f->length);
	win->stat = DATA_WINDOW;
	return 0;
}

/* reset all the params that not get updated */
reiser4_internal void
reset_cluster_params(reiser4_cluster_t * clust)
{
	assert("edward-197", clust != NULL);

	clust->dstat = INVAL_DISK_CLUSTER;
	clust->tc.uptodate = 0;
	clust->tc.len = 0;
}

/* Main write procedure for cryptcompress objects,
   this slices user's data into clusters and copies to page cache.
   If @buf != NULL, returns number of bytes in successfully written clusters,
   otherwise returns error */
/* FIXME_EDWARD replace flow by something lightweigth */

static loff_t
write_cryptcompress_flow(struct file * file , struct inode * inode, const char *buf, size_t count, loff_t pos)
{
	int i;
	flow_t f;
	hint_t hint;
	lock_handle lh;
	int result = 0;
	size_t to_write = 0;
	loff_t file_off;
	reiser4_slide_t win;
	reiser4_cluster_t clust;

	assert("edward-161", schedulable());
	assert("edward-748", crc_inode_ok(inode));
	assert("edward-159", current_blocksize == PAGE_CACHE_SIZE);
	assert("edward-1274", get_current_context()->grabbed_blocks == 0);

	result = check_cryptcompress(inode);
	if (result)
		return result;
	result = load_file_hint(file, &hint);
	if (result)
		return result;
	init_lh(&lh);
	hint.ext_coord.lh = &lh;

	result = flow_by_inode_cryptcompress(inode, (char *)buf, 1 /* user space */, count, pos, WRITE_OP, &f);
	if (result)
		goto out;
	to_write = f.length;

        /* current write position in file */
	file_off = pos;
	reiser4_slide_init(&win);
	reiser4_cluster_init(&clust, &win);
	clust.hint = &hint;

	result = set_cluster_params(inode, &clust, &win, &f, file_off);
	if (result)
		goto out;

	if (next_window_stat(&win) == HOLE_WINDOW) {
		result = prepare_cluster(inode, file_off, f.length, &clust, PCL_APPEND);
		if (result)
			goto out;
	}
	do {
		char *src;
		unsigned page_off, page_count;

		assert("edward-750", schedulable());

		result = prepare_cluster(inode, file_off, f.length, &clust, PCL_APPEND);
		if (result)
			goto out;

		assert("edward-751", crc_inode_ok(inode));
		assert("edward-204", win.stat == DATA_WINDOW);
		assert("edward-1288", clust.hint->ext_coord.valid);
		assert("edward-752", znode_is_write_locked(hint.ext_coord.coord.node));

		put_hint_cluster(&clust, inode, ZNODE_WRITE_LOCK);

		/* set write position in page */
		page_off = off_to_pgoff(win.off);

                /* copy user's data to cluster pages */
		for (i = off_to_pg(win.off), src = f.data; i < count_to_nrpages(win.off + win.count); i++, src += page_count) {
			page_count = off_to_pgcount(win.off + win.count, i) - page_off;

			assert("edward-1039", page_off + page_count <= PAGE_CACHE_SIZE);
			assert("edward-287", clust.pages[i] != NULL);

			lock_page(clust.pages[i]);
			result = __copy_from_user((char *)kmap(clust.pages[i]) + page_off, src, page_count);
			kunmap(clust.pages[i]);
			if (unlikely(result)) {
				unlock_page(clust.pages[i]);
				result = -EFAULT;
				goto err3;
			}
			SetPageUptodate(clust.pages[i]);
			unlock_page(clust.pages[i]);
			page_off = 0;
		}
		assert("edward-753", crc_inode_ok(inode));

		set_cluster_pages_dirty(&clust);

		result = try_capture_cluster(&clust, inode);
		if (result)
			goto err2;

		assert("edward-998", f.user == 1);

		move_flow_forward(&f, win.count);

		/* disk cluster may be already clean at this point */

		/* . update cluster
		   . set hint for new offset
		   . unlock znode
		   . update inode
		   . balance dirty pages
		*/
		result = balance_dirty_page_cluster(&clust, inode, 0, f.length);
		if(result)
			goto err1;
		assert("edward-755", hint.ext_coord.lh->owner == NULL);
		reset_cluster_params(&clust);
		continue;
	err3:
		page_cache_release(clust.pages[0]);
	err2:
		release_cluster_pages_and_jnode(&clust);
	err1:
		if (clust.reserved)
			free_reserved4cluster(inode,
					      &clust,
					      estimate_insert_cluster(inode, 0));
		break;
	} while (f.length);
 out:
	done_lh(&lh);
 	if (result == -EEXIST)
		printk("write returns EEXIST!\n");

	put_cluster_handle(&clust, TFM_READ);
	save_file_hint(file, &hint);
	if (buf) {
		/* if nothing were written - there must be an error */
		assert("edward-195", ergo((to_write == f.length), result < 0));
		return (to_write - f.length) ? (to_write - f.length) : result;
	}
	return result;
}

static ssize_t
write_crc_file(struct file * file, /* file to write to */
	   struct inode *inode, /* inode */
	   const char *buf, /* address of user-space buffer */
	   size_t count, /* number of bytes to write */
	   loff_t * off /* position to write which */)
{

	int result;
	loff_t pos;
	ssize_t written;
	cryptcompress_info_t * info = cryptcompress_inode_data(inode);

	assert("edward-196", crc_inode_ok(inode));

	result = generic_write_checks(file, off, &count, 0);
	if (unlikely(result != 0))
		return result;

	if (unlikely(count == 0))
		return 0;

        /* FIXME-EDWARD: other UNIX features */

	down_write(&info->lock);
	LOCK_CNT_INC(inode_sem_w);

	pos = *off;
	written = write_cryptcompress_flow(file, inode, (char *)buf, count, pos);
	if (written < 0) {
		if (written == -EEXIST)
			printk("write_crc_file returns EEXIST!\n");
		return written;
	}

        /* update position in a file */
	*off = pos + written;

	up_write(&info->lock);
	LOCK_CNT_DEC(inode_sem_w);

	/* return number of written bytes */
	return written;
}

/* plugin->u.file.write */
reiser4_internal ssize_t
write_cryptcompress(struct file * file, /* file to write to */
		    const char *buf, /* address of user-space buffer */
		    size_t count, /* number of bytes to write */
		    loff_t * off /* position to write which */)
{
	ssize_t result;
	struct inode *inode;

	inode = file->f_dentry->d_inode;

	down(&inode->i_sem);

	result = write_crc_file(file, inode, buf, count, off);

	up(&inode->i_sem);
	return result;
}

static void
readpages_crc(struct address_space *mapping, struct list_head *pages, void *data)
{
	file_plugin *fplug;
	item_plugin *iplug;

	assert("edward-1112", mapping != NULL);
	assert("edward-1113", mapping->host != NULL);

	fplug = inode_file_plugin(mapping->host);
	assert("edward-1114", fplug == file_plugin_by_id(CRC_FILE_PLUGIN_ID));
	iplug = item_plugin_by_id(CTAIL_ID);

	iplug->s.file.readpages(data, mapping, pages);

	return;
}

static reiser4_block_nr
cryptcompress_estimate_read(struct inode *inode)
{
    	/* reserve one block to update stat data item */
	assert("edward-1193",
	       inode_file_plugin(inode)->estimate.update == estimate_update_common);
	return estimate_update_common(inode);
}

/* plugin->u.file.read */
ssize_t read_cryptcompress(struct file * file, char *buf, size_t size, loff_t * off)
{
	ssize_t result;
	struct inode *inode;
	reiser4_file_fsdata * fsdata;
	cryptcompress_info_t * info;
	reiser4_block_nr needed;

	inode = file->f_dentry->d_inode;
	assert("edward-1194", !inode_get_flag(inode, REISER4_NO_SD));

	info = cryptcompress_inode_data(inode);
	needed = cryptcompress_estimate_read(inode);
	/* FIXME-EDWARD:
	   Grab space for sd_update so find_cluster will be happy */
#if 0
	result = reiser4_grab_space(needed, BA_CAN_COMMIT);
	if (result != 0)
		return result;
#endif
	fsdata = reiser4_get_file_fsdata(file);
	fsdata->ra2.data = file;
	fsdata->ra2.readpages = readpages_crc;

	down_read(&info->lock);
	LOCK_CNT_INC(inode_sem_r);

	result = generic_file_read(file, buf, size, off);

	up_read(&info->lock);
	LOCK_CNT_DEC(inode_sem_r);

	return result;
}

static void
set_append_cluster_key(const coord_t *coord, reiser4_key *key, struct inode *inode)
{
	item_key_by_coord(coord, key);
	set_key_offset(key, clust_to_off(clust_by_coord(coord, inode) + 1, inode));
}

/* If @index > 0, find real disk cluster of the index (@index - 1),
   If @index == 0 find the real disk cluster of the object of maximal index.
   Keep incremented index of the result in @found.
   It succes was returned:
   (@index == 0 && @found == 0) means that the object doesn't have real disk
   clusters.
   (@index != 0 && @found == 0) means that disk cluster of (@index -1 ) doesn't
   exist.
*/
static int
find_real_disk_cluster(struct inode * inode, cloff_t * found, cloff_t index)
{
	int result;
	reiser4_key key;
	loff_t offset;
	hint_t hint;
	lookup_bias bias;
	coord_t *coord;
	lock_handle lh;
	item_plugin *iplug;
	file_plugin *fplug = inode_file_plugin(inode);

	assert("edward-1131", fplug == file_plugin_by_id(CRC_FILE_PLUGIN_ID));
	assert("edward-95", crc_inode_ok(inode));

	init_lh(&lh);
	hint_init_zero(&hint);
	hint.ext_coord.lh = &lh;

	bias =   (index ? FIND_EXACT : FIND_MAX_NOT_MORE_THAN);
	offset = (index ? clust_to_off(index, inode) - 1 : get_key_offset(max_key()));

	fplug->key_by_inode(inode, offset, &key);

	/* find the last item of this object */
	result = find_cluster_item(&hint, &key, ZNODE_READ_LOCK, 0/* ra_info */, bias, 0);
	if (cbk_errored(result)) {
		done_lh(&lh);
		return result;
	}
	if (result == CBK_COORD_NOTFOUND) {
		/* no real disk clusters */
		done_lh(&lh);
		*found = 0;
		return 0;
	}
	/* disk cluster is found */
	coord = &hint.ext_coord.coord;
	coord_clear_iplug(coord);
	result = zload(coord->node);
	if (unlikely(result)) {
		done_lh(&lh);
		return result;
	}
	iplug = item_plugin_by_coord(coord);
	assert("edward-277", iplug == item_plugin_by_id(CTAIL_ID));
	assert("edward-1202", ctail_ok(coord));

	set_append_cluster_key(coord, &key, inode);

	*found = off_to_clust(get_key_offset(&key), inode);

	assert("edward-1132", ergo(index, index == *found));

	zrelse(coord->node);
	done_lh(&lh);

	return 0;
}

static int
find_fake_appended(struct inode *inode, cloff_t *index)
{
	return find_real_disk_cluster(inode, index, 0 /* find last real one */);
}

/* Set left coord when unit is not found after node_lookup()
   This takes into account that there can be holes in a sequence
   of disk clusters */

static void
adjust_left_coord(coord_t * left_coord)
{
	switch(left_coord->between) {
	case AFTER_UNIT:
		left_coord->between = AFTER_ITEM;
	case AFTER_ITEM:
	case BEFORE_UNIT:
		break;
	default:
		impossible("edward-1204", "bad left coord to cut");
	}
	return;
}

#define CRC_CUT_TREE_MIN_ITERATIONS 64
reiser4_internal int
cut_tree_worker_cryptcompress(tap_t * tap, const reiser4_key * from_key,
			      const reiser4_key * to_key, reiser4_key * smallest_removed,
			      struct inode * object, int truncate, int *progress)
{
	lock_handle next_node_lock;
	coord_t left_coord;
	int result;

	assert("edward-1158", tap->coord->node != NULL);
	assert("edward-1159", znode_is_write_locked(tap->coord->node));
	assert("edward-1160", znode_get_level(tap->coord->node) == LEAF_LEVEL);

	*progress = 0;
	init_lh(&next_node_lock);

	while (1) {
		znode       *node;  /* node from which items are cut */
		node_plugin *nplug; /* node plugin for @node */

		node = tap->coord->node;

		/* Move next_node_lock to the next node on the left. */
		result = reiser4_get_left_neighbor(
			&next_node_lock, node, ZNODE_WRITE_LOCK, GN_CAN_USE_UPPER_LEVELS);
		if (result != 0 && result != -E_NO_NEIGHBOR)
			break;
		/* FIXME-EDWARD: Check can we delete the node as a whole. */
		result = tap_load(tap);
		if (result)
			return result;

			/* Prepare the second (right) point for cut_node() */
		if (*progress)
			coord_init_last_unit(tap->coord, node);

		else if (item_plugin_by_coord(tap->coord)->b.lookup == NULL)
			/* set rightmost unit for the items without lookup method */
			tap->coord->unit_pos = coord_last_unit_pos(tap->coord);

		nplug = node->nplug;

		assert("edward-1161", nplug);
		assert("edward-1162", nplug->lookup);

		/* left_coord is leftmost unit cut from @node */
		result = nplug->lookup(node, from_key,
				       FIND_EXACT, &left_coord);

		if (IS_CBKERR(result))
			break;

		if (result == CBK_COORD_NOTFOUND)
			adjust_left_coord(&left_coord);

		/* adjust coordinates so that they are set to existing units */
		if (coord_set_to_right(&left_coord) || coord_set_to_left(tap->coord)) {
			result = 0;
			break;
		}

		if (coord_compare(&left_coord, tap->coord) == COORD_CMP_ON_RIGHT) {
			/* keys from @from_key to @to_key are not in the tree */
			result = 0;
			break;
		}

		/* cut data from one node */
		*smallest_removed = *min_key();
		result = kill_node_content(&left_coord,
					   tap->coord,
					   from_key,
					   to_key,
					   smallest_removed,
					   next_node_lock.node,
					   object, truncate);
#if REISER4_DEBUG
		/*node_check(node, ~0U);*/
#endif
		tap_relse(tap);

		if (result)
			break;

		++ (*progress);

		/* Check whether all items with keys >= from_key were removed
		 * from the tree. */
		if (keyle(smallest_removed, from_key))
			/* result = 0;*/
				break;

		if (next_node_lock.node == NULL)
			break;

		result = tap_move(tap, &next_node_lock);
		done_lh(&next_node_lock);
		if (result)
			break;

		/* Break long cut_tree operation (deletion of a large file) if
		 * atom requires commit. */
		if (*progress > CRC_CUT_TREE_MIN_ITERATIONS
		    && current_atom_should_commit())
			{
				result = -E_REPEAT;
				break;
			}
	}
	done_lh(&next_node_lock);
	return result;
}

/* Append or expand hole in two steps (exclusive access should be aquired!)
   1) write zeroes to the current real cluster,
   2) expand hole via fake clusters (just increase i_size) */
static int
cryptcompress_append_hole(struct inode * inode /*contains old i_size */,
			  loff_t new_size)
{
	int result = 0;
	hint_t hint;
	loff_t hole_size;
	int nr_zeroes;
	lock_handle lh;
	reiser4_slide_t win;
	reiser4_cluster_t clust;

	assert("edward-1133", inode->i_size < new_size);
	assert("edward-1134", schedulable());
	assert("edward-1135", crc_inode_ok(inode));
	assert("edward-1136", current_blocksize == PAGE_CACHE_SIZE);
	assert("edward-1333", off_to_cloff(inode->i_size, inode) != 0);

	init_lh(&lh);
	hint_init_zero(&hint);
	hint.ext_coord.lh = &lh;

	reiser4_slide_init(&win);
	reiser4_cluster_init(&clust, &win);
	clust.hint = &hint;

	/* set cluster handle */
	result = alloc_cluster_pgset(&clust, cluster_nrpages(inode));
	if (result)
		goto out;
	hole_size = new_size - inode->i_size;
	nr_zeroes = min_count(inode_cluster_size(inode) - off_to_cloff(inode->i_size, inode), hole_size);

	set_window(&clust, &win, inode, inode->i_size, inode->i_size + nr_zeroes);
	win.stat = HOLE_WINDOW;

	assert("edward-1137", clust.index == off_to_clust(inode->i_size, inode));

	result = prepare_cluster(inode, 0, 0, &clust, PCL_APPEND);

	assert("edward-1271", !result);
	if (result)
		goto out;
	assert("edward-1139",
	       clust.dstat == PREP_DISK_CLUSTER ||
	       clust.dstat == UNPR_DISK_CLUSTER);

	hole_size -= nr_zeroes;
	if (!hole_size)
		/* nothing to append anymore */
		goto out;

	/* fake_append: */
	INODE_SET_FIELD(inode, i_size, new_size);
 out:
	done_lh(&lh);
	put_cluster_handle(&clust, TFM_READ);
	return result;
}

#if REISER4_DEBUG
static int
pages_truncate_ok(struct inode * inode, loff_t old_size, pgoff_t start)
{
	struct pagevec pvec;
	int i;
	int count;
	int rest;

	rest = count_to_nrpages(old_size) - start;

	pagevec_init(&pvec, 0);
	count = min_count(pagevec_space(&pvec), rest);

	while (rest) {
		count = min_count(pagevec_space(&pvec), rest);
		pvec.nr = find_get_pages(inode->i_mapping, start,
					  count, pvec.pages);
		for (i = 0; i < pagevec_count(&pvec); i++) {
			if (PageUptodate(pvec.pages[i])) {
				warning("edward-1205",
					"truncated page of index %lu is uptodate",
					pvec.pages[i]->index);
				return 0;
			}
		}
		start += count;
		rest -= count;
		pagevec_release(&pvec);
	}
	return 1;
}

static int
body_truncate_ok(struct inode * inode, cloff_t aidx)
{
	int result;
	cloff_t raidx;

	result = find_fake_appended(inode, &raidx);
	return !result && (aidx == raidx);
}
#endif

static int
update_cryptcompress_size(struct inode * inode, reiser4_key * key, int update_sd)
{
	return (get_key_offset(key) & ((loff_t)(inode_cluster_size(inode)) - 1) ?
		0 :
		update_file_size(inode, key, update_sd));
}

/* prune cryptcompress file in two steps (exclusive access should be acquired!)
   1) cut all disk clusters but the last one partially truncated,
   2) set zeroes and capture last partially truncated page cluster if the last
      one exists, otherwise truncate via prune fake cluster (just decrease i_size)
*/
static int
prune_cryptcompress(struct inode * inode, loff_t new_size, int update_sd,
		      cloff_t aidx)
{
	int result = 0;
	unsigned nr_zeroes;
	loff_t to_prune;
	loff_t old_size;
	cloff_t ridx;

	hint_t hint;
	lock_handle lh;
	reiser4_slide_t win;
	reiser4_cluster_t clust;

	assert("edward-1140", inode->i_size >= new_size);
	assert("edward-1141", schedulable());
	assert("edward-1142", crc_inode_ok(inode));
	assert("edward-1143", current_blocksize == PAGE_CACHE_SIZE);

	init_lh(&lh);
	hint_init_zero(&hint);
	hint.ext_coord.lh = &lh;

	reiser4_slide_init(&win);
	reiser4_cluster_init(&clust, &win);
	clust.hint = &hint;

	/* rightmost completely truncated cluster */
	ridx = count_to_nrclust(new_size, inode);

	assert("edward-1174", ridx <= aidx);
	old_size = inode->i_size;
	if (ridx != aidx) {
		result = cut_file_items(inode,
					clust_to_off(ridx, inode),
					update_sd,
					clust_to_off(aidx, inode),
					update_cryptcompress_size);
		if (result)
			goto out;
	}
	if (!off_to_cloff(new_size, inode)) {
		/* no partially truncated clusters */
		assert("edward-1145", inode->i_size == new_size);
		goto finish;
	}
	assert("edward-1146", new_size < inode->i_size);

	to_prune = inode->i_size - new_size;

	/* check if partially truncated cluster is fake */
	result = find_real_disk_cluster(inode, &aidx, ridx);
	if (result)
		goto out;
	if (!aidx)
		/* yup, this is fake one */
		goto finish;

	assert("edward-1148", aidx == ridx);

	/* try to capture partially truncated page cluster */
	result = alloc_cluster_pgset(&clust, cluster_nrpages(inode));
	if (result)
		goto out;
	nr_zeroes = (off_to_pgoff(new_size) ?
		     PAGE_CACHE_SIZE - off_to_pgoff(new_size) :
		     0);
	set_window(&clust, &win, inode, new_size, new_size + nr_zeroes);
	win.stat = HOLE_WINDOW;

	assert("edward-1149", clust.index == ridx - 1);

	result = prepare_cluster(inode, 0, 0, &clust, PCL_TRUNCATE);
	if (result)
		goto out;
	assert("edward-1151",
	       clust.dstat == PREP_DISK_CLUSTER ||
	       clust.dstat == UNPR_DISK_CLUSTER);

	assert("edward-1191", inode->i_size == new_size);
	assert("edward-1206", body_truncate_ok(inode, ridx));
 finish:
	/* drop all the pages that don't have jnodes (i.e. pages
	   which can not be truncated by cut_file_items() because
	   of holes represented by fake disk clusters) including
	   the pages of partially truncated cluster which was
	   released by prepare_cluster() */
	truncate_inode_pages(inode->i_mapping, new_size);
	INODE_SET_FIELD(inode, i_size, new_size);
 out:
	assert("edward-1334", !result);
	assert("edward-1209",
	       pages_truncate_ok(inode, old_size, count_to_nrpages(new_size)));
	assert("edward-1335",
	       jnodes_truncate_ok(inode, count_to_nrclust(new_size, inode)));
	done_lh(&lh);
	put_cluster_handle(&clust, TFM_READ);
	return result;
}

static int
start_truncate_fake(struct inode *inode, cloff_t aidx, loff_t new_size, int update_sd)
{
	int result = 0;
	int bytes;

	if (new_size > inode->i_size) {
		/* append */
		if (inode->i_size < clust_to_off(aidx, inode))
			/* no fake bytes */
			return 0;
		bytes = new_size - inode->i_size;
		INODE_SET_FIELD(inode, i_size, inode->i_size + bytes);
	}
	else {
		/* prune */
		if (inode->i_size <= clust_to_off(aidx, inode))
			/* no fake bytes */
			return 0;
		bytes = inode->i_size - max_count(new_size, clust_to_off(aidx, inode));
		if (!bytes)
			return 0;
		INODE_SET_FIELD(inode, i_size, inode->i_size - bytes);
		/* In the case of fake prune we need to drop page cluster.
		   There are only 2 cases for partially truncated page:
		   1. If is is dirty, therefore it is anonymous
		   (was dirtied via mmap), and will be captured
		   later via ->capture().
		   2. If is clean, therefore it is filled by zeroes.
		   In both cases we don't need to make it dirty and
		   capture here.
		*/
		truncate_inode_pages(inode->i_mapping, inode->i_size);
		assert("edward-1336",
		       jnodes_truncate_ok(inode, count_to_nrclust(inode->i_size, inode)));
	}
	if (update_sd)
		result = update_sd_cryptcompress(inode);
	return result;
}

/* This is called in setattr_cryptcompress when it is used to truncate,
   and in delete_cryptcompress */
static int
cryptcompress_truncate(struct inode *inode, /* old size */
		       loff_t new_size, /* new size */
		       int update_sd)
{
	int result;
	cloff_t aidx;

	result = find_fake_appended(inode, &aidx);
	if (result)
		return result;
	assert("edward-1208",
	       ergo(aidx > 0, inode->i_size > clust_to_off(aidx - 1, inode)));

	result = start_truncate_fake(inode, aidx, new_size, update_sd);
	if (result)
		return result;
	if (inode->i_size == new_size)
		/* nothing to truncate anymore */
		return 0;
	return (inode->i_size < new_size ?
		cryptcompress_append_hole(inode, new_size) :
		prune_cryptcompress(inode, new_size, update_sd, aidx));
}

/* plugin->u.file.truncate */
reiser4_internal int
truncate_cryptcompress(struct inode *inode, loff_t new_size)
{
	return 0;
}

/* page cluser is anonymous if it contains at least one anonymous page */
static int
capture_anonymous_cluster(reiser4_cluster_t * clust, struct inode * inode)
{
	int result;

	assert("edward-1073", clust != NULL);
	assert("edward-1074", inode != NULL);
	assert("edward-1075", clust->dstat == INVAL_DISK_CLUSTER);

	result = prepare_cluster(inode, 0, 0, clust, PCL_APPEND);
	if (result)
		return result;
	set_cluster_pages_dirty(clust);

	result = try_capture_cluster(clust, inode);
	put_hint_cluster(clust, inode, ZNODE_WRITE_LOCK);
	if (result)
		release_cluster_pages_and_jnode(clust);
	return result;
}

#define MAX_CLUSTERS_TO_CAPTURE(inode)      (1024 >> inode_cluster_shift(inode))

/* read lock should be acquired */
static int
capture_anonymous_clusters(struct address_space * mapping, pgoff_t * index, int to_capture)
{
	int result = 0;
	int found;
	int progress = 0;
	struct page * page = NULL;
	hint_t hint;
	lock_handle lh;
	reiser4_cluster_t clust;

	assert("edward-1127", mapping != NULL);
	assert("edward-1128", mapping->host != NULL);

	init_lh(&lh);
	hint_init_zero(&hint);
	hint.ext_coord.lh = &lh;
	reiser4_cluster_init(&clust, 0);
	clust.hint = &hint;

	result = alloc_cluster_pgset(&clust, cluster_nrpages(mapping->host));
	if (result)
		goto out;

	while (to_capture > 0) {
		found = find_get_pages_tag(mapping, index, PAGECACHE_TAG_REISER4_MOVED, 1, &page);
		if (!found) {
			*index = (pgoff_t) - 1;
			break;
		}
		assert("edward-1109", page != NULL);

		move_cluster_forward(&clust, mapping->host, page->index, &progress);
		result = capture_anonymous_cluster(&clust, mapping->host);
		page_cache_release(page);
		if (result)
			break;
		to_capture --;
	}
	if (result) {
		warning("edward-1077", "Cannot capture anon pages: result=%i (captured=%d)\n",
			result,
			((__u32)MAX_CLUSTERS_TO_CAPTURE(mapping->host)) - to_capture);
	} else {
		/* something had to be found */
		assert("edward-1078", to_capture <= MAX_CLUSTERS_TO_CAPTURE(mapping->host));
		if (to_capture <= 0)
			/* there may be left more pages */
			__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
	}
 out:
	done_lh(&lh);
	put_cluster_handle(&clust, TFM_READ);
	return result;
}

/* Check mapping for existence of not captured dirty pages.
   This returns !0 if either page tree contains pages tagged
   PAGECACHE_TAG_REISER4_MOVED */
static int
crc_inode_has_anon_pages(struct inode *inode)
{
	return mapping_tagged(inode->i_mapping, PAGECACHE_TAG_REISER4_MOVED);
}

/* plugin->u.file.capture */
reiser4_internal int
capture_cryptcompress(struct inode *inode, struct writeback_control *wbc)
{
	int result;
	int to_capture;
	pgoff_t nrpages;
	pgoff_t index = 0;
	cryptcompress_info_t * info;

	if (!crc_inode_has_anon_pages(inode))
		return 0;

	info = cryptcompress_inode_data(inode);
	nrpages = count_to_nrpages(i_size_read(inode));

	if (wbc->sync_mode != WB_SYNC_ALL)
		to_capture = min_count(wbc->nr_to_write, MAX_CLUSTERS_TO_CAPTURE(inode));
	else
		to_capture = MAX_CLUSTERS_TO_CAPTURE(inode);
	do {
		reiser4_context ctx;

		if (is_in_reiser4_context()) {
			/* FIXME-EDWARD: REMOVEME */
			all_grabbed2free();

			/* It can be in the context of write system call from
			   balance_dirty_pages() */
			if (down_read_trylock(&info->lock) == 0) {
				result = RETERR(-EBUSY);
				break;
			}
		} else
			down_read(&info->lock);

		init_context(&ctx, inode->i_sb);
		ctx.nobalance = 1;

		assert("edward-1079", lock_stack_isclean(get_current_lock_stack()));

		LOCK_CNT_INC(inode_sem_r);

		result = capture_anonymous_clusters(inode->i_mapping, &index, to_capture);

		up_read(&info->lock);

		LOCK_CNT_DEC(inode_sem_r);

		if (result != 0 || wbc->sync_mode != WB_SYNC_ALL) {
			reiser4_exit_context(&ctx);
			break;
		}
		result = txnmgr_force_commit_all(inode->i_sb, 0);
		reiser4_exit_context(&ctx);
	} while (result == 0 && index < nrpages);

	return result;
}

/* plugin->u.file.mmap */
reiser4_internal int
mmap_cryptcompress(struct file * file, struct vm_area_struct * vma)
{
	return -ENOSYS;
	//return generic_file_mmap(file, vma);
}


/* plugin->u.file.release */
/* plugin->u.file.get_block */
/* This function is used for ->bmap() VFS method in reiser4 address_space_operations */
reiser4_internal int
get_block_cryptcompress(struct inode *inode, sector_t block, struct buffer_head *bh_result, int create UNUSED_ARG)
{
	if (current_blocksize != inode_cluster_size(inode))
		return RETERR(-EINVAL);
	else {
		int result;
		reiser4_key key;
		hint_t hint;
		lock_handle lh;
		item_plugin *iplug;

		assert("edward-1166", 0);
		assert("edward-420", create == 0);
		key_by_inode_cryptcompress(inode, (loff_t)block * current_blocksize, &key);
		init_lh(&lh);
		hint_init_zero(&hint);
		hint.ext_coord.lh = &lh;
		result = find_cluster_item(&hint, &key, ZNODE_READ_LOCK, 0, FIND_EXACT, 0);
		if (result != CBK_COORD_FOUND) {
			done_lh(&lh);
			return result;
		}
		result = zload(hint.ext_coord.coord.node);
		if (unlikely(result)) {
			done_lh(&lh);
			return result;
		}
		iplug = item_plugin_by_coord(&hint.ext_coord.coord);

		assert("edward-421", iplug == item_plugin_by_id(CTAIL_ID));

		if (iplug->s.file.get_block)
			result = iplug->s.file.get_block(&hint.ext_coord.coord, block, bh_result);
		else
			result = RETERR(-EINVAL);

		zrelse(hint.ext_coord.coord.node);
		done_lh(&lh);
		return result;
	}
}

/* plugin->u.file.delete method
   see plugin.h for description */
reiser4_internal int
delete_cryptcompress(struct inode *inode)
{
	int result;

	assert("edward-429", inode->i_nlink == 0);

	if (inode->i_size) {
		result = cryptcompress_truncate(inode, 0, 0);
		if (result) {
			warning("edward-430", "cannot truncate cryptcompress file  %lli: %i",
				(unsigned long long)get_inode_oid(inode), result);
			return result;
		}
	}
	return delete_object(inode, 0);
}

/* plugin->u.file.pre_delete method
   see plugin.h for description */
reiser4_internal int
pre_delete_cryptcompress(struct inode *inode)
{
	return cryptcompress_truncate(inode, 0, 0);
}

/* plugin->u.file.setattr method
   see plugin.h for description */
reiser4_internal int
setattr_cryptcompress(struct inode *inode,	/* Object to change attributes */
		      struct iattr *attr /* change description */ )
{
	int result;

	result = check_cryptcompress(inode);
	if (result)
		return result;
	if (attr->ia_valid & ATTR_SIZE) {
		/* EDWARD-FIXME-HANS: VS-FIXME-HANS:
		   Q: this case occurs when? truncate?
		   A: yes

		   Q: If so, why isn't this code in truncate itself instead of here?

		   A: because vfs calls fs's truncate after it has called truncate_inode_pages to get rid of pages
		   corresponding to part of file being truncated. In reiser4 it may cause existence of unallocated
		   extents which do not have jnodes. Flush code does not expect that. Solution of this problem is
		   straightforward. As vfs's truncate is implemented using setattr operation (common implementaion of
		   which calls truncate_inode_pages and fs's truncate in case when size of file changes) - it seems
		   reasonable to have reiser4_setattr which will take care of removing pages, jnodes and extents
		   simultaneously in case of truncate.
		   Q: do you think implementing truncate using setattr is ugly,
		   and vfs needs improving, or is there some sense in which this is a good design?

		   A: VS-FIXME-HANS:
		*/

		/* truncate does reservation itself and requires exclusive access obtained */
		if (inode->i_size != attr->ia_size) {
			loff_t old_size;
			cryptcompress_info_t * info = cryptcompress_inode_data(inode);

			down_write(&info->lock);
			LOCK_CNT_INC(inode_sem_w);

			inode_check_scale(inode, inode->i_size, attr->ia_size);

			old_size = inode->i_size;

			result = cryptcompress_truncate(inode, attr->ia_size, 1/* update stat data */);
			if (result) {
				warning("edward-1192", "truncate_cryptcompress failed: oid %lli, "
					"old size %lld, new size %lld, retval %d",
					(unsigned long long)get_inode_oid(inode),
					old_size, attr->ia_size, result);
			}
			up_write(&info->lock);
			LOCK_CNT_DEC(inode_sem_w);
		} else
			result = 0;
	} else
		result = setattr_common(inode, attr);
	return result;
}

static int
save_len_cryptcompress_plugin(struct inode * inode, reiser4_plugin * plugin)
{
	assert("edward-457", inode != NULL);
	assert("edward-458", plugin != NULL);
	assert("edward-459", plugin->h.id == CRC_FILE_PLUGIN_ID);
	return 0;
}

static int
load_cryptcompress_plugin(struct inode * inode, reiser4_plugin * plugin, char **area, int *len)
{
	assert("edward-455", inode != NULL);
	assert("edward-456", (reiser4_inode_data(inode)->pset != NULL));

	plugin_set_file(&reiser4_inode_data(inode)->pset, file_plugin_by_id(CRC_FILE_PLUGIN_ID));
	return 0;
}

static int
change_crypto_file(struct inode * inode, reiser4_plugin * plugin)
{
	/* cannot change object plugin of already existing object */
	return RETERR(-EINVAL);
}

struct reiser4_plugin_ops cryptcompress_plugin_ops = {
	.load      = load_cryptcompress_plugin,
	.save_len  = save_len_cryptcompress_plugin,
	.save      = NULL,
	.alignment = 8,
	.change    = change_crypto_file
};

/*
  Local variables:
  c-indentation-style: "K&R"
  mode-name: "LC"
  c-basic-offset: 8
  tab-width: 8
  fill-column: 120
  scroll-step: 1
  End:
*/
