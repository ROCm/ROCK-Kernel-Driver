/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SKAS_UACCESS_H
#define __SKAS_UACCESS_H

#include "linux/string.h"
#include "linux/sched.h"
#include "linux/err.h"
#include "asm/processor.h"
#include "asm/pgtable.h"
#include "asm/errno.h"
#include "asm/current.h"
#include "asm/a.out.h"
#include "kern_util.h"

#define access_ok_skas(type, addr, size) \
	((segment_eq(get_fs(), KERNEL_DS)) || \
	 (((unsigned long) (addr) < TASK_SIZE) && \
	  ((unsigned long) (addr) + (size) < TASK_SIZE)))

static inline int verify_area_skas(int type, const void * addr, 
				   unsigned long size)
{
	return(access_ok_skas(type, addr, size) ? 0 : -EFAULT);
}

static inline unsigned long maybe_map(unsigned long virt, int is_write)
{
	pte_t pte;

	void *phys = um_virt_to_phys(current, virt, &pte);
	int dummy_code;

	if(IS_ERR(phys) || (is_write && !pte_write(pte))){
		if(handle_page_fault(virt, 0, is_write, 0, &dummy_code))
			return(0);
		phys = um_virt_to_phys(current, virt, NULL);
	}
	return((unsigned long) __va((unsigned long) phys));
}

static inline int buffer_op(unsigned long addr, int len, 
			    int (*op)(unsigned long addr, int len, void *arg),
			    void *arg)
{
	int size = min(PAGE_ALIGN(addr) - addr, (unsigned long) len);
	int remain = len, n;

	n = (*op)(addr, size, arg);
	if(n != 0)
		return(n < 0 ? remain : 0);

	addr += size;
	remain -= size;
	if(remain == 0) 
		return(0);

	while(addr < ((addr + remain) & PAGE_MASK)){
		n = (*op)(addr, PAGE_SIZE, arg);
		if(n != 0)
			return(n < 0 ? remain : 0);

		addr += PAGE_SIZE;
		remain -= PAGE_SIZE;
	}
	if(remain == 0)
		return(0);

	n = (*op)(addr, remain, arg);
	if(n != 0)
		return(n < 0 ? remain : 0);
	return(0);
}

static inline int copy_chunk_from_user(unsigned long from, int len, void *arg)
{
	unsigned long *to_ptr = arg, to = *to_ptr;

	from = maybe_map(from, 0);
	if(from == 0)
		return(-1);

	memcpy((void *) to, (void *) from, len);
	*to_ptr += len;
	return(0);
}

static inline int copy_from_user_skas(void *to, const void *from, int n)
{
	if(segment_eq(get_fs(), KERNEL_DS)){
		memcpy(to, from, n);
		return(0);
	}

	return(access_ok_skas(VERIFY_READ, from, n) ?
	       buffer_op((unsigned long) from, n, copy_chunk_from_user, &to) :
	       n);
}

static inline int copy_chunk_to_user(unsigned long to, int len, void *arg)
{
	unsigned long *from_ptr = arg, from = *from_ptr;

	to = maybe_map(to, 1);
	if(to == 0)
		return(-1);

	memcpy((void *) to, (void *) from, len);
	*from_ptr += len;
	return(0);
}

static inline int copy_to_user_skas(void *to, const void *from, int n)
{
	if(segment_eq(get_fs(), KERNEL_DS)){
		memcpy(to, from, n);
		return(0);
	}

	return(access_ok_skas(VERIFY_WRITE, to, n) ?
	       buffer_op((unsigned long) to, n, copy_chunk_to_user, &from) :
	       n);
}

static inline int strncpy_chunk_from_user(unsigned long from, int len, 
					  void *arg)
{
        char **to_ptr = arg, *to = *to_ptr;
	int n;

	from = maybe_map(from, 0);
	if(from == 0)
		return(-1);

	strncpy(to, (void *) from, len);
	n = strnlen(to, len);
	*to_ptr += n;

	if(n < len) 
	        return(1);
	return(0);
}

static inline int strncpy_from_user_skas(char *dst, const char *src, int count)
{
	int n;
	char *ptr = dst;

	if(segment_eq(get_fs(), KERNEL_DS)){
		strncpy(dst, src, count);
		return(strnlen(dst, count));
	}

	if(!access_ok_skas(VERIFY_READ, src, 1))
		return(-EFAULT);

	n = buffer_op((unsigned long) src, count, strncpy_chunk_from_user, 
		      &ptr);
	if(n != 0)
		return(-EFAULT);
	return(strnlen(dst, count));
}

static inline int clear_chunk(unsigned long addr, int len, void *unused)
{
	addr = maybe_map(addr, 1);
	if(addr == 0) 
		return(-1);

	memset((void *) addr, 0, len);
	return(0);
}

static inline int __clear_user_skas(void *mem, int len)
{
	return(buffer_op((unsigned long) mem, len, clear_chunk, NULL));
}

static inline int clear_user_skas(void *mem, int len)
{
	if(segment_eq(get_fs(), KERNEL_DS)){
		memset(mem, 0, len);
		return(0);
	}

	return(access_ok_skas(VERIFY_WRITE, mem, len) ? 
	       buffer_op((unsigned long) mem, len, clear_chunk, NULL) : len);
}

static inline int strnlen_chunk(unsigned long str, int len, void *arg)
{
	int *len_ptr = arg, n;

	str = maybe_map(str, 0);
	if(str == 0) 
		return(-1);

	n = strnlen((void *) str, len);
	*len_ptr += n;

	if(n < len)
		return(1);
	return(0);
}

static inline int strnlen_user_skas(const void *str, int len)
{
	int count = 0, n;

	if(segment_eq(get_fs(), KERNEL_DS))
		return(strnlen(str, len) + 1);

	n = buffer_op((unsigned long) str, len, strnlen_chunk, &count);
	if(n == 0)
		return(count + 1);
	return(-EFAULT);
}

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
