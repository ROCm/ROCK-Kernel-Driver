/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sched.h>
#include <fcntl.h>
#include <setjmp.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/page.h>
#include <asm/unistd.h>
#include <asm/ptrace.h>
#include "user_util.h"
#include "kern_util.h"
#include "signal_user.h"
#include "mem_user.h"
#include "user.h"
#include "process.h"
#include "sigcontext.h"
#include "sysdep/sigcontext.h"
#include "init.h"
#include "chan_user.h"
#include "irq_user.h"
#include "frame_user.h"
#include "syscall_user.h"
#include "ptrace_user.h"
#include "task.h"
#include "os.h"

static void signal_segv(int sig)
{
	write(2, "Seg fault in signals\n", strlen("Seg fault in signals\n"));
	for(;;) ;
}

int detach(int pid, int sig)
{
	return(ptrace(PTRACE_DETACH, pid, 0, sig));
}

int attach(int pid)
{
	int err;

	err = ptrace(PTRACE_ATTACH, pid, 0, 0);
	if(err < 0) return(-errno);
	else return(err);
}

int cont(int pid)
{
	return(ptrace(PTRACE_CONT, pid, 0, 0));
}

void kill_child_dead(int pid)
{
	kill(pid, SIGKILL);
	kill(pid, SIGCONT);
	while(waitpid(pid, NULL, 0) > 0) kill(pid, SIGCONT);
}

int debug = 0;
int debug_stop = 1;
int debug_parent = 0;

int honeypot = 0;

static int signal_tramp(void *arg)
{
	int (*proc)(void *);

	if(honeypot && munmap((void *) (host_task_size - 0x10000000),
			      0x10000000)) 
		panic("Unmapping stack failed");
	if(ptrace(PTRACE_TRACEME, 0, 0, 0) < 0)
		panic("ptrace PTRACE_TRACEME failed");
	os_stop_process(os_getpid());
	change_sig(SIGWINCH, 0);
	signal(SIGUSR1, SIG_IGN);
	change_sig(SIGCHLD, 0);
	signal(SIGSEGV, (__sighandler_t) sig_handler);
	set_timers(0);
	set_cmdline("(idle thread)");
	set_init_pid(os_getpid());
	proc = arg;
	return((*proc)(NULL));
}

static void last_ditch_exit(int sig)
{
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	uml_cleanup();
	exit(1);
}

static void sleeping_process_signal(int pid, int sig)
{
	switch(sig){
	/* These two result from UML being ^Z-ed and bg-ed.  PTRACE_CONT is
	 * right because the process must be in the kernel already.
	 */
	case SIGCONT:
	case SIGTSTP:
		if(ptrace(PTRACE_CONT, pid, 0, sig) < 0)
			tracer_panic("sleeping_process_signal : Failed to "
				     "continue pid %d, errno = %d\n", pid,
				     sig);
		break;

	/* This happens when the debugger (e.g. strace) is doing system call 
	 * tracing on the kernel.  During a context switch, the current task
	 * will be set to the incoming process and the outgoing process will
	 * hop into write and then read.  Since it's not the current process
	 * any more, the trace of those will land here.  So, we need to just 
	 * PTRACE_SYSCALL it.
	 */
	case SIGTRAP:
		if(ptrace(PTRACE_SYSCALL, pid, 0, 0) < 0)
			tracer_panic("sleeping_process_signal : Failed to "
				     "PTRACE_SYSCALL pid %d, errno = %d\n",
				     pid, sig);
		break;
	case SIGSTOP:
		break;
	default:
		tracer_panic("sleeping process %d got unexpected "
			     "signal : %d\n", pid, sig);
		break;
	}
}

#ifdef CONFIG_SMP
#error need to make these arrays
#endif

int debugger_pid = -1;
int debugger_parent = -1;
int debugger_fd = -1;
int gdb_pid = -1;

struct {
	unsigned long address;
	int is_write;
	int pid;
	unsigned long sp;
	int is_user;
} segfault_record[1024];

int segfault_index = 0;

struct {
	int pid;
	int signal;
	unsigned long addr;
	struct timeval time;
} signal_record[1024];

int signal_index = 0;
int nsignals = 0;
int debug_trace = 0;
extern int io_nsignals, io_count, intr_count;

extern void signal_usr1(int sig);

int tracing_pid = -1;

int signals(int (*init_proc)(void *), void *sp)
{
	void *task = NULL;
	unsigned long eip = 0;
	int status, pid = 0, sig = 0, cont_type, tracing = 0, op = 0;
	int last_index, proc_id = 0, n, err, old_tracing = 0, strace = 0;

	capture_signal_stack();
	signal(SIGPIPE, SIG_IGN);
	setup_tracer_winch();
	tracing_pid = os_getpid();
	printk("tracing thread pid = %d\n", tracing_pid);

	pid = clone(signal_tramp, sp, CLONE_FILES | SIGCHLD, init_proc);
	n = waitpid(pid, &status, WUNTRACED);
	if(n < 0){
		printf("waitpid on idle thread failed, errno = %d\n", errno);
		exit(1);
	}
	if((ptrace(PTRACE_CONT, pid, 0, 0) < 0)){
		printf("Failed to continue idle thread, errno = %d\n", errno);
		exit(1);
	}

	signal(SIGSEGV, signal_segv);
	signal(SIGUSR1, signal_usr1);
	set_handler(SIGINT, last_ditch_exit, SA_ONESHOT | SA_NODEFER, -1);
	set_handler(SIGTERM, last_ditch_exit, SA_ONESHOT | SA_NODEFER, -1);
	set_handler(SIGHUP, last_ditch_exit, SA_ONESHOT | SA_NODEFER, -1);
	if(debug_trace){
		printk("Tracing thread pausing to be attached\n");
		stop();
	}
	if(debug){
		if(gdb_pid != -1) 
			debugger_pid = attach_debugger(pid, gdb_pid, 1);
		else debugger_pid = init_ptrace_proxy(pid, 1, debug_stop);
		if(debug_parent){
			debugger_parent = os_process_parent(debugger_pid);
			init_parent_proxy(debugger_parent);
			err = attach(debugger_parent);
			if(err){
				printk("Failed to attach debugger parent %d, "
				       "errno = %d\n", debugger_parent, err);
				debugger_parent = -1;
			}
			else {
				if(ptrace(PTRACE_SYSCALL, debugger_parent, 
					  0, 0) < 0){
					printk("Failed to continue debugger "
					       "parent, errno = %d\n", errno);
					debugger_parent = -1;
				}
			}
		}
	}
	set_cmdline("(tracing thread)");
	while(1){
		if((pid = waitpid(-1, &status, WUNTRACED)) <= 0){
			if(errno != ECHILD){
				printk("wait failed - errno = %d\n", errno);
			}
			continue;
		}
		if(pid == debugger_pid){
			int cont = 0;

			if(WIFEXITED(status) || WIFSIGNALED(status))
				debugger_pid = -1;
			/* XXX Figure out how to deal with gdb and SMP */
			else cont = debugger_signal(status, cpu_tasks[0].pid);
			if(cont == PTRACE_SYSCALL) strace = 1;
			continue;
		}
		else if(pid == debugger_parent){
			debugger_parent_signal(status, pid);
			continue;
		}
		nsignals++;
		if(WIFEXITED(status)) ;
#ifdef notdef
		{
			printk("Child %d exited with status %d\n", pid, 
			       WEXITSTATUS(status));
		}
#endif
		else if(WIFSIGNALED(status)){
			sig = WTERMSIG(status);
			if(sig != 9){
				printk("Child %d exited with signal %d\n", pid,
				       sig);
			}
		}
		else if(WIFSTOPPED(status)){
			sig = WSTOPSIG(status);
			if(signal_index == 1024){
				signal_index = 0;
				last_index = 1023;
			}
			else last_index = signal_index - 1;
			if(((sig == SIGPROF) || (sig == SIGVTALRM) || 
			    (sig == SIGALRM)) &&
			   (signal_record[last_index].signal == sig) &&
			   (signal_record[last_index].pid == pid))
				signal_index = last_index;
			signal_record[signal_index].pid = pid;
			gettimeofday(&signal_record[signal_index].time, NULL);
			eip = ptrace(PTRACE_PEEKUSER, pid, PT_IP_OFFSET, 0);
			signal_record[signal_index].addr = eip;
			signal_record[signal_index++].signal = sig;
			
			proc_id = pid_to_processor_id(pid);
			if(proc_id == -1){
				sleeping_process_signal(pid, sig);
				continue;
			}

			task = cpu_tasks[proc_id].task;
			tracing = is_tracing(task);
			old_tracing = tracing;

			switch(sig){
			case SIGUSR1:
				sig = 0;
				op = do_proc_op(task, proc_id);
				switch(op){
				case OP_TRACE_ON:
					arch_leave_kernel(task, pid);
					tracing = 1;
					break;
				case OP_REBOOT:
				case OP_HALT:
					unmap_physmem();
					kmalloc_ok = 0;
					ptrace(PTRACE_KILL, pid, 0, 0);
					return(op == OP_REBOOT);
				case OP_NONE:
					printk("Detaching pid %d\n", pid);
					detach(pid, SIGSTOP);
					continue;
				default:
					break;
				}
				/* OP_EXEC switches host processes on us,
				 * we want to continue the new one.
				 */
				pid = cpu_tasks[proc_id].pid;
				break;
			case SIGTRAP:
				if(!tracing && (debugger_pid != -1)){
					child_signal(pid, status);
					continue;
				}
				tracing = 0;
				if(do_syscall(task, pid)) sig = SIGUSR2;
				else clear_singlestep(task);
				break;
			case SIGPROF:
				if(tracing) sig = 0;
				break;
			case SIGCHLD:
			case SIGHUP:
				sig = 0;
				break;
			case SIGSEGV:
			case SIGIO:
			case SIGALRM:
			case SIGVTALRM:
			case SIGFPE:
			case SIGBUS:
			case SIGILL:
			case SIGWINCH:
			default:
				tracing = 0;
				break;
			}
			set_tracing(task, tracing);

			if(!tracing && old_tracing)
				arch_enter_kernel(task, pid);

			if(!tracing && (debugger_pid != -1) && (sig != 0) &&
				(sig != SIGALRM) && (sig != SIGVTALRM) &&
				(sig != SIGSEGV) && (sig != SIGTRAP) &&
				(sig != SIGUSR2) && (sig != SIGIO)){
				child_signal(pid, status);
				continue;
			}

			if(tracing){
				if(singlestepping(task))
					cont_type = PTRACE_SINGLESTEP;
				else cont_type = PTRACE_SYSCALL;
			}
			else cont_type = PTRACE_CONT;

			if((cont_type == PTRACE_CONT) && 
			   (debugger_pid != -1) && strace)
				cont_type = PTRACE_SYSCALL;

			if(ptrace(cont_type, pid, 0, sig) != 0){
				tracer_panic("ptrace failed to continue "
					     "process - errno = %d\n", 
					     errno);
			}
		}
	}
	return(0);
}

static int __init uml_debugtrace_setup(char *line, int *add)
{
	debug_trace = 1;
	return 0;
}
__uml_setup("debugtrace", uml_debugtrace_setup,
"debugtrace\n"
"    Causes the tracing thread to pause until it is attached by a\n"
"    debugger and continued.  This is mostly for debugging crashes\n"
"    early during boot, and should be pretty much obsoleted by\n"
"    the debug switch.\n\n"
);

static int __init uml_honeypot_setup(char *line, int *add)
{
	jail_setup("", add);
	honeypot = 1;
	return 0;
}
__uml_setup("honeypot", uml_honeypot_setup, 
"honeypot\n"
"    This makes UML put process stacks in the same location as they are\n"
"    on the host, allowing expoits such as stack smashes to work against\n"
"    UML.  This implies 'jail'.\n\n"
);

int nsegfaults = 0;

void segv_handler(int sig, struct uml_pt_regs *regs)
{
	struct sigcontext_struct *context = regs->sc;
	int index;

	if(regs->is_user && !SEGV_IS_FIXABLE(context)){
		bad_segv(SC_FAULT_ADDR(context), SC_IP(context), 
			 SC_FAULT_WRITE(context));
		return;
	}
	lock_trap();
	index = segfault_index++;
	if(segfault_index == 1024) segfault_index = 0;
	unlock_trap();
	nsegfaults++;
	segfault_record[index].address = SC_FAULT_ADDR(context);
	segfault_record[index].pid = os_getpid();
	segfault_record[index].is_write = SC_FAULT_WRITE(context);
	segfault_record[index].sp = SC_SP(context);
	segfault_record[index].is_user = regs->is_user;
	segv(SC_FAULT_ADDR(context), SC_IP(context), SC_FAULT_WRITE(context),
	     regs->is_user, context);
}

extern int kern_timer_on;

struct signal_info {
	void (*handler)(int, struct uml_pt_regs *);
	int is_irq;
};

static struct signal_info sig_info[] = {
	[ SIGTRAP ] { handler :		relay_signal,
		      is_irq :		0 },
	[ SIGFPE ] { handler :		relay_signal,
		     is_irq :		0 },
	[ SIGILL ] { handler :		relay_signal,
		     is_irq :		0 },
	[ SIGBUS ] { handler :		bus_handler,
		     is_irq :		0 },
	[ SIGSEGV] { handler :		segv_handler,
		     is_irq :		0 },
	[ SIGIO ] { handler :		sigio_handler,
		    is_irq :		1 },
	[ SIGVTALRM ] { handler :	timer_handler,
			is_irq :	1 },
	[ SIGALRM ] { handler :		timer_handler,
		      is_irq :		1 },
	[ SIGUSR2 ] { handler :		syscall_handler,
		      is_irq :		0 },
};

void sig_handler_common(int sig, struct sigcontext *sc)
{
	struct uml_pt_regs save_regs, *r;
	struct signal_info *info;
	int save_errno = errno, save_timer = kern_timer_on, is_user;

	unprotect_kernel_mem();

	r = (struct uml_pt_regs *) TASK_REGS(get_current());
	save_regs = *r;
	is_user = user_context(SC_SP(sc));
	r->is_user = is_user;
	r->sc = sc;
	if(sig != SIGUSR2) r->syscall = -1;

	change_sig(SIGUSR1, 1);
	info = &sig_info[sig];
	if(!info->is_irq) unblock_signals();

	(*info->handler)(sig, r);

	kern_timer_on = save_timer;
	if(is_user){
		interrupt_end();
		block_signals();
		change_sig(SIGUSR1, 0);
		set_user_mode(NULL);
	}
	*r = save_regs;
	errno = save_errno;
	if(is_user) protect_kernel_mem();
}

void sig_handler(int sig, struct sigcontext sc)
{
	sig_handler_common(sig, &sc);
}

extern int timer_irq_inited, missed_ticks;

extern int jail_timer_off;

void alarm_handler(int sig, struct sigcontext sc)
{
	int user;

	if(!timer_irq_inited) return;
	missed_ticks++;
	user = user_context(SC_SP(&sc));
	if(!user && !kern_timer_on) return;
	if(!user && jail_timer_off) return;
	sig_handler_common(sig, &sc);
}

void do_longjmp(void *p)
{
    jmp_buf *jbuf = (jmp_buf *) p;

    longjmp(*jbuf, 1);
}

static int __init uml_debug_setup(char *line, int *add)
{
	char *next;

	debug = 1;
	*add = 0;
	if(*line != '=') return(0);
	line++;

	while(line != NULL){
		next = strchr(line, ',');
		if(next) *next++ = '\0';
		
		if(!strcmp(line, "go"))	debug_stop = 0;
		else if(!strcmp(line, "parent")) debug_parent = 1;
		else printk("Unknown debug option : '%s'\n", line);

		line = next;
	}
	return(0);
}

__uml_setup("debug", uml_debug_setup,
"debug\n"
"    Starts up the kernel under the control of gdb. See the \n"
"    kernel debugging tutorial and the debugging session pages\n"
"    at http://user-mode-linux.sourceforge.net/ for more information.\n\n"
);

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
