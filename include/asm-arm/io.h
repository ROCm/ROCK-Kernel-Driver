/*
 *  linux/include/asm-arm/io.h
 *
 *  Copyright (C) 1996-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *  16-Sep-1996	RMK	Inlined the inx/outx functions & optimised for both
 *			constant addresses and variable addresses.
 *  04-Dec-1997	RMK	Moved a lot of this stuff to the new architecture
 *			specific IO header files.
 *  27-Mar-1999	PJB	Second parameter of memcpy_toio is const..
 *  04-Apr-1999	PJB	Added check_signature.
 *  12-Dec-1999	RMK	More cleanups
 *  18-Jun-2000 RMK	Removed virt_to_* and friends definitions
 */
#ifndef __ASM_ARM_IO_H
#define __ASM_ARM_IO_H

#include <linux/types.h>
#include <asm/arch/hardware.h>
#include <asm/arch/io.h>

#define outb_p(val,port)		outb((val),(port))
#define outw_p(val,port)		outw((val),(port))
#define outl_p(val,port)		outl((val),(port))
#define inb_p(port)			inb((port))
#define inw_p(port)			inw((port))
#define inl_p(port)			inl((port))

extern void outsb(unsigned int port, const void *from, int len);
extern void outsw(unsigned int port, const void *from, int len);
extern void outsl(unsigned int port, const void *from, int len);
extern void insb(unsigned int port, void *from, int len);
extern void insw(unsigned int port, void *from, int len);
extern void insl(unsigned int port, void *from, int len);

#define outsb_p(port,from,len)		outsb(port,from,len)
#define outsw_p(port,from,len)		outsw(port,from,len)
#define outsl_p(port,from,len)		outsl(port,from,len)
#define insb_p(port,to,len)		insb(port,to,len)
#define insw_p(port,to,len)		insw(port,to,len)
#define insl_p(port,to,len)		insl(port,to,len)

#ifdef __KERNEL__

#include <asm/memory.h>

/* the following macro is depreciated */
#define ioaddr(port)			__ioaddr((port))

/*
 * ioremap and friends
 */
extern void * __ioremap(unsigned long offset, size_t size, unsigned long flags);
extern void __iounmap(void *addr);

#define ioremap(off,sz)			__arch_ioremap((off),(sz),0)
#define ioremap_nocache(off,sz)		__arch_ioremap((off),(sz),1)
#define iounmap(_addr)			__iounmap(_addr)

/*
 * DMA-consistent mapping functions.  These allocate/free a region of
 * uncached, unwrite-buffered mapped memory space for use with DMA
 * devices.  This is the "generic" version.  The PCI specific version
 * is in pci.h
 */
extern void *consistent_alloc(int gfp, size_t size, dma_addr_t *handle);
extern void consistent_free(void *vaddr);
extern void consistent_sync(void *vaddr, size_t size, int rw);

extern void __readwrite_bug(const char *fn);

/*
 * String version of IO memory access ops:
 */
extern void _memcpy_fromio(void *, unsigned long, size_t);
extern void _memcpy_toio(unsigned long, const void *, size_t);
extern void _memset_io(unsigned long, int, size_t);

#define __raw_writeb(val,addr)		__arch_putb(val,addr)
#define __raw_writew(val,addr)		__arch_putw(val,addr)
#define __raw_writel(val,addr)		__arch_putl(val,addr)

#define __raw_readb(addr)		__arch_getb(addr)
#define __raw_readw(addr)		__arch_getw(addr)
#define __raw_readl(addr)		__arch_getl(addr)

/*
 * If this architecture has PCI memory IO, then define the read/write
 * macros.
 */
#ifdef __mem_pci

#define readb(addr)			__arch_getb(__mem_pci(addr))
#define readw(addr)			__arch_getw(__mem_pci(addr))
#define readl(addr)			__arch_getl(__mem_pci(addr))
#define writeb(val,addr)		__arch_putb(val,__mem_pci(addr))
#define writew(val,addr)		__arch_putw(val,__mem_pci(addr))
#define writel(val,addr)		__arch_putl(val,__mem_pci(addr))

#define memset_io(a,b,c)		_memset_io(__mem_pci(a),(b),(c))
#define memcpy_fromio(a,b,c)		_memcpy_fromio((a),__mem_pci(b),(c))
#define memcpy_toio(a,b,c)		_memcpy_toio(__mem_pci(a),(b),(c))

#define eth_io_copy_and_sum(a,b,c,d) \
				eth_copy_and_sum((a),__mem_pci(b),(c),(d))

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

#else	/* __mem_pci */

#define readb(addr)			(__readwrite_bug("readb"),0)
#define readw(addr)			(__readwrite_bug("readw"),0)
#define readl(addr)			(__readwrite_bug("readl"),0)
#define writeb(v,addr)			__readwrite_bug("writeb")
#define writew(v,addr)			__readwrite_bug("writew")
#define writel(v,addr)			__readwrite_bug("writel")

#define eth_io_copy_and_sum(a,b,c,d)	__readwrite_bug("eth_io_copy_and_sum")

#define check_signature(io,sig,len)	(0)

#endif	/* __mem_pci */

/*
 * If this architecture has ISA IO, then define the isa_read/isa_write
 * macros.
 */
#ifdef __mem_isa

#define isa_readb(addr)			__arch_getb(__mem_isa(addr))
#define isa_readw(addr)			__arch_getw(__mem_isa(addr))
#define isa_readl(addr)			__arch_getl(__mem_isa(addr))
#define isa_writeb(val,addr)		__arch_putb(val,__mem_isa(addr))
#define isa_writew(val,addr)		__arch_putw(val,__mem_isa(addr))
#define isa_writel(val,addr)		__arch_putl(val,__mem_isa(addr))
#define isa_memset_io(a,b,c)		_memset_io(__mem_isa(a),(b),(c))
#define isa_memcpy_fromio(a,b,c)	_memcpy_fromio((a),__mem_isa(b),(c))
#define isa_memcpy_toio(a,b,c)		_memcpy_toio(__mem_isa((a)),(b),(c))

#define isa_eth_io_copy_and_sum(a,b,c,d) \
				eth_copy_and_sum((a),__mem_isa(b),(c),(d))

static inline int
isa_check_signature(unsigned long io_addr, const unsigned char *signature,
		    int length)
{
	int retval = 0;
	do {
		if (isa_readb(io_addr) != *signature)
			goto out;
		io_addr++;
		signature++;
		length--;
	} while (length);
	retval = 1;
out:
	return retval;
}

#else	/* __mem_isa */

#define isa_readb(addr)			(__readwrite_bug("isa_readb"),0)
#define isa_readw(addr)			(__readwrite_bug("isa_readw"),0)
#define isa_readl(addr)			(__readwrite_bug("isa_readl"),0)
#define isa_writeb(val,addr)		__readwrite_bug("isa_writeb")
#define isa_writew(val,addr)		__readwrite_bug("isa_writew")
#define isa_writel(val,addr)		__readwrite_bug("isa_writel")
#define isa_memset_io(a,b,c)		__readwrite_bug("isa_memset_io")
#define isa_memcpy_fromio(a,b,c)	__readwrite_bug("isa_memcpy_fromio")
#define isa_memcpy_toio(a,b,c)		__readwrite_bug("isa_memcpy_toio")

#define isa_eth_io_copy_and_sum(a,b,c,d) \
				__readwrite_bug("isa_eth_io_copy_and_sum")

#define isa_check_signature(io,sig,len)	(0)

#endif	/* __mem_isa */
#endif	/* __KERNEL__ */
#endif	/* __ASM_ARM_IO_H */
