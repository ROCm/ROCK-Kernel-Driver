/*
 * New 2.5 block I/O model
 *
 * Copyright (C) 2001 Jens Axboe <axboe@suse.de>
 *
 * This program is free software; you can redistribute it and/or mo
 * it under the terms of the GNU General Public License as publishe
 * the Free Software Foundation; either version 2 of the License, o
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of

 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public Licens
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-
 */
#ifndef __LINUX_BIO_H
#define __LINUX_BIO_H

#define BIO_DEBUG

#ifdef BIO_DEBUG
#define BIO_BUG_ON	BUG_ON
#else
#define BIO_BUG_ON
#endif

/*
 * hash profiling stuff..
 */
#define BIO_HASH_PROFILING

#define BLKHASHPROF	_IOR(0x12,108,sizeof(struct bio_hash_stats))
#define BLKHASHCLEAR	_IO(0x12,109)

#define MAX_PROFILE_BUCKETS	64

struct bio_hash_stats {
	atomic_t nr_lookups;
	atomic_t nr_hits;
	atomic_t nr_inserts;
	atomic_t nr_entries;
	atomic_t max_entries;
	atomic_t max_bucket_size;
	atomic_t bucket_size[MAX_PROFILE_BUCKETS + 1];
};

/*
 * was unsigned short, but we might as well be ready for > 64kB I/O pages
 */
struct bio_vec {
	struct page	*bv_page;
	unsigned int	bv_len;
	unsigned int	bv_offset;
};

struct bio_vec_list {
	unsigned int	bvl_cnt;	/* how may bio_vec's */
	unsigned int	bvl_idx;	/* current index into bvl_vec */
	unsigned int	bvl_size;	/* total size in bytes */
	unsigned int	bvl_max;	/* max bvl_vecs we can hold, used
					   as index into pool */
	struct bio_vec	bvl_vec[0];	/* the iovec array */
};

typedef struct bio_hash_s {
	struct bio_hash_s *next_hash;
	struct bio_hash_s **pprev_hash;
	unsigned long valid_counter;
} bio_hash_t;

struct bio_hash_bucket {
	rwlock_t lock;
	bio_hash_t *hash;
} __attribute__((__aligned__(16)));

#define BIO_HASH_BITS	(bio_hash_bits)
#define BIO_HASH_SIZE	(1UL << BIO_HASH_BITS)

/*
 * shamelessly stolen from the list.h implementation
 */
#define hash_entry(ptr, type, member)	\
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))
#define bio_hash_entry(ptr)		\
	hash_entry((ptr), struct bio, bi_hash)

/*
 * main unit of I/O for the block layer and lower layers (ie drivers and
 * stacking drivers)
 */
struct bio {
	sector_t		bi_sector;
	struct bio		*bi_next;	/* request queue link */
	bio_hash_t		bi_hash;
	atomic_t		bi_cnt;		/* pin count */
	kdev_t			bi_dev;		/* will be block device */
	struct bio_vec_list	*bi_io_vec;
	unsigned long		bi_flags;	/* status, command, etc */
	unsigned long		bi_rw;		/* bottom bits READ/WRITE,
						 * top bits priority
						 */
	int (*bi_end_io)(struct bio *bio, int nr_sectors);
	void			*bi_private;

	void			*bi_hash_desc;	/* cookie for hash */

	void (*bi_destructor)(struct bio *);	/* destructor */
};

#define BIO_SECTOR_BITS	9
#define BIO_OFFSET_MASK	((1UL << (PAGE_CACHE_SHIFT - BIO_SECTOR_BITS)) - 1)
#define BIO_PAGE_MASK	(PAGE_CACHE_SIZE - 1)

/*
 * bio flags
 */
#define BIO_UPTODATE	0	/* ok after I/O completion */
#define BIO_RW_BLOCK	1	/* RW_AHEAD set, and read/write would block */
#define BIO_EOF		2	/* out-out-bounds error */
#define BIO_PREBUILT	3	/* not merged big */
#define BIO_CLONED	4	/* doesn't own data */

#define bio_is_hashed(bio)	((bio)->bi_hash.pprev_hash)

/*
 * bio bi_rw flags
 *
 * bit 0 -- read (not set) or write (set)
 * bit 1 -- rw-ahead when set
 * bit 2 -- barrier
 */
#define BIO_RW		0
#define BIO_RW_AHEAD	1
#define BIO_BARRIER	2

/*
 * various member access, note that bio_data should of course not be used
 * on highmem page vectors
 */
#define bio_iovec_idx(bio, idx)	(&((bio)->bi_io_vec->bvl_vec[(idx)]))
#define bio_iovec(bio)		bio_iovec_idx((bio), (bio)->bi_io_vec->bvl_idx)
#define bio_page(bio)		bio_iovec((bio))->bv_page
#define bio_size(bio)		((bio)->bi_io_vec->bvl_size)
#define bio_offset(bio)		bio_iovec((bio))->bv_offset
#define bio_sectors(bio)	(bio_size((bio)) >> BIO_SECTOR_BITS)
#define bio_data(bio)		(page_address(bio_page((bio))) + bio_offset((bio)))
#define bio_barrier(bio)	((bio)->bi_rw & (1 << BIO_BARRIER))

/*
 * will die
 */
#define bio_to_phys(bio)	(page_to_phys(bio_page((bio))) + bio_offset((bio)))
#define bvec_to_phys(bv)	(page_to_phys((bv)->bv_page) + (bv)->bv_offset)

/*
 * hack to avoid doing 64-bit calculations on 32-bit archs, instead use a
 * pseudo-pfn check to do segment coalescing
 */
#define bio_sec_pfn(bio) \
	((((bio_page(bio) - bio_page(bio)->zone->zone_mem_map) << PAGE_SHIFT) / bio_size(bio)) + (bio_offset(bio) >> 9))

/*
 * queues that have highmem support enabled may still need to revert to
 * PIO transfers occasionally and thus map high pages temporarily. For
 * permanent PIO fall back, user is probably better off disabling highmem
 * I/O completely on that queue (see ide-dma for example)
 */
#define bio_kmap(bio)	kmap(bio_page((bio))) + bio_offset((bio))
#define bio_kunmap(bio)	kunmap(bio_page((bio)))

#define BIO_CONTIG(bio, nxt) \
	(bio_to_phys((bio)) + bio_size((bio)) == bio_to_phys((nxt)))
#define __BIO_PHYS_4G(addr1, addr2) \
	(((addr1) | 0xffffffff) == (((addr2) -1 ) | 0xffffffff))
#define BIO_PHYS_4G(b1, b2) \
	__BIO_PHYS_4G(bio_to_phys((b1)), bio_to_phys((b2)) + bio_size((b2)))

typedef int (bio_end_io_t) (struct bio *, int);
typedef void (bio_destructor_t) (struct bio *);

#define bio_io_error(bio) bio_endio((bio), 0, bio_sectors((bio)))

#define bio_for_each_segment(bvl, bio, i)				\
	for (bvl = bio_iovec((bio)), i = (bio)->bi_io_vec->bvl_idx;	\
	     i < (bio)->bi_io_vec->bvl_cnt;				\
	     bvl++, i++)

/*
 * get a reference to a bio, so it won't disappear. the intended use is
 * something like:
 *
 * bio_get(bio);
 * submit_bio(rw, bio);
 * if (bio->bi_flags ...)
 *	do_something
 * bio_put(bio);
 *
 * without the bio_get(), it could potentially complete I/O before submit_bio
 * returns. and then bio would be freed memory when if (bio->bi_flags ...)
 * runs
 */
#define bio_get(bio)	atomic_inc(&(bio)->bi_cnt)

extern struct bio *bio_alloc(int, int);
extern void bio_put(struct bio *);

/*
 * the hash stuff is pretty closely tied to the request queue (needed for
 * locking etc anyway, and it's in no way an attempt at a generic hash)
 */
struct request_queue;

extern inline void bio_hash_remove(struct bio *);
extern inline void bio_hash_add(struct bio *, void *, unsigned int);
extern inline struct bio *bio_hash_find(kdev_t, sector_t, unsigned int);
extern inline int bio_hash_add_unique(struct bio *, void *, unsigned int);
extern void bio_hash_invalidate(struct request_queue *, kdev_t);
extern int bio_endio(struct bio *, int, int);

extern struct bio *bio_clone(struct bio *, int);
extern struct bio *bio_copy(struct bio *, int);

extern int bio_ioctl(kdev_t, unsigned int, unsigned long);

#endif /* __LINUX_BIO_H */
