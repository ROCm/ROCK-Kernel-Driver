/* 
 * Copyright (C) 2000, 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __TT_UACCESS_H
#define __TT_UACCESS_H

#include "linux/string.h"
#include "linux/sched.h"
#include "asm/processor.h"
#include "asm/errno.h"
#include "asm/current.h"
#include "asm/a.out.h"
#include "uml_uaccess.h"

#define ABOVE_KMEM (16 * 1024 * 1024)

extern unsigned long end_vm;
extern unsigned long uml_physmem;

#define under_task_size(addr, size) \
	(((unsigned long) (addr) < TASK_SIZE) && \
         (((unsigned long) (addr) + (size)) < TASK_SIZE))

#define is_stack(addr, size) \
	(((unsigned long) (addr) < STACK_TOP) && \
	 ((unsigned long) (addr) >= STACK_TOP - ABOVE_KMEM) && \
	 (((unsigned long) (addr) + (size)) <= STACK_TOP))

#define access_ok_tt(type, addr, size) \
	((type == VERIFY_READ) || (segment_eq(get_fs(), KERNEL_DS)) || \
         (((unsigned long) (addr) <= ((unsigned long) (addr) + (size))) && \
          (under_task_size(addr, size) || is_stack(addr, size))))

static inline int verify_area_tt(int type, const void * addr, 
				 unsigned long size)
{
	return(access_ok_tt(type, addr, size) ? 0 : -EFAULT);
}

extern unsigned long get_fault_addr(void);

extern int __do_copy_from_user(void *to, const void *from, int n,
			       void **fault_addr, void **fault_catcher);

static inline int copy_from_user_tt(void *to, const void *from, int n)
{
	return(access_ok_tt(VERIFY_READ, from, n) ?
	       __do_copy_from_user(to, from, n, 
				   &current->thread.fault_addr,
				   &current->thread.fault_catcher) : n);
}

static inline int copy_to_user_tt(void *to, const void *from, int n)
{
	return(access_ok_tt(VERIFY_WRITE, to, n) ?
	       __do_copy_to_user(to, from, n, 
				   &current->thread.fault_addr,
				   &current->thread.fault_catcher) : n);
}

extern int __do_strncpy_from_user(char *dst, const char *src, size_t n,
				  void **fault_addr, void **fault_catcher);

static inline int strncpy_from_user_tt(char *dst, const char *src, int count)
{
	int n;

	if(!access_ok_tt(VERIFY_READ, src, 1)) return(-EFAULT);
	n = __do_strncpy_from_user(dst, src, count, 
				   &current->thread.fault_addr,
				   &current->thread.fault_catcher);
	if(n < 0) return(-EFAULT);
	return(n);
}

extern int __do_clear_user(void *mem, size_t len, void **fault_addr,
			   void **fault_catcher);

static inline int __clear_user_tt(void *mem, int len)
{
	return(__do_clear_user(mem, len,
			       &current->thread.fault_addr,
			       &current->thread.fault_catcher));
}

static inline int clear_user_tt(void *mem, int len)
{
	return(access_ok_tt(VERIFY_WRITE, mem, len) ? 
	       __do_clear_user(mem, len, 
			       &current->thread.fault_addr,
			       &current->thread.fault_catcher) : len);
}

extern int __do_strnlen_user(const char *str, unsigned long n,
			     void **fault_addr, void **fault_catcher);

static inline int strnlen_user_tt(const void *str, int len)
{
	return(__do_strnlen_user(str, len,
				 &current->thread.fault_addr,
				 &current->thread.fault_catcher));
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
