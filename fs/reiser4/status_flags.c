/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Functions that deal with reiser4 status block, query status and update it, if needed */

#include <linux/page-flags.h>
#include <linux/bio.h>
#include <linux/highmem.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include "debug.h"
#include "dformat.h"
#include "status_flags.h"
#include "super.h"

/* This is our end I/O handler that marks page uptodate if IO was successful. It also
   unconditionally unlocks the page, so we can see that io was done.
   We do not free bio, because we hope to reuse that. */
static int reiser4_status_endio(struct bio *bio, unsigned int bytes_done, int err)
{
	if (bio->bi_size)
		return 1;

	if (test_bit(BIO_UPTODATE, &bio->bi_flags)) {
		SetPageUptodate(bio->bi_io_vec->bv_page);
	} else {
		ClearPageUptodate(bio->bi_io_vec->bv_page);
		SetPageError(bio->bi_io_vec->bv_page);
	}
	unlock_page(bio->bi_io_vec->bv_page);
//	bio_put(bio);
	return 0;
}

/* Initialise status code. This is expected to be called from the disk format
   code. block paremeter is where status block lives. */
reiser4_internal int reiser4_status_init(reiser4_block_nr block)
{
	struct super_block *sb = reiser4_get_current_sb();
	struct reiser4_status *statuspage;
	struct bio *bio;
	struct page *page;

	get_super_private(sb)->status_page = NULL;
	get_super_private(sb)->status_bio = NULL;

	page = alloc_pages(GFP_KERNEL, 0);
	if (!page)
		return -ENOMEM;

	bio = bio_alloc(GFP_KERNEL, 1);
	if (bio != NULL) {
		bio->bi_sector = block * (sb->s_blocksize >> 9);
		bio->bi_bdev = sb->s_bdev;
		bio->bi_io_vec[0].bv_page = page;
		bio->bi_io_vec[0].bv_len = sb->s_blocksize;
		bio->bi_io_vec[0].bv_offset = 0;
		bio->bi_vcnt = 1;
		bio->bi_size = sb->s_blocksize;
		bio->bi_end_io = reiser4_status_endio;
	} else {
		__free_pages(page, 0);
		return -ENOMEM;
	}
	lock_page(page);
	submit_bio(READ, bio);
	blk_run_address_space(get_super_fake(sb)->i_mapping);
	/*blk_run_queues();*/
	wait_on_page_locked(page);
	if ( !PageUptodate(page) ) {
		warning("green-2007", "I/O error while tried to read status page\n");
		return -EIO;
	}

	statuspage = kmap_atomic(page, KM_USER0);
	if ( memcmp( statuspage->magic, REISER4_STATUS_MAGIC, sizeof(REISER4_STATUS_MAGIC)) ) {
		/* Magic does not match. */
		kunmap_atomic(page, KM_USER0);
		warning("green-2008", "Wrong magic in status block\n");
		__free_pages(page, 0);
		bio_put(bio);
		return -EINVAL;
	}
	kunmap_atomic(page, KM_USER0);

	get_super_private(sb)->status_page = page;
	get_super_private(sb)->status_bio = bio;
	return 0;
}

/* Query the status of fs. Returns if the FS can be safely mounted.
   Also if "status" and "extended" parameters are given, it will fill
   actual parts of status from disk there. */
reiser4_internal int reiser4_status_query(u64 *status, u64 *extended)
{
	struct super_block *sb = reiser4_get_current_sb();
	struct reiser4_status *statuspage;
	int retval;

	if ( !get_super_private(sb)->status_page ) { // No status page?
		return REISER4_STATUS_MOUNT_UNKNOWN;
	}
	statuspage = kmap_atomic(get_super_private(sb)->status_page, KM_USER0);
	switch ( (long)d64tocpu(&statuspage->status) ) { // FIXME: this cast is a hack for 32 bit arches to work.
	case REISER4_STATUS_OK:
		retval = REISER4_STATUS_MOUNT_OK;
		break;
	case REISER4_STATUS_CORRUPTED:
		retval = REISER4_STATUS_MOUNT_WARN;
		break;
	case REISER4_STATUS_DAMAGED:
	case REISER4_STATUS_DESTROYED:
	case REISER4_STATUS_IOERROR:
		retval = REISER4_STATUS_MOUNT_RO;
		break;
	default:
		retval = REISER4_STATUS_MOUNT_UNKNOWN;
		break;
	}

	if ( status )
		*status = d64tocpu(&statuspage->status);
	if ( extended )
		*extended = d64tocpu(&statuspage->extended_status);

	kunmap_atomic(get_super_private(sb)->status_page, KM_USER0);
	return retval;
}

/* This function should be called when something bad happens (e.g. from reiser4_panic).
   It fills the status structure and tries to push it to disk. */
reiser4_internal int
reiser4_status_write(u64 status, u64 extended_status, char *message)
{
	struct super_block *sb = reiser4_get_current_sb();
	struct reiser4_status *statuspage;
	struct bio *bio = get_super_private(sb)->status_bio;

	if ( !get_super_private(sb)->status_page ) { // No status page?
		return -1;
	}
	statuspage = kmap_atomic(get_super_private(sb)->status_page, KM_USER0);

	cputod64(status, &statuspage->status);
	cputod64(extended_status, &statuspage->extended_status);
	strncpy(statuspage->texterror, message, REISER4_TEXTERROR_LEN);

#ifdef CONFIG_FRAME_POINTER
#define GETFRAME(no)						\
	cputod64((unsigned long)__builtin_return_address(no),	\
		 &statuspage->stacktrace[no])

	GETFRAME(0);
	GETFRAME(1);
	GETFRAME(2);
	GETFRAME(3);
	GETFRAME(4);
	GETFRAME(5);
	GETFRAME(6);
	GETFRAME(7);
	GETFRAME(8);
	GETFRAME(9);

#undef GETFRAME
#endif
	kunmap_atomic(get_super_private(sb)->status_page, KM_USER0);
	bio->bi_bdev = sb->s_bdev;
	bio->bi_io_vec[0].bv_page = get_super_private(sb)->status_page;
	bio->bi_io_vec[0].bv_len = sb->s_blocksize;
	bio->bi_io_vec[0].bv_offset = 0;
	bio->bi_vcnt = 1;
	bio->bi_size = sb->s_blocksize;
	bio->bi_end_io = reiser4_status_endio;
	lock_page(get_super_private(sb)->status_page); // Safe as nobody should touch our page.
	/* We can block now, but we have no other choice anyway */
	submit_bio(WRITE, bio);
	blk_run_address_space(get_super_fake(sb)->i_mapping);
	/*blk_run_queues();*/ // Now start the i/o.
	return 0; // We do not wait for io to finish.
}

/* Frees the page with status and bio structure. Should be called by disk format at umount time */
reiser4_internal int reiser4_status_finish(void)
{
	struct super_block *sb = reiser4_get_current_sb();

	__free_pages(get_super_private(sb)->status_page, 0);
	get_super_private(sb)->status_page = NULL;
	bio_put(get_super_private(sb)->status_bio);
	get_super_private(sb)->status_bio = NULL;
	return 0;
}

