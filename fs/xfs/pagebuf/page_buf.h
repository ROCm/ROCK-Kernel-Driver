/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

/*
 * Written by Steve Lord, Jim Mostek, Russell Cattelan at SGI
 */

#ifndef __PAGE_BUF_H__
#define __PAGE_BUF_H__

#include <linux/config.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <asm/system.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/uio.h>

/*
 * Turn this on to get pagebuf lock ownership
#define PAGEBUF_LOCK_TRACKING
*/

/*
 *	Base types
 */

/* daddr must be signed since -1 is used for bmaps that are not yet allocated */
typedef loff_t page_buf_daddr_t;

#define PAGE_BUF_DADDR_NULL ((page_buf_daddr_t) (-1LL))

typedef size_t page_buf_dsize_t;		/* size of buffer in blocks */

#define page_buf_ctob(pp)	((pp) * PAGE_CACHE_SIZE)
#define page_buf_btoc(dd)	(((dd) + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT)
#define page_buf_btoct(dd)	((dd) >> PAGE_CACHE_SHIFT)
#define page_buf_poff(aa)	((aa) & ~PAGE_CACHE_MASK)

typedef enum page_buf_rw_e {
	PBRW_READ = 1,			/* transfer into target memory */
	PBRW_WRITE = 2,			/* transfer from target memory */
	PBRW_ZERO = 3			/* Zero target memory */
} page_buf_rw_t;

typedef enum {				/* pbm_flags values */
	PBMF_EOF =		0x01,	/* mapping contains EOF		*/
	PBMF_HOLE =		0x02,	/* mapping covers a hole	*/
	PBMF_DELAY =		0x04,	/* mapping covers delalloc region  */
	PBMF_UNWRITTEN =	0x20,	/* mapping covers allocated	*/
					/* but uninitialized file data	*/
	PBMF_NEW =		0x40	/* just allocated		*/
} bmap_flags_t;

typedef enum {
	/* base extent manipulation calls */
	BMAP_READ = (1 << 0),		/* read extents */
	BMAP_WRITE = (1 << 1),		/* create extents */
	BMAP_ALLOCATE = (1 << 2),	/* delayed allocate to real extents */
	BMAP_UNWRITTEN  = (1 << 3),	/* unwritten extents to real extents */
	/* modifiers */
	BMAP_IGNSTATE = (1 << 4),	/* ignore unwritten state on read */
	BMAP_DIRECT = (1 << 5),		/* direct instead of buffered write */
	BMAP_MMAP = (1 << 6),		/* allocate for mmap write */
	BMAP_SYNC = (1 << 7),		/* sync write */
	BMAP_TRYLOCK = (1 << 8),	/* non-blocking request */
	BMAP_DEVICE = (1 << 9),		/* we only want to know the device */
} bmapi_flags_t;

typedef enum page_buf_flags_e {		/* pb_flags values */
	PBF_READ = (1 << 0),	/* buffer intended for reading from device */
	PBF_WRITE = (1 << 1),	/* buffer intended for writing to device   */
	PBF_MAPPED = (1 << 2),  /* buffer mapped (pb_addr valid)           */
	PBF_PARTIAL = (1 << 3), /* buffer partially read                   */
	PBF_ASYNC = (1 << 4),   /* initiator will not wait for completion  */
	PBF_NONE = (1 << 5),    /* buffer not read at all                  */
	PBF_DELWRI = (1 << 6),  /* buffer has dirty pages                  */
	PBF_FREED = (1 << 7),   /* buffer has been freed and is invalid    */
	PBF_SYNC = (1 << 8),    /* force updates to disk                   */
	PBF_MAPPABLE = (1 << 9),/* use directly-addressable pages          */
	PBF_STALE = (1 << 10),	/* buffer has been staled, do not find it  */
	PBF_FS_MANAGED = (1 << 11), /* filesystem controls freeing memory  */
	PBF_FS_DATAIOD = (1 << 12), /* schedule IO completion on fs datad  */

	/* flags used only as arguments to access routines */
	PBF_LOCK = (1 << 13),	/* lock requested			   */
	PBF_TRYLOCK = (1 << 14), /* lock requested, but do not wait	   */
	PBF_DONT_BLOCK = (1 << 15), /* do not block in current thread	   */

	/* flags used only internally */
	_PBF_LOCKABLE = (1 << 16), /* page_buf_t may be locked		   */
	_PBF_PRIVATE_BH = (1 << 17), /* do not use public buffer heads	   */
	_PBF_ALL_PAGES_MAPPED = (1 << 18), /* all pages in range mapped	   */
	_PBF_ADDR_ALLOCATED = (1 << 19), /* pb_addr space was allocated	   */
	_PBF_MEM_ALLOCATED = (1 << 20), /* pb_mem+underlying pages alloc'd */

	PBF_FORCEIO = (1 << 21),
	PBF_FLUSH = (1 << 22),	/* flush disk write cache		   */
	PBF_READ_AHEAD = (1 << 23),
	PBF_RUN_QUEUES = (1 << 24), /* run block device task queue	   */

} page_buf_flags_t;

#define PBF_UPDATE (PBF_READ | PBF_WRITE)
#define PBF_NOT_DONE(pb) (((pb)->pb_flags & (PBF_PARTIAL|PBF_NONE)) != 0)
#define PBF_DONE(pb) (((pb)->pb_flags & (PBF_PARTIAL|PBF_NONE)) == 0)

typedef struct pb_target {
	dev_t			pbr_dev;
	struct block_device	*pbr_bdev;
	struct address_space	*pbr_mapping;
	unsigned int		pbr_bsize;
	unsigned int		pbr_sshift;
	size_t			pbr_smask;
} pb_target_t;

/*
 *	page_buf_bmap_t:  File system I/O map
 *
 * The pbm_bn, pbm_offset and pbm_length fields are expressed in disk blocks.
 * The pbm_length field specifies the size of the underlying backing store
 * for the particular mapping.
 *
 * The pbm_bsize, pbm_size and pbm_delta fields are in bytes and indicate
 * the size of the mapping, the number of bytes that are valid to access
 * (read or write), and the offset into the mapping, given the offset
 * supplied to the file I/O map routine.  pbm_delta is the offset of the
 * desired data from the beginning of the mapping.
 *
 * When a request is made to read beyond the logical end of the object,
 * pbm_size may be set to 0, but pbm_offset and pbm_length should be set to
 * the actual amount of underlying storage that has been allocated, if any.
 */

typedef struct page_buf_bmap_s {
	page_buf_daddr_t pbm_bn;	/* block number in file system	    */
	pb_target_t	*pbm_target;	/* device to do I/O to		    */
	loff_t		pbm_offset;	/* byte offset of mapping in file   */
	size_t		pbm_delta;	/* offset of request into bmap	    */
	size_t		pbm_bsize;	/* size of this mapping in bytes    */
	bmap_flags_t	pbm_flags;	/* options flags for mapping	    */
} page_buf_bmap_t;

typedef page_buf_bmap_t pb_bmap_t;


/*
 *	page_buf_t:  Buffer structure for page cache-based buffers
 *
 * This buffer structure is used by the page cache buffer management routines
 * to refer to an assembly of pages forming a logical buffer.  The actual
 * I/O is performed with buffer_head or bio structures, as required by drivers,
 * for drivers which do not understand this structure.  The buffer structure is
 * used on temporary basis only, and discarded when released.
 *
 * The real data storage is recorded in the page cache.  Metadata is
 * hashed to the inode for the block device on which the file system resides.
 * File data is hashed to the inode for the file.  Pages which are only
 * partially filled with data have bits set in their block_map entry
 * to indicate which disk blocks in the page are not valid.
 */

struct page_buf_s;
typedef void (*page_buf_iodone_t)(struct page_buf_s *);
			/* call-back function on I/O completion */
typedef void (*page_buf_relse_t)(struct page_buf_s *);
			/* call-back function on I/O completion */
typedef int (*page_buf_bdstrat_t)(struct page_buf_s *);

#define PB_PAGES	4

typedef struct page_buf_s {
	struct semaphore	pb_sema;	/* semaphore for lockables  */
	unsigned long		pb_flushtime;	/* time to flush pagebuf    */
	atomic_t		pb_pin_count;	/* pin count		    */
	wait_queue_head_t	pb_waiters;	/* unpin waiters	    */
	struct list_head	pb_list;
	page_buf_flags_t	pb_flags;	/* status flags */
	struct list_head	pb_hash_list;
	struct pb_target	*pb_target;	/* logical object */
	atomic_t		pb_hold;	/* reference count */
	page_buf_daddr_t	pb_bn;		/* block number for I/O */
	loff_t			pb_file_offset;	/* offset in file */
	size_t			pb_buffer_length; /* size of buffer in bytes */
	size_t			pb_count_desired; /* desired transfer size */
	void			*pb_addr;	/* virtual address of buffer */
	struct work_struct	pb_iodone_work;
	atomic_t		pb_io_remaining;/* #outstanding I/O requests */
	page_buf_iodone_t	pb_iodone;	/* I/O completion function */
	page_buf_relse_t	pb_relse;	/* releasing function */
	page_buf_bdstrat_t	pb_strat;	/* pre-write function */
	struct semaphore	pb_iodonesema;	/* Semaphore for I/O waiters */
	void			*pb_fspriv;
	void			*pb_fspriv2;
	void			*pb_fspriv3;
	unsigned short		pb_error;	/* error code on I/O */
	unsigned short		pb_page_count;	/* size of page array */
	unsigned short		pb_offset;	/* page offset in first page */
	unsigned char		pb_locked;	/* page array is locked */
	unsigned char		pb_hash_index;	/* hash table index	*/
	struct page		**pb_pages;	/* array of page pointers */
	struct page		*pb_page_array[PB_PAGES]; /* inline pages */
#ifdef PAGEBUF_LOCK_TRACKING
	int			pb_last_holder;
#endif
} page_buf_t;


/* Finding and Reading Buffers */

extern page_buf_t *pagebuf_find(	/* find buffer for block if	*/
					/* the block is in memory	*/
		struct pb_target *,	/* inode for block		*/
		loff_t,			/* starting offset of range	*/
		size_t,			/* length of range		*/
		page_buf_flags_t);	/* PBF_LOCK			*/

extern page_buf_t *pagebuf_get(		/* allocate a buffer		*/
		struct pb_target *,	/* inode for buffer		*/
		loff_t,			/* starting offset of range     */
		size_t,			/* length of range              */
		page_buf_flags_t);	/* PBF_LOCK, PBF_READ,		*/
					/* PBF_ASYNC			*/

extern page_buf_t *pagebuf_lookup(
		struct pb_target *,
		loff_t,			/* starting offset of range	*/
		size_t,			/* length of range		*/
		page_buf_flags_t);	/* PBF_READ, PBF_WRITE,		*/
					/* PBF_FORCEIO, _PBF_LOCKABLE	*/

extern page_buf_t *pagebuf_get_empty(	/* allocate pagebuf struct with	*/
					/*  no memory or disk address	*/
		size_t len,
		struct pb_target *);	/* mount point "fake" inode	*/

extern page_buf_t *pagebuf_get_no_daddr(/* allocate pagebuf struct	*/
					/* without disk address		*/
		size_t len,
		struct pb_target *);	/* mount point "fake" inode	*/

extern int pagebuf_associate_memory(
		page_buf_t *,
		void *,
		size_t);

extern void pagebuf_hold(		/* increment reference count	*/
		page_buf_t *);		/* buffer to hold		*/

extern void pagebuf_readahead(		/* read ahead into cache	*/
		struct pb_target  *,	/* target for buffer (or NULL)	*/
		loff_t,			/* starting offset of range     */
		size_t,			/* length of range              */
		page_buf_flags_t);	/* additional read flags	*/

/* Releasing Buffers */

extern void pagebuf_free(		/* deallocate a buffer		*/
		page_buf_t *);		/* buffer to deallocate		*/

extern void pagebuf_rele(		/* release hold on a buffer	*/
		page_buf_t *);		/* buffer to release		*/

/* Locking and Unlocking Buffers */

extern int pagebuf_cond_lock(		/* lock buffer, if not locked	*/
					/* (returns -EBUSY if locked)	*/
		page_buf_t *);		/* buffer to lock		*/

extern int pagebuf_lock_value(		/* return count on lock		*/
		page_buf_t *);          /* buffer to check              */

extern int pagebuf_lock(		/* lock buffer                  */
		page_buf_t *);          /* buffer to lock               */

extern void pagebuf_unlock(		/* unlock buffer		*/
		page_buf_t *);		/* buffer to unlock		*/

/* Buffer Read and Write Routines */

extern void pagebuf_iodone(		/* mark buffer I/O complete	*/
		page_buf_t *,		/* buffer to mark		*/
		int,			/* use data/log helper thread.	*/
		int);			/* run completion locally, or in
					 * a helper thread.		*/

extern void pagebuf_ioerror(		/* mark buffer in error	(or not) */
		page_buf_t *,		/* buffer to mark		*/
		unsigned int);		/* error to store (0 if none)	*/

extern int pagebuf_iostart(		/* start I/O on a buffer	*/
		page_buf_t *,		/* buffer to start		*/
		page_buf_flags_t);	/* PBF_LOCK, PBF_ASYNC,		*/
					/* PBF_READ, PBF_WRITE,		*/
					/* PBF_DELWRI, PBF_SYNC		*/

extern int pagebuf_iorequest(		/* start real I/O		*/
		page_buf_t *);		/* buffer to convey to device	*/

extern int pagebuf_iowait(		/* wait for buffer I/O done	*/
		page_buf_t *);		/* buffer to wait on		*/

extern void pagebuf_iomove(		/* move data in/out of pagebuf	*/
		page_buf_t *,		/* buffer to manipulate		*/
		size_t,			/* starting buffer offset	*/
		size_t,			/* length in buffer		*/
		caddr_t,		/* data pointer			*/
		page_buf_rw_t);		/* direction			*/

static inline int pagebuf_iostrategy(page_buf_t *pb)
{
	return pb->pb_strat ? pb->pb_strat(pb) : pagebuf_iorequest(pb);
}

static inline int pagebuf_geterror(page_buf_t *pb)
{
	return pb ? pb->pb_error : ENOMEM;
}

/* Buffer Utility Routines */

extern caddr_t pagebuf_offset(		/* pointer at offset in buffer	*/
		page_buf_t *,		/* buffer to offset into	*/
		size_t);		/* offset			*/

/* Pinning Buffer Storage in Memory */

extern void pagebuf_pin(		/* pin buffer in memory		*/
		page_buf_t *);		/* buffer to pin		*/

extern void pagebuf_unpin(		/* unpin buffered data		*/
		page_buf_t *);		/* buffer to unpin		*/

extern int pagebuf_ispin(		/* check if buffer is pinned	*/
		page_buf_t *);		/* buffer to check		*/

/* Delayed Write Buffer Routines */

#define PBDF_WAIT    0x01
extern void pagebuf_delwri_flush(
		pb_target_t *,
		unsigned long,
		int *);

extern void pagebuf_delwri_dequeue(
		page_buf_t *);

/* Buffer Daemon Setup Routines */

extern int pagebuf_init(void);
extern void pagebuf_terminate(void);

#endif /* __PAGE_BUF_H__ */
