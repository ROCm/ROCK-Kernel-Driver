#ifndef __ASM_PROC_MM
#define __ASM_PROC_MM
#include <linux/types.h>

#include <asm/compat.h>

struct mm_mmap32 {
	compat_ulong_t addr;
	compat_ulong_t len;
	compat_ulong_t prot;
	compat_ulong_t flags;
	compat_ulong_t fd;
	compat_ulong_t offset;
};

struct mm_munmap32 {
	compat_ulong_t addr;
	compat_ulong_t len;
};

struct mm_mprotect32 {
	compat_ulong_t addr;
	compat_ulong_t len;
        compat_uint_t prot;
};

struct proc_mm_op32 {
	compat_int_t op;
	union {
		struct mm_mmap32 mmap;
		struct mm_munmap32 munmap;
	        struct mm_mprotect32 mprotect;
		compat_int_t copy_segments;
	} u;
};

extern ssize_t write_proc_mm_emul(struct file *file, const char *buffer,
			     size_t count, loff_t *ppos);

extern long do64_mmap(struct mm_struct *mm, unsigned long addr, unsigned long len, unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long off);

static inline long __do_mmap(struct mm_struct *mm, unsigned long addr,
		     unsigned long len, unsigned long prot,
		     unsigned long flags, unsigned long fd,
		     unsigned long off)
{
	/* The latter one is stricter, since will actually check that off is page
	 * aligned. The first one skipped the check. */

	/* return do32_mmap2(mm, addr, len, prot, flags, fd, off >>
	 * PAGE_SHIFT);*/
	return do64_mmap(mm, addr, len, prot, flags, fd, off);
}

#endif /* __ASM_PROC_MM */
