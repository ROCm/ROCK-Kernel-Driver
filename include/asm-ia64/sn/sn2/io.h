/* 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef _ASM_SN_SN2_IO_H
#define _ASM_SN_SN2_IO_H

extern void * sn_io_addr(unsigned long port); /* Forward definition */
extern void sn_mmiob(void); /* Forward definition */
#include <asm/intrinsics.h>

#define __sn_mf_a()   ia64_mfa()

extern void sn_dma_flush(unsigned long);

#define __sn_inb ___sn_inb
#define __sn_inw ___sn_inw
#define __sn_inl ___sn_inl
#define __sn_outb ___sn_outb
#define __sn_outw ___sn_outw
#define __sn_outl ___sn_outl
#define __sn_readb ___sn_readb
#define __sn_readw ___sn_readw
#define __sn_readl ___sn_readl
#define __sn_readq ___sn_readq

/*
 * The following routines are SN Platform specific, called when
 * a reference is made to inX/outX set macros.  SN Platform
 * inX set of macros ensures that Posted DMA writes on the
 * Bridge is flushed.
 *
 * The routines should be self explainatory.
 */

static inline unsigned int
___sn_inb (unsigned long port)
{
	volatile unsigned char *addr;
	unsigned char ret = -1;

	if ((addr = sn_io_addr(port))) {
		ret = *addr;
		__sn_mf_a();
		sn_dma_flush((unsigned long)addr);
	}
	return ret;
}

static inline unsigned int
___sn_inw (unsigned long port)
{
	volatile unsigned short *addr;
	unsigned short ret = -1;

	if ((addr = sn_io_addr(port))) {
		ret = *addr;
		__sn_mf_a();
		sn_dma_flush((unsigned long)addr);
	}
	return ret;
}

static inline unsigned int
___sn_inl (unsigned long port)
{
	volatile unsigned int *addr;
	unsigned int ret = -1;

	if ((addr = sn_io_addr(port))) {
		ret = *addr;
		__sn_mf_a();
		sn_dma_flush((unsigned long)addr);
	}
	return ret;
}

static inline void
___sn_outb (unsigned char val, unsigned long port)
{
	volatile unsigned char *addr;

	if ((addr = sn_io_addr(port))) {
		*addr = val;
		sn_mmiob();
	}
}

static inline void
___sn_outw (unsigned short val, unsigned long port)
{
	volatile unsigned short *addr;

	if ((addr = sn_io_addr(port))) {
		*addr = val;
		sn_mmiob();
	}
}

static inline void
___sn_outl (unsigned int val, unsigned long port)
{
	volatile unsigned int *addr;

	if ((addr = sn_io_addr(port))) {
		*addr = val;
		sn_mmiob();
	}
}

/*
 * The following routines are SN Platform specific, called when 
 * a reference is made to readX/writeX set macros.  SN Platform 
 * readX set of macros ensures that Posted DMA writes on the 
 * Bridge is flushed.
 * 
 * The routines should be self explainatory.
 */

static inline unsigned char
___sn_readb (void *addr)
{
	unsigned char val;

	val = *(volatile unsigned char *)addr;
	__sn_mf_a();
	sn_dma_flush((unsigned long)addr);
        return val;
}

static inline unsigned short
___sn_readw (void *addr)
{
	unsigned short val;

	val = *(volatile unsigned short *)addr;
	__sn_mf_a();
	sn_dma_flush((unsigned long)addr);
        return val;
}

static inline unsigned int
___sn_readl (void *addr)
{
	unsigned int val;

	val = *(volatile unsigned int *) addr;
	__sn_mf_a();
	sn_dma_flush((unsigned long)addr);
        return val;
}

static inline unsigned long
___sn_readq (void *addr)
{
	unsigned long val;

	val = *(volatile unsigned long *) addr;
	__sn_mf_a();
	sn_dma_flush((unsigned long)addr);
        return val;
}

/*
 * For generic and SN2 kernels, we have a set of fast access
 * PIO macros.	These macros are provided on SN Platform
 * because the normal inX and readX macros perform an
 * additional task of flushing Post DMA request on the Bridge.
 *
 * These routines should be self explainatory.
 */

static inline unsigned int
sn_inb_fast (unsigned long port)
{
	volatile unsigned char *addr = (unsigned char *)port;
	unsigned char ret;

	ret = *addr;
	__sn_mf_a();
	return ret;
}

static inline unsigned int
sn_inw_fast (unsigned long port)
{
	volatile unsigned short *addr = (unsigned short *)port;
	unsigned short ret;

	ret = *addr;
	__sn_mf_a();
	return ret;
}

static inline unsigned int
sn_inl_fast (unsigned long port)
{
	volatile unsigned int *addr = (unsigned int *)port;
	unsigned int ret;

	ret = *addr;
	__sn_mf_a();
	return ret;
}

static inline unsigned char
sn_readb_fast (void *addr)
{
	return *(volatile unsigned char *)addr;
}

static inline unsigned short
sn_readw_fast (void *addr)
{
	return *(volatile unsigned short *)addr;
}

static inline unsigned int
sn_readl_fast (void *addr)
{
	return *(volatile unsigned int *) addr;
}

static inline unsigned long
sn_readq_fast (void *addr)
{
	return *(volatile unsigned long *) addr;
}

#endif
