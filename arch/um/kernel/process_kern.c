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
#include "mode.h"
#include "mode_kern.h"
#include "choose-mode.h"

/* This is a per-cpu array.  A processor only modifies its entry and it only
 * cares about its entry, so it's OK if another processor is modifying its
 * entry.
 */
struct cpu_task cpu_tasks[NR_CPUS] = { [0 ... NR_CPUS - 1] = { -1, NULL } };

struct task_struct *get_task(int pid, int require)
{
        struct task_struct *task, *ret;

        ret = NULL;
        read_lock(&tasklist_lock);
        for_each_process(task){
                if(task->pid == pid){
                        ret = task;
                        break;
                }
        }
        read_unlock(&tasklist_lock);
        if(require && (ret == NULL)) panic("get_task couldn't find a task\n");
        return(ret);
}

int external_pid(void *t)
{
	struct task_struct *task = t ? t : current;

	return(CHOOSE_MODE_PROC(external_pid_tt, external_pid_skas, task));
}

int pid_to_processor_id(int pid)
{
	int i;

	for(i = 0; i < ncpus; i++){
		if(cpu_tasks[i].pid == pid) return(i);
	}
	return(-1);
}

void free_stack(unsigned long stack, int order)
{
	free_pages(stack, order);
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

int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	struct task_struct *p;

	current->thread.request.u.thread.proc = fn;
	current->thread.request.u.thread.arg = arg;
	p = do_fork(CLONE_VM | flags, 0, NULL, 0, NULL, NULL);
	if(IS_ERR(p)) panic("do_fork failed in kernel_thread");
	return(p->pid);
}

void switch_mm(struct mm_struct *prev, struct mm_struct *next, 
	       struct task_struct *tsk)
{
	unsigned cpu = smp_processor_id();
	if (prev != next) 
		clear_bit(cpu, &prev->cpu_vm_mask);
	set_bit(cpu, &next->cpu_vm_mask);
}

void set_current(void *t)
{
	struct task_struct *task = t;

	cpu_tasks[task->thread_info->cpu] = ((struct cpu_task) 
		{ external_pid(task), task });
}

void *switch_to(void *prev, void *next, void *last)
{
	return(CHOOSE_MODE(switch_to_tt(prev, next), 
			   switch_to_skas(prev, next)));
}

void interrupt_end(void)
{
	if(need_resched()) schedule();
	if(test_tsk_thread_flag(current, TIF_SIGPENDING)) do_signal(0);
}

void release_thread(struct task_struct *task)
{
	CHOOSE_MODE(release_thread_tt(task), release_thread_skas(task));
}
 
void exit_thread(void)
{
	CHOOSE_MODE(exit_thread_tt(), exit_thread_skas());
	unprotect_stack((unsigned long) current->thread_info);
}
 
void *get_current(void)
{
	return(current);
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long sp,
		unsigned long stack_top, struct task_struct * p, 
		struct pt_regs *regs)
{
	p->thread = (struct thread_struct) INIT_THREAD;
	p->thread.kernel_stack = 
		(unsigned long) p->thread_info + 2 * PAGE_SIZE;
	return(CHOOSE_MODE_PROC(copy_thread_tt, copy_thread_skas, nr, 
				clone_flags, sp, stack_top, p, regs));
}

void initial_thread_cb(void (*proc)(void *), void *arg)
{
	int save_kmalloc_ok = kmalloc_ok;

	kmalloc_ok = 0;
	CHOOSE_MODE_PROC(initial_thread_cb_tt, initial_thread_cb_skas, proc, 
			 arg);
	kmalloc_ok = save_kmalloc_ok;
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
	idle_timer();

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
	CHOOSE_MODE(init_idle_tt(), init_idle_skas());
}

int page_size(void)
{
	return(PAGE_SIZE);
}

int page_mask(void)
{
	return(PAGE_MASK);
}

void *um_virt_to_phys(struct task_struct *task, unsigned long addr, 
		      pte_t *pte_out)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	if(task->mm == NULL) 
		return(ERR_PTR(-EINVAL));
	pgd = pgd_offset(task->mm, addr);
	pmd = pmd_offset(pgd, addr);
	if(!pmd_present(*pmd)) 
		return(ERR_PTR(-EINVAL));
	pte = pte_offset_kernel(pmd, addr);
	if(!pte_present(*pte)) 
		return(ERR_PTR(-EINVAL));
	if(pte_out != NULL)
		*pte_out = *pte;
	return((void *) (pte_val(*pte) & PAGE_MASK) + (addr & ~PAGE_MASK));
}

char *current_cmd(void)
{
#if defined(CONFIG_SMP) || defined(CONFIG_HIGHMEM)
	return("(Unknown)");
#else
	void *addr = um_virt_to_phys(current, current->mm->arg_start, NULL);
	return IS_ERR(addr) ? "(Unknown)": __va((unsigned long) addr);
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

EXPORT_SYMBOL(enable_hlt);

void disable_hlt(void)
{
	panic("disable_hlt");
}

EXPORT_SYMBOL(disable_hlt);

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

void not_implemented(void)
{
	printk(KERN_DEBUG "Something isn't implemented in here\n");
}

EXPORT_SYMBOL(not_implemented);

int user_context(unsigned long sp)
{
	unsigned long stack;

	stack = sp & (PAGE_MASK << CONFIG_KERNEL_STACK_ORDER);
	stack += 2 * PAGE_SIZE;
	return(stack != current->thread.kernel_stack);
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

char *uml_strdup(char *string)
{
	char *new;

	new = kmalloc(strlen(string) + 1, GFP_KERNEL);
	if(new == NULL) return(NULL);
	strcpy(new, string);
	return(new);
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

int smp_sigio_handler(void)
{
#ifdef CONFIG_SMP
	int cpu = current->thread_info->cpu;
	IPI_handler(cpu);
	if(cpu != 0)
		return(1);
#endif
	return(0);
}

int um_in_interrupt(void)
{
	return(in_interrupt());
}

int cpu(void)
{
	return(current->thread_info->cpu);
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
