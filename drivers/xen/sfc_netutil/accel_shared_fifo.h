/****************************************************************************
 * Solarflare driver for Xen network acceleration
 *
 * Copyright 2006-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications <linux-xen-drivers@solarflare.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#ifndef NET_ACCEL_SHARED_FIFO_H
#define NET_ACCEL_SHARED_FIFO_H

/*
 * This is based on fifo.h, but handles sharing between address spaces
 * that don't trust each other, by splitting out the read and write
 * indices. This costs at least one pointer indirection more than the
 * vanilla version per access.
 */

typedef struct {
	char*	 fifo;
	unsigned      fifo_mask;
	unsigned      *fifo_rd_i;
	unsigned      *fifo_wr_i;
} sh_byte_fifo2;

#define SH_FIFO2_M(f, x)     ((x) & ((f)->fifo_mask))

static inline unsigned log2_ge(unsigned long n, unsigned min_order) {
	unsigned order = min_order;
	while((1ul << order) < n) ++order;
	return order;
}

static inline unsigned long pow2(unsigned order) {
	return (1ul << order);
}

#define is_pow2(x)  (pow2(log2_ge((x), 0)) == (x))

#define sh_fifo2_valid(f)  ((f) && (f)->fifo && (f)->fifo_mask > 0 &&   \
			    is_pow2((f)->fifo_mask+1u))

#define sh_fifo2_init(f, cap, _rptr, _wptr)		\
	do {						\
		BUG_ON(!is_pow2((cap) + 1));		\
		(f)->fifo_rd_i = _rptr;			\
		(f)->fifo_wr_i = _wptr;			\
		*(f)->fifo_rd_i = *(f)->fifo_wr_i = 0u; \
		(f)->fifo_mask = (cap);			\
	} while(0)

#define sh_fifo2_num(f)      SH_FIFO2_M((f),*(f)->fifo_wr_i - *(f)->fifo_rd_i)
#define sh_fifo2_space(f)    SH_FIFO2_M((f),*(f)->fifo_rd_i - *(f)->fifo_wr_i-1u)
#define sh_fifo2_is_empty(f)  (sh_fifo2_num(f)==0)
#define sh_fifo2_not_empty(f) (sh_fifo2_num(f)!=0)
#define sh_fifo2_is_full(f)   (sh_fifo2_space(f)==0u)
#define sh_fifo2_not_full(f)  (sh_fifo2_space(f)!=0u)
#define sh_fifo2_buf_size(f) ((f)->fifo_mask + 1u)
#define sh_fifo2_capacity(f) ((f)->fifo_mask)
#define sh_fifo2_end(f)      ((f)->fifo + sh_fifo2_buf_size(f))
#define sh_fifo2_not_half_full(f) (sh_fifo2_space(f) > (sh_fifo2_capacity(f) >> 1))

#define sh_fifo2_peek(f)     ((f)->fifo[SH_FIFO2_M((f), *(f)->fifo_rd_i)])
#define sh_fifo2_peekp(f)    ((f)->fifo + SH_FIFO2_M((f), *(f)->fifo_rd_i))
#define sh_fifo2_poke(f)     ((f)->fifo[SH_FIFO2_M((f), *(f)->fifo_wr_i)])
#define sh_fifo2_pokep(f)    ((f)->fifo + SH_FIFO2_M((f), *(f)->fifo_wr_i))
#define sh_fifo2_peek_i(f,i) ((f)->fifo[SH_FIFO2_M((f), *(f)->fifo_rd_i+(i))])
#define sh_fifo2_poke_i(f,i) ((f)->fifo[SH_FIFO2_M((f), *(f)->fifo_wr_i+(i))])

#define sh_fifo2_rd_next(f)					\
	do {*(f)->fifo_rd_i = *(f)->fifo_rd_i + 1u;} while(0)
#define sh_fifo2_wr_next(f)					\
	do {*(f)->fifo_wr_i = *(f)->fifo_wr_i + 1u;} while(0)
#define sh_fifo2_rd_adv(f, n)					\
	do {*(f)->fifo_rd_i = *(f)->fifo_rd_i + (n);} while(0)
#define sh_fifo2_wr_adv(f, n)					\
	do {*(f)->fifo_wr_i = *(f)->fifo_wr_i + (n);} while(0)

#define sh_fifo2_put(f, v)						\
	do {sh_fifo2_poke(f) = (v); wmb(); sh_fifo2_wr_next(f);} while(0)

#define sh_fifo2_get(f, pv)						\
	do {*(pv) = sh_fifo2_peek(f); mb(); sh_fifo2_rd_next(f);} while(0)

static inline unsigned sh_fifo2_contig_num(sh_byte_fifo2 *f)
{
	unsigned fifo_wr_i = SH_FIFO2_M(f, *f->fifo_wr_i);
	unsigned fifo_rd_i = SH_FIFO2_M(f, *f->fifo_rd_i);

	return (fifo_wr_i >= fifo_rd_i)
		? fifo_wr_i - fifo_rd_i
		: f->fifo_mask + 1u - *(f)->fifo_rd_i;
}

static inline unsigned sh_fifo2_contig_space(sh_byte_fifo2 *f)
{
	unsigned fifo_wr_i = SH_FIFO2_M(f, *f->fifo_wr_i);
	unsigned fifo_rd_i = SH_FIFO2_M(f, *f->fifo_rd_i);

	return (fifo_rd_i > fifo_wr_i)
		? fifo_rd_i - fifo_wr_i - 1
		: (f->fifo_mask + 1u - fifo_wr_i
		   /*
		    * The last byte can't be used if the read pointer
		    * is at zero.
		    */
		   - (fifo_rd_i==0));
}


#endif /* NET_ACCEL_SHARED_FIFO_H */
