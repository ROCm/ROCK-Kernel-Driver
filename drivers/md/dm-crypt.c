/*
 * Copyright (C) 2003 Christophe Saout <christophe@saout.de>
 *
 * This file is released under the GPL.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/mempool.h>
#include <linux/slab.h>
#include <linux/crypto.h>
#include <linux/workqueue.h>
#include <asm/atomic.h>
#include <asm/scatterlist.h>

#include "dm.h"

#define PFX	"crypt: "

/*
 * per bio private data
 */
struct crypt_io {
	struct dm_target *target;
	struct bio *bio;
	struct bio *first_clone;
	struct work_struct work;
	atomic_t pending;
	int error;
};

/*
 * context holding the current state of a multi-part conversion
 */
struct convert_context {
	struct bio *bio_in;
	struct bio *bio_out;
	unsigned int offset_in;
	unsigned int offset_out;
	int idx_in;
	int idx_out;
	sector_t sector;
	int write;
};

/*
 * Crypt: maps a linear range of a block device
 * and encrypts / decrypts at the same time.
 */
struct crypt_config {
	struct dm_dev *dev;
	sector_t start;

	/*
	 * pool for per bio private data and
	 * for encryption buffer pages
	 */
	mempool_t *io_pool;
	mempool_t *page_pool;

	/*
	 * crypto related data
	 */
	struct crypto_tfm *tfm;
	sector_t iv_offset;
	int (*iv_generator)(struct crypt_config *cc, u8 *iv, sector_t sector);
	int iv_size;
	int key_size;
	u8 key[0];
};

#define MIN_IOS        256
#define MIN_POOL_PAGES 32
#define MIN_BIO_PAGES  8

static kmem_cache_t *_crypt_io_pool;

/*
 * Mempool alloc and free functions for the page
 */
static void *mempool_alloc_page(int gfp_mask, void *data)
{
	return alloc_page(gfp_mask);
}

static void mempool_free_page(void *page, void *data)
{
	__free_page(page);
}


/*
 * Different IV generation algorithms
 */
static int crypt_iv_plain(struct crypt_config *cc, u8 *iv, sector_t sector)
{
	*(u32 *)iv = cpu_to_le32(sector & 0xffffffff);
	if (cc->iv_size > sizeof(u32) / sizeof(u8))
		memset(iv + (sizeof(u32) / sizeof(u8)), 0,
		       cc->iv_size - (sizeof(u32) / sizeof(u8)));

	return 0;
}

static inline int
crypt_convert_scatterlist(struct crypt_config *cc, struct scatterlist *out,
                          struct scatterlist *in, unsigned int length,
                          int write, sector_t sector)
{
	u8 iv[cc->iv_size];
	int r;

	if (cc->iv_generator) {
		r = cc->iv_generator(cc, iv, sector);
		if (r < 0)
			return r;

		if (write)
			r = crypto_cipher_encrypt_iv(cc->tfm, out, in, length, iv);
		else
			r = crypto_cipher_decrypt_iv(cc->tfm, out, in, length, iv);
	} else {
		if (write)
			r = crypto_cipher_encrypt(cc->tfm, out, in, length);
		else
			r = crypto_cipher_decrypt(cc->tfm, out, in, length);
	}

	return r;
}

static void
crypt_convert_init(struct crypt_config *cc, struct convert_context *ctx,
                   struct bio *bio_out, struct bio *bio_in,
                   sector_t sector, int write)
{
	ctx->bio_in = bio_in;
	ctx->bio_out = bio_out;
	ctx->offset_in = 0;
	ctx->offset_out = 0;
	ctx->idx_in = bio_in ? bio_in->bi_idx : 0;
	ctx->idx_out = bio_out ? bio_out->bi_idx : 0;
	ctx->sector = sector + cc->iv_offset;
	ctx->write = write;
}

/*
 * Encrypt / decrypt data from one bio to another one (can be the same one)
 */
static int crypt_convert(struct crypt_config *cc,
                         struct convert_context *ctx)
{
	int r = 0;

	while(ctx->idx_in < ctx->bio_in->bi_vcnt &&
	      ctx->idx_out < ctx->bio_out->bi_vcnt) {
		struct bio_vec *bv_in = bio_iovec_idx(ctx->bio_in, ctx->idx_in);
		struct bio_vec *bv_out = bio_iovec_idx(ctx->bio_out, ctx->idx_out);
		struct scatterlist sg_in = {
			.page = bv_in->bv_page,
			.offset = bv_in->bv_offset + ctx->offset_in,
			.length = 1 << SECTOR_SHIFT
		};
		struct scatterlist sg_out = {
			.page = bv_out->bv_page,
			.offset = bv_out->bv_offset + ctx->offset_out,
			.length = 1 << SECTOR_SHIFT
		};

		ctx->offset_in += sg_in.length;
		if (ctx->offset_in >= bv_in->bv_len) {
			ctx->offset_in = 0;
			ctx->idx_in++;
		}

		ctx->offset_out += sg_out.length;
		if (ctx->offset_out >= bv_out->bv_len) {
			ctx->offset_out = 0;
			ctx->idx_out++;
		}

		r = crypt_convert_scatterlist(cc, &sg_out, &sg_in, sg_in.length,
		                              ctx->write, ctx->sector);
		if (r < 0)
			break;

		ctx->sector++;
	}

	return r;
}

/*
 * Generate a new unfragmented bio with the given size
 * This should never violate the device limitations
 * May return a smaller bio when running out of pages
 */
static struct bio *
crypt_alloc_buffer(struct crypt_config *cc, unsigned int size,
                   struct bio *base_bio, int *bio_vec_idx)
{
	struct bio *bio;
	int nr_iovecs = dm_div_up(size, PAGE_SIZE);
	int gfp_mask = GFP_NOIO | __GFP_HIGHMEM;
	int flags = current->flags;
	int i;

	/*
	 * Tell VM to act less aggressively and fail earlier.
	 * This is not necessary but increases throughput.
	 * FIXME: Is this really intelligent?
	 */
	current->flags &= ~PF_MEMALLOC;

	if (base_bio)
		bio = bio_clone(base_bio, GFP_NOIO);
	else
		bio = bio_alloc(GFP_NOIO, nr_iovecs);
	if (!bio) {
		if (flags & PF_MEMALLOC)
			current->flags |= PF_MEMALLOC;
		return NULL;
	}

	/* if the last bio was not complete, continue where that one ended */
	bio->bi_idx = *bio_vec_idx;
	bio->bi_vcnt = *bio_vec_idx;
	bio->bi_size = 0;
	bio->bi_flags &= ~(1 << BIO_SEG_VALID);

	/* bio->bi_idx pages have already been allocated */
	size -= bio->bi_idx * PAGE_SIZE;

	for(i = bio->bi_idx; i < nr_iovecs; i++) {
		struct bio_vec *bv = bio_iovec_idx(bio, i);

		bv->bv_page = mempool_alloc(cc->page_pool, gfp_mask);
		if (!bv->bv_page)
			break;

		/*
		 * if additional pages cannot be allocated without waiting,
		 * return a partially allocated bio, the caller will then try
		 * to allocate additional bios while submitting this partial bio
		 */
		if ((i - bio->bi_idx) == (MIN_BIO_PAGES - 1))
			gfp_mask = (gfp_mask | __GFP_NOWARN) & ~__GFP_WAIT;

		bv->bv_offset = 0;
		if (size > PAGE_SIZE)
			bv->bv_len = PAGE_SIZE;
		else
			bv->bv_len = size;

		bio->bi_size += bv->bv_len;
		bio->bi_vcnt++;
		size -= bv->bv_len;
	}

	if (flags & PF_MEMALLOC)
		current->flags |= PF_MEMALLOC;

	if (!bio->bi_size) {
		bio_put(bio);
		return NULL;
	}

	/*
	 * Remember the last bio_vec allocated to be able
	 * to correctly continue after the splitting.
	 */
	*bio_vec_idx = bio->bi_vcnt;

	return bio;
}

static void crypt_free_buffer_pages(struct crypt_config *cc,
                                    struct bio *bio, unsigned int bytes)
{
	unsigned int start, end;
	struct bio_vec *bv;
	int i;

	/*
	 * This is ugly, but Jens Axboe thinks that using bi_idx in the
	 * endio function is too dangerous at the moment, so I calculate the
	 * correct position using bi_vcnt and bi_size.
	 * The bv_offset and bv_len fields might already be modified but we
	 * know that we always allocated whole pages.
	 * A fix to the bi_idx issue in the kernel is in the works, so
	 * we will hopefully be able to revert to the cleaner solution soon.
	 */
	i = bio->bi_vcnt - 1;
	bv = bio_iovec_idx(bio, i);
	end = (i << PAGE_SHIFT) + (bv->bv_offset + bv->bv_len) - bio->bi_size;
	start = end - bytes;

	start >>= PAGE_SHIFT;
	if (!bio->bi_size)
		end = bio->bi_vcnt;
	else
		end >>= PAGE_SHIFT;

	for(i = start; i < end; i++) {
		bv = bio_iovec_idx(bio, i);
		BUG_ON(!bv->bv_page);
		mempool_free(bv->bv_page, cc->page_pool);
		bv->bv_page = NULL;
	}
}

/*
 * One of the bios was finished. Check for completion of
 * the whole request and correctly clean up the buffer.
 */
static void dec_pending(struct crypt_io *io, int error)
{
	struct crypt_config *cc = (struct crypt_config *) io->target->private;

	if (error < 0)
		io->error = error;

	if (!atomic_dec_and_test(&io->pending))
		return;

	if (io->first_clone)
		bio_put(io->first_clone);

	bio_endio(io->bio, io->bio->bi_size, io->error);

	mempool_free(io, cc->io_pool);
}

/*
 * kcryptd:
 *
 * Needed because it would be very unwise to do decryption in an
 * interrupt context, so bios returning from read requests get
 * queued here.
 */
static struct workqueue_struct *_kcryptd_workqueue;

static void kcryptd_do_work(void *data)
{
	struct crypt_io *io = (struct crypt_io *) data;
	struct crypt_config *cc = (struct crypt_config *) io->target->private;
	struct convert_context ctx;
	int r;

	crypt_convert_init(cc, &ctx, io->bio, io->bio,
	                   io->bio->bi_sector - io->target->begin, 0);
	r = crypt_convert(cc, &ctx);

	dec_pending(io, r);
}

static void kcryptd_queue_io(struct crypt_io *io)
{
	INIT_WORK(&io->work, kcryptd_do_work, io);
	queue_work(_kcryptd_workqueue, &io->work);
}

/*
 * Decode key from its hex representation
 */
static int crypt_decode_key(u8 *key, char *hex, int size)
{
	char buffer[3];
	char *endp;
	int i;

	buffer[2] = '\0';

	for(i = 0; i < size; i++) {
		buffer[0] = *hex++;
		buffer[1] = *hex++;

		key[i] = (u8)simple_strtoul(buffer, &endp, 16);

		if (endp != &buffer[2])
			return -EINVAL;
	}

	if (*hex != '\0')
		return -EINVAL;

	return 0;
}

/*
 * Encode key into its hex representation
 */
static void crypt_encode_key(char *hex, u8 *key, int size)
{
	int i;

	for(i = 0; i < size; i++) {
		sprintf(hex, "%02x", *key);
		hex += 2;
		key++;
	}
}

/*
 * Construct an encryption mapping:
 * <cipher> <key> <iv_offset> <dev_path> <start>
 */
static int crypt_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct crypt_config *cc;
	struct crypto_tfm *tfm;
	char *tmp;
	char *cipher;
	char *mode;
	int crypto_flags;
	int key_size;

	if (argc != 5) {
		ti->error = PFX "Not enough arguments";
		return -EINVAL;
	}

	tmp = argv[0];
	cipher = strsep(&tmp, "-");
	mode = strsep(&tmp, "-");

	if (tmp)
		DMWARN(PFX "Unexpected additional cipher options");

	key_size = strlen(argv[1]) >> 1;

	cc = kmalloc(sizeof(*cc) + key_size * sizeof(u8), GFP_KERNEL);
	if (cc == NULL) {
		ti->error =
			PFX "Cannot allocate transparent encryption context";
		return -ENOMEM;
	}

	if (!mode || strcmp(mode, "plain") == 0)
		cc->iv_generator = crypt_iv_plain;
	else if (strcmp(mode, "ecb") == 0)
		cc->iv_generator = NULL;
	else {
		ti->error = PFX "Invalid chaining mode";
		goto bad1;
	}

	if (cc->iv_generator)
		crypto_flags = CRYPTO_TFM_MODE_CBC;
	else
		crypto_flags = CRYPTO_TFM_MODE_ECB;

	tfm = crypto_alloc_tfm(cipher, crypto_flags);
	if (!tfm) {
		ti->error = PFX "Error allocating crypto tfm";
		goto bad1;
	}
	if (crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER) {
		ti->error = PFX "Expected cipher algorithm";
		goto bad2;
	}

	if (tfm->crt_cipher.cit_decrypt_iv && tfm->crt_cipher.cit_encrypt_iv)
		/* at least a 32 bit sector number should fit in our buffer */
		cc->iv_size = max(crypto_tfm_alg_ivsize(tfm),
		                  (unsigned int)(sizeof(u32) / sizeof(u8)));
	else {
		cc->iv_size = 0;
		if (cc->iv_generator) {
			DMWARN(PFX "Selected cipher does not support IVs");
			cc->iv_generator = NULL;
		}
	}

	cc->io_pool = mempool_create(MIN_IOS, mempool_alloc_slab,
				     mempool_free_slab, _crypt_io_pool);
	if (!cc->io_pool) {
		ti->error = PFX "Cannot allocate crypt io mempool";
		goto bad2;
	}

	cc->page_pool = mempool_create(MIN_POOL_PAGES, mempool_alloc_page,
				       mempool_free_page, NULL);
	if (!cc->page_pool) {
		ti->error = PFX "Cannot allocate page mempool";
		goto bad3;
	}

	cc->tfm = tfm;
	cc->key_size = key_size;
	if ((key_size == 0 && strcmp(argv[1], "-") != 0)
	    || crypt_decode_key(cc->key, argv[1], key_size) < 0) {
		ti->error = PFX "Error decoding key";
		goto bad4;
	}

	if (tfm->crt_cipher.cit_setkey(tfm, cc->key, key_size) < 0) {
		ti->error = PFX "Error setting key";
		goto bad4;
	}

	if (sscanf(argv[2], SECTOR_FORMAT, &cc->iv_offset) != 1) {
		ti->error = PFX "Invalid iv_offset sector";
		goto bad4;
	}

	if (sscanf(argv[4], SECTOR_FORMAT, &cc->start) != 1) {
		ti->error = PFX "Invalid device sector";
		goto bad4;
	}

	if (dm_get_device(ti, argv[3], cc->start, ti->len,
	                  dm_table_get_mode(ti->table), &cc->dev)) {
		ti->error = PFX "Device lookup failed";
		goto bad4;
	}

	ti->private = cc;
	return 0;

bad4:
	mempool_destroy(cc->page_pool);
bad3:
	mempool_destroy(cc->io_pool);
bad2:
	crypto_free_tfm(tfm);
bad1:
	kfree(cc);
	return -EINVAL;
}

static void crypt_dtr(struct dm_target *ti)
{
	struct crypt_config *cc = (struct crypt_config *) ti->private;

	mempool_destroy(cc->page_pool);
	mempool_destroy(cc->io_pool);

	crypto_free_tfm(cc->tfm);
	dm_put_device(ti, cc->dev);
	kfree(cc);
}

static int crypt_endio(struct bio *bio, unsigned int done, int error)
{
	struct crypt_io *io = (struct crypt_io *) bio->bi_private;
	struct crypt_config *cc = (struct crypt_config *) io->target->private;

	if (bio_data_dir(bio) == WRITE) {
		/*
		 * free the processed pages, even if
		 * it's only a partially completed write
		 */
		crypt_free_buffer_pages(cc, bio, done);
	}

	if (bio->bi_size)
		return 1;

	bio_put(bio);

	/*
	 * successful reads are decrypted by the worker thread
	 */
	if ((bio_data_dir(bio) == READ)
	    && bio_flagged(bio, BIO_UPTODATE)) {
		kcryptd_queue_io(io);
		return 0;
	}

	dec_pending(io, error);
	return error;
}

static inline struct bio *
crypt_clone(struct crypt_config *cc, struct crypt_io *io, struct bio *bio,
            sector_t sector, int *bvec_idx, struct convert_context *ctx)
{
	struct bio *clone;

	if (bio_data_dir(bio) == WRITE) {
		clone = crypt_alloc_buffer(cc, bio->bi_size,
                                 io->first_clone, bvec_idx);
		if (clone) {
			ctx->bio_out = clone;
			if (crypt_convert(cc, ctx) < 0) {
				crypt_free_buffer_pages(cc, clone,
				                        clone->bi_size);
				bio_put(clone);
				return NULL;
			}
		}
	} else {
		/*
		 * The block layer might modify the bvec array, so always
		 * copy the required bvecs because we need the original
		 * one in order to decrypt the whole bio data *afterwards*.
		 */
		clone = bio_alloc(GFP_NOIO, bio_segments(bio));
		if (clone) {
			clone->bi_idx = 0;
			clone->bi_vcnt = bio_segments(bio);
			clone->bi_size = bio->bi_size;
			memcpy(clone->bi_io_vec, bio_iovec(bio),
			       sizeof(struct bio_vec) * clone->bi_vcnt);
		}
	}

	if (!clone)
		return NULL;

	clone->bi_private = io;
	clone->bi_end_io = crypt_endio;
	clone->bi_bdev = cc->dev->bdev;
	clone->bi_sector = cc->start + sector;
	clone->bi_rw = bio->bi_rw;

	return clone;
}

static int crypt_map(struct dm_target *ti, struct bio *bio,
		     union map_info *map_context)
{
	struct crypt_config *cc = (struct crypt_config *) ti->private;
	struct crypt_io *io = mempool_alloc(cc->io_pool, GFP_NOIO);
	struct convert_context ctx;
	struct bio *clone;
	unsigned int remaining = bio->bi_size;
	sector_t sector = bio->bi_sector - ti->begin;
	int bvec_idx = 0;

	io->target = ti;
	io->bio = bio;
	io->first_clone = NULL;
	io->error = 0;
	atomic_set(&io->pending, 1); /* hold a reference */

	if (bio_data_dir(bio) == WRITE)
		crypt_convert_init(cc, &ctx, NULL, bio, sector, 1);

	/*
	 * The allocated buffers can be smaller than the whole bio,
	 * so repeat the whole process until all the data can be handled.
	 */
	while (remaining) {
		clone = crypt_clone(cc, io, bio, sector, &bvec_idx, &ctx);
		if (!clone)
			goto cleanup;

		if (!io->first_clone) {
			/*
			 * hold a reference to the first clone, because it
			 * holds the bio_vec array and that can't be freed
			 * before all other clones are released
			 */
			bio_get(clone);
			io->first_clone = clone;
		}
		atomic_inc(&io->pending);

		remaining -= clone->bi_size;
		sector += bio_sectors(clone);

		generic_make_request(clone);

		/* out of memory -> run queues */
		if (remaining)
			blk_congestion_wait(bio_data_dir(clone), HZ/100);
	}

	/* drop reference, clones could have returned before we reach this */
	dec_pending(io, 0);
	return 0;

cleanup:
	if (io->first_clone) {
		dec_pending(io, -ENOMEM);
		return 0;
	}

	/* if no bio has been dispatched yet, we can directly return the error */
	mempool_free(io, cc->io_pool);
	return -ENOMEM;
}

static int crypt_status(struct dm_target *ti, status_type_t type,
			char *result, unsigned int maxlen)
{
	struct crypt_config *cc = (struct crypt_config *) ti->private;
	char buffer[32];
	const char *cipher;
	const char *mode = NULL;
	int offset;

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		cipher = crypto_tfm_alg_name(cc->tfm);

		switch(cc->tfm->crt_cipher.cit_mode) {
		case CRYPTO_TFM_MODE_CBC:
			mode = "plain";
			break;
		case CRYPTO_TFM_MODE_ECB:
			mode = "ecb";
			break;
		default:
			BUG();
		}

		snprintf(result, maxlen, "%s-%s ", cipher, mode);
		offset = strlen(result);

		if (cc->key_size > 0) {
			if ((maxlen - offset) < ((cc->key_size << 1) + 1))
				return -ENOMEM;

			crypt_encode_key(result + offset, cc->key, cc->key_size);
			offset += cc->key_size << 1;
		} else {
			if (offset >= maxlen)
				return -ENOMEM;
			result[offset++] = '-';
		}

		format_dev_t(buffer, cc->dev->bdev->bd_dev);
		snprintf(result + offset, maxlen - offset, " " SECTOR_FORMAT
		         " %s " SECTOR_FORMAT, cc->iv_offset,
		         buffer, cc->start);
		break;
	}
	return 0;
}

static struct target_type crypt_target = {
	.name   = "crypt",
	.version= {1, 0, 0},
	.module = THIS_MODULE,
	.ctr    = crypt_ctr,
	.dtr    = crypt_dtr,
	.map    = crypt_map,
	.status = crypt_status,
};

static int __init dm_crypt_init(void)
{
	int r;

	_crypt_io_pool = kmem_cache_create("dm-crypt_io",
	                                   sizeof(struct crypt_io),
	                                   0, 0, NULL, NULL);
	if (!_crypt_io_pool)
		return -ENOMEM;

	_kcryptd_workqueue = create_workqueue("kcryptd");
	if (!_kcryptd_workqueue) {
		r = -ENOMEM;
		DMERR(PFX "couldn't create kcryptd");
		goto bad1;
	}

	r = dm_register_target(&crypt_target);
	if (r < 0) {
		DMERR(PFX "register failed %d", r);
		goto bad2;
	}

	return 0;

bad2:
	destroy_workqueue(_kcryptd_workqueue);
bad1:
	kmem_cache_destroy(_crypt_io_pool);
	return r;
}

static void __exit dm_crypt_exit(void)
{
	int r = dm_unregister_target(&crypt_target);

	if (r < 0)
		DMERR(PFX "unregister failed %d", r);

	destroy_workqueue(_kcryptd_workqueue);
	kmem_cache_destroy(_crypt_io_pool);
}

module_init(dm_crypt_init);
module_exit(dm_crypt_exit);

MODULE_AUTHOR("Christophe Saout <christophe@saout.de>");
MODULE_DESCRIPTION(DM_NAME " target for transparent encryption / decryption");
MODULE_LICENSE("GPL");
