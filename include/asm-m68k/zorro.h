#ifndef _ASM_M68K_ZORRO_H
#define _ASM_M68K_ZORRO_H
#include <asm/raw_io.h>

#define z_readb raw_inb
#define z_readw raw_inw
#define z_readl raw_inl

#define z_writeb raw_outb
#define z_writew raw_outw
#define z_writel raw_outl

#define z_memset_io(a,b,c)	memset((void *)(a),(b),(c))
#define z_memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define z_memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))


/* Values for nocacheflag and cmode */
#ifndef IOMAP_FULL_CACHING		
#define IOMAP_FULL_CACHING		0
#define IOMAP_NOCACHE_SER		1
#define IOMAP_NOCACHE_NONSER		2
#define IOMAP_WRITETHROUGH		3
#endif

extern void iounmap(void *addr);

extern void *__ioremap(unsigned long physaddr, unsigned long size,
		       int cacheflag);
extern void __iounmap(void *addr, unsigned long size);


extern inline void *z_remap_nocache_ser(unsigned long physaddr, 
					  unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_SER);
}

extern inline void *z_remap_nocache_nonser(unsigned long physaddr, 
					     unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_NOCACHE_NONSER);
}

extern inline void *z_remap_writethrough(unsigned long physaddr,
					   unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_WRITETHROUGH);
}
extern inline void *z_remap_fullcache(unsigned long physaddr,
					unsigned long size)
{
	return __ioremap(physaddr, size, IOMAP_FULL_CACHING);
}

#define z_unmap iounmap
#define z_iounmap iounmap
#define z_ioremap z_remap_nocache_ser

#endif /* _ASM_ZORRO_H */
