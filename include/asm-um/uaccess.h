/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_UACCESS_H
#define __UM_UACCESS_H

#include "linux/string.h"
#include "linux/sched.h"
#include "asm/processor.h"
#include "asm/errno.h"
#include "asm/current.h"
#include "asm/a.out.h"

#define VERIFY_READ 0
#define VERIFY_WRITE 1

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })

#define ABOVE_KMEM (16 * 1024 * 1024)

#define KERNEL_DS	MAKE_MM_SEG(0xFFFFFFFF)
#define USER_DS		MAKE_MM_SEG(TASK_SIZE)

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))

extern unsigned long end_vm;
extern unsigned long uml_physmem;

#define under_task_size(addr, size) \
	(((unsigned long) (addr) < TASK_SIZE) && \
         (((unsigned long) (addr) + (size)) < TASK_SIZE))

#define is_stack(addr, size) \
	(((unsigned long) (addr) < STACK_TOP) && \
	 ((unsigned long) (addr) >= STACK_TOP - ABOVE_KMEM) && \
	 (((unsigned long) (addr) + (size)) <= STACK_TOP))

#define segment_eq(a, b) ((a).seg == (b).seg)

#define access_ok(type, addr, size) \
	((type == VERIFY_READ) || (segment_eq(get_fs(), KERNEL_DS)) || \
         (((unsigned long) (addr) <= ((unsigned long) (addr) + (size))) && \
          (under_task_size(addr, size) || is_stack(addr, size))))

static inline int verify_area(int type, const void * addr, unsigned long size)
{
	return(access_ok(type, addr, size) ? 0 : -EFAULT);
}

extern unsigned long get_fault_addr(void);

extern int __do_copy_from_user(void *to, const void *from, int n,
				  void **fault_addr, void **fault_catcher);

static inline int copy_from_user(void *to, const void *from, int n)
{
	return(access_ok(VERIFY_READ, from, n) ?
	       __do_copy_from_user(to, from, n, 
				   &current->thread.fault_addr,
				   &current->thread.fault_catcher) : n);
}

#define __copy_from_user(to, from, n) copy_from_user(to, from, n)

extern int __do_copy_to_user(void *to, const void *from, int n,
				  void **fault_addr, void **fault_catcher);

static inline int copy_to_user(void *to, const void *from, int n)
{
	return(access_ok(VERIFY_WRITE, to, n) ?
	       __do_copy_to_user(to, from, n, 
				   &current->thread.fault_addr,
				   &current->thread.fault_catcher) : n);
}

#define __copy_to_user(to, from, n) copy_to_user(to, from, n)

#define __get_user(x, ptr) \
({ \
        const __typeof__(ptr) __private_ptr = ptr; \
        __typeof__(*(__private_ptr)) __private_val; \
        int __private_ret = -EFAULT; \
        (x) = 0; \
	if (__copy_from_user(&__private_val, (__private_ptr), \
	    sizeof(*(__private_ptr))) == 0) {\
        	(x) = (__typeof__(*(__private_ptr))) __private_val; \
		__private_ret = 0; \
	} \
        __private_ret; \
}) 

#define get_user(x, ptr) \
({ \
        const __typeof__((*ptr)) *private_ptr = (ptr); \
        (access_ok(VERIFY_READ, private_ptr, sizeof(*private_ptr)) ? \
	 __get_user(x, private_ptr) : ((x) = 0, -EFAULT)); \
})

#define __put_user(x, ptr) \
({ \
        __typeof__(ptr) __private_ptr = ptr; \
        __typeof__(*(__private_ptr)) __private_val; \
        int __private_ret = -EFAULT; \
        __private_val = (__typeof__(*(__private_ptr))) (x); \
        if (__copy_to_user((__private_ptr), &__private_val, \
			   sizeof(*(__private_ptr))) == 0) { \
		__private_ret = 0; \
	} \
        __private_ret; \
})

#define put_user(x, ptr) \
({ \
        __typeof__(*(ptr)) *private_ptr = (ptr); \
        (access_ok(VERIFY_WRITE, private_ptr, sizeof(*private_ptr)) ? \
	 __put_user(x, private_ptr) : -EFAULT); \
})

extern int __do_strncpy_from_user(char *dst, const char *src, size_t n,
				  void **fault_addr, void **fault_catcher);

static inline int strncpy_from_user(char *dst, const char *src, int count)
{
	int n;

	if(!access_ok(VERIFY_READ, src, 1)) return(-EFAULT);
	n = __do_strncpy_from_user(dst, src, count, 
				   &current->thread.fault_addr,
				   &current->thread.fault_catcher);
	if(n < 0) return(-EFAULT);
	return(n);
}

extern int __do_clear_user(void *mem, size_t len, void **fault_addr,
			   void **fault_catcher);

static inline int __clear_user(void *mem, int len)
{
	return(__do_clear_user(mem, len,
			       &current->thread.fault_addr,
			       &current->thread.fault_catcher));
}

static inline int clear_user(void *mem, int len)
{
	return(access_ok(VERIFY_WRITE, mem, len) ? 
	       __do_clear_user(mem, len, 
			       &current->thread.fault_addr,
			       &current->thread.fault_catcher) : len);
}

extern int __do_strnlen_user(const char *str, unsigned long n,
			     void **fault_addr, void **fault_catcher);

static inline int strnlen_user(void *str, int len)
{
	return(__do_strnlen_user(str, len,
				 &current->thread.fault_addr,
				 &current->thread.fault_catcher));
}

#define strlen_user(str) strnlen_user(str, ~0UL >> 1)

struct exception_table_entry
{
        unsigned long insn;
	unsigned long fixup;
};

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
