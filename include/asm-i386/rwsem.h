/* rwsem.h: R/W semaphores optimised using i386 assembly
 *
 * Written by David Howells (dhowells@redhat.com).
 *
 * Derived from asm-i386/semaphore.h
 */

#ifndef _I386_RWSEM_H
#define _I386_RWSEM_H

#ifndef _LINUX_RWSEM_H
#error please dont include asm/rwsem.h directly, use linux/rwsem.h instead
#endif

#ifdef __KERNEL__

#ifdef CONFIG_X86_XADD
#include <asm/rwsem-xadd.h> /* use XADD based semaphores if possible */
#else
#include <asm/rwsem-spin.h> /* use optimised spinlock based semaphores otherwise */
#endif

/* we use FASTCALL convention for the helpers */
extern struct rw_semaphore *FASTCALL(__rwsem_down_read_failed(struct rw_semaphore *sem));
extern struct rw_semaphore *FASTCALL(__rwsem_down_write_failed(struct rw_semaphore *sem));
extern struct rw_semaphore *FASTCALL(__rwsem_wake(struct rw_semaphore *sem));

#endif /* __KERNEL__ */
#endif /* _I386_RWSEM_H */
