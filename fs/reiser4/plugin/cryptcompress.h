/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */
/* See http://www.namesys.com/cryptcompress_design.html */

#if !defined( __FS_REISER4_CRYPTCOMPRESS_H__ )
#define __FS_REISER4_CRYPTCOMPRESS_H__


#include <linux/pagemap.h>
#include <linux/crypto.h>

#define MIN_CLUSTER_SIZE PAGE_CACHE_SIZE
#define MAX_CLUSTER_SHIFT 4
#define DEFAULT_CLUSTER_SHIFT 0
#define MIN_SIZE_FOR_COMPRESSION 64
#define MIN_CRYPTO_BLOCKSIZE 8
#define CLUSTER_MAGIC_SIZE (MIN_CRYPTO_BLOCKSIZE >> 1)

/* cluster status */
typedef enum {
	DATA_CLUSTER = 0,
	HOLE_CLUSTER = 1, /* indicates hole for write ops */
	FAKE_CLUSTER = 2  /* indicates absence of disk cluster for read ops */
} reiser4_cluster_status;

/* reiser4 transforms */
typedef enum {
	CRYPTO_TFM,
	DIGEST_TFM,
	COMPRESS_TFM,
	LAST_TFM
} reiser4_tfm;

/* Write modes for item conversion in flush squeeze phase */
typedef enum {
	CRC_FIRST_ITEM = 1,
	CRC_APPEND_ITEM = 2,
	CRC_OVERWRITE_ITEM = 3,
	CRC_CUT_ITEM = 4
} crc_write_mode_t;

/* reiser4 cluster manager transforms page cluster into disk cluster (and back) via
   input/output stream of crypto/compression algorithms using copy on clustering.
   COC means that page cluster will be assembled into united stream before compression,
   and output stream of decompression algorithm will be split into pages.
   This manager consists mostly of operations on the following object which represents
   one cluster:
*/
typedef struct reiser4_cluster{
	__u8 * buf;      /* pointer to input/output stream of crypto/compression algorithm */
	size_t bsize;    /* size of the buffer allocated for the stream */
	size_t len;      /* actual length of the stream above */
	int nr_pages;    /* number of attached pages */
	struct page ** pages; /* attached pages */
	struct file * file;
	hint_t * hint;
	reiser4_cluster_status stat;
	/* sliding frame of cluster size in loff_t-space to translate main file 'offsets'
	   like read/write position, size, new size (for truncate), etc.. into number
	   of pages, cluster status, etc..*/
	unsigned long index; /* cluster index, coord of the frame */
	unsigned off;    /* offset we want to read/write/truncate from */
	unsigned count;  /* bytes to read/write/truncate */
	unsigned delta;  /* bytes of user's data to append to the hole */
} reiser4_cluster_t;

/* security attributes supposed to be stored on disk
   are loaded by stat-data methods (see plugin/item/static_stat.c */
typedef struct crypto_stat {
	__u8 * keyid;  /* pointer to a fingerprint */
	__u16 keysize; /* key size, bits */
} crypto_stat_t;

/* cryptcompress specific part of reiser4_inode */
typedef struct cryptcompress_info {
	struct crypto_tfm *tfm[LAST_TFM];
	__u32 * expkey;
} cryptcompress_info_t;

cryptcompress_info_t *cryptcompress_inode_data(const struct inode * inode);
int equal_to_rdk(znode *, const reiser4_key *);
int equal_to_ldk(znode *, const reiser4_key *);
int goto_right_neighbor(coord_t *, lock_handle *);
int load_file_hint(struct file *, hint_t *, lock_handle *);
void save_file_hint(struct file *, const hint_t *);

/* declarations of functions implementing methods of cryptcompress object plugin */
void init_inode_data_cryptcompress(struct inode *inode, reiser4_object_create_data *crd, int create);
int create_cryptcompress(struct inode *, struct inode *, reiser4_object_create_data *);
int open_cryptcompress(struct inode * inode, struct file * file);
int truncate_cryptcompress(struct inode *, loff_t size);
int readpage_cryptcompress(void *, struct page *);
int capture_cryptcompress(struct inode *inode, const struct writeback_control *wbc, long *);
ssize_t write_cryptcompress(struct file *, const char *buf, size_t size, loff_t *off);
int release_cryptcompress(struct inode *inode, struct file *);
int mmap_cryptcompress(struct file *, struct vm_area_struct *vma);
int get_block_cryptcompress(struct inode *, sector_t block, struct buffer_head *bh_result, int create);
int flow_by_inode_cryptcompress(struct inode *, char *buf, int user, loff_t, loff_t, rw_op, flow_t *);
int key_by_inode_cryptcompress(struct inode *, loff_t off, reiser4_key *);
int delete_cryptcompress(struct inode *);
int owns_item_cryptcompress(const struct inode *, const coord_t *);
int setattr_cryptcompress(struct inode *, struct iattr *);
void readpages_cryptcompress(struct file *, struct address_space *, struct list_head *pages);
void init_inode_data_cryptcompress(struct inode *, reiser4_object_create_data *, int create);
int pre_delete_cryptcompress(struct inode *);
void hint_init_zero(hint_t *, lock_handle *);
void destroy_inode_cryptcompress(struct inode * inode);
int crc_inode_ok(struct inode * inode);

static inline struct crypto_tfm *
inode_get_tfm (struct inode * inode, reiser4_tfm tfm)
{
	return cryptcompress_inode_data(inode)->tfm[tfm];
}

static inline struct crypto_tfm *
inode_get_crypto (struct inode * inode)
{
	return (inode_get_tfm(inode, CRYPTO_TFM));
}

static inline struct crypto_tfm *
inode_get_digest (struct inode * inode)
{
	return (inode_get_tfm(inode, DIGEST_TFM));
}

static inline unsigned int
crypto_blocksize(struct inode * inode)
{
	assert("edward-758", inode_get_tfm(inode, CRYPTO_TFM) != NULL);
	return crypto_tfm_alg_blocksize(inode_get_tfm(inode, CRYPTO_TFM));
}

#define REGISTER_NONE_ALG(ALG, TFM)                                  \
static int alloc_none_ ## ALG (struct inode * inode)                 \
{                                                                    \
        cryptcompress_info_t * info;                                 \
        assert("edward-760", inode != NULL);                         \
	                                                             \
	info = cryptcompress_inode_data(inode);                      \
                                                                     \
                                                                     \
	cryptcompress_inode_data(inode)->tfm[TFM ## _TFM] = NULL;    \
	return 0;                                                    \
                                                                     \
}                                                                    \
static void free_none_ ## ALG (struct inode * inode)                 \
{                                                                    \
        cryptcompress_info_t * info;                                 \
        assert("edward-761", inode != NULL);                         \
	                                                             \
	info = cryptcompress_inode_data(inode);                      \
	                                                             \
	assert("edward-762", info != NULL);                          \
	                                                             \
	info->tfm[TFM ## _TFM] = NULL;                               \
}

#endif /* __FS_REISER4_CRYPTCOMPRESS_H__ */

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
