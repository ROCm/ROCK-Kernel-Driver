/* -----------------------------------------------------------------------------
 *
 * Native PARISC/Linux Project (http://www.puffingroup.com/parisc)
 *
 * modified by Matthew Wilcox <willy@bofh.ai> 1999-07-26
 */


#define ASSEMBLY

#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/unistd.h>


	.text

#define ENTRY_NAME(_name_) .word _name_

	.align 4
	.export hpux_call_table
hpux_call_table:
	ENTRY_NAME(sys_ni_syscall)	/* 0 */
	ENTRY_NAME(sys_exit)
	ENTRY_NAME(hpux_fork_wrapper)
	ENTRY_NAME(sys_read)
	ENTRY_NAME(sys_write)
	ENTRY_NAME(sys_open)	/* 5 */
	ENTRY_NAME(sys_close)
	ENTRY_NAME(hpux_wait)
	ENTRY_NAME(sys_creat)
	ENTRY_NAME(sys_link)
	ENTRY_NAME(sys_unlink)	/* 10 */
	ENTRY_NAME(hpux_execv_wrapper)
	ENTRY_NAME(sys_chdir)
	ENTRY_NAME(sys_time)
	ENTRY_NAME(sys_mknod)
	ENTRY_NAME(sys_chmod)	/* 15 */
	ENTRY_NAME(sys_chown)
	ENTRY_NAME(hpux_brk)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_lseek)
	ENTRY_NAME(sys_getpid)	/* 20 */
	ENTRY_NAME(hpux_mount)
	ENTRY_NAME(sys_oldumount)
	ENTRY_NAME(sys_setuid)
	ENTRY_NAME(sys_getuid)
	ENTRY_NAME(sys_stime)	/* 25 */
	ENTRY_NAME(hpux_ptrace)
	ENTRY_NAME(sys_alarm)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_pause)
	ENTRY_NAME(sys_utime)	/* 30 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_access)
	ENTRY_NAME(hpux_nice)
	ENTRY_NAME(sys_ni_syscall)	/* 35 */
	ENTRY_NAME(sys_sync)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_newstat)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_newlstat)	/* 40 */
	ENTRY_NAME(sys_dup)
	ENTRY_NAME(hpux_pipe_wrapper)
	ENTRY_NAME(sys_times)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 45 */
	ENTRY_NAME(sys_setgid)
	ENTRY_NAME(sys_getgid)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 50 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(hpux_ioctl)
	ENTRY_NAME(sys_ni_syscall)	/* 55 */
	ENTRY_NAME(sys_symlink)
	ENTRY_NAME(hpux_utssys)
	ENTRY_NAME(sys_readlink)
	ENTRY_NAME(hpux_execve_wrapper)
	ENTRY_NAME(sys_umask)	/* 60 */
	ENTRY_NAME(sys_chroot)
	ENTRY_NAME(sys_fcntl)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 65 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(hpux_sbrk)
	ENTRY_NAME(sys_ni_syscall)	/* 70 */
	ENTRY_NAME(sys_mmap)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 75 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 80 */
	ENTRY_NAME(sys_getpgid)
	ENTRY_NAME(sys_setpgid)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 85 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_dup2)		/* 90 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_newfstat)
	ENTRY_NAME(sys_select)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 95 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 100 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 105 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 110 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 115 */
	ENTRY_NAME(sys_gettimeofday)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 120 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_fchown)
	ENTRY_NAME(sys_fchmod)
	ENTRY_NAME(sys_ni_syscall)	/* 125 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_rename)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 130 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(hpux_sysconf)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 135 */
	ENTRY_NAME(sys_mkdir)
	ENTRY_NAME(sys_rmdir)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 140 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 145 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 150 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 155 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 160 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 165 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 170 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 175 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 180 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 185 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 190 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(hpux_getdomainname)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 195 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_waitpid)	/* 200 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 205 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 210 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 215 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 220 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 225 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 230 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 235 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 240 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 245 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 250 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 255 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 260 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 265 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 270 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_fchdir)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_accept)		/* 275 */
	ENTRY_NAME(sys_bind)
	ENTRY_NAME(sys_connect)
	ENTRY_NAME(sys_getpeername)
	ENTRY_NAME(sys_getsockname)
	ENTRY_NAME(sys_getsockopt)	/* 280 */
	ENTRY_NAME(sys_listen)
	ENTRY_NAME(sys_recv)
	ENTRY_NAME(sys_recvfrom)
	ENTRY_NAME(sys_recvmsg)
	ENTRY_NAME(sys_send)		/* 285 */
	ENTRY_NAME(sys_sendmsg)
	ENTRY_NAME(sys_sendto)
	ENTRY_NAME(sys_setsockopt)
	ENTRY_NAME(sys_shutdown)
	ENTRY_NAME(sys_socket)		/* 290 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 295 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 300 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 305 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 310 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 315 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 320 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 325 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 330 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_lchown)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 335 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 340 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 345 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 350 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_nanosleep)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 355 */
	ENTRY_NAME(hpux_getdents)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 360 */
	ENTRY_NAME(hpux_fstat64)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 365 */
	ENTRY_NAME(hpux_lstat64)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(hpux_stat64)
	ENTRY_NAME(sys_ni_syscall)	/* 370 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 375 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 380 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 385 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 390 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 395 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 400 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 405 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 410 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 415 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 420 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 425 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 430 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 435 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 440 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 445 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 450 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 455 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 460 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 465 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 470 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 475 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 480 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 485 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 490 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 495 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 500 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 505 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)	/* 510 */
	ENTRY_NAME(sys_ni_syscall)
	ENTRY_NAME(sys_ni_syscall)
.end

