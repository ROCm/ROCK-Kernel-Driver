/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 97, 98, 99, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 *
 * Changed system calls macros _syscall5 - _syscall7 to push args 5 to 7 onto
 * the stack. Robin Farine for ACN S.A, Copyright (C) 1996 by ACN S.A
 */
#ifndef _ASM_UNISTD_H
#define _ASM_UNISTD_H

/*
 * Linux o32 style syscalls are in the range from 4000 to 4999.
 */
#define __NR_Linux32			4000
#define __NR_Linux32_syscall		(__NR_Linux32 +   0)
#define __NR_Linux32_exit		(__NR_Linux32 +   1)
#define __NR_Linux32_fork		(__NR_Linux32 +   2)
#define __NR_Linux32_read		(__NR_Linux32 +   3)
#define __NR_Linux32_write		(__NR_Linux32 +   4)
#define __NR_Linux32_open		(__NR_Linux32 +   5)
#define __NR_Linux32_close		(__NR_Linux32 +   6)
#define __NR_Linux32_waitpid		(__NR_Linux32 +   7)
#define __NR_Linux32_creat		(__NR_Linux32 +   8)
#define __NR_Linux32_link		(__NR_Linux32 +   9)
#define __NR_Linux32_unlink		(__NR_Linux32 +  10)
#define __NR_Linux32_execve		(__NR_Linux32 +  11)
#define __NR_Linux32_chdir		(__NR_Linux32 +  12)
#define __NR_Linux32_time		(__NR_Linux32 +  13)
#define __NR_Linux32_mknod		(__NR_Linux32 +  14)
#define __NR_Linux32_chmod		(__NR_Linux32 +  15)
#define __NR_Linux32_lchown		(__NR_Linux32 +  16)
#define __NR_Linux32_break		(__NR_Linux32 +  17)
#define __NR_Linux32_oldstat		(__NR_Linux32 +  18)
#define __NR_Linux32_lseek		(__NR_Linux32 +  19)
#define __NR_Linux32_getpid		(__NR_Linux32 +  20)
#define __NR_Linux32_mount		(__NR_Linux32 +  21)
#define __NR_Linux32_umount		(__NR_Linux32 +  22)
#define __NR_Linux32_setuid		(__NR_Linux32 +  23)
#define __NR_Linux32_getuid		(__NR_Linux32 +  24)
#define __NR_Linux32_stime		(__NR_Linux32 +  25)
#define __NR_Linux32_ptrace		(__NR_Linux32 +  26)
#define __NR_Linux32_alarm		(__NR_Linux32 +  27)
#define __NR_Linux32_oldfstat		(__NR_Linux32 +  28)
#define __NR_Linux32_pause		(__NR_Linux32 +  29)
#define __NR_Linux32_utime		(__NR_Linux32 +  30)
#define __NR_Linux32_stty		(__NR_Linux32 +  31)
#define __NR_Linux32_gtty		(__NR_Linux32 +  32)
#define __NR_Linux32_access		(__NR_Linux32 +  33)
#define __NR_Linux32_nice		(__NR_Linux32 +  34)
#define __NR_Linux32_ftime		(__NR_Linux32 +  35)
#define __NR_Linux32_sync		(__NR_Linux32 +  36)
#define __NR_Linux32_kill		(__NR_Linux32 +  37)
#define __NR_Linux32_rename		(__NR_Linux32 +  38)
#define __NR_Linux32_mkdir		(__NR_Linux32 +  39)
#define __NR_Linux32_rmdir		(__NR_Linux32 +  40)
#define __NR_Linux32_dup		(__NR_Linux32 +  41)
#define __NR_Linux32_pipe		(__NR_Linux32 +  42)
#define __NR_Linux32_times		(__NR_Linux32 +  43)
#define __NR_Linux32_prof		(__NR_Linux32 +  44)
#define __NR_Linux32_brk		(__NR_Linux32 +  45)
#define __NR_Linux32_setgid		(__NR_Linux32 +  46)
#define __NR_Linux32_getgid		(__NR_Linux32 +  47)
#define __NR_Linux32_signal		(__NR_Linux32 +  48)
#define __NR_Linux32_geteuid		(__NR_Linux32 +  49)
#define __NR_Linux32_getegid		(__NR_Linux32 +  50)
#define __NR_Linux32_acct		(__NR_Linux32 +  51)
#define __NR_Linux32_umount2		(__NR_Linux32 +  52)
#define __NR_Linux32_lock		(__NR_Linux32 +  53)
#define __NR_Linux32_ioctl		(__NR_Linux32 +  54)
#define __NR_Linux32_fcntl		(__NR_Linux32 +  55)
#define __NR_Linux32_mpx		(__NR_Linux32 +  56)
#define __NR_Linux32_setpgid		(__NR_Linux32 +  57)
#define __NR_Linux32_ulimit		(__NR_Linux32 +  58)
#define __NR_Linux32_unused59		(__NR_Linux32 +  59)
#define __NR_Linux32_umask		(__NR_Linux32 +  60)
#define __NR_Linux32_chroot		(__NR_Linux32 +  61)
#define __NR_Linux32_ustat		(__NR_Linux32 +  62)
#define __NR_Linux32_dup2		(__NR_Linux32 +  63)
#define __NR_Linux32_getppid		(__NR_Linux32 +  64)
#define __NR_Linux32_getpgrp		(__NR_Linux32 +  65)
#define __NR_Linux32_setsid		(__NR_Linux32 +  66)
#define __NR_Linux32_sigaction		(__NR_Linux32 +  67)
#define __NR_Linux32_sgetmask		(__NR_Linux32 +  68)
#define __NR_Linux32_ssetmask		(__NR_Linux32 +  69)
#define __NR_Linux32_setreuid		(__NR_Linux32 +  70)
#define __NR_Linux32_setregid		(__NR_Linux32 +  71)
#define __NR_Linux32_sigsuspend		(__NR_Linux32 +  72)
#define __NR_Linux32_sigpending		(__NR_Linux32 +  73)
#define __NR_Linux32_sethostname	(__NR_Linux32 +  74)
#define __NR_Linux32_setrlimit		(__NR_Linux32 +  75)
#define __NR_Linux32_getrlimit		(__NR_Linux32 +  76)
#define __NR_Linux32_getrusage		(__NR_Linux32 +  77)
#define __NR_Linux32_gettimeofday	(__NR_Linux32 +  78)
#define __NR_Linux32_settimeofday	(__NR_Linux32 +  79)
#define __NR_Linux32_getgroups		(__NR_Linux32 +  80)
#define __NR_Linux32_setgroups		(__NR_Linux32 +  81)
#define __NR_Linux32_reserved82		(__NR_Linux32 +  82)
#define __NR_Linux32_symlink		(__NR_Linux32 +  83)
#define __NR_Linux32_oldlstat		(__NR_Linux32 +  84)
#define __NR_Linux32_readlink		(__NR_Linux32 +  85)
#define __NR_Linux32_uselib		(__NR_Linux32 +  86)
#define __NR_Linux32_swapon		(__NR_Linux32 +  87)
#define __NR_Linux32_reboot		(__NR_Linux32 +  88)
#define __NR_Linux32_readdir		(__NR_Linux32 +  89)
#define __NR_Linux32_mmap		(__NR_Linux32 +  90)
#define __NR_Linux32_munmap		(__NR_Linux32 +  91)
#define __NR_Linux32_truncate		(__NR_Linux32 +  92)
#define __NR_Linux32_ftruncate		(__NR_Linux32 +  93)
#define __NR_Linux32_fchmod		(__NR_Linux32 +  94)
#define __NR_Linux32_fchown		(__NR_Linux32 +  95)
#define __NR_Linux32_getpriority	(__NR_Linux32 +  96)
#define __NR_Linux32_setpriority	(__NR_Linux32 +  97)
#define __NR_Linux32_profil		(__NR_Linux32 +  98)
#define __NR_Linux32_statfs		(__NR_Linux32 +  99)
#define __NR_Linux32_fstatfs		(__NR_Linux32 + 100)
#define __NR_Linux32_ioperm		(__NR_Linux32 + 101)
#define __NR_Linux32_socketcall		(__NR_Linux32 + 102)
#define __NR_Linux32_syslog		(__NR_Linux32 + 103)
#define __NR_Linux32_setitimer		(__NR_Linux32 + 104)
#define __NR_Linux32_getitimer		(__NR_Linux32 + 105)
#define __NR_Linux32_stat		(__NR_Linux32 + 106)
#define __NR_Linux32_lstat		(__NR_Linux32 + 107)
#define __NR_Linux32_fstat		(__NR_Linux32 + 108)
#define __NR_Linux32_unused109		(__NR_Linux32 + 109)
#define __NR_Linux32_iopl		(__NR_Linux32 + 110)
#define __NR_Linux32_vhangup		(__NR_Linux32 + 111)
#define __NR_Linux32_idle		(__NR_Linux32 + 112)
#define __NR_Linux32_vm86		(__NR_Linux32 + 113)
#define __NR_Linux32_wait4		(__NR_Linux32 + 114)
#define __NR_Linux32_swapoff		(__NR_Linux32 + 115)
#define __NR_Linux32_sysinfo		(__NR_Linux32 + 116)
#define __NR_Linux32_ipc		(__NR_Linux32 + 117)
#define __NR_Linux32_fsync		(__NR_Linux32 + 118)
#define __NR_Linux32_sigreturn		(__NR_Linux32 + 119)
#define __NR_Linux32_clone		(__NR_Linux32 + 120)
#define __NR_Linux32_setdomainname	(__NR_Linux32 + 121)
#define __NR_Linux32_uname		(__NR_Linux32 + 122)
#define __NR_Linux32_modify_ldt		(__NR_Linux32 + 123)
#define __NR_Linux32_adjtimex		(__NR_Linux32 + 124)
#define __NR_Linux32_mprotect		(__NR_Linux32 + 125)
#define __NR_Linux32_sigprocmask	(__NR_Linux32 + 126)
#define __NR_Linux32_create_module	(__NR_Linux32 + 127)
#define __NR_Linux32_init_module	(__NR_Linux32 + 128)
#define __NR_Linux32_delete_module	(__NR_Linux32 + 129)
#define __NR_Linux32_get_kernel_syms	(__NR_Linux32 + 130)
#define __NR_Linux32_quotactl		(__NR_Linux32 + 131)
#define __NR_Linux32_getpgid		(__NR_Linux32 + 132)
#define __NR_Linux32_fchdir		(__NR_Linux32 + 133)
#define __NR_Linux32_bdflush		(__NR_Linux32 + 134)
#define __NR_Linux32_sysfs		(__NR_Linux32 + 135)
#define __NR_Linux32_personality	(__NR_Linux32 + 136)
#define __NR_Linux32_afs_syscall	(__NR_Linux32 + 137) /* Syscall for Andrew File System */
#define __NR_Linux32_setfsuid		(__NR_Linux32 + 138)
#define __NR_Linux32_setfsgid		(__NR_Linux32 + 139)
#define __NR_Linux32__llseek		(__NR_Linux32 + 140)
#define __NR_Linux32_getdents		(__NR_Linux32 + 141)
#define __NR_Linux32__newselect		(__NR_Linux32 + 142)
#define __NR_Linux32_flock		(__NR_Linux32 + 143)
#define __NR_Linux32_msync		(__NR_Linux32 + 144)
#define __NR_Linux32_readv		(__NR_Linux32 + 145)
#define __NR_Linux32_writev		(__NR_Linux32 + 146)
#define __NR_Linux32_cacheflush		(__NR_Linux32 + 147)
#define __NR_Linux32_cachectl		(__NR_Linux32 + 148)
#define __NR_Linux32_sysmips		(__NR_Linux32 + 149)
#define __NR_Linux32_unused150		(__NR_Linux32 + 150)
#define __NR_Linux32_getsid		(__NR_Linux32 + 151)
#define __NR_Linux32_fdatasync		(__NR_Linux32 + 152)
#define __NR_Linux32__sysctl		(__NR_Linux32 + 153)
#define __NR_Linux32_mlock		(__NR_Linux32 + 154)
#define __NR_Linux32_munlock		(__NR_Linux32 + 155)
#define __NR_Linux32_mlockall		(__NR_Linux32 + 156)
#define __NR_Linux32_munlockall		(__NR_Linux32 + 157)
#define __NR_Linux32_sched_setparam	(__NR_Linux32 + 158)
#define __NR_Linux32_sched_getparam	(__NR_Linux32 + 159)
#define __NR_Linux32_sched_setscheduler	(__NR_Linux32 + 160)
#define __NR_Linux32_sched_getscheduler	(__NR_Linux32 + 161)
#define __NR_Linux32_sched_yield	(__NR_Linux32 + 162)
#define __NR_Linux32_sched_get_priority_max	(__NR_Linux32 + 163)
#define __NR_Linux32_sched_get_priority_min	(__NR_Linux32 + 164)
#define __NR_Linux32_sched_rr_get_interval	(__NR_Linux32 + 165)
#define __NR_Linux32_nanosleep		(__NR_Linux32 + 166)
#define __NR_Linux32_mremap		(__NR_Linux32 + 167)
#define __NR_Linux32_accept		(__NR_Linux32 + 168)
#define __NR_Linux32_bind		(__NR_Linux32 + 169)
#define __NR_Linux32_connect		(__NR_Linux32 + 170)
#define __NR_Linux32_getpeername	(__NR_Linux32 + 171)
#define __NR_Linux32_getsockname	(__NR_Linux32 + 172)
#define __NR_Linux32_getsockopt		(__NR_Linux32 + 173)
#define __NR_Linux32_listen		(__NR_Linux32 + 174)
#define __NR_Linux32_recv		(__NR_Linux32 + 175)
#define __NR_Linux32_recvfrom		(__NR_Linux32 + 176)
#define __NR_Linux32_recvmsg		(__NR_Linux32 + 177)
#define __NR_Linux32_send		(__NR_Linux32 + 178)
#define __NR_Linux32_sendmsg		(__NR_Linux32 + 179)
#define __NR_Linux32_sendto		(__NR_Linux32 + 180)
#define __NR_Linux32_setsockopt		(__NR_Linux32 + 181)
#define __NR_Linux32_shutdown		(__NR_Linux32 + 182)
#define __NR_Linux32_socket		(__NR_Linux32 + 183)
#define __NR_Linux32_socketpair		(__NR_Linux32 + 184)
#define __NR_Linux32_setresuid		(__NR_Linux32 + 185)
#define __NR_Linux32_getresuid		(__NR_Linux32 + 186)
#define __NR_Linux32_query_module	(__NR_Linux32 + 187)
#define __NR_Linux32_poll		(__NR_Linux32 + 188)
#define __NR_Linux32_nfsservctl		(__NR_Linux32 + 189)
#define __NR_Linux32_setresgid		(__NR_Linux32 + 190)
#define __NR_Linux32_getresgid		(__NR_Linux32 + 191)
#define __NR_Linux32_prctl		(__NR_Linux32 + 192)
#define __NR_Linux32_rt_sigreturn	(__NR_Linux32 + 193)
#define __NR_Linux32_rt_sigaction	(__NR_Linux32 + 194)
#define __NR_Linux32_rt_sigprocmask	(__NR_Linux32 + 195)
#define __NR_Linux32_rt_sigpending	(__NR_Linux32 + 196)
#define __NR_Linux32_rt_sigtimedwait	(__NR_Linux32 + 197)
#define __NR_Linux32_rt_sigqueueinfo	(__NR_Linux32 + 198)
#define __NR_Linux32_rt_sigsuspend	(__NR_Linux32 + 199)
#define __NR_Linux32_pread		(__NR_Linux32 + 200)
#define __NR_Linux32_pwrite		(__NR_Linux32 + 201)
#define __NR_Linux32_chown		(__NR_Linux32 + 202)
#define __NR_Linux32_getcwd		(__NR_Linux32 + 203)
#define __NR_Linux32_capget		(__NR_Linux32 + 204)
#define __NR_Linux32_capset		(__NR_Linux32 + 205)
#define __NR_Linux32_sigaltstack	(__NR_Linux32 + 206)
#define __NR_Linux32_sendfile		(__NR_Linux32 + 207)
#define __NR_Linux32_getpmsg		(__NR_Linux32 + 208)
#define __NR_Linux32_putpmsg		(__NR_Linux32 + 209)
#define __NR_Linux32_mmap2		(__NR_Linux32 + 210)
#define __NR_Linux32_truncate64		(__NR_Linux32 + 211)
#define __NR_Linux32_ftruncate64	(__NR_Linux32 + 212)
#define __NR_Linux32_stat64		(__NR_Linux32 + 213)
#define __NR_Linux32_lstat64		(__NR_Linux32 + 214)
#define __NR_Linux32_fstat64		(__NR_Linux32 + 215)
#define __NR_Linux32_root_pivot		(__NR_Linux32 + 216)
#define __NR_Linux32_mincore		(__NR_Linux32 + 217)
#define __NR_Linux32_madvise		(__NR_Linux32 + 218)
#define __NR_Linux32_getdents64		(__NR_Linux32 + 219)
#define __NR_Linux32_fcntl64		(__NR_Linux32 + 220)

/*
 * Offset of the last Linux o32 flavoured syscall
 */
#define __NR_Linux32_syscalls		220

/*
 * Linux 64-bit syscalls are in the range from 5000 to 5999.
 */
#define __NR_Linux			5000
#define __NR_syscall			(__NR_Linux +   0)
#define __NR_exit			(__NR_Linux +   1)
#define __NR_fork			(__NR_Linux +   2)
#define __NR_read			(__NR_Linux +   3)
#define __NR_write			(__NR_Linux +   4)
#define __NR_open			(__NR_Linux +   5)
#define __NR_close			(__NR_Linux +   6)
#define __NR_waitpid			(__NR_Linux +   7)
#define __NR_creat			(__NR_Linux +   8)
#define __NR_link			(__NR_Linux +   9)
#define __NR_unlink			(__NR_Linux +  10)
#define __NR_execve			(__NR_Linux +  11)
#define __NR_chdir			(__NR_Linux +  12)
#define __NR_time			(__NR_Linux +  13)
#define __NR_mknod			(__NR_Linux +  14)
#define __NR_chmod			(__NR_Linux +  15)
#define __NR_lchown			(__NR_Linux +  16)
#define __NR_break			(__NR_Linux +  17)
#define __NR_oldstat			(__NR_Linux +  18)
#define __NR_lseek			(__NR_Linux +  19)
#define __NR_getpid			(__NR_Linux +  20)
#define __NR_mount			(__NR_Linux +  21)
#define __NR_umount			(__NR_Linux +  22)
#define __NR_setuid			(__NR_Linux +  23)
#define __NR_getuid			(__NR_Linux +  24)
#define __NR_stime			(__NR_Linux +  25)
#define __NR_ptrace			(__NR_Linux +  26)
#define __NR_alarm			(__NR_Linux +  27)
#define __NR_oldfstat			(__NR_Linux +  28)
#define __NR_pause			(__NR_Linux +  29)
#define __NR_utime			(__NR_Linux +  30)
#define __NR_stty			(__NR_Linux +  31)
#define __NR_gtty			(__NR_Linux +  32)
#define __NR_access			(__NR_Linux +  33)
#define __NR_nice			(__NR_Linux +  34)
#define __NR_ftime			(__NR_Linux +  35)
#define __NR_sync			(__NR_Linux +  36)
#define __NR_kill			(__NR_Linux +  37)
#define __NR_rename			(__NR_Linux +  38)
#define __NR_mkdir			(__NR_Linux +  39)
#define __NR_rmdir			(__NR_Linux +  40)
#define __NR_dup			(__NR_Linux +  41)
#define __NR_pipe			(__NR_Linux +  42)
#define __NR_times			(__NR_Linux +  43)
#define __NR_prof			(__NR_Linux +  44)
#define __NR_brk			(__NR_Linux +  45)
#define __NR_setgid			(__NR_Linux +  46)
#define __NR_getgid			(__NR_Linux +  47)
#define __NR_signal			(__NR_Linux +  48)
#define __NR_geteuid			(__NR_Linux +  49)
#define __NR_getegid			(__NR_Linux +  50)
#define __NR_acct			(__NR_Linux +  51)
#define __NR_umount2			(__NR_Linux +  52)
#define __NR_lock			(__NR_Linux +  53)
#define __NR_ioctl			(__NR_Linux +  54)
#define __NR_fcntl			(__NR_Linux +  55)
#define __NR_mpx			(__NR_Linux +  56)
#define __NR_setpgid			(__NR_Linux +  57)
#define __NR_ulimit			(__NR_Linux +  58)
#define __NR_unused59			(__NR_Linux +  59)
#define __NR_umask			(__NR_Linux +  60)
#define __NR_chroot			(__NR_Linux +  61)
#define __NR_ustat			(__NR_Linux +  62)
#define __NR_dup2			(__NR_Linux +  63)
#define __NR_getppid			(__NR_Linux +  64)
#define __NR_getpgrp			(__NR_Linux +  65)
#define __NR_setsid			(__NR_Linux +  66)
#define __NR_sigaction			(__NR_Linux +  67)
#define __NR_sgetmask			(__NR_Linux +  68)
#define __NR_ssetmask			(__NR_Linux +  69)
#define __NR_setreuid			(__NR_Linux +  70)
#define __NR_setregid			(__NR_Linux +  71)
#define __NR_sigsuspend			(__NR_Linux +  72)
#define __NR_sigpending			(__NR_Linux +  73)
#define __NR_sethostname		(__NR_Linux +  74)
#define __NR_setrlimit			(__NR_Linux +  75)
#define __NR_getrlimit			(__NR_Linux +  76)
#define __NR_getrusage			(__NR_Linux +  77)
#define __NR_gettimeofday		(__NR_Linux +  78)
#define __NR_settimeofday		(__NR_Linux +  79)
#define __NR_getgroups			(__NR_Linux +  80)
#define __NR_setgroups			(__NR_Linux +  81)
#define __NR_reserved82			(__NR_Linux +  82)
#define __NR_symlink			(__NR_Linux +  83)
#define __NR_oldlstat			(__NR_Linux +  84)
#define __NR_readlink			(__NR_Linux +  85)
#define __NR_uselib			(__NR_Linux +  86)
#define __NR_swapon			(__NR_Linux +  87)
#define __NR_reboot			(__NR_Linux +  88)
#define __NR_readdir			(__NR_Linux +  89)
#define __NR_mmap			(__NR_Linux +  90)
#define __NR_munmap			(__NR_Linux +  91)
#define __NR_truncate			(__NR_Linux +  92)
#define __NR_ftruncate			(__NR_Linux +  93)
#define __NR_fchmod			(__NR_Linux +  94)
#define __NR_fchown			(__NR_Linux +  95)
#define __NR_getpriority		(__NR_Linux +  96)
#define __NR_setpriority		(__NR_Linux +  97)
#define __NR_profil			(__NR_Linux +  98)
#define __NR_statfs			(__NR_Linux +  99)
#define __NR_fstatfs			(__NR_Linux + 100)
#define __NR_ioperm			(__NR_Linux + 101)
#define __NR_socketcall			(__NR_Linux + 102)
#define __NR_syslog			(__NR_Linux + 103)
#define __NR_setitimer			(__NR_Linux + 104)
#define __NR_getitimer			(__NR_Linux + 105)
#define __NR_stat			(__NR_Linux + 106)
#define __NR_lstat			(__NR_Linux + 107)
#define __NR_fstat			(__NR_Linux + 108)
#define __NR_unused109			(__NR_Linux + 109)
#define __NR_iopl			(__NR_Linux + 110)
#define __NR_vhangup			(__NR_Linux + 111)
#define __NR_idle			(__NR_Linux + 112)
#define __NR_vm86			(__NR_Linux + 113)
#define __NR_wait4			(__NR_Linux + 114)
#define __NR_swapoff			(__NR_Linux + 115)
#define __NR_sysinfo			(__NR_Linux + 116)
#define __NR_ipc			(__NR_Linux + 117)
#define __NR_fsync			(__NR_Linux + 118)
#define __NR_sigreturn			(__NR_Linux + 119)
#define __NR_clone			(__NR_Linux + 120)
#define __NR_setdomainname		(__NR_Linux + 121)
#define __NR_uname			(__NR_Linux + 122)
#define __NR_modify_ldt			(__NR_Linux + 123)
#define __NR_adjtimex			(__NR_Linux + 124)
#define __NR_mprotect			(__NR_Linux + 125)
#define __NR_sigprocmask		(__NR_Linux + 126)
#define __NR_create_module		(__NR_Linux + 127)
#define __NR_init_module		(__NR_Linux + 128)
#define __NR_delete_module		(__NR_Linux + 129)
#define __NR_get_kernel_syms		(__NR_Linux + 130)
#define __NR_quotactl			(__NR_Linux + 131)
#define __NR_getpgid			(__NR_Linux + 132)
#define __NR_fchdir			(__NR_Linux + 133)
#define __NR_bdflush			(__NR_Linux + 134)
#define __NR_sysfs			(__NR_Linux + 135)
#define __NR_personality		(__NR_Linux + 136)
#define __NR_afs_syscall		(__NR_Linux + 137) /* Syscall for Andrew File System */
#define __NR_setfsuid			(__NR_Linux + 138)
#define __NR_setfsgid			(__NR_Linux + 139)
#define __NR__llseek			(__NR_Linux + 140)
#define __NR_getdents			(__NR_Linux + 141)
#define __NR__newselect			(__NR_Linux + 142)
#define __NR_flock			(__NR_Linux + 143)
#define __NR_msync			(__NR_Linux + 144)
#define __NR_readv			(__NR_Linux + 145)
#define __NR_writev			(__NR_Linux + 146)
#define __NR_cacheflush			(__NR_Linux + 147)
#define __NR_cachectl			(__NR_Linux + 148)
#define __NR_sysmips			(__NR_Linux + 149)
#define __NR_unused150			(__NR_Linux + 150)
#define __NR_getsid			(__NR_Linux + 151)
#define __NR_fdatasync			(__NR_Linux + 152)
#define __NR__sysctl			(__NR_Linux + 153)
#define __NR_mlock			(__NR_Linux + 154)
#define __NR_munlock			(__NR_Linux + 155)
#define __NR_mlockall			(__NR_Linux + 156)
#define __NR_munlockall			(__NR_Linux + 157)
#define __NR_sched_setparam		(__NR_Linux + 158)
#define __NR_sched_getparam		(__NR_Linux + 159)
#define __NR_sched_setscheduler		(__NR_Linux + 160)
#define __NR_sched_getscheduler		(__NR_Linux + 161)
#define __NR_sched_yield		(__NR_Linux + 162)
#define __NR_sched_get_priority_max	(__NR_Linux + 163)
#define __NR_sched_get_priority_min	(__NR_Linux + 164)
#define __NR_sched_rr_get_interval	(__NR_Linux + 165)
#define __NR_nanosleep			(__NR_Linux + 166)
#define __NR_mremap			(__NR_Linux + 167)
#define __NR_accept			(__NR_Linux + 168)
#define __NR_bind			(__NR_Linux + 169)
#define __NR_connect			(__NR_Linux + 170)
#define __NR_getpeername		(__NR_Linux + 171)
#define __NR_getsockname		(__NR_Linux + 172)
#define __NR_getsockopt			(__NR_Linux + 173)
#define __NR_listen			(__NR_Linux + 174)
#define __NR_recv			(__NR_Linux + 175)
#define __NR_recvfrom			(__NR_Linux + 176)
#define __NR_recvmsg			(__NR_Linux + 177)
#define __NR_send			(__NR_Linux + 178)
#define __NR_sendmsg			(__NR_Linux + 179)
#define __NR_sendto			(__NR_Linux + 180)
#define __NR_setsockopt			(__NR_Linux + 181)
#define __NR_shutdown			(__NR_Linux + 182)
#define __NR_socket			(__NR_Linux + 183)
#define __NR_socketpair			(__NR_Linux + 184)
#define __NR_setresuid			(__NR_Linux + 185)
#define __NR_getresuid			(__NR_Linux + 186)
#define __NR_query_module		(__NR_Linux + 187)
#define __NR_poll			(__NR_Linux + 188)
#define __NR_nfsservctl			(__NR_Linux + 189)
#define __NR_setresgid			(__NR_Linux + 190)
#define __NR_getresgid			(__NR_Linux + 191)
#define __NR_prctl			(__NR_Linux + 192)
#define __NR_rt_sigreturn		(__NR_Linux + 193)
#define __NR_rt_sigaction		(__NR_Linux + 194)
#define __NR_rt_sigprocmask		(__NR_Linux + 195)
#define __NR_rt_sigpending		(__NR_Linux + 196)
#define __NR_rt_sigtimedwait		(__NR_Linux + 197)
#define __NR_rt_sigqueueinfo		(__NR_Linux + 198)
#define __NR_rt_sigsuspend		(__NR_Linux + 199)
#define __NR_pread			(__NR_Linux + 200)
#define __NR_pwrite			(__NR_Linux + 201)
#define __NR_chown			(__NR_Linux + 202)
#define __NR_getcwd			(__NR_Linux + 203)
#define __NR_capget			(__NR_Linux + 204)
#define __NR_capset			(__NR_Linux + 205)
#define __NR_sigaltstack		(__NR_Linux + 206)
#define __NR_sendfile			(__NR_Linux + 207)
#define __NR_getpmsg			(__NR_Linux + 208)
#define __NR_putpmsg			(__NR_Linux + 209)
#define __NR_root_pivot			(__NR_Linux + 210)
#define __NR_mincore			(__NR_Linux + 211)
#define __NR_madvise			(__NR_Linux + 212)
#define __NR_getdents64			(__NR_Linux + 213)

/*
 * Offset of the last Linux flavoured syscall
 */
#define __NR_Linux_syscalls		213

#ifndef _LANGUAGE_ASSEMBLY

/* XXX - _foo needs to be __foo, while __NR_bar could be _NR_bar. */
#define _syscall0(type,name) \
type name(void) \
{ \
long __res, __err; \
__asm__ volatile ("li\t$2, %2\n\t" \
		  "syscall\n\t" \
		  "move\t%0, $2\n\t" \
		  "move\t%1, $7" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name) \
                  : "$2", "$7","$8","$9","$10","$11","$12","$13","$14","$15", \
		    "$24"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

/*
 * DANGER: This macro isn't usable for the pipe(2) call
 * which has a unusual return convention.
 */
#define _syscall1(type,name,atype,a) \
type name(atype a) \
{ \
long __res, __err; \
__asm__ volatile ("move\t$4, %3\n\t" \
		  "li\t$2, %2\n\t" \
		  "syscall\n\t" \
		  "move\t%0, $2\n\t" \
		  "move\t%1, $7" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)) \
                  : "$2","$4","$7","$8","$9","$10","$11","$12","$13","$14", \
		    "$15","$24"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#define _syscall2(type,name,atype,a,btype,b) \
type name(atype a,btype b) \
{ \
long __res, __err; \
__asm__ volatile ("move\t$4, %3\n\t" \
		  "move\t$5, %4\n\t" \
		  "li\t$2, %2\n\t" \
		  "syscall\n\t" \
		  "move\t%0, $2\n\t" \
		  "move\t%1, $7" \
		  : "=r" (__res), "=r" (__err) \
		  : "i" (__NR_##name),"r" ((long)(a)), \
		                      "r" ((long)(b)) \
		  : "$2","$4","$5","$7","$8","$9","$10","$11","$12","$13","$14","$15", \
                    "$24"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#define _syscall3(type,name,atype,a,btype,b,ctype,c) \
type name (atype a, btype b, ctype c) \
{ \
long __res, __err; \
__asm__ volatile ("move\t$4, %3\n\t" \
                  "move\t$5, %4\n\t" \
                  "move\t$6, %5\n\t" \
		  "li\t$2, %2\n\t" \
		  "syscall\n\t" \
		  "move\t%0, $2\n\t" \
		  "move\t%1, $7" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)) \
		  : "$2","$4","$5","$6","$7","$8","$9","$10","$11","$12", \
		    "$13","$14","$15","$24"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#define _syscall4(type,name,atype,a,btype,b,ctype,c,dtype,d) \
type name (atype a, btype b, ctype c, dtype d) \
{ \
long __res, __err; \
__asm__ volatile ("move\t$4, %3\n\t" \
                  "move\t$5, %4\n\t" \
                  "move\t$6, %5\n\t" \
                  "move\t$7, %6\n\t" \
		  "li\t$2, %2\n\t" \
		  "syscall\n\t" \
		  "move\t%0, $2\n\t" \
		  "move\t%1, $7" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)), \
                                      "r" ((long)(d)) \
                  : "$2","$4","$5","$6","$7","$8","$9","$10","$11","$12", \
		    "$13","$14","$15","$24"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#if (_MIPS_SIM == _ABIN32) || (_MIPS_SIM == _ABI64)

#define _syscall5(type,name,atype,a,btype,b,ctype,c,dtype,d,etype,e) \
type name (atype a,btype b,ctype c,dtype d,etype e) \
{ \
long __res, __err; \
__asm__ volatile ("move\t$4, %3\n\t" \
                  "move\t$5, %4\n\t" \
                  "move\t$6, %5\n\t" \
                  "move\t$7, %6\n\t" \
		  "move\t$8, %7\n\t" \
		  "sw\t$2, 16($29)\n\t" \
		  "li\t$2, %2\n\t" \
		  "syscall\n\t" \
		  "move\t%0, $2\n\t" \
		  "move\t%1, $7" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)), \
                                      "r" ((long)(d)), \
                                      "r" ((long)(e)) \
                  : "$2","$4","$5","$6","$7","$8","$9","$10","$11","$12", \
                    "$13","$14","$15","$24"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#define _syscall6(type,name,atype,a,btype,b,ctype,c,dtype,d,etype,e,ftype,f) \
type name (atype a,btype b,ctype c,dtype d,etype e,ftype f) \
{ \
long __res, __err; \
__asm__ volatile ("move\t$4, %3\n\t" \
                  "move\t$5, %4\n\t" \
                  "move\t$6, %5\n\t" \
                  "move\t$7, %6\n\t" \
                  "move\t$8, %7\n\t" \
                  "move\t$9, %8\n\t" \
		  "li\t$2, %2\n\t" \
		  "syscall\n\t" \
		  "move\t%0, $2\n\t" \
		  "move\t%1, $7" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)), \
                                      "r" ((long)(d)), \
                                      "m" ((long)(e)), \
                                      "m" ((long)(f)) \
                  : "$2","$3","$4","$5","$6","$7","$8","$9","$10","$11", \
                    "$12","$13","$14","$15","$24"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#define _syscall7(type,name,atype,a,btype,b,ctype,c,dtype,d,etype,e,ftype,f,gtype,g) \
type name (atype a,btype b,ctype c,dtype d,etype e,ftype f,gtype g) \
{ \
long __res, __err; \
__asm__ volatile ("move\t$4, %3\n\t" \
                  "move\t$5, %4\n\t" \
                  "move\t$6, %5\n\t" \
                  "move\t$7, %6\n\t" \
                  "move\t$8, %7\n\t" \
                  "move\t$9, %8\n\t" \
                  "move\t$10, %9\n\t" \
		  "li\t$2, %2\n\t" \
		  "syscall\n\t" \
		  "move\t%0, $2\n\t" \
		  "move\t%1, $7" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)), \
                                      "r" ((long)(d)), \
                                      "r" ((long)(e)), \
                                      "r" ((long)(f)), \
                                      "r" ((long)(g)) \
                  : "$2","$3","$4","$5","$6","$7","$8","$9","$10","$11", \
                    "$12","$13","$14","$15","$24"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#else /* not N32 or 64 ABI */

/* These are here for sake of fucking lusercode living in the fucking believe
   having to fuck around with the syscall interface themselfes.  */

#define _syscall5(type,name,atype,a,btype,b,ctype,c,dtype,d,etype,e) \
type name (atype a,btype b,ctype c,dtype d,etype e) \
{ \
long __res, __err; \
__asm__ volatile ("move\t$4, %3\n\t" \
                  "move\t$5, %4\n\t" \
                  "move\t$6, %5\n\t" \
		  "lw\t$2, %7\n\t" \
                  "move\t$7, %6\n\t" \
		  "subu\t$29, 24\n\t" \
		  "sw\t$2, 16($29)\n\t" \
		  "li\t$2, %2\n\t" \
		  "syscall\n\t" \
		  "move\t%0, $2\n\t" \
		  "move\t%1, $7" \
		  "addiu\t$29,24" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)), \
                                      "r" ((long)(d)), \
                                      "m" ((long)(e)) \
                  : "$2","$4","$5","$6","$7","$8","$9","$10","$11","$12", \
                    "$13","$14","$15","$24"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#define _syscall6(type,name,atype,a,btype,b,ctype,c,dtype,d,etype,e,ftype,f) \
type name (atype a,btype b,ctype c,dtype d,etype e,ftype f) \
{ \
long __res, __err; \
__asm__ volatile ("move\t$4, %3\n\t" \
                  "move\t$5, %4\n\t" \
                  "move\t$6, %5\n\t" \
		  "lw\t$2, %7\n\t" \
		  "lw\t$3, %8\n\t" \
                  "move\t$7, %6\n\t" \
		  "subu\t$29, 24\n\t" \
		  "sw\t$2, 16($29)\n\t" \
		  "sw\t$3, 20($29)\n\t" \
		  "li\t$2, %2\n\t" \
                  "syscall\n\t" \
		  "move\t%0, $2\n\t" \
		  "move\t%1, $7" \
		  "addiu\t$29, 24" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)), \
                                      "r" ((long)(d)), \
                                      "m" ((long)(e)), \
                                      "m" ((long)(f)) \
                  : "$2","$3","$4","$5","$6","$7","$8","$9","$10","$11", \
                    "$12","$13","$14","$15","$24"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#define _syscall7(type,name,atype,a,btype,b,ctype,c,dtype,d,etype,e,ftype,f,gtype,g) \
type name (atype a,btype b,ctype c,dtype d,etype e,ftype f,gtype g) \
{ \
long __res, __err; \
__asm__ volatile ("move\t$4, %3\n\t" \
                  "move\t$5, %4\n\t" \
                  "move\t$6, %5\n\t" \
		  "lw\t$2, %7\n\t" \
		  "lw\t$3, %8\n\t" \
                  "move\t$7, %6\n\t" \
		  "subu\t$29, 32\n\t" \
		  "sw\t$2, 16($29)\n\t" \
		  "lw\t$2, %9\n\t" \
		  "sw\t$3, 20($29)\n\t" \
		  "sw\t$2, 24($29)\n\t" \
		  "li\t$2, %2\n\t" \
                  "syscall\n\t" \
		  "move\t%0, $2\n\t" \
		  "move\t%1, $7" \
		  "addiu\t$29, 32" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)), \
                                      "r" ((long)(d)), \
                                      "m" ((long)(e)), \
                                      "m" ((long)(f)), \
                                      "m" ((long)(g)) \
                  : "$2","$3","$4","$5","$6","$7","$8","$9","$10","$11", \
                    "$12","$13","$14","$15","$24"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#endif

#ifdef __KERNEL_SYSCALLS__

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
#define __NR__exit __NR_exit
static inline _syscall0(int,sync)
static inline _syscall0(pid_t,setsid)
static inline _syscall3(int,write,int,fd,const char *,buf,off_t,count)
static inline _syscall3(int,read,int,fd,char *,buf,off_t,count)
static inline _syscall3(off_t,lseek,int,fd,off_t,offset,int,count)
static inline _syscall1(int,dup,int,fd)
static inline _syscall3(int,execve,const char *,file,char **,argv,char **,envp)
static inline _syscall3(int,open,const char *,file,int,flag,int,mode)
static inline _syscall1(int,close,int,fd)
static inline _syscall1(int,_exit,int,exitcode)
static inline _syscall3(pid_t,waitpid,pid_t,pid,int *,wait_stat,int,options)
static inline _syscall1(int,delete_module,const char *,name)

static inline pid_t wait(int * wait_stat)
{
	return waitpid(-1,wait_stat,0);
}

#endif /* !defined (__KERNEL_SYSCALLS__) */
#endif /* !defined (_LANGUAGE_ASSEMBLY) */

#endif /* _ASM_UNISTD_H */
