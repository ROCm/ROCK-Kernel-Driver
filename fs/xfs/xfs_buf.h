/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
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
 * or the like.	 Any license provided herein, whether implied or
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
#ifndef __XFS_BUF_H__
#define __XFS_BUF_H__

/* These are just for xfs_syncsub... it sets an internal variable
 * then passes it to VOP_FLUSH_PAGES or adds the flags to a newly gotten buf_t
 */
#define XFS_B_ASYNC		PBF_ASYNC
#define XFS_B_DELWRI		PBF_DELWRI
#define XFS_B_READ		PBF_READ
#define XFS_B_WRITE		PBF_WRITE
#define XFS_B_STALE		PBF_STALE

#define XFS_BUF_TRYLOCK		PBF_TRYLOCK
#define XFS_INCORE_TRYLOCK	PBF_TRYLOCK
#define XFS_BUF_LOCK		PBF_LOCK
#define XFS_BUF_MAPPED		PBF_MAPPED

#define BUF_BUSY		PBF_DONT_BLOCK

#define XFS_BUF_BFLAGS(x)	((x)->pb_flags)
#define XFS_BUF_ZEROFLAGS(x)	\
	((x)->pb_flags &= ~(PBF_READ|PBF_WRITE|PBF_ASYNC|PBF_SYNC|PBF_DELWRI))

#define XFS_BUF_STALE(x)	((x)->pb_flags |= XFS_B_STALE)
#define XFS_BUF_UNSTALE(x)	((x)->pb_flags &= ~XFS_B_STALE)
#define XFS_BUF_ISSTALE(x)	((x)->pb_flags & XFS_B_STALE)
#define XFS_BUF_SUPER_STALE(x)	do {				\
					XFS_BUF_STALE(x);	\
					xfs_buf_undelay(x);	\
					XFS_BUF_DONE(x);	\
				} while (0)

#define XFS_BUF_MANAGE		PBF_FS_MANAGED
#define XFS_BUF_UNMANAGE(x)	((x)->pb_flags &= ~PBF_FS_MANAGED)

static inline void xfs_buf_undelay(page_buf_t *pb)
{
	if (pb->pb_flags & PBF_DELWRI) {
		if (pb->pb_list.next != &pb->pb_list) {
			pagebuf_delwri_dequeue(pb);
			pagebuf_rele(pb);
		} else {
			pb->pb_flags &= ~PBF_DELWRI;
		}
	}
}

#define XFS_BUF_DELAYWRITE(x)	 ((x)->pb_flags |= PBF_DELWRI)
#define XFS_BUF_UNDELAYWRITE(x)	 xfs_buf_undelay(x)
#define XFS_BUF_ISDELAYWRITE(x)	 ((x)->pb_flags & PBF_DELWRI)

#define XFS_BUF_ERROR(x,no)	 pagebuf_ioerror(x,no)
#define XFS_BUF_GETERROR(x)	 pagebuf_geterror(x)
#define XFS_BUF_ISERROR(x)	 (pagebuf_geterror(x)?1:0)

#define XFS_BUF_DONE(x)		 ((x)->pb_flags &= ~(PBF_PARTIAL|PBF_NONE))
#define XFS_BUF_UNDONE(x)	 ((x)->pb_flags |= PBF_PARTIAL|PBF_NONE)
#define XFS_BUF_ISDONE(x)	 (!(PBF_NOT_DONE(x)))

#define XFS_BUF_BUSY(x)		 ((x)->pb_flags |= PBF_FORCEIO)
#define XFS_BUF_UNBUSY(x)	 ((x)->pb_flags &= ~PBF_FORCEIO)
#define XFS_BUF_ISBUSY(x)	 (1)

#define XFS_BUF_ASYNC(x)	 ((x)->pb_flags |= PBF_ASYNC)
#define XFS_BUF_UNASYNC(x)	 ((x)->pb_flags &= ~PBF_ASYNC)
#define XFS_BUF_ISASYNC(x)	 ((x)->pb_flags & PBF_ASYNC)

#define XFS_BUF_FLUSH(x)	 ((x)->pb_flags |= PBF_FLUSH)
#define XFS_BUF_UNFLUSH(x)	 ((x)->pb_flags &= ~PBF_FLUSH)
#define XFS_BUF_ISFLUSH(x)	 ((x)->pb_flags & PBF_FLUSH)

#define XFS_BUF_SHUT(x)		 printk("XFS_BUF_SHUT not implemented yet\n")
#define XFS_BUF_UNSHUT(x)	 printk("XFS_BUF_UNSHUT not implemented yet\n")
#define XFS_BUF_ISSHUT(x)	 (0)

#define XFS_BUF_HOLD(x)		pagebuf_hold(x)
#define XFS_BUF_READ(x)		((x)->pb_flags |= PBF_READ)
#define XFS_BUF_UNREAD(x)	((x)->pb_flags &= ~PBF_READ)
#define XFS_BUF_ISREAD(x)	((x)->pb_flags & PBF_READ)

#define XFS_BUF_WRITE(x)	((x)->pb_flags |= PBF_WRITE)
#define XFS_BUF_UNWRITE(x)	((x)->pb_flags &= ~PBF_WRITE)
#define XFS_BUF_ISWRITE(x)	((x)->pb_flags & PBF_WRITE)

#define XFS_BUF_ISUNINITIAL(x)	 (0)
#define XFS_BUF_UNUNINITIAL(x)	 (0)

#define XFS_BUF_BP_ISMAPPED(bp)	 1

typedef struct page_buf_s xfs_buf_t;
#define xfs_buf page_buf_s

typedef struct pb_target xfs_buftarg_t;
#define xfs_buftarg pb_target

#define XFS_BUF_DATAIO(x)	((x)->pb_flags |= PBF_FS_DATAIOD)
#define XFS_BUF_UNDATAIO(x)	((x)->pb_flags &= ~PBF_FS_DATAIOD)

#define XFS_BUF_IODONE_FUNC(buf)	(buf)->pb_iodone
#define XFS_BUF_SET_IODONE_FUNC(buf, func)	\
			(buf)->pb_iodone = (func)
#define XFS_BUF_CLR_IODONE_FUNC(buf)		\
			(buf)->pb_iodone = NULL
#define XFS_BUF_SET_BDSTRAT_FUNC(buf, func)	\
			(buf)->pb_strat = (func)
#define XFS_BUF_CLR_BDSTRAT_FUNC(buf)		\
			(buf)->pb_strat = NULL

#define XFS_BUF_FSPRIVATE(buf, type)		\
			((type)(buf)->pb_fspriv)
#define XFS_BUF_SET_FSPRIVATE(buf, value)	\
			(buf)->pb_fspriv = (void *)(value)
#define XFS_BUF_FSPRIVATE2(buf, type)		\
			((type)(buf)->pb_fspriv2)
#define XFS_BUF_SET_FSPRIVATE2(buf, value)	\
			(buf)->pb_fspriv2 = (void *)(value)
#define XFS_BUF_FSPRIVATE3(buf, type)		\
			((type)(buf)->pb_fspriv3)
#define XFS_BUF_SET_FSPRIVATE3(buf, value)	\
			(buf)->pb_fspriv3  = (void *)(value)
#define XFS_BUF_SET_START(buf)

#define XFS_BUF_SET_BRELSE_FUNC(buf, value) \
			(buf)->pb_relse = (value)

#define XFS_BUF_PTR(bp)		(xfs_caddr_t)((bp)->pb_addr)

extern inline xfs_caddr_t xfs_buf_offset(page_buf_t *bp, size_t offset)
{
	if (bp->pb_flags & PBF_MAPPED)
		return XFS_BUF_PTR(bp) + offset;
	return (xfs_caddr_t) pagebuf_offset(bp, offset);
}

#define XFS_BUF_SET_PTR(bp, val, count)		\
				pagebuf_associate_memory(bp, val, count)
#define XFS_BUF_ADDR(bp)	((bp)->pb_bn)
#define XFS_BUF_SET_ADDR(bp, blk)		\
			((bp)->pb_bn = (page_buf_daddr_t)(blk))
#define XFS_BUF_OFFSET(bp)	((bp)->pb_file_offset)
#define XFS_BUF_SET_OFFSET(bp, off)		\
			((bp)->pb_file_offset = (off))
#define XFS_BUF_COUNT(bp)	((bp)->pb_count_desired)
#define XFS_BUF_SET_COUNT(bp, cnt)		\
			((bp)->pb_count_desired = (cnt))
#define XFS_BUF_SIZE(bp)	((bp)->pb_buffer_length)
#define XFS_BUF_SET_SIZE(bp, cnt)		\
			((bp)->pb_buffer_length = (cnt))
#define XFS_BUF_SET_VTYPE_REF(bp, type, ref)
#define XFS_BUF_SET_VTYPE(bp, type)
#define XFS_BUF_SET_REF(bp, ref)

#define XFS_BUF_ISPINNED(bp)   pagebuf_ispin(bp)

#define XFS_BUF_VALUSEMA(bp)	pagebuf_lock_value(bp)
#define XFS_BUF_CPSEMA(bp)	(pagebuf_cond_lock(bp) == 0)
#define XFS_BUF_VSEMA(bp)	pagebuf_unlock(bp)
#define XFS_BUF_PSEMA(bp,x)	pagebuf_lock(bp)
#define XFS_BUF_V_IODONESEMA(bp) up(&bp->pb_iodonesema);

/* setup the buffer target from a buftarg structure */
#define XFS_BUF_SET_TARGET(bp, target)	\
	(bp)->pb_target = (target)

#define XFS_BUF_TARGET(bp)	((bp)->pb_target)

#define XFS_BUFTARG_NAME(target) \
	({ char __b[BDEVNAME_SIZE]; bdevname((target->pbr_bdev), __b); __b; })
	
#define XFS_BUF_SET_VTYPE_REF(bp, type, ref)
#define XFS_BUF_SET_VTYPE(bp, type)
#define XFS_BUF_SET_REF(bp, ref)

#define xfs_buf_read(target, blkno, len, flags) \
		pagebuf_get((target), (blkno), (len), \
			PBF_LOCK | PBF_READ | PBF_MAPPED | PBF_MAPPABLE)
#define xfs_buf_get(target, blkno, len, flags) \
		pagebuf_get((target), (blkno), (len), \
			PBF_LOCK | PBF_MAPPED | PBF_MAPPABLE)

#define xfs_buf_read_flags(target, blkno, len, flags) \
		pagebuf_get((target), (blkno), (len), \
			PBF_READ | PBF_MAPPABLE | flags)
#define xfs_buf_get_flags(target, blkno, len, flags) \
		pagebuf_get((target), (blkno), (len), \
			PBF_MAPPABLE | flags)

static inline int	xfs_bawrite(void *mp, page_buf_t *bp)
{
	bp->pb_fspriv3 = mp;
	bp->pb_strat = xfs_bdstrat_cb;
	xfs_buf_undelay(bp);
	return pagebuf_iostart(bp, PBF_WRITE | PBF_ASYNC | PBF_RUN_QUEUES);
}

static inline void	xfs_buf_relse(page_buf_t *bp)
{
	if ((bp->pb_flags & _PBF_LOCKABLE) && !bp->pb_relse)
		pagebuf_unlock(bp);
	pagebuf_rele(bp);
}


#define xfs_bpin(bp)		pagebuf_pin(bp)
#define xfs_bunpin(bp)		pagebuf_unpin(bp)

#ifdef PAGEBUF_TRACE
# define PB_DEFINE_TRACES
# include <pagebuf/page_buf_trace.h>
# define xfs_buftrace(id, bp)	PB_TRACE(bp, PB_TRACE_REC(external), (void *)id)
#else
# define xfs_buftrace(id, bp)	do { } while (0)
#endif


#define xfs_biodone(pb)		    \
	    pagebuf_iodone(pb, (pb->pb_flags & PBF_FS_DATAIOD), 0)

#define xfs_incore(buftarg,blkno,len,lockit) \
	    pagebuf_find(buftarg, blkno ,len, lockit)


#define xfs_biomove(pb, off, len, data, rw) \
	    pagebuf_iomove((pb), (off), (len), (data), \
		((rw) == XFS_B_WRITE) ? PBRW_WRITE : PBRW_READ)

#define xfs_biozero(pb, off, len) \
	    pagebuf_iomove((pb), (off), (len), NULL, PBRW_ZERO)


static inline int	XFS_bwrite(page_buf_t *pb)
{
	int	iowait = (pb->pb_flags & PBF_ASYNC) == 0;
	int	error = 0;

	pb->pb_flags |= PBF_SYNC;
	if (!iowait)
		pb->pb_flags |= PBF_RUN_QUEUES;

	xfs_buf_undelay(pb);
	pagebuf_iostrategy(pb);
	if (iowait) {
		error = pagebuf_iowait(pb);
		xfs_buf_relse(pb);
	}
	return error;
}

#define XFS_bdwrite(pb)		     \
	    pagebuf_iostart(pb, PBF_DELWRI | PBF_ASYNC)

static inline int xfs_bdwrite(void *mp, page_buf_t *bp)
{
	bp->pb_strat = xfs_bdstrat_cb;
	bp->pb_fspriv3 = mp;

	return pagebuf_iostart(bp, PBF_DELWRI | PBF_ASYNC);
}

#define XFS_bdstrat(bp) pagebuf_iorequest(bp)

#define xfs_iowait(pb)	pagebuf_iowait(pb)


/*
 * Go through all incore buffers, and release buffers
 * if they belong to the given device. This is used in
 * filesystem error handling to preserve the consistency
 * of its metadata.
 */

#define xfs_binval(buftarg)	xfs_flush_buftarg(buftarg)

#define XFS_bflush(buftarg)	xfs_flush_buftarg(buftarg)

#define xfs_incore_relse(buftarg,delwri_only,wait)	\
	xfs_relse_buftarg(buftarg)

#define xfs_baread(target, rablkno, ralen)  \
	pagebuf_readahead((target), (rablkno), (ralen), PBF_DONT_BLOCK)

#define xfs_buf_get_empty(len, target)	pagebuf_get_empty((len), (target))
#define xfs_buf_get_noaddr(len, target)	pagebuf_get_no_daddr((len), (target))
#define xfs_buf_free(bp)		pagebuf_free(bp)

#endif	/* __XFS_BUF_H__ */
