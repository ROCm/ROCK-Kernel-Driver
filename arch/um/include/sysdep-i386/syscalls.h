/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "asm/unistd.h"
#include "sysdep/ptrace.h"

typedef long syscall_handler_t(struct pt_regs);

#define EXECUTE_SYSCALL(syscall, regs) \
	((long (*)(struct syscall_args)) (*sys_call_table[syscall]))(SYSCALL_ARGS(&regs->regs))

extern syscall_handler_t sys_modify_ldt;
extern syscall_handler_t old_mmap_i386;
extern syscall_handler_t old_select;
extern syscall_handler_t sys_ni_syscall;

#define ARCH_SYSCALLS \
	[ __NR_mmap ] = old_mmap_i386, \
	[ __NR_select ] = old_select, \
	[ __NR_vm86old ] = sys_ni_syscall, \
        [ __NR_modify_ldt ] = sys_modify_ldt, \
	[ __NR_lchown32 ] = sys_lchown, \
	[ __NR_getuid32 ] = sys_getuid, \
	[ __NR_getgid32 ] = sys_getgid, \
	[ __NR_geteuid32 ] = sys_geteuid, \
	[ __NR_getegid32 ] = sys_getegid, \
	[ __NR_setreuid32 ] = sys_setreuid, \
	[ __NR_setregid32 ] = sys_setregid, \
	[ __NR_getgroups32 ] = sys_getgroups, \
	[ __NR_setgroups32 ] = sys_setgroups, \
	[ __NR_fchown32 ] = sys_fchown, \
	[ __NR_setresuid32 ] = sys_setresuid, \
	[ __NR_getresuid32 ] = sys_getresuid, \
	[ __NR_setresgid32 ] = sys_setresgid, \
	[ __NR_getresgid32 ] = sys_getresgid, \
	[ __NR_chown32 ] = sys_chown, \
	[ __NR_setuid32 ] = sys_setuid, \
	[ __NR_setgid32 ] = sys_setgid, \
	[ __NR_setfsuid32 ] = sys_setfsuid, \
	[ __NR_setfsgid32 ] = sys_setfsgid, \
	[ __NR_pivot_root ] = sys_pivot_root, \
	[ __NR_mincore ] = sys_mincore, \
	[ __NR_madvise ] = sys_madvise, \
        [ 222 ] = sys_ni_syscall, 
        
/* 222 doesn't yet have a name in include/asm-i386/unistd.h */

#define LAST_ARCH_SYSCALL 222

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
