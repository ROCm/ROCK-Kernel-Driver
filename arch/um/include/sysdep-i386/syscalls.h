/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "asm/unistd.h"
#include "sysdep/ptrace.h"

typedef long syscall_handler_t(struct pt_regs);

#define EXECUTE_SYSCALL(syscall, regs) \
	((long (*)(struct syscall_args)) (*sys_call_table[syscall]))(SYSCALL_ARGS(&regs->regs))

#define ARCH_SYSCALLS \
	[ __NR_mmap ] = (syscall_handler_t *) old_mmap_i386, \
	[ __NR_select ] = (syscall_handler_t *) old_select, \
	[ __NR_vm86old ] = (syscall_handler_t *) sys_ni_syscall, \
        [ __NR_modify_ldt ] = (syscall_handler_t *) sys_modify_ldt, \
	[ __NR_lchown32 ] = (syscall_handler_t *) sys_lchown, \
	[ __NR_getuid32 ] = (syscall_handler_t *) sys_getuid, \
	[ __NR_getgid32 ] = (syscall_handler_t *) sys_getgid, \
	[ __NR_geteuid32 ] = (syscall_handler_t *) sys_geteuid, \
	[ __NR_getegid32 ] = (syscall_handler_t *) sys_getegid, \
	[ __NR_setreuid32 ] = (syscall_handler_t *) sys_setreuid, \
	[ __NR_setregid32 ] = (syscall_handler_t *) sys_setregid, \
	[ __NR_getgroups32 ] = (syscall_handler_t *) sys_getgroups, \
	[ __NR_setgroups32 ] = (syscall_handler_t *) sys_setgroups, \
	[ __NR_fchown32 ] = (syscall_handler_t *) sys_fchown, \
	[ __NR_setresuid32 ] = (syscall_handler_t *) sys_setresuid, \
	[ __NR_getresuid32 ] = (syscall_handler_t *) sys_getresuid, \
	[ __NR_setresgid32 ] = (syscall_handler_t *) sys_setresgid, \
	[ __NR_getresgid32 ] = (syscall_handler_t *) sys_getresgid, \
	[ __NR_chown32 ] = (syscall_handler_t *) sys_chown, \
	[ __NR_setuid32 ] = (syscall_handler_t *) sys_setuid, \
	[ __NR_setgid32 ] = (syscall_handler_t *) sys_setgid, \
	[ __NR_setfsuid32 ] = (syscall_handler_t *) sys_setfsuid, \
	[ __NR_setfsgid32 ] = (syscall_handler_t *) sys_setfsgid, \
	[ __NR_pivot_root ] = (syscall_handler_t *) sys_pivot_root, \
	[ __NR_mincore ] = (syscall_handler_t *) sys_mincore, \
	[ __NR_madvise ] = (syscall_handler_t *) sys_madvise, \
        [ 222 ] = (syscall_handler_t *) sys_ni_syscall,
        
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
