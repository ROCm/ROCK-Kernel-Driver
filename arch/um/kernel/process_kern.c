/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/config.h"
#include "linux/kernel.h"
#include "linux/sched.h"
#include "linux/interrupt.h"
#include "linux/mm.h"
#include "linux/slab.h"
#include "linux/utsname.h"
#include "linux/fs.h"
#include "linux/utime.h"
#include "linux/smp_lock.h"
#include "linux/module.h"
#include "linux/init.h"
#include "linux/capability.h"
#include "asm/unistd.h"
#include "asm/mman.h"
#include "asm/segment.h"
#include "asm/stat.h"
#include "asm/pgtable.h"
#include "asm/processor.h"
#include "asm/tlbflush.h"
#include "asm/spinlock.h"
#include "asm/uaccess.h"
#include "asm/user.h"
#include "user_util.h"
#include "kern_util.h"
#include "kern.h"
#include "signal_kern.h"
#include "signal_user.h"
#include "init.h"
#include "irq_user.h"
#include "mem_user.h"
#include "time_user.h"
#include "tlb.h"
#include "frame_kern.h"
#include "sigcontext.h"
#include "2_5compat.h"
#include "os.h"

struct cpu_task cpu_tasks[NR_CPUS] = { [0 ... NR_CPUS - 1] = { -1, NULL } };

struct task_struct *get_task(int pid, int require)
{
        struct task_struct *task, *ret;

        ret = NULL;
        read_lock(&tasklist_lock);
        for_each_task(task){
                if(task->pid == pid){
                        ret = task;
                        break;
                }
        }
        read_unlock(&tasklist_lock);
        if(require && (ret == NULL)) panic("get_task couldn't find a task\n");
        return(ret);
}

int is_valid_pid(int pid)
{
	struct task_struct *task;

        read_lock(&tasklist_lock);
        for_each_task(task){
                if(task->thread.extern_pid == pid){
			read_unlock(&tasklist_lock);
			return(1);
                }
        }
	read_unlock(&tasklist_lock);
	return(0);
}

int external_pid(void *t)
{
	struct task_struct *task = t ? t : current;

	return(task->thread.extern_pid);
}

int pid_to_processor_id(int pid)
{
	int i;

	for(i = 0; i < num_online_cpus(); i++){
		if(cpu_tasks[i].pid == pid) return(i);
	}
	return(-1);
}

void free_stack(unsigned long stack, int order)
{
	free_pages(stack, order);
}

void set_init_pid(int pid)
{
	int err;

	init_task.thread.extern_pid = pid;
	err = os_pipe(init_task.thread.switch_pipe, 1, 1);
	if(err)	panic("Can't create switch pipe for init_task, errno = %d", 
		      err);
}

int set_user_mode(void *t)
{
	struct task_struct *task;

	task = t ? t : current;
	if(task->thread.tracing) return(1);
	task->thread.request.op = OP_TRACE_ON;
	os_usr1_process(os_getpid());
	return(0);
}

void set_tracing(void *task, int tracing)
{
	((struct task_struct *) task)->thread.tracing = tracing;
}

int is_tracing(void *t)
{
	return (((struct task_struct *) t)->thread.tracing);
}

unsigned long alloc_stack(int order, int atomic)
{
	unsigned long page;
	int flags = GFP_KERNEL;

	if(atomic) flags |= GFP_ATOMIC;
	if((page = __get_free_pages(flags, order)) == 0)
		return(0);
	stack_protections(page);
	return(page);
}

extern void schedule_tail(struct task_struct *prev);

static void new_thread_handler(int sig)
{
	int (*fn)(void *);
	void *arg;

	fn = current->thread.request.u.thread.proc;
	arg = current->thread.request.u.thread.arg;
	current->thread.regs.regs.sc = (void *) (&sig + 1);
	suspend_new_thread(current->thread.switch_pipe[0]);

	free_page(current->thread.temp_stack);
	set_cmdline("(kernel thread)");
	force_flush_all();

	current->thread.prev_sched = NULL;
	change_sig(SIGUSR1, 1);
	unblock_signals();
	if(!run_kernel_thread(fn, arg, &current->thread.jmp))
		do_exit(0);
}

static int new_thread_proc(void *stack)
{
	block_signals();
	init_new_thread(stack, new_thread_handler);
	os_usr1_process(os_getpid());
	return(0);
}

int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	struct task_struct *p;

	current->thread.request.u.thread.proc = fn;
	current->thread.request.u.thread.arg = arg;
	p = do_fork(CLONE_VM | flags, 0, NULL, 0, NULL);
	if(IS_ERR(p)) panic("do_fork failed in kernel_thread");
	return(p->pid);
}

void switch_mm(struct mm_struct *prev, struct mm_struct *next, 
	       struct task_struct *tsk, unsigned cpu)
{
	if (prev != next) 
		clear_bit(cpu, &prev->cpu_vm_mask);
	set_bit(cpu, &next->cpu_vm_mask);
}

void set_current(void *t)
{
	struct task_struct *task = t;

	cpu_tasks[task->thread_info->cpu] = ((struct cpu_task) 
		{ task->thread.extern_pid, task });
}

void *switch_to(void *prev, void *next, void *last)
{
	struct task_struct *from, *to;
	unsigned long flags;
	int vtalrm, alrm, prof, err, cpu;
	char c;
	static int reading;

	from = prev;
	to = next;

	to->thread.prev_sched = from;

	cpu = from->thread_info->cpu;
	if(cpu == 0) 
		forward_interrupts(to->thread.extern_pid);
#ifdef CONFIG_SMP
	forward_ipi(cpu_data[cpu].ipi_pipe[0], to->thread.extern_pid);
#endif
	local_irq_save(flags);

	vtalrm = change_sig(SIGVTALRM, 0);
	alrm = change_sig(SIGALRM, 0);
	prof = change_sig(SIGPROF, 0);

	forward_pending_sigio(to->thread.extern_pid);

	c = 0;
	set_current(to);

	reading = 0;
	err = user_write(to->thread.switch_pipe[1], &c, sizeof(c));
	if(err != sizeof(c))
		panic("write of switch_pipe failed, errno = %d", -err);

	reading = 1;
	if(from->state == TASK_ZOMBIE) os_kill_process(os_getpid());

	err = user_read(from->thread.switch_pipe[0], &c, sizeof(c));
	if(err != sizeof(c))
		panic("read of switch_pipe failed, errno = %d", -err);

	/* This works around a nasty race with 'jail'.  If we are switching
	 * between two threads of a threaded app and the incoming process 
	 * runs before the outgoing process reaches the read, and it makes
	 * it all the way out to userspace, then it will have write-protected 
	 * the outgoing process stack.  Then, when the outgoing process 
	 * returns from the write, it will segfault because it can no longer
	 * write its own stack.  So, in order to avoid that, the incoming 
	 * thread sits in a loop yielding until 'reading' is set.  This 
	 * isn't entirely safe, since there may be a reschedule from a timer
	 * happening between setting 'reading' and sleeping in read.  But,
	 * it should get a whole quantum in which to reach the read and sleep,
	 * which should be enough.
	 */

	if(jail){
		while(!reading) sched_yield();
	}

	change_sig(SIGVTALRM, vtalrm);
	change_sig(SIGALRM, alrm);
	change_sig(SIGPROF, prof);

	arch_switch();

	flush_tlb_all();
	local_irq_restore(flags);

	return(current->thread.prev_sched);
}

void interrupt_end(void)
{
	if(need_resched()) schedule();
	if(test_tsk_thread_flag(current, TIF_SIGPENDING)) do_signal(0);
}

void release_thread(struct task_struct *task)
{
	os_kill_process(task->thread.extern_pid);
}

void exit_thread(void)
{
	close(current->thread.switch_pipe[0]);
	close(current->thread.switch_pipe[1]);
	unprotect_stack((unsigned long) current->thread_info);
}

/* Signal masking - signals are blocked at the start of fork_tramp.  They
 * are re-enabled when finish_fork_handler is entered by fork_tramp hitting
 * itself with a SIGUSR1.  set_user_mode has to be run with SIGUSR1 off,
 * so it is blocked before it's called.  They are re-enabled on sigreturn
 * despite the fact that they were blocked when the SIGUSR1 was issued because
 * copy_thread copies the parent's signcontext, including the signal mask
 * onto the signal frame.
 */

extern int hit_me;

void finish_fork_handler(int sig)
{
	current->thread.regs.regs.sc = (void *) (&sig + 1);
	suspend_new_thread(current->thread.switch_pipe[0]);
	
	force_flush_all();
	if(current->mm != current->parent->mm)
		protect(uml_reserved, high_physmem - uml_reserved, 1, 1, 0, 1);
	task_protections((unsigned long) current->thread_info);

	current->thread.prev_sched = NULL;

	free_page(current->thread.temp_stack);
	block_signals();
	change_sig(SIGUSR1, 0);
	set_user_mode(current);
}

void *get_current(void)
{
	return(current);
}

/* This sigusr1 business works around a bug in gcc's -pg support.  
 * Normally a procedure's mcount call comes after esp has been copied to 
 * ebp and the new frame is constructed.  With procedures with no locals,
 * the mcount comes before, as the first thing that the procedure does.
 * When that procedure is main for a thread, ebp comes in as NULL.  So,
 * when mcount dereferences it, it segfaults.  So, UML works around this
 * by adding a non-optimizable local to the various trampolines, fork_tramp
 * and outer_tramp below, and exec_tramp.
 */

static int sigusr1 = SIGUSR1;

int fork_tramp(void *stack)
{
	int sig = sigusr1;

	block_signals();
	init_new_thread(stack, finish_fork_handler);

	kill(os_getpid(), sig);
	return(0);
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long sp,
		unsigned long stack_top, struct task_struct * p, 
		struct pt_regs *regs)
{
	int new_pid, err;
	unsigned long stack;
	int (*tramp)(void *);

	p->thread = (struct thread_struct) INIT_THREAD;
	p->thread.kernel_stack = 
		(unsigned long) p->thread_info + 2 * PAGE_SIZE;

	if(current->thread.forking)
		tramp = fork_tramp;
	else {
		tramp = new_thread_proc;
		p->thread.request.u.thread = current->thread.request.u.thread;
	}

	err = os_pipe(p->thread.switch_pipe, 1, 1);
	if(err){
		printk("copy_thread : pipe failed, errno = %d\n", -err);
		return(err);
	}

	stack = alloc_stack(0, 0);
	if(stack == 0){
		printk(KERN_ERR "copy_thread : failed to allocate "
		       "temporary stack\n");
		return(-ENOMEM);
	}

	clone_flags &= CLONE_VM;
	p->thread.temp_stack = stack;
	new_pid = start_fork_tramp((void *) p->thread.kernel_stack, stack,
				   clone_flags, tramp);
	if(new_pid < 0){
		printk(KERN_ERR "copy_thread : clone failed - errno = %d\n", 
		       -new_pid);
		return(new_pid);
	}

	if(current->thread.forking){
		sc_to_sc(p->thread.regs.regs.sc, current->thread.regs.regs.sc);
		PT_REGS_SET_SYSCALL_RETURN(&p->thread.regs, 0);
		if(sp != 0) PT_REGS_SP(&p->thread.regs) = sp;
	}
	p->thread.extern_pid = new_pid;

	current->thread.request.op = OP_FORK;
	current->thread.request.u.fork.pid = new_pid;
	os_usr1_process(os_getpid());
	return(0);
}

void tracing_reboot(void)
{
	current->thread.request.op = OP_REBOOT;
	os_usr1_process(os_getpid());
}

void tracing_halt(void)
{
	current->thread.request.op = OP_HALT;
	os_usr1_process(os_getpid());
}

void tracing_cb(void (*proc)(void *), void *arg)
{
	if(os_getpid() == tracing_pid){
		(*proc)(arg);
	}
	else {
		current->thread.request.op = OP_CB;
		current->thread.request.u.cb.proc = proc;
		current->thread.request.u.cb.arg = arg;
		os_usr1_process(os_getpid());
	}
}

int do_proc_op(void *t, int proc_id)
{
	struct task_struct *task;
	struct thread_struct *thread;
	int op, pid;

	task = t;
	thread = &task->thread;
	op = thread->request.op;
	switch(op){
	case OP_NONE:
	case OP_TRACE_ON:
		break;
	case OP_EXEC:
		pid = thread->request.u.exec.pid;
		do_exec(thread->extern_pid, pid);
		thread->extern_pid = pid;
		cpu_tasks[task->thread_info->cpu].pid = pid;
		break;
	case OP_FORK:
		attach_process(thread->request.u.fork.pid);
		break;
	case OP_CB:
		(*thread->request.u.cb.proc)(thread->request.u.cb.arg);
		break;
	case OP_REBOOT:
	case OP_HALT:
		break;
	default:
		tracer_panic("Bad op in do_proc_op");
		break;
	}
	thread->request.op = OP_NONE;
	return(op);
}

unsigned long stack_sp(unsigned long page)
{
	return(page + PAGE_SIZE - sizeof(void *));
}

int current_pid(void)
{
	return(current->pid);
}

void default_idle(void)
{
 	if(current->thread_info->cpu == 0) idle_timer();

	atomic_inc(&init_mm.mm_count);
	current->mm = &init_mm;
	current->active_mm = &init_mm;

	while(1){
		/* endless idle loop with no priority at all */
		SET_PRI(current);

		/*
		 * although we are an idle CPU, we do not want to
		 * get into the scheduler unnecessarily.
		 */
		irq_stat[smp_processor_id()].idle_timestamp = jiffies;
		if(need_resched())
			schedule();
		
		idle_sleep(10);
	}
}

void cpu_idle(void)
{
	default_idle();
}

int page_size(void)
{
	return(PAGE_SIZE);
}

int page_mask(void)
{
	return(PAGE_MASK);
}

unsigned long um_virt_to_phys(void *t, unsigned long addr)
{
	struct task_struct *task;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	task = t;
	if(task->mm == NULL) return(0xffffffff);
	pgd = pgd_offset(task->mm, addr);
	pmd = pmd_offset(pgd, addr);
	if(!pmd_present(*pmd)) return(0xffffffff);
	pte = pte_offset_kernel(pmd, addr);
	if(!pte_present(*pte)) return(0xffffffff);
	return((pte_val(*pte) & PAGE_MASK) + (addr & ~PAGE_MASK));
}

char *current_cmd(void)
{
#ifdef CONFIG_SMP
	return("(Unknown)");
#else
	unsigned long addr = um_virt_to_phys(current, current->mm->arg_start);
	return addr == 0xffffffff? "(Unknown)": __va(addr);
#endif
}

void force_sigbus(void)
{
	printk(KERN_ERR "Killing pid %d because of a lack of memory\n", 
	       current->pid);
	lock_kernel();
	sigaddset(&current->pending.signal, SIGBUS);
	recalc_sigpending();
	current->flags |= PF_SIGNALED;
	do_exit(SIGBUS | 0x80);
}

void dump_thread(struct pt_regs *regs, struct user *u)
{
}

void enable_hlt(void)
{
	panic("enable_hlt");
}

void disable_hlt(void)
{
	panic("disable_hlt");
}

extern int signal_frame_size;

void *um_kmalloc(int size)
{
	return(kmalloc(size, GFP_KERNEL));
}

void *um_kmalloc_atomic(int size)
{
	return(kmalloc(size, GFP_ATOMIC));
}

unsigned long get_fault_addr(void)
{
	return((unsigned long) current->thread.fault_addr);
}

EXPORT_SYMBOL(get_fault_addr);

void clear_singlestep(void *t)
{
	struct task_struct *task = (struct task_struct *) t;

	task->ptrace &= ~PT_DTRACE;
}

int singlestepping(void *t)
{
	struct task_struct *task = (struct task_struct *) t;

	if(task->thread.singlestep_syscall)
		return(0);
	return(task->ptrace & PT_DTRACE);
}

void not_implemented(void)
{
	printk(KERN_DEBUG "Something isn't implemented in here\n");
}

EXPORT_SYMBOL(not_implemented);

int user_context(unsigned long sp)
{
	return((sp & (PAGE_MASK << 1)) != current->thread.kernel_stack);
}

extern void remove_umid_dir(void);
__uml_exitcall(remove_umid_dir);

extern exitcall_t __uml_exitcall_begin, __uml_exitcall_end;

void do_uml_exitcalls(void)
{
	exitcall_t *call;

	call = &__uml_exitcall_end;
	while (--call >= &__uml_exitcall_begin)
		(*call)();
}

void *round_up(unsigned long addr)
{
	return(ROUND_UP(addr));
}

void *round_down(unsigned long addr)
{
	return(ROUND_DOWN(addr));
}

char *uml_strdup(char *string)
{
	char *new;

	new = kmalloc(strlen(string) + 1, GFP_KERNEL);
	if(new == NULL) return(NULL);
	strcpy(new, string);
	return(new);
}

int jail = 0;

int __init jail_setup(char *line, int *add)
{
	int ok = 1;

	if(jail) return(0);
#ifdef CONFIG_SMP
	printf("'jail' may not used used in a kernel with CONFIG_SMP "
	       "enabled\n");
	ok = 0;
#endif
#ifdef CONFIG_HOSTFS
	printf("'jail' may not used used in a kernel with CONFIG_HOSTFS "
	       "enabled\n");
	ok = 0;
#endif
#ifdef CONFIG_MODULES
	printf("'jail' may not used used in a kernel with CONFIG_MODULES "
	       "enabled\n");
	ok = 0;
#endif	
	if(!ok) exit(1);

	/* CAP_SYS_RAWIO controls the ability to open /dev/mem and /dev/kmem.
	 * Removing it from the bounding set eliminates the ability of anything
	 * to acquire it, and thus read or write kernel memory.
	 */
	cap_lower(cap_bset, CAP_SYS_RAWIO);
	jail = 1;
	return(0);
}

__uml_setup("jail", jail_setup,
"jail\n"
"    Enables the protection of kernel memory from processes.\n\n"
);

static void mprotect_kernel_mem(int w)
{
	unsigned long start, end;

	if(!jail || (current == &init_task)) return;

	start = (unsigned long) current->thread_info + PAGE_SIZE;
	end = (unsigned long) current->thread_info + PAGE_SIZE * 4;
	protect(uml_reserved, start - uml_reserved, 1, w, 1, 1);
	protect(end, high_physmem - end, 1, w, 1, 1);

	start = (unsigned long) ROUND_DOWN(&_stext);
	end = (unsigned long) ROUND_UP(&_etext);
	protect(start, end - start, 1, w, 1, 1);

	start = (unsigned long) ROUND_DOWN(&_unprotected_end);
	end = (unsigned long) ROUND_UP(&_edata);
	protect(start, end - start, 1, w, 1, 1);

	start = (unsigned long) ROUND_DOWN(&__bss_start);
	end = (unsigned long) ROUND_UP(brk_start);
	protect(start, end - start, 1, w, 1, 1);

	mprotect_kernel_vm(w);
}

int jail_timer_off = 0;

void unprotect_kernel_mem(void)
{
	mprotect_kernel_mem(1);
	jail_timer_off = 0;
}

void protect_kernel_mem(void)
{
	jail_timer_off = 1;
	mprotect_kernel_mem(0);
}

void *get_init_task(void)
{
	return(&init_thread_union.thread_info.task);
}

int copy_to_user_proc(void *to, void *from, int size)
{
	return(copy_to_user(to, from, size));
}

int copy_from_user_proc(void *to, void *from, int size)
{
	return(copy_from_user(to, from, size));
}

int clear_user_proc(void *buf, int size)
{
	return(clear_user(buf, size));
}

void set_thread_sc(void *sc)
{
	current->thread.regs.regs.sc = sc;
}

int smp_sigio_handler(void)
{
#ifdef CONFIG_SMP
	IPI_handler(hard_smp_processor_id());
	if (hard_smp_processor_id() != 0) return(1);
#endif
	return(0);
}

int um_in_interrupt(void)
{
	return(in_interrupt());
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
