/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/config.h"
#include "linux/unistd.h"
#include "linux/sys.h"
#include "linux/swap.h"
#include "linux/syscalls.h"
#include "linux/sysctl.h"
#include "asm/signal.h"
#include "sysdep/syscalls.h"
#include "kern_util.h"

#ifdef CONFIG_NFSD
#define NFSSERVCTL sys_nfsservctl
#else
#define NFSSERVCTL sys_ni_syscall
#endif

#define LAST_GENERIC_SYSCALL __NR_vserver

#if LAST_GENERIC_SYSCALL > LAST_ARCH_SYSCALL
#define LAST_SYSCALL LAST_GENERIC_SYSCALL
#else
#define LAST_SYSCALL LAST_ARCH_SYSCALL
#endif

extern syscall_handler_t sys_fork;
extern syscall_handler_t sys_execve;
extern syscall_handler_t um_time;
extern syscall_handler_t um_mount;
extern syscall_handler_t um_stime;
extern syscall_handler_t sys_ptrace;
extern syscall_handler_t sys_pipe;
extern syscall_handler_t sys_olduname;
extern syscall_handler_t sys_sigaction;
extern syscall_handler_t sys_sigsuspend;
extern syscall_handler_t old_readdir;
extern syscall_handler_t sys_uname;
extern syscall_handler_t sys_ipc;
extern syscall_handler_t sys_sigreturn;
extern syscall_handler_t sys_clone;
extern syscall_handler_t sys_rt_sigreturn;
extern syscall_handler_t sys_rt_sigaction;
extern syscall_handler_t sys_sigaltstack;
extern syscall_handler_t sys_vfork;
extern syscall_handler_t sys_mmap2;
extern syscall_handler_t old_mmap_i386;
extern syscall_handler_t old_select;
extern syscall_handler_t sys_modify_ldt;
extern syscall_handler_t sys_rt_sigsuspend;

syscall_handler_t *sys_call_table[] = {
	[ __NR_restart_syscall ] = (syscall_handler_t *) sys_restart_syscall,
	[ __NR_exit ] (syscall_handler_t *) sys_exit,
	[ __NR_fork ] (syscall_handler_t *) sys_fork,
	[ __NR_read ] = (syscall_handler_t *) sys_read,
	[ __NR_write ] = (syscall_handler_t *) sys_write,

	/* These three are declared differently in asm/unistd.h */
	[ __NR_open ] = (syscall_handler_t *) sys_open,
	[ __NR_close ] = (syscall_handler_t *) sys_close,
	[ __NR_waitpid ] = (syscall_handler_t *) sys_waitpid,
	[ __NR_creat ] (syscall_handler_t *) sys_creat,
	[ __NR_link ] (syscall_handler_t *) sys_link,
	[ __NR_unlink ] (syscall_handler_t *) sys_unlink,
	[ __NR_execve ] = (syscall_handler_t *) sys_execve,

	/* declared differently in kern_util.h */
	[ __NR_chdir ] (syscall_handler_t *) sys_chdir,
	[ __NR_time ] = um_time,
	[ __NR_mknod ] (syscall_handler_t *) sys_mknod,
	[ __NR_chmod ] (syscall_handler_t *) sys_chmod,
	[ __NR_lchown ] (syscall_handler_t *) sys_lchown16,
	[ __NR_break ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_oldstat ] (syscall_handler_t *) sys_stat,
	[ __NR_lseek ] = (syscall_handler_t *) sys_lseek,
	[ __NR_getpid ] (syscall_handler_t *) sys_getpid,
	[ __NR_mount ] = um_mount,
	[ __NR_umount ] (syscall_handler_t *) sys_oldumount,
	[ __NR_setuid ] (syscall_handler_t *) sys_setuid16,
	[ __NR_getuid ] (syscall_handler_t *) sys_getuid16,
	[ __NR_stime ] = um_stime,
	[ __NR_ptrace ] (syscall_handler_t *) sys_ptrace,
	[ __NR_alarm ] (syscall_handler_t *) sys_alarm,
	[ __NR_oldfstat ] (syscall_handler_t *) sys_fstat,
	[ __NR_pause ] (syscall_handler_t *) sys_pause,
	[ __NR_utime ] (syscall_handler_t *) sys_utime,
	[ __NR_stty ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_gtty ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_access ] (syscall_handler_t *) sys_access,
	[ __NR_nice ] (syscall_handler_t *) sys_nice,
	[ __NR_ftime ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_sync ] (syscall_handler_t *) sys_sync,
	[ __NR_kill ] (syscall_handler_t *) sys_kill,
	[ __NR_rename ] (syscall_handler_t *) sys_rename,
	[ __NR_mkdir ] (syscall_handler_t *) sys_mkdir,
	[ __NR_rmdir ] (syscall_handler_t *) sys_rmdir,

	/* Declared differently in asm/unistd.h */
	[ __NR_dup ] = (syscall_handler_t *) sys_dup,
	[ __NR_pipe ] (syscall_handler_t *) sys_pipe,
	[ __NR_times ] (syscall_handler_t *) sys_times,
	[ __NR_prof ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_brk ] (syscall_handler_t *) sys_brk,
	[ __NR_setgid ] (syscall_handler_t *) sys_setgid16,
	[ __NR_getgid ] (syscall_handler_t *) sys_getgid16,
	[ __NR_signal ] (syscall_handler_t *) sys_signal,
	[ __NR_geteuid ] (syscall_handler_t *) sys_geteuid16,
	[ __NR_getegid ] (syscall_handler_t *) sys_getegid16,
	[ __NR_acct ] (syscall_handler_t *) sys_acct,
	[ __NR_umount2 ] (syscall_handler_t *) sys_umount,
	[ __NR_lock ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_ioctl ] (syscall_handler_t *) sys_ioctl,
	[ __NR_fcntl ] (syscall_handler_t *) sys_fcntl,
	[ __NR_mpx ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_setpgid ] (syscall_handler_t *) sys_setpgid,
	[ __NR_ulimit ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_oldolduname ] (syscall_handler_t *) sys_olduname,
	[ __NR_umask ] (syscall_handler_t *) sys_umask,
	[ __NR_chroot ] (syscall_handler_t *) sys_chroot,
	[ __NR_ustat ] (syscall_handler_t *) sys_ustat,
	[ __NR_dup2 ] (syscall_handler_t *) sys_dup2,
	[ __NR_getppid ] (syscall_handler_t *) sys_getppid,
	[ __NR_getpgrp ] (syscall_handler_t *) sys_getpgrp,
	[ __NR_setsid ] = (syscall_handler_t *) sys_setsid,
	[ __NR_sigaction ] (syscall_handler_t *) sys_sigaction,
	[ __NR_sgetmask ] (syscall_handler_t *) sys_sgetmask,
	[ __NR_ssetmask ] (syscall_handler_t *) sys_ssetmask,
	[ __NR_setreuid ] (syscall_handler_t *) sys_setreuid16,
	[ __NR_setregid ] (syscall_handler_t *) sys_setregid16,
	[ __NR_sigsuspend ] (syscall_handler_t *) sys_sigsuspend,
	[ __NR_sigpending ] (syscall_handler_t *) sys_sigpending,
	[ __NR_sethostname ] (syscall_handler_t *) sys_sethostname,
	[ __NR_setrlimit ] (syscall_handler_t *) sys_setrlimit,
	[ __NR_getrlimit ] (syscall_handler_t *) sys_old_getrlimit,
	[ __NR_getrusage ] (syscall_handler_t *) sys_getrusage,
	[ __NR_gettimeofday ] (syscall_handler_t *) sys_gettimeofday,
	[ __NR_settimeofday ] (syscall_handler_t *) sys_settimeofday,
	[ __NR_getgroups ] (syscall_handler_t *) sys_getgroups16,
	[ __NR_setgroups ] (syscall_handler_t *) sys_setgroups16,
	[ __NR_symlink ] (syscall_handler_t *) sys_symlink,
	[ __NR_oldlstat ] (syscall_handler_t *) sys_lstat,
	[ __NR_readlink ] (syscall_handler_t *) sys_readlink,
	[ __NR_uselib ] (syscall_handler_t *) sys_uselib,
	[ __NR_swapon ] = (syscall_handler_t *) sys_swapon,
	[ __NR_reboot ] (syscall_handler_t *) sys_reboot,
	[ __NR_readdir ] = old_readdir,
	[ __NR_munmap ] (syscall_handler_t *) sys_munmap,
	[ __NR_truncate ] (syscall_handler_t *) sys_truncate,
	[ __NR_ftruncate ] (syscall_handler_t *) sys_ftruncate,
	[ __NR_fchmod ] (syscall_handler_t *) sys_fchmod,
	[ __NR_fchown ] (syscall_handler_t *) sys_fchown16,
	[ __NR_getpriority ] (syscall_handler_t *) sys_getpriority,
	[ __NR_setpriority ] (syscall_handler_t *) sys_setpriority,
	[ __NR_profil ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_statfs ] (syscall_handler_t *) sys_statfs,
	[ __NR_fstatfs ] (syscall_handler_t *) sys_fstatfs,
	[ __NR_ioperm ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_socketcall ] (syscall_handler_t *) sys_socketcall,
	[ __NR_syslog ] (syscall_handler_t *) sys_syslog,
	[ __NR_setitimer ] (syscall_handler_t *) sys_setitimer,
	[ __NR_getitimer ] (syscall_handler_t *) sys_getitimer,
	[ __NR_stat ] (syscall_handler_t *) sys_newstat,
	[ __NR_lstat ] (syscall_handler_t *) sys_newlstat,
	[ __NR_fstat ] (syscall_handler_t *) sys_newfstat,
	[ __NR_olduname ] (syscall_handler_t *) sys_uname,
	[ __NR_iopl ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_vhangup ] (syscall_handler_t *) sys_vhangup,
	[ __NR_idle ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_wait4 ] = (syscall_handler_t *) sys_wait4,
	[ __NR_swapoff ] = (syscall_handler_t *) sys_swapoff,
	[ __NR_sysinfo ] (syscall_handler_t *) sys_sysinfo,
	[ __NR_ipc ] (syscall_handler_t *) sys_ipc,
	[ __NR_fsync ] (syscall_handler_t *) sys_fsync,
	[ __NR_sigreturn ] (syscall_handler_t *) sys_sigreturn,
	[ __NR_clone ] (syscall_handler_t *) sys_clone,
	[ __NR_setdomainname ] (syscall_handler_t *) sys_setdomainname,
	[ __NR_uname ] (syscall_handler_t *) sys_newuname,
	[ __NR_adjtimex ] (syscall_handler_t *) sys_adjtimex,
	[ __NR_mprotect ] (syscall_handler_t *) sys_mprotect,
	[ __NR_sigprocmask ] (syscall_handler_t *) sys_sigprocmask,
	[ __NR_create_module ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_init_module ] (syscall_handler_t *) sys_init_module,
	[ __NR_delete_module ] (syscall_handler_t *) sys_delete_module,
	[ __NR_get_kernel_syms ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_quotactl ] (syscall_handler_t *) sys_quotactl,
	[ __NR_getpgid ] (syscall_handler_t *) sys_getpgid,
	[ __NR_fchdir ] (syscall_handler_t *) sys_fchdir,
	[ __NR_bdflush ] (syscall_handler_t *) sys_bdflush,
	[ __NR_sysfs ] (syscall_handler_t *) sys_sysfs,
	[ __NR_personality ] (syscall_handler_t *) sys_personality,
	[ __NR_afs_syscall ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_setfsuid ] (syscall_handler_t *) sys_setfsuid16,
	[ __NR_setfsgid ] (syscall_handler_t *) sys_setfsgid16,
	[ __NR__llseek ] (syscall_handler_t *) sys_llseek,
	[ __NR_getdents ] (syscall_handler_t *) sys_getdents,
	[ __NR__newselect ] = (syscall_handler_t *) sys_select,
	[ __NR_flock ] (syscall_handler_t *) sys_flock,
	[ __NR_msync ] (syscall_handler_t *) sys_msync,
	[ __NR_readv ] (syscall_handler_t *) sys_readv,
	[ __NR_writev ] (syscall_handler_t *) sys_writev,
	[ __NR_getsid ] (syscall_handler_t *) sys_getsid,
	[ __NR_fdatasync ] (syscall_handler_t *) sys_fdatasync,
	[ __NR__sysctl ] = (syscall_handler_t *) sys_sysctl,
	[ __NR_mlock ] (syscall_handler_t *) sys_mlock,
	[ __NR_munlock ] (syscall_handler_t *) sys_munlock,
	[ __NR_mlockall ] (syscall_handler_t *) sys_mlockall,
	[ __NR_munlockall ] (syscall_handler_t *) sys_munlockall,
	[ __NR_sched_setparam ] (syscall_handler_t *) sys_sched_setparam,
	[ __NR_sched_getparam ] (syscall_handler_t *) sys_sched_getparam,
	[ __NR_sched_setscheduler ] (syscall_handler_t *) sys_sched_setscheduler,
	[ __NR_sched_getscheduler ] (syscall_handler_t *) sys_sched_getscheduler,
	[ __NR_sched_yield ] = (syscall_handler_t *) yield,
	[ __NR_sched_get_priority_max ] (syscall_handler_t *) sys_sched_get_priority_max,
	[ __NR_sched_get_priority_min ] (syscall_handler_t *) sys_sched_get_priority_min,
	[ __NR_sched_rr_get_interval ] (syscall_handler_t *) sys_sched_rr_get_interval,
	[ __NR_nanosleep ] (syscall_handler_t *) sys_nanosleep,
	[ __NR_mremap ] (syscall_handler_t *) sys_mremap,
	[ __NR_setresuid ] (syscall_handler_t *) sys_setresuid16,
	[ __NR_getresuid ] (syscall_handler_t *) sys_getresuid16,
	[ __NR_vm86 ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_query_module ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_poll ] (syscall_handler_t *) sys_poll,
	[ __NR_nfsservctl ] = (syscall_handler_t *) NFSSERVCTL,
	[ __NR_setresgid ] (syscall_handler_t *) sys_setresgid16,
	[ __NR_getresgid ] (syscall_handler_t *) sys_getresgid16,
	[ __NR_prctl ] (syscall_handler_t *) sys_prctl,
	[ __NR_rt_sigreturn ] (syscall_handler_t *) sys_rt_sigreturn,
	[ __NR_rt_sigaction ] (syscall_handler_t *) sys_rt_sigaction,
	[ __NR_rt_sigprocmask ] (syscall_handler_t *) sys_rt_sigprocmask,
	[ __NR_rt_sigpending ] (syscall_handler_t *) sys_rt_sigpending,
	[ __NR_rt_sigtimedwait ] (syscall_handler_t *) sys_rt_sigtimedwait,
	[ __NR_rt_sigqueueinfo ] (syscall_handler_t *) sys_rt_sigqueueinfo,
	[ __NR_rt_sigsuspend ] (syscall_handler_t *) sys_rt_sigsuspend,
	[ __NR_pread64 ] (syscall_handler_t *) sys_pread64,
	[ __NR_pwrite64 ] (syscall_handler_t *) sys_pwrite64,
	[ __NR_chown ] (syscall_handler_t *) sys_chown16,
	[ __NR_getcwd ] (syscall_handler_t *) sys_getcwd,
	[ __NR_capget ] (syscall_handler_t *) sys_capget,
	[ __NR_capset ] (syscall_handler_t *) sys_capset,
	[ __NR_sigaltstack ] (syscall_handler_t *) sys_sigaltstack,
	[ __NR_sendfile ] (syscall_handler_t *) sys_sendfile,
	[ __NR_getpmsg ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_putpmsg ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_vfork ] (syscall_handler_t *) sys_vfork,
	[ __NR_ugetrlimit ] (syscall_handler_t *) sys_getrlimit,
	[ __NR_mmap2 ] (syscall_handler_t *) sys_mmap2,
	[ __NR_truncate64 ] (syscall_handler_t *) sys_truncate64,
	[ __NR_ftruncate64 ] (syscall_handler_t *) sys_ftruncate64,
	[ __NR_stat64 ] (syscall_handler_t *) sys_stat64,
	[ __NR_lstat64 ] (syscall_handler_t *) sys_lstat64,
	[ __NR_fstat64 ] (syscall_handler_t *) sys_fstat64,
	[ __NR_getdents64 ] (syscall_handler_t *) sys_getdents64,
	[ __NR_fcntl64 ] (syscall_handler_t *) sys_fcntl64,
	[ 223 ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_gettid ] (syscall_handler_t *) sys_gettid,
	[ __NR_readahead ] (syscall_handler_t *) sys_readahead,
	[ __NR_setxattr ] (syscall_handler_t *) sys_setxattr,
	[ __NR_lsetxattr ] (syscall_handler_t *) sys_lsetxattr,
	[ __NR_fsetxattr ] (syscall_handler_t *) sys_fsetxattr,
	[ __NR_getxattr ] (syscall_handler_t *) sys_getxattr,
	[ __NR_lgetxattr ] (syscall_handler_t *) sys_lgetxattr,
	[ __NR_fgetxattr ] (syscall_handler_t *) sys_fgetxattr,
	[ __NR_listxattr ] (syscall_handler_t *) sys_listxattr,
	[ __NR_llistxattr ] (syscall_handler_t *) sys_llistxattr,
	[ __NR_flistxattr ] (syscall_handler_t *) sys_flistxattr,
	[ __NR_removexattr ] (syscall_handler_t *) sys_removexattr,
	[ __NR_lremovexattr ] (syscall_handler_t *) sys_lremovexattr,
	[ __NR_fremovexattr ] (syscall_handler_t *) sys_fremovexattr,
	[ __NR_tkill ] (syscall_handler_t *) sys_tkill,
	[ __NR_sendfile64 ] (syscall_handler_t *) sys_sendfile64,
	[ __NR_futex ] (syscall_handler_t *) sys_futex,
	[ __NR_sched_setaffinity ] (syscall_handler_t *) sys_sched_setaffinity,
	[ __NR_sched_getaffinity ] (syscall_handler_t *) sys_sched_getaffinity,
	[ __NR_set_thread_area ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_get_thread_area ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_io_setup ] (syscall_handler_t *) sys_io_setup,
	[ __NR_io_destroy ] (syscall_handler_t *) sys_io_destroy,
	[ __NR_io_getevents ] (syscall_handler_t *) sys_io_getevents,
	[ __NR_io_submit ] (syscall_handler_t *) sys_io_submit,
	[ __NR_io_cancel ] (syscall_handler_t *) sys_io_cancel,
	[ __NR_fadvise64 ] (syscall_handler_t *) sys_fadvise64,
	[ 251 ] (syscall_handler_t *) sys_ni_syscall,
	[ __NR_exit_group ] (syscall_handler_t *) sys_exit_group,
	[ __NR_lookup_dcookie ] (syscall_handler_t *) sys_lookup_dcookie,
	[ __NR_epoll_create ] (syscall_handler_t *) sys_epoll_create,
	[ __NR_epoll_ctl ] (syscall_handler_t *) sys_epoll_ctl,
	[ __NR_epoll_wait ] (syscall_handler_t *) sys_epoll_wait,
        [ __NR_remap_file_pages ] (syscall_handler_t *) sys_remap_file_pages,
        [ __NR_set_tid_address ] (syscall_handler_t *) sys_set_tid_address,
	[ __NR_timer_create ] (syscall_handler_t *) sys_timer_create,
	[ __NR_timer_settime ] (syscall_handler_t *) sys_timer_settime,
	[ __NR_timer_gettime ] (syscall_handler_t *) sys_timer_gettime,
	[ __NR_timer_getoverrun ] (syscall_handler_t *) sys_timer_getoverrun,
	[ __NR_timer_delete ] (syscall_handler_t *) sys_timer_delete,
	[ __NR_clock_settime ] (syscall_handler_t *) sys_clock_settime,
	[ __NR_clock_gettime ] (syscall_handler_t *) sys_clock_gettime,
	[ __NR_clock_getres ] (syscall_handler_t *) sys_clock_getres,
	[ __NR_clock_nanosleep ] (syscall_handler_t *) sys_clock_nanosleep,
	[ __NR_statfs64 ] (syscall_handler_t *) sys_statfs64,
	[ __NR_fstatfs64 ] (syscall_handler_t *) sys_fstatfs64,
	[ __NR_tgkill ] (syscall_handler_t *) sys_tgkill,
	[ __NR_utimes ] (syscall_handler_t *) sys_utimes,
	[ __NR_fadvise64_64 ] (syscall_handler_t *) sys_fadvise64_64,
	[ __NR_vserver ] (syscall_handler_t *) sys_ni_syscall,

	ARCH_SYSCALLS
	[ LAST_SYSCALL + 1 ... NR_syscalls ] = 
	        (syscall_handler_t *) sys_ni_syscall
};

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
