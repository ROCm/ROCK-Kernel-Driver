#include "syscall_32.c"

#include <linux/thread_info.h>

#ifdef TIF_CSTAR
extern asmlinkage void cstar_set_tif(void);

#define	ptregs_fork cstar_set_tif
#define	ptregs_clone cstar_set_tif
#define	ptregs_vfork cstar_set_tif

__visible const sys_call_ptr_t cstar_call_table[__NR_syscall_compat_max+1] = {
	/*
	 * Smells like a compiler bug -- it doesn't work
	 * when the & below is removed.
	 */
	[0 ... __NR_syscall_compat_max] = &sys_ni_syscall,
#include <asm/syscalls_32.h>
};
#endif /* TIF_CSTAR */
