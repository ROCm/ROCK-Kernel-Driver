#include <linux/types.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/compiler.h>

#include <asm/i387.h>
#include <asm/hardirq.h> 
#include <asm/page.h>

/*
 *	MMX 3DNow! library helper functions
 *
 *	To do:
 *	We can use MMX just for prefetch in IRQ's. This may be a win. 
 *		(reported so on K6-III)
 *	We should use a better code neutral filler for the short jump
 *		leal ebx. [ebx] is apparently best for K6-2, but Cyrix ??
 *	We also want to clobber the filler register so we dont get any
 *		register forwarding stalls on the filler. 
 *
 *	Add *user handling. Checksums are not a win with MMX on any CPU
 *	tested so far for any MMX solution figured.
 *
 *	22/09/2000 - Arjan van de Ven 
 *		Improved for non-egineering-sample Athlons 
 *
 *	2002 Andi Kleen. Some cleanups and changes for x86-64. 
 *	Not really tuned yet. Using the Athlon version for now.
 *      This currenly uses MMX for 8 byte stores, but on hammer we could
 *      use integer 8 byte stores too and avoid the FPU save overhead.
 *	Disadvantage is that the integer load/stores have strong ordering
 *	model and may be slower.
 * 
 *	$Id$
 */

#ifdef MMX_MEMCPY_THRESH

 
void *_mmx_memcpy(void *to, const void *from, size_t len)
{
 	void *p;
 	int i;
  
 	p = to;

 	if (unlikely(in_interrupt()))
		goto standard;
	
	/* XXX: check if this is still memory bound with unaligned to/from.
	   if not align them here to 8bytes. */
 	i = len >> 6; /* len/64 */
 
 	kernel_fpu_begin();

	__asm__ __volatile__ (
		"   prefetch (%0)\n"		/* This set is 28 bytes */
		"   prefetch 64(%0)\n"
		"   prefetch 128(%0)\n"
		"   prefetch 192(%0)\n"
		"   prefetch 256(%0)\n"
		"\n"
		: : "r" (from) );
		
	for(; i>5; i--)
	{
		__asm__ __volatile__ (
		"  prefetch 320(%0)\n"
		"  movq (%0), %%mm0\n"
		"  movq 8(%0), %%mm1\n"
		"  movq 16(%0), %%mm2\n"
		"  movq 24(%0), %%mm3\n"
		"  movq %%mm0, (%1)\n"
		"  movq %%mm1, 8(%1)\n"
		"  movq %%mm2, 16(%1)\n"
		"  movq %%mm3, 24(%1)\n"
		"  movq 32(%0), %%mm0\n"
		"  movq 40(%0), %%mm1\n"
		"  movq 48(%0), %%mm2\n"
		"  movq 56(%0), %%mm3\n"
		"  movq %%mm0, 32(%1)\n"
		"  movq %%mm1, 40(%1)\n"
		"  movq %%mm2, 48(%1)\n"
		"  movq %%mm3, 56(%1)\n"
		: : "r" (from), "r" (to) : "memory");
		from+=64;
		to+=64;
	}

	for(; i>0; i--)
	{
		__asm__ __volatile__ (
		"  movq (%0), %%mm0\n"
		"  movq 8(%0), %%mm1\n"
		"  movq 16(%0), %%mm2\n"
		"  movq 24(%0), %%mm3\n"
		"  movq %%mm0, (%1)\n"
		"  movq %%mm1, 8(%1)\n"
		"  movq %%mm2, 16(%1)\n"
		"  movq %%mm3, 24(%1)\n"
		"  movq 32(%0), %%mm0\n"
		"  movq 40(%0), %%mm1\n"
		"  movq 48(%0), %%mm2\n"
		"  movq 56(%0), %%mm3\n"
		"  movq %%mm0, 32(%1)\n"
		"  movq %%mm1, 40(%1)\n"
		"  movq %%mm2, 48(%1)\n"
		"  movq %%mm3, 56(%1)\n"
		: : "r" (from), "r" (to) : "memory");
		from+=64;
		to+=64;
	}
	len &= 63; 	
	kernel_fpu_end();

	/*
	 *	Now do the tail of the block
	 */

 standard:
	__inline_memcpy(to, from, len);
	return p;
}
#endif

static inline void fast_clear_page(void *page)
{
	int i;

	kernel_fpu_begin();
	
	__asm__ __volatile__ (
		"  pxor %%mm0, %%mm0\n" : :
	);

	for(i=0;i<4096/64;i++)
	{
		__asm__ __volatile__ (
		"  movntq %%mm0, (%0)\n"
		"  movntq %%mm0, 8(%0)\n"
		"  movntq %%mm0, 16(%0)\n"
		"  movntq %%mm0, 24(%0)\n"
		"  movntq %%mm0, 32(%0)\n"
		"  movntq %%mm0, 40(%0)\n"
		"  movntq %%mm0, 48(%0)\n"
		"  movntq %%mm0, 56(%0)\n"
		: : "r" (page) : "memory");
		page+=64;
	}
	/* since movntq is weakly-ordered, a "sfence" is needed to become
	 * ordered again.
	 */
	__asm__ __volatile__ (
		"  sfence \n" : :
	);
	kernel_fpu_end();
}

static inline void fast_copy_page(void *to, void *from)
{
	int i;

	kernel_fpu_begin();

	/* maybe the prefetch stuff can go before the expensive fnsave...
	 * but that is for later. -AV
	 */
	__asm__ __volatile__ (
		"   prefetch (%0)\n"
		"   prefetch 64(%0)\n"
		"   prefetch 128(%0)\n"
		"   prefetch 192(%0)\n"
		"   prefetch 256(%0)\n"
		: : "r" (from) );

	for(i=0; i<(4096-320)/64; i++)
	{
		__asm__ __volatile__ (
		"   prefetch 320(%0)\n"
		"   movq (%0), %%mm0\n"
		"   movntq %%mm0, (%1)\n"
		"   movq 8(%0), %%mm1\n"
		"   movntq %%mm1, 8(%1)\n"
		"   movq 16(%0), %%mm2\n"
		"   movntq %%mm2, 16(%1)\n"
		"   movq 24(%0), %%mm3\n"
		"   movntq %%mm3, 24(%1)\n"
		"   movq 32(%0), %%mm4\n"
		"   movntq %%mm4, 32(%1)\n"
		"   movq 40(%0), %%mm5\n"
		"   movntq %%mm5, 40(%1)\n"
		"   movq 48(%0), %%mm6\n"
		"   movntq %%mm6, 48(%1)\n"
		"   movq 56(%0), %%mm7\n"
		"   movntq %%mm7, 56(%1)\n"
		: : "r" (from), "r" (to) : "memory");
		from+=64;
		to+=64;
	}
	for(i=(4096-320)/64; i<4096/64; i++)
	{
		__asm__ __volatile__ (
		"2: movq (%0), %%mm0\n"
		"   movntq %%mm0, (%1)\n"
		"   movq 8(%0), %%mm1\n"
		"   movntq %%mm1, 8(%1)\n"
		"   movq 16(%0), %%mm2\n"
		"   movntq %%mm2, 16(%1)\n"
		"   movq 24(%0), %%mm3\n"
		"   movntq %%mm3, 24(%1)\n"
		"   movq 32(%0), %%mm4\n"
		"   movntq %%mm4, 32(%1)\n"
		"   movq 40(%0), %%mm5\n"
		"   movntq %%mm5, 40(%1)\n"
		"   movq 48(%0), %%mm6\n"
		"   movntq %%mm6, 48(%1)\n"
		"   movq 56(%0), %%mm7\n"
		"   movntq %%mm7, 56(%1)\n"
		: : "r" (from), "r" (to) : "memory");
		from+=64;
		to+=64;
	}
	/* since movntq is weakly-ordered, a "sfence" is needed to become
	 * ordered again.
	 */
	__asm__ __volatile__ (
		"  sfence \n" : :
	);
	kernel_fpu_end();
}

void mmx_clear_page(void * page)
{
#if 1 
	__builtin_memset(page,0,PAGE_SIZE);
#else
	/* AK: these in_interrupt checks should not be needed. */
	if(unlikely(in_interrupt()))
		__builtin_memset(page,0,PAGE_SIZE);
	else
		fast_clear_page(page);
#endif
}

void mmx_copy_page(void *to, void *from)
{
#if 1
	__builtin_memcpy(to,from,PAGE_SIZE);
#else
	/* AK: these in_interrupt checks should not be needed. */
	if(unlikely(in_interrupt()))
		__builtin_memcpy(to,from,PAGE_SIZE);
	else
		fast_copy_page(to, from);
#endif
}
