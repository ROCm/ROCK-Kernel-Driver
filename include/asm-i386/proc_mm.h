#ifndef __ASM_PROC_MM
#define __ASM_PROC_MM

#include <asm/page.h>

extern long do_mmap2(struct mm_struct *mm, unsigned long addr,
		unsigned long len, unsigned long prot, unsigned long flags,
		unsigned long fd, unsigned long pgoff);

static inline long __do_mmap(struct mm_struct *mm, unsigned long addr,
		     unsigned long len, unsigned long prot,
		     unsigned long flags, unsigned long fd,
		     unsigned long off)
{
	return do_mmap2(mm, addr, len, prot, flags, fd, off >> PAGE_SHIFT);
}

#endif /* __ASM_PROC_MM */
