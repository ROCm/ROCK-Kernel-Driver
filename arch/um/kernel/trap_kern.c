/* 
 * Copyright (C) 2000, 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/kernel.h"
#include "linux/sched.h"
#include "linux/mm.h"
#include "linux/spinlock.h"
#include "linux/config.h"
#include "linux/init.h"
#include "linux/ptrace.h"
#include "asm/semaphore.h"
#include "asm/pgtable.h"
#include "asm/tlbflush.h"
#include "asm/a.out.h"
#include "asm/current.h"
#include "user_util.h"
#include "kern_util.h"
#include "kern.h"
#include "chan_kern.h"
#include "debug.h"
#include "mconsole_kern.h"
#include "2_5compat.h"

extern int nsyscalls;

unsigned long segv(unsigned long address, unsigned long ip, int is_write, 
		   int is_user, void *sc)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct siginfo si;
	void *catcher;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long page;

	if((address >= start_vm) && (address < end_vm)){
		flush_tlb_kernel_vm();
		return(0);
	}
	if(mm == NULL) panic("Segfault with no mm");
	catcher = current->thread.fault_catcher;
	si.si_code = SEGV_MAPERR;
	down_read(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if(!vma) goto bad;
	else if(vma->vm_start <= address) goto good_area;
	else if(!(vma->vm_flags & VM_GROWSDOWN)) goto bad;
	else if(expand_stack(vma, address)) goto bad;

 good_area:
	si.si_code = SEGV_ACCERR;
	if(is_write && !(vma->vm_flags & VM_WRITE)) goto bad;
	page = address & PAGE_MASK;
	if(page == (unsigned long) current->thread_info + PAGE_SIZE)
		panic("Kernel stack overflow");
	pgd = pgd_offset(mm, page);
	pmd = pmd_offset(pgd, page);
 survive:
	do {
		switch (handle_mm_fault(mm, vma, address, is_write)){
		case VM_FAULT_MINOR:
			current->min_flt++;
			break;
		case VM_FAULT_MAJOR:
			current->maj_flt++;
			break;
		case VM_FAULT_SIGBUS:
			goto do_sigbus;
		case VM_FAULT_OOM:
			goto out_of_memory;
		default:
			BUG();
		}
		pte = pte_offset_kernel(pmd, page);
	} while(!pte_present(*pte));
	*pte = pte_mkyoung(*pte);
	if(pte_write(*pte)) *pte = pte_mkdirty(*pte);
	flush_tlb_page(vma, page);
	up_read(&mm->mmap_sem);
	return(0);
do_sigbus:
	up_read(&mm->mmap_sem);

	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	si.si_signo = SIGBUS;
	si.si_errno = 0;
	si.si_code = BUS_ADRERR;
	si.si_addr = (void *)address;
	force_sig_info(SIGBUS, &si, current);
	if(!is_user) goto bad;
	return(0);
/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	up_read(&mm->mmap_sem);
	if (current->pid == 1) {
		yield();
		down_read(&mm->mmap_sem);
		goto survive;
	}
	printk("VM: killing process %s\n", current->comm);
	if(is_user)
		do_exit(SIGKILL);

	/* Fall through to bad */

 bad:
	if(catcher != NULL){
		current->thread.fault_addr = (void *) address;
		up_read(&mm->mmap_sem);
		do_longjmp(catcher);
	} 
	else if(current->thread.fault_addr != NULL){
		panic("fault_addr set but no fault catcher");
	}
	else if(arch_fixup(ip, sc))
		return(0);

	if(!is_user) 
		panic("Kernel mode fault at addr 0x%lx, ip 0x%lx", 
		      address, ip);
	si.si_signo = SIGSEGV;
	si.si_addr = (void *) address;
	current->thread.cr2 = address;
	current->thread.err = is_write;
	force_sig_info(SIGSEGV, &si, current);
	up_read(&mm->mmap_sem);
	return(0);
}

void bad_segv(unsigned long address, unsigned long ip, int is_write)
{
	struct siginfo si;

	printk(KERN_ERR "Unfixable SEGV in '%s' (pid %d) at 0x%lx "
	       "(ip 0x%lx)\n", current->comm, current->pid, address, ip);
	si.si_signo = SIGSEGV;
	si.si_code = SEGV_ACCERR;
	si.si_addr = (void *) address;
	current->thread.cr2 = address;
	current->thread.err = is_write;
	force_sig_info(SIGSEGV, &si, current);
}

void relay_signal(int sig, struct uml_pt_regs *regs)
{
	if(arch_handle_signal(sig, regs)) return;
	if(!regs->is_user) panic("Kernel mode signal %d", sig);
	force_sig(sig, current);
}

void bus_handler(int sig, struct uml_pt_regs *regs)
{
	if(current->thread.fault_catcher != NULL)
		do_longjmp(current->thread.fault_catcher);
	else relay_signal(sig, regs);
}

void trap_init(void)
{
}

spinlock_t trap_lock = SPIN_LOCK_UNLOCKED;

void lock_trap(void)
{
	spin_lock(&trap_lock);
}

void unlock_trap(void)
{
	spin_unlock(&trap_lock);
}

extern int debugger_pid;
extern int debugger_fd;
extern int debugger_parent;

#ifdef CONFIG_PT_PROXY

int debugger_signal(int status, pid_t pid)
{
	return(debugger_proxy(status, pid));
}

void child_signal(pid_t pid, int status)
{
	child_proxy(pid, status);
}

static void gdb_announce(char *dev_name, int dev)
{
	printf("gdb assigned device '%s'\n", dev_name);
}

static struct chan_opts opts = {
	announce : 	gdb_announce,
	xterm_title :	"UML kernel debugger",
	raw :		0,
	tramp_stack :	0,
};

static void *xterm_data;
static int xterm_fd;

extern void *xterm_init(char *, int, struct chan_opts *);
extern int xterm_open(int, int, int, void *);
extern void xterm_close(int, void *);

int open_gdb_chan(void)
{
	char stack[PAGE_SIZE];

	opts.tramp_stack = (unsigned long) stack;
	xterm_data = xterm_init("", 0, &opts);
	xterm_fd = xterm_open(1, 1, 1, xterm_data);
	return(xterm_fd);
}

static void exit_debugger_cb(void *unused)
{
	if(debugger_pid != -1){
		if(gdb_pid != -1){
			fake_child_exit();
			gdb_pid = -1;
		}
		else kill_child_dead(debugger_pid);
		debugger_pid = -1;
		if(debugger_parent != -1)
			detach(debugger_parent, SIGINT);
	}
	if(xterm_data != NULL) xterm_close(xterm_fd, xterm_data);
}

static void exit_debugger(void)
{
	tracing_cb(exit_debugger_cb, NULL);
}

__uml_exitcall(exit_debugger);

struct gdb_data {
	char *str;
	int err;
};

static void config_gdb_cb(void *arg)
{
	struct gdb_data *data = arg;
	struct task_struct *task;
	int pid;

	data->err = -1;
	if(debugger_pid != -1) exit_debugger_cb(NULL);
	if(!strncmp(data->str, "pid,", strlen("pid,"))){
		data->str += strlen("pid,");
		pid = simple_strtoul(data->str, NULL, 0);
		task = cpu_tasks[0].task;
		debugger_pid = attach_debugger(task->thread.extern_pid,
					       pid, 0);
		if(debugger_pid != -1){
			data->err = 0;
			gdb_pid = pid;
		}
		return;
	}
	data->err = 0;
	debugger_pid = start_debugger(linux_prog, 0, 0, &debugger_fd);
	init_proxy(debugger_pid, 0, 0);
}

int gdb_config(char *str)
{
	struct gdb_data data;

	if(*str++ != '=') return(-1);
	data.str = str;
	tracing_cb(config_gdb_cb, &data);
	return(data.err);
}

void remove_gdb_cb(void *unused)
{
	exit_debugger_cb(NULL);
}

int gdb_remove(char *unused)
{
	tracing_cb(remove_gdb_cb, NULL);
	return(0);
}

#ifdef CONFIG_MCONSOLE

static struct mc_device gdb_mc = {
	name:		"gdb",
	config:		gdb_config,
	remove:		gdb_remove,
};

int gdb_mc_init(void)
{
	mconsole_register_dev(&gdb_mc);
	return(0);
}

__initcall(gdb_mc_init);

#endif

void signal_usr1(int sig)
{
	if(debugger_pid != -1){
		printk(KERN_ERR "The debugger is already running\n");
		return;
	}
	debugger_pid = start_debugger(linux_prog, 0, 0, &debugger_fd);
	init_proxy(debugger_pid, 0, 0);
}

int init_ptrace_proxy(int idle_pid, int startup, int stop)
{
	int pid, status;

	pid = start_debugger(linux_prog, startup, stop, &debugger_fd);
	status = wait_for_stop(idle_pid, SIGSTOP, PTRACE_CONT, NULL);
 	if(pid < 0){
		cont(idle_pid);
		return(-1);
	}
	init_proxy(pid, 1, status);
	return(pid);
}

int attach_debugger(int idle_pid, int pid, int stop)
{
	int status = 0, err;

	err = attach(pid);
	if(err < 0){
		printf("Failed to attach pid %d, errno = %d\n", pid, -err);
		return(-1);
	}
	if(stop) status = wait_for_stop(idle_pid, SIGSTOP, PTRACE_CONT, NULL);
	init_proxy(pid, 1, status);
	return(pid);
}

#ifdef notdef /* Put this back in when it does something useful */
static int __init uml_gdb_init_setup(char *line, int *add)
{
	gdb_init = uml_strdup(line);
	return 0;
}

__uml_setup("gdb=", uml_gdb_init_setup, 
"gdb=<channel description>\n\n"
);
#endif

static int __init uml_gdb_pid_setup(char *line, int *add)
{
	gdb_pid = simple_strtoul(line, NULL, 0);
	*add = 0;
	return 0;
}

__uml_setup("gdb-pid=", uml_gdb_pid_setup, 
"gdb-pid=<pid>\n"
"    gdb-pid is used to attach an external debugger to UML.  This may be\n"
"    an already-running gdb or a debugger-like process like strace.\n\n"
);

#else

int debugger_signal(int status, pid_t pid){ return(0); }
void child_signal(pid_t pid, int status){ }
int init_ptrace_proxy(int idle_pid, int startup, int stop)
{
	printk(KERN_ERR "debug requested when CONFIG_PT_PROXY is off\n");
	kill_child_dead(idle_pid);
	exit(1);
}

void signal_usr1(int sig)
{
	printk(KERN_ERR "debug requested when CONFIG_PT_PROXY is off\n");
}

int attach_debugger(int idle_pid, int pid, int stop)
{
	printk(KERN_ERR "attach_debugger called when CONFIG_PT_PROXY "
	       "is off\n");
	return(-1);
}

int config_gdb(char *str)
{
	return(-1);
}

int remove_gdb(void)
{
	return(-1);
}

int init_parent_proxy(int pid)
{
	return(-1);
}

void debugger_parent_signal(int status, int pid)
{
}

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
