/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: mthca_doorbell.h 1349 2004-12-16 21:09:43Z roland $
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/preempt.h>

#define MTHCA_RD_DOORBELL      0x00
#define MTHCA_SEND_DOORBELL    0x10
#define MTHCA_RECEIVE_DOORBELL 0x18
#define MTHCA_CQ_DOORBELL      0x20
#define MTHCA_EQ_DOORBELL      0x28

#if BITS_PER_LONG == 64
/*
 * Assume that we can just write a 64-bit doorbell atomically.  s390
 * actually doesn't have writeq() but S/390 systems don't even have
 * PCI so we won't worry about it.
 */

#define MTHCA_DECLARE_DOORBELL_LOCK(name)
#define MTHCA_INIT_DOORBELL_LOCK(ptr)    do { } while (0)
#define MTHCA_GET_DOORBELL_LOCK(ptr)      (NULL)

static inline void mthca_write64(u32 val[2], void __iomem *dest,
				 spinlock_t *doorbell_lock)
{
	__raw_writeq(*(u64 *) val, dest);
}

#elif defined(CONFIG_INFINIBAND_MTHCA_SSE_DOORBELL)
/* Use SSE to write 64 bits atomically without a lock. */

#define MTHCA_DECLARE_DOORBELL_LOCK(name)
#define MTHCA_INIT_DOORBELL_LOCK(ptr)    do { } while (0)
#define MTHCA_GET_DOORBELL_LOCK(ptr)      (NULL)

static inline unsigned long mthca_get_fpu(void)
{
	unsigned long cr0;

	preempt_disable();
	asm volatile("mov %%cr0,%0; clts" : "=r" (cr0));
	return cr0;
}

static inline void mthca_put_fpu(unsigned long cr0)
{
	asm volatile("mov %0,%%cr0" : : "r" (cr0));
	preempt_enable();
}

static inline void mthca_write64(u32 val[2], void __iomem *dest,
				 spinlock_t *doorbell_lock)
{
	/* i386 stack is aligned to 8 bytes, so this should be OK: */
	u8 xmmsave[8] __attribute__((aligned(8)));
	unsigned long cr0;

	cr0 = mthca_get_fpu();

	asm volatile (
		"movlps %%xmm0,(%0); \n\t"
		"movlps (%1),%%xmm0; \n\t"
		"movlps %%xmm0,(%2); \n\t"
		"movlps (%0),%%xmm0; \n\t"
		:
		: "r" (xmmsave), "r" (val), "r" (dest)
		: "memory" );

	mthca_put_fpu(cr0);
}

#else
/* Just fall back to a spinlock to protect the doorbell */

#define MTHCA_DECLARE_DOORBELL_LOCK(name) spinlock_t name;
#define MTHCA_INIT_DOORBELL_LOCK(ptr)     spin_lock_init(ptr)
#define MTHCA_GET_DOORBELL_LOCK(ptr)      (ptr)

static inline void mthca_write64(u32 val[2], void __iomem *dest,
				 spinlock_t *doorbell_lock)
{
	unsigned long flags;

	spin_lock_irqsave(doorbell_lock, flags);
	__raw_writel(val[0], dest);
	__raw_writel(val[1], dest + 4);
	spin_unlock_irqrestore(doorbell_lock, flags);
}

#endif
