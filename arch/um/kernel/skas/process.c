/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/mman.h>
#include <sys/user.h>
#include <asm/unistd.h>
#include "user.h"
#include "ptrace_user.h"
#include "time_user.h"
#include "sysdep/ptrace.h"
#include "user_util.h"
#include "kern_util.h"
#include "skas.h"
#include "sysdep/sigcontext.h"
#include "os.h"
#include "proc_mm.h"
#include "skas_ptrace.h"

unsigned long exec_regs[FRAME_SIZE];
unsigned long exec_fp_regs[HOST_FP_SIZE];
unsigned long exec_fpx_regs[HOST_XFP_SIZE];
int have_fpx_regs = 1;

static void handle_segv(int pid)
{
	struct ptrace_faultinfo fault;
	int err;

	err = ptrace(PTRACE_FAULTINFO, pid, 0, &fault);
	if(err)
		panic("handle_segv - PTRACE_FAULTINFO failed, errno = %d\n",
		      errno);

	segv(fault.addr, 0, FAULT_WRITE(fault.is_write), 1, NULL);
}

static void handle_trap(int pid, union uml_pt_regs *regs)
{
	int err, syscall_nr, status;

	syscall_nr = PT_SYSCALL_NR(regs->skas.regs);
	if(syscall_nr < 1){
		relay_signal(SIGTRAP, regs);
		return;
	}
	UPT_SYSCALL_NR(regs) = syscall_nr;

	err = ptrace(PTRACE_POKEUSER, pid, PT_SYSCALL_NR_OFFSET, __NR_getpid);
	if(err < 0)
	        panic("handle_trap - nullifying syscall failed errno = %d\n", 
		      errno);

	err = ptrace(PTRACE_SYSCALL, pid, 0, 0);
	if(err < 0)
	        panic("handle_trap - continuing to end of syscall failed, "
		      "errno = %d\n", errno);

	err = waitpid(pid, &status, WUNTRACED);
	if((err < 0) || !WIFSTOPPED(status) || (WSTOPSIG(status) != SIGTRAP))
		panic("handle_trap - failed to wait at end of syscall, "
		      "errno = %d, status = %d\n", errno, status);

	handle_syscall(regs);
}

int userspace_pid;

static int userspace_tramp(void *arg)
{
	init_new_thread_signals(0);
	enable_timer();
	ptrace(PTRACE_TRACEME, 0, 0, 0);
	os_stop_process(os_getpid());
	return(0);
}

void start_userspace(void)
{
	void *stack;
	unsigned long sp;
	int pid, status, n;

	stack = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if(stack == MAP_FAILED)
		panic("start_userspace : mmap failed, errno = %d", errno);
	sp = (unsigned long) stack + PAGE_SIZE - sizeof(void *);

	pid = clone(userspace_tramp, (void *) sp, 
		    CLONE_FILES | CLONE_VM | SIGCHLD, NULL);
	if(pid < 0)
		panic("start_userspace : clone failed, errno = %d", errno);

	do {
		n = waitpid(pid, &status, WUNTRACED);
		if(n < 0)
			panic("start_userspace : wait failed, errno = %d", 
			      errno);
	} while(WIFSTOPPED(status) && (WSTOPSIG(status) == SIGVTALRM));

	if(!WIFSTOPPED(status) || (WSTOPSIG(status) != SIGSTOP))
		panic("start_userspace : expected SIGSTOP, got status = %d",
		      status);

	if(munmap(stack, PAGE_SIZE) < 0)
		panic("start_userspace : munmap failed, errno = %d\n", errno);

	userspace_pid = pid;
}

void userspace(union uml_pt_regs *regs)
{
	int err, status, op;

	restore_registers(regs);
		
	err = ptrace(PTRACE_SYSCALL, userspace_pid, 0, 0);
	if(err)
		panic("userspace - PTRACE_SYSCALL failed, errno = %d\n", 
		       errno);
	while(1){
		err = waitpid(userspace_pid, &status, WUNTRACED);
		if(err < 0)
			panic("userspace - waitpid failed, errno = %d\n", 
			      errno);

		regs->skas.is_user = 1;
		save_registers(regs);

		if(WIFSTOPPED(status)){
		  	switch(WSTOPSIG(status)){
			case SIGSEGV:
				handle_segv(userspace_pid);
				break;
			case SIGTRAP:
			        handle_trap(userspace_pid, regs);
				break;
			case SIGIO:
			case SIGVTALRM:
			case SIGILL:
			case SIGBUS:
			case SIGFPE:
				user_signal(WSTOPSIG(status), regs);
				break;
			default:
			        printk("userspace - child stopped with signal "
				       "%d\n", WSTOPSIG(status));
			}
			interrupt_end();
		}

		restore_registers(regs);

		op = singlestepping_skas() ? PTRACE_SINGLESTEP : 
			PTRACE_SYSCALL;
		err = ptrace(op, userspace_pid, 0, 0);
		if(err)
			panic("userspace - PTRACE_SYSCALL failed, "
			      "errno = %d\n", errno);
	}
}

void new_thread(void *stack, void **switch_buf_ptr, void **fork_buf_ptr,
		void (*handler)(int))
{
	jmp_buf switch_buf, fork_buf;

	*switch_buf_ptr = &switch_buf;
	*fork_buf_ptr = &fork_buf;

	if(setjmp(fork_buf) == 0)
		new_thread_proc(stack, handler);

	remove_sigstack();
}

void thread_wait(void *sw, void *fb)
{
	jmp_buf buf, **switch_buf = sw, *fork_buf;

	*switch_buf = &buf;
	fork_buf = fb;
	if(setjmp(buf) == 0)
		longjmp(*fork_buf, 1);
}

static int move_registers(int int_op, int fp_op, union uml_pt_regs *regs,
			  unsigned long *fp_regs)
{
	if(ptrace(int_op, userspace_pid, 0, regs->skas.regs) < 0)
		return(-errno);
	if(ptrace(fp_op, userspace_pid, 0, fp_regs) < 0)
		return(-errno);
	return(0);
}

void save_registers(union uml_pt_regs *regs)
{
	unsigned long *fp_regs;
	int err, fp_op;

	if(have_fpx_regs){
		fp_op = PTRACE_GETFPXREGS;
		fp_regs = regs->skas.xfp;
	}
	else {
		fp_op = PTRACE_GETFPREGS;
		fp_regs = regs->skas.fp;
	}

	err = move_registers(PTRACE_GETREGS, fp_op, regs, fp_regs);
	if(err)
		panic("save_registers - saving registers failed, errno = %d\n",
		      err);
}

void restore_registers(union uml_pt_regs *regs)
{
	unsigned long *fp_regs;
	int err, fp_op;

	if(have_fpx_regs){
		fp_op = PTRACE_SETFPXREGS;
		fp_regs = regs->skas.xfp;
	}
	else {
		fp_op = PTRACE_SETFPREGS;
		fp_regs = regs->skas.fp;
	}

	err = move_registers(PTRACE_SETREGS, fp_op, regs, fp_regs);
	if(err)
		panic("restore_registers - saving registers failed, "
		      "errno = %d\n", err);
}

void switch_threads(void *me, void *next)
{
	jmp_buf my_buf, **me_ptr = me, *next_buf = next;
	
	*me_ptr = &my_buf;
	if(setjmp(my_buf) == 0)
		longjmp(*next_buf, 1);
}

static jmp_buf initial_jmpbuf;

/* XXX Make these percpu */
static void (*cb_proc)(void *arg);
static void *cb_arg;
static jmp_buf *cb_back;

int start_idle_thread(void *stack, void *switch_buf_ptr, void **fork_buf_ptr)
{
	jmp_buf **switch_buf = switch_buf_ptr;
	int n;

	*fork_buf_ptr = &initial_jmpbuf;
	n = setjmp(initial_jmpbuf);
	if(n == 0)
		new_thread_proc((void *) stack, new_thread_handler);
	else if(n == 1)
		remove_sigstack();
	else if(n == 2){
		(*cb_proc)(cb_arg);
		longjmp(*cb_back, 1);
	}
	else if(n == 3){
		kmalloc_ok = 0;
		return(0);
	}
	else if(n == 4){
		kmalloc_ok = 0;
		return(1);
	}
	longjmp(**switch_buf, 1);
}

void remove_sigstack(void)
{
	stack_t stack = ((stack_t) { .ss_flags	= SS_DISABLE,
				     .ss_sp	= NULL,
				     .ss_size	= 0 });

	if(sigaltstack(&stack, NULL) != 0)
		panic("disabling signal stack failed, errno = %d\n", errno);
}

void initial_thread_cb_skas(void (*proc)(void *), void *arg)
{
	jmp_buf here;

	cb_proc = proc;
	cb_arg = arg;
	cb_back = &here;

	block_signals();
	if(setjmp(here) == 0)
		longjmp(initial_jmpbuf, 2);
	unblock_signals();

	cb_proc = NULL;
	cb_arg = NULL;
	cb_back = NULL;
}

void halt_skas(void)
{
	block_signals();
	longjmp(initial_jmpbuf, 3);
}

void reboot_skas(void)
{
	block_signals();
	longjmp(initial_jmpbuf, 4);
}

int new_mm(int from)
{
	struct proc_mm_op copy;
	int n, fd = os_open_file("/proc/mm", of_write(OPENFLAGS()), 0);

	if(fd < 0)
		return(-errno);

	if(from != -1){
		copy = ((struct proc_mm_op) { .op 	= MM_COPY_SEGMENTS,
					      .u 	= 
					      { .copy_segments	= from } } );
		n = os_write_file(fd, &copy, sizeof(copy));
		if(n != sizeof(copy)) 
			printk("new_mm : /proc/mm copy_segments failed, "
			       "errno = %d\n", errno);
	}
	return(fd);
}

void switch_mm_skas(int mm_fd)
{
	int err;

	err = ptrace(PTRACE_SWITCH_MM, userspace_pid, 0, mm_fd);
	if(err)
		panic("switch_mm_skas - PTRACE_SWITCH_MM failed, errno = %d\n",
		      errno);
}

void kill_off_processes_skas(void)
{
	os_kill_process(userspace_pid, 1);
}

void init_registers(int pid)
{
	int err;

	if(ptrace(PTRACE_GETREGS, pid, 0, exec_regs) < 0)
		panic("check_ptrace : PTRACE_GETREGS failed, errno = %d", 
		      errno);

	err = ptrace(PTRACE_GETFPXREGS, pid, 0, exec_fpx_regs);
	if(!err)
		return;

	have_fpx_regs = 0;
	if(errno != EIO)
		panic("check_ptrace : PTRACE_GETFPXREGS failed, errno = %d", 
		      errno);

	err = ptrace(PTRACE_GETFPREGS, pid, 0, exec_fp_regs);
	if(err)
		panic("check_ptrace : PTRACE_GETFPREGS failed, errno = %d", 
		      errno);
}

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
