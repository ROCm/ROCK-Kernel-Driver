/* $Id: io.h,v 1.9 2000/02/04 07:40:53 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 Waldorf GmbH
 * Copyright (C) 1994 - 2000 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_IO_H
#define _ASM_IO_H

#include <linux/config.h>
#include <asm/addrspace.h>
#include <asm/page.h>

#ifdef CONFIG_SGI_IP22
#include <asm/sgi/io.h>
#endif

#ifdef CONFIG_SGI_IP27
#include <asm/sn/io.h>
#endif

extern unsigned long bus_to_baddr[256];

/*
 * Slowdown I/O port space accesses for antique hardware.
 */
#undef CONF_SLOWDOWN_IO

/*
 * On MIPS, we have the whole physical address space mapped at all
 * times, so "ioremap()" and "iounmap()" do not need to do anything.
 *
 * We cheat a bit and always return uncachable areas until we've fixed
 * the drivers to handle caching properly.
 */
extern inline void *
ioremap(unsigned long offset, unsigned long size)
{
	return (void *) (IO_SPACE_BASE | offset);
}

/* This one maps high address device memory and turns off caching for that
 *  area.  It's useful if some control registers are in such an area and write
 * combining or read caching is not desirable.
 */
extern inline void *
ioremap_nocache (unsigned long offset, unsigned long size)
{
	return (void *) (IO_SPACE_BASE | offset);
}

extern inline void iounmap(void *addr)
{
}

/*
 * This assumes sane hardware.  The Origin is.
 */
#define readb(addr)		(*(volatile unsigned char *) (addr))
#define readw(addr)		(*(volatile unsigned short *) (addr))
#define readl(addr)		(*(volatile unsigned int *) (addr))

#define writeb(b,addr)		(*(volatile unsigned char *) (addr) = (b))
#define writew(b,addr)		(*(volatile unsigned short *) (addr) = (b))
#define writel(b,addr)		(*(volatile unsigned int *) (addr) = (b))

#define memset_io(a,b,c)	memset((void *) a,(b),(c))
#define memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))

/* The ISA versions are supplied by system specific code */

/*
 * On MIPS I/O ports are memory mapped, so we access them using normal
 * load/store instructions. mips_io_port_base is the virtual address to
 * which all ports are being mapped.  For sake of efficiency some code
 * assumes that this is an address that can be loaded with a single lui
 * instruction, so the lower 16 bits must be zero.  Should be true on
 * on any sane architecture; generic code does not use this assumption.
 */
extern unsigned long mips_io_port_base;

#define __SLOW_DOWN_IO \
	__asm__ __volatile__( \
		"sb\t$0,0x80(%0)" \
		: : "r" (mips_io_port_base));

#ifdef CONF_SLOWDOWN_IO
#ifdef REALLY_SLOW_IO
#define SLOW_DOWN_IO { __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; }
#else
#define SLOW_DOWN_IO __SLOW_DOWN_IO
#endif
#else
#define SLOW_DOWN_IO
#endif

/*
 * Change virtual addresses to physical addresses and vv.
 * These are trivial on the 1:1 Linux/MIPS mapping
 */
extern inline unsigned long virt_to_phys(volatile void * address)
{
	return (unsigned long)address - PAGE_OFFSET;
}

extern inline void * phys_to_virt(unsigned long address)
{
	return (void *)(address + PAGE_OFFSET);
}

/*
 * isa_slot_offset is the address where E(ISA) busaddress 0 is is mapped
 * for the processor.  This implies the assumption that there is only
 * one of these busses.
 */
extern unsigned long isa_slot_offset;

/*
 * We don't have csum_partial_copy_fromio() yet, so we cheat here and
 * just copy it. The net code will then do the checksum later.
 */
#define eth_io_copy_and_sum(skb,src,len,unused) memcpy_fromio((skb)->data,(src),(len))

static inline int
check_signature(unsigned long io_addr, const unsigned char *signature,
                int length)
{
	int retval = 0;
	do {
		if (readb(io_addr) != *signature)
			goto out;
		io_addr++;
		signature++;
		length--;
	} while (length);
	retval = 1;
out:
	return retval;
}

/*
 * Talk about misusing macros..
 */

#define __OUT1(s) \
extern inline void __out##s(unsigned int value, unsigned long port) {

#define __OUT2(m) \
__asm__ __volatile__ ("s" #m "\t%0,%1(%2)"

#define __OUT(m,s) \
__OUT1(s) __OUT2(m) : : "r" (value), "i" (0), "r" (mips_io_port_base+port)); } \
__OUT1(s##c) __OUT2(m) : : "r" (value), "ir" (port), "r" (mips_io_port_base)); } \
__OUT1(s##_p) __OUT2(m) : : "r" (value), "i" (0), "r" (mips_io_port_base+port)); \
	SLOW_DOWN_IO; } \
__OUT1(s##c_p) __OUT2(m) : : "r" (value), "ir" (port), "r" (mips_io_port_base)); \
	SLOW_DOWN_IO; }

#define __IN1(t,s) \
extern __inline__ t __in##s(unsigned long port) { t _v;

/*
 * Required nops will be inserted by the assembler
 */
#define __IN2(m) \
__asm__ __volatile__ ("l" #m "\t%0,%1(%2)"

#define __IN(t,m,s) \
__IN1(t,s) __IN2(m) : "=r" (_v) : "i" (0), "r" (mips_io_port_base+port)); return _v; } \
__IN1(t,s##c) __IN2(m) : "=r" (_v) : "ir" (port), "r" (mips_io_port_base)); return _v; } \
__IN1(t,s##_p) __IN2(m) : "=r" (_v) : "i" (0), "r" (mips_io_port_base+port)); SLOW_DOWN_IO; return _v; } \
__IN1(t,s##c_p) __IN2(m) : "=r" (_v) : "ir" (port), "r" (mips_io_port_base)); SLOW_DOWN_IO; return _v; }

#define __INS1(s) \
extern inline void __ins##s(unsigned long port, void * addr, unsigned long count) {

#define __INS2(m) \
if (count) \
__asm__ __volatile__ ( \
	".set\tnoreorder\n\t" \
	".set\tnoat\n" \
	"1:\tl" #m "\t$1, %4(%5)\n\t" \
	"dsubiu\t%1, 1\n\t" \
	"s" #m "\t$1,(%0)\n\t" \
	"bnez\t%1, 1b\n\t" \
	"daddiu\t%0, %6\n\t" \
	".set\tat\n\t" \
	".set\treorder"

#define __INS(m,s,i) \
__INS1(s) __INS2(m) \
	: "=r" (addr), "=r" (count) \
	: "0" (addr), "1" (count), "i" (0), "r" (mips_io_port_base+port), "I" (i) \
	: "$1");} \
__INS1(s##c) __INS2(m) \
	: "=r" (addr), "=r" (count) \
	: "0" (addr), "1" (count), "ir" (port), "r" (mips_io_port_base), "I" (i) \
	: "$1");}

#define __OUTS1(s) \
extern inline void __outs##s(unsigned long port, const void * addr, unsigned long count) {

#define __OUTS2(m) \
if (count) \
__asm__ __volatile__ ( \
	".set\tnoreorder\n\t" \
	".set\tnoat\n" \
	"1:\tl" #m "\t$1, (%0)\n\t" \
	"dsubu\t%1, 1\n\t" \
	"s" #m "\t$1, %4(%5)\n\t" \
	"bnez\t%1, 1b\n\t" \
	"daddiu\t%0, %6\n\t" \
	".set\tat\n\t" \
	".set\treorder"

#define __OUTS(m,s,i) \
__OUTS1(s) __OUTS2(m) \
	: "=r" (addr), "=r" (count) \
	: "0" (addr), "1" (count), "i" (0), "r" (mips_io_port_base+port), "I" (i) \
	: "$1");} \
__OUTS1(s##c) __OUTS2(m) \
	: "=r" (addr), "=r" (count) \
	: "0" (addr), "1" (count), "ir" (port), "r" (mips_io_port_base), "I" (i) \
	: "$1");}

__IN(unsigned char,b,b)
__IN(unsigned short,h,w)
__IN(unsigned int,w,l)

__OUT(b,b)
__OUT(h,w)
__OUT(w,l)

__INS(b,b,1)
__INS(h,w,2)
__INS(w,l,4)

__OUTS(b,b,1)
__OUTS(h,w,2)
__OUTS(w,l,4)

/*
 * Note that due to the way __builtin_constant_p() works, you
 *  - can't use it inside an inline function (it will never be true)
 *  - you don't have to worry about side effects within the __builtin..
 */
#define outb(val,port) \
((__builtin_constant_p((port)^(3)) && ((port)^(3)) < 32768) ? \
	__outbc((val),(port)^(3)) : \
	__outb((val),(port)^(3)))

#define inb(port) \
((__builtin_constant_p((port)^(3)) && ((port)^(3)) < 32768) ? \
	__inbc((port)^(3)) : \
	__inb((port)^(3)))

#define outb_p(val,port) \
((__builtin_constant_p((port)^(3)) && ((port)^(3)) < 32768) ? \
	__outbc_p((val),(port)^(3)) : \
	__outb_p((val),(port)^(3)))

#define inb_p(port) \
((__builtin_constant_p((port)^(3)) && ((port)^(3)) < 32768) ? \
	__inbc_p((port)^(3)) : \
	__inb_p((port)^(3)))

#define outw(val,port) \
((__builtin_constant_p(((port)^(2))) && ((port)^(2)) < 32768) ? \
	__outwc((val),((port)^(2))) : \
	__outw((val),((port)^(2))))

#define inw(port) \
((__builtin_constant_p(((port)^(2))) && ((port)^(2)) < 32768) ? \
	__inwc((port)^(2)) : \
	__inw((port)^(2)))

#define outw_p(val,port) \
((__builtin_constant_p((port)^(2)) && ((port)^(2)) < 32768) ? \
	__outwc_p((val),(port)^(2)) : \
	__outw_p((val),(port)^(2)))

#define inw_p(port) \
((__builtin_constant_p((port)^(2)) && ((port)^(2)) < 32768) ? \
	__inwc_p((port)^(2)) : \
	__inw_p((port)^(2)))

#define outl(val,port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outlc((val),(port)) : \
	__outl((val),(port)))

#define inl(port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inlc(port) : \
	__inl(port))

#define outl_p(val,port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outlc_p((val),(port)) : \
	__outl_p((val),(port)))

#define inl_p(port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inlc_p(port) : \
	__inl_p(port))


#define outsb(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outsbc((port),(addr),(count)) : \
	__outsb ((port),(addr),(count)))

#define insb(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__insbc((port),(addr),(count)) : \
	__insb((port),(addr),(count)))

#define outsw(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outswc((port),(addr),(count)) : \
	__outsw ((port),(addr),(count)))

#define insw(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inswc((port),(addr),(count)) : \
	__insw((port),(addr),(count)))

#define outsl(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outslc((port),(addr),(count)) : \
	__outsl ((port),(addr),(count)))

#define insl(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inslc((port),(addr),(count)) : \
	__insl((port),(addr),(count)))

/*
 * The caches on some architectures aren't dma-coherent and have need to
 * handle this in software.  There are three types of operations that
 * can be applied to dma buffers.
 *
 *  - dma_cache_wback_inv(start, size) makes caches and coherent by
 *    writing the content of the caches back to memory, if necessary.
 *    The function also invalidates the affected part of the caches as
 *    necessary before DMA transfers from outside to memory.
 *  - dma_cache_wback(start, size) makes caches and coherent by
 *    writing the content of the caches back to memory, if necessary.
 *    The function also invalidates the affected part of the caches as
 *    necessary before DMA transfers from outside to memory.
 *  - dma_cache_inv(start, size) invalidates the affected parts of the
 *    caches.  Dirty lines of the caches may be written back or simply
 *    be discarded.  This operation is necessary before dma operations
 *    to the memory.
 */
#ifdef CONFIG_COHERENT_IO

/* This is for example for IP27.  */
extern inline void dma_cache_wback_inv(unsigned long start, unsigned long size)
{
}

extern inline void dma_cache_wback(unsigned long start, unsigned long size)
{
}

extern inline void dma_cache_inv(unsigned long start, unsigned long size)
{
}

#else

extern void (*_dma_cache_wback_inv)(unsigned long start, unsigned long size);
extern void (*_dma_cache_wback)(unsigned long start, unsigned long size);
extern void (*_dma_cache_inv)(unsigned long start, unsigned long size);

#define dma_cache_wback_inv(start,size)	_dma_cache_wback_inv(start,size)
#define dma_cache_wback(start,size)	_dma_cache_wback(start,size)
#define dma_cache_inv(start,size)	_dma_cache_inv(start,size)

#endif

#endif /* _ASM_IO_H */
