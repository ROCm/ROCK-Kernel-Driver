/*
 * include/asm-parisc/cache.h
 */

#ifndef __ARCH_PARISC_CACHE_H
#define __ARCH_PARISC_CACHE_H

/*
** XXX FIXME : L1_CACHE_BYTES (cacheline size) should be a boot time thing.
** 
** 32-bit on PA2.0 is not covered well by the #ifdef __LP64__ below.
** PA2.0 processors have 64-byte cachelines.
**
** The issue is mostly cacheline ping-ponging on SMP boxes.
** To avoid this, code should define stuff to be per CPU on cacheline
** aligned boundaries. This can make a 2x or more difference in perf
** depending on how badly the thrashing is.
**
** We don't need to worry about I/O since all PA2.0 boxes (except T600)
** are I/O coherent. That means flushing less than you needed to generally
** doesn't matter - the I/O MMU will read/modify/write the cacheline.
**
** (Digression: it is possible to program I/O MMU's to not first read
** a cacheline for inbound data - ie just grab ownership and start writing.
** While it improves I/O throughput, you gotta know the device driver
** is well behaved and can deal with the issues.)
*/
#if defined(__LP64__)
#define L1_CACHE_BYTES 64
#else
#define L1_CACHE_BYTES 32
#endif

#define L1_CACHE_ALIGN(x)       (((x)+(L1_CACHE_BYTES-1))&~(L1_CACHE_BYTES-1))

#define SMP_CACHE_BYTES L1_CACHE_BYTES

#define __cacheline_aligned __attribute__((__aligned__(L1_CACHE_BYTES)))

extern void init_cache(void);		/* initializes cache-flushing */
extern void flush_data_cache(void);	/* flushes data-cache only */
extern void flush_instruction_cache(void);/* flushes code-cache only */
extern void flush_all_caches(void);	/* flushes code and data-cache */

extern int get_cache_info(char *);

extern struct pdc_cache_info cache_info;

#define fdce(addr) asm volatile("fdce 0(%0)" : : "r" (addr))
#define fice(addr) asm volatile("fice 0(%%sr1,%0)" : : "r" (addr))

#define pdtlbe(addr) asm volatile("pdtlbe 0(%%sr1,%0)" : : "r" (addr))
#define pdtlb_kernel(addr)  asm volatile("pdtlb 0(%0)" : : "r" (addr));
#define pitlbe(addr) asm volatile("pitlbe 0(%%sr1,%0)" : : "r" (addr))

#define kernel_fdc(addr) asm volatile("fdc 0(%%sr0, %0)" : : "r" (addr))

#endif
