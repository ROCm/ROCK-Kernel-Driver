/* smp.c: Sparc64 SMP support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>

#include <asm/head.h>
#include <asm/ptrace.h>
#include <asm/atomic.h>

#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>
#include <asm/uaccess.h>
#include <asm/timer.h>
#include <asm/starfire.h>

#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>

extern int linux_num_cpus;
extern void calibrate_delay(void);
extern unsigned prom_cpu_nodes[];

struct cpuinfo_sparc cpu_data[NR_CPUS]  __attribute__ ((aligned (64)));

volatile int __cpu_number_map[NR_CPUS]  __attribute__ ((aligned (64)));
volatile int __cpu_logical_map[NR_CPUS] __attribute__ ((aligned (64)));

/* Please don't make this stuff initdata!!!  --DaveM */
static unsigned char boot_cpu_id = 0;
static int smp_activated = 0;

/* Kernel spinlock */
spinlock_t kernel_flag = SPIN_LOCK_UNLOCKED;

volatile int smp_processors_ready = 0;
unsigned long cpu_present_map = 0;
int smp_num_cpus = 1;
int smp_threads_ready = 0;

void __init smp_setup(char *str, int *ints)
{
	/* XXX implement me XXX */
}

int smp_info(char *buf)
{
	int len = 7, i;
	
	strcpy(buf, "State:\n");
	for (i = 0; i < NR_CPUS; i++)
		if(cpu_present_map & (1UL << i))
			len += sprintf(buf + len,
					"CPU%d:\t\tonline\n", i);
	return len;
}

int smp_bogo(char *buf)
{
	int len = 0, i;
	
	for (i = 0; i < NR_CPUS; i++)
		if(cpu_present_map & (1UL << i))
			len += sprintf(buf + len,
				       "Cpu%dBogo\t: %lu.%02lu\n",
				       i, cpu_data[i].udelay_val / (500000/HZ),
				       (cpu_data[i].udelay_val / (5000/HZ)) % 100);
	return len;
}

void __init smp_store_cpu_info(int id)
{
	int i;

	/* multiplier and counter set by
	   smp_setup_percpu_timer()  */
	cpu_data[id].udelay_val			= loops_per_jiffy;

	cpu_data[id].pgcache_size		= 0;
	cpu_data[id].pte_cache[0]		= NULL;
	cpu_data[id].pte_cache[1]		= NULL;
	cpu_data[id].pgdcache_size		= 0;
	cpu_data[id].pgd_cache			= NULL;
	cpu_data[id].idle_volume		= 1;

	for(i = 0; i < 16; i++)
		cpu_data[id].irq_worklists[i] = 0;
}

void __init smp_commence(void)
{
}

static void smp_setup_percpu_timer(void);
static void smp_tune_scheduling(void);

static volatile unsigned long callin_flag = 0;

extern void inherit_locked_prom_mappings(int save_p);
extern void cpu_probe(void);

void __init smp_callin(void)
{
	int cpuid = hard_smp_processor_id();
	unsigned long pstate;

	inherit_locked_prom_mappings(0);

	__flush_cache_all();
	__flush_tlb_all();

	cpu_probe();

	/* Guarentee that the following sequences execute
	 * uninterrupted.
	 */
	__asm__ __volatile__("rdpr	%%pstate, %0\n\t"
			     "wrpr	%0, %1, %%pstate"
			     : "=r" (pstate)
			     : "i" (PSTATE_IE));

	/* Set things up so user can access tick register for profiling
	 * purposes.  Also workaround BB_ERRATA_1 by doing a dummy
	 * read back of %tick after writing it.
	 */
	__asm__ __volatile__("
	sethi	%%hi(0x80000000), %%g1
	ba,pt	%%xcc, 1f
	 sllx	%%g1, 32, %%g1
	.align	64
1:	rd	%%tick, %%g2
	add	%%g2, 6, %%g2
	andn	%%g2, %%g1, %%g2
	wrpr	%%g2, 0, %%tick
	rdpr	%%tick, %%g0"
	: /* no outputs */
	: /* no inputs */
	: "g1", "g2");

	/* Restore PSTATE_IE. */
	__asm__ __volatile__("wrpr	%0, 0x0, %%pstate"
			     : /* no outputs */
			     : "r" (pstate));

	smp_setup_percpu_timer();

	__sti();

	calibrate_delay();
	smp_store_cpu_info(cpuid);
	callin_flag = 1;
	__asm__ __volatile__("membar #Sync\n\t"
			     "flush  %%g6" : : : "memory");

	/* Clear this or we will die instantly when we
	 * schedule back to this idler...
	 */
	current->thread.flags &= ~(SPARC_FLAG_NEWCHILD);

	/* Attach to the address space of init_task. */
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;

	while(!smp_processors_ready)
		membar("#LoadLoad");
}

extern int cpu_idle(void);
extern void init_IRQ(void);

void initialize_secondary(void)
{
}

int start_secondary(void *unused)
{
	trap_init();
	init_IRQ();
	smp_callin();
	return cpu_idle();
}

void cpu_panic(void)
{
	printk("CPU[%d]: Returns from cpu_idle!\n", smp_processor_id());
	panic("SMP bolixed\n");
}

extern struct prom_cpuinfo linux_cpus[64];

extern unsigned long sparc64_cpu_startup;

/* The OBP cpu startup callback truncates the 3rd arg cookie to
 * 32-bits (I think) so to be safe we have it read the pointer
 * contained here so we work on >4GB machines. -DaveM
 */
static struct task_struct *cpu_new_task = NULL;

void __init smp_boot_cpus(void)
{
	int cpucount = 0, i;

	printk("Entering UltraSMPenguin Mode...\n");
	__sti();
	smp_store_cpu_info(boot_cpu_id);
	smp_tune_scheduling();
	init_idle();

	if(linux_num_cpus == 1)
		return;

	for(i = 0; i < NR_CPUS; i++) {
		if(i == boot_cpu_id)
			continue;

		if(cpu_present_map & (1UL << i)) {
			unsigned long entry = (unsigned long)(&sparc64_cpu_startup);
			unsigned long cookie = (unsigned long)(&cpu_new_task);
			struct task_struct *p;
			int timeout;
			int no;

			prom_printf("Starting CPU %d... ", i);
			kernel_thread(start_secondary, NULL, CLONE_PID);
			cpucount++;

			p = init_task.prev_task;
			init_tasks[cpucount] = p;

			p->processor = i;
			p->has_cpu = 1; /* we schedule the first task manually */

			del_from_runqueue(p);
			unhash_process(p);

			callin_flag = 0;
			for (no = 0; no < linux_num_cpus; no++)
				if (linux_cpus[no].mid == i)
					break;
			cpu_new_task = p;
			prom_startcpu(linux_cpus[no].prom_node,
				      entry, cookie);
			for(timeout = 0; timeout < 5000000; timeout++) {
				if(callin_flag)
					break;
				udelay(100);
			}
			if(callin_flag) {
				__cpu_number_map[i] = cpucount;
				__cpu_logical_map[cpucount] = i;
				prom_cpu_nodes[i] = linux_cpus[no].prom_node;
				prom_printf("OK\n");
			} else {
				cpucount--;
				printk("Processor %d is stuck.\n", i);
				prom_printf("FAILED\n");
			}
		}
		if(!callin_flag) {
			cpu_present_map &= ~(1UL << i);
			__cpu_number_map[i] = -1;
		}
	}
	cpu_new_task = NULL;
	if(cpucount == 0) {
		printk("Error: only one processor found.\n");
		cpu_present_map = (1UL << smp_processor_id());
	} else {
		unsigned long bogosum = 0;

		for(i = 0; i < NR_CPUS; i++) {
			if(cpu_present_map & (1UL << i))
				bogosum += cpu_data[i].udelay_val;
		}
		printk("Total of %d processors activated (%lu.%02lu BogoMIPS).\n",
		       cpucount + 1,
		       (bogosum + 2500)/500000,
		       ((bogosum + 2500)/5000)%100);
		smp_activated = 1;
		smp_num_cpus = cpucount + 1;
	}
	smp_processors_ready = 1;
	membar("#StoreStore | #StoreLoad");
}

/* #define XCALL_DEBUG */

static inline void xcall_deliver(u64 data0, u64 data1, u64 data2, u64 pstate, unsigned long cpu)
{
	u64 result, target;
	int stuck, tmp;

	if (this_is_starfire) {
		/* map to real upaid */
		cpu = (((cpu & 0x3c) << 1) |
			((cpu & 0x40) >> 4) |
			(cpu & 0x3));
	}

	target = (cpu << 14) | 0x70;
#ifdef XCALL_DEBUG
	printk("CPU[%d]: xcall(data[%016lx:%016lx:%016lx],tgt[%016lx])\n",
	       smp_processor_id(), data0, data1, data2, target);
#endif
again:
	/* Ok, this is the real Spitfire Errata #54.
	 * One must read back from a UDB internal register
	 * after writes to the UDB interrupt dispatch, but
	 * before the membar Sync for that write.
	 * So we use the high UDB control register (ASI 0x7f,
	 * ADDR 0x20) for the dummy read. -DaveM
	 */
	tmp = 0x40;
	__asm__ __volatile__("
	wrpr	%1, %2, %%pstate
	stxa	%4, [%0] %3
	stxa	%5, [%0+%8] %3
	add	%0, %8, %0
	stxa	%6, [%0+%8] %3
	membar	#Sync
	stxa	%%g0, [%7] %3
	membar	#Sync
	mov	0x20, %%g1
	ldxa	[%%g1] 0x7f, %%g0
	membar	#Sync"
	: "=r" (tmp)
	: "r" (pstate), "i" (PSTATE_IE), "i" (ASI_UDB_INTR_W),
	  "r" (data0), "r" (data1), "r" (data2), "r" (target), "r" (0x10), "0" (tmp)
       : "g1");

	/* NOTE: PSTATE_IE is still clear. */
	stuck = 100000;
	do {
		__asm__ __volatile__("ldxa [%%g0] %1, %0"
			: "=r" (result)
			: "i" (ASI_INTR_DISPATCH_STAT));
		if(result == 0) {
			__asm__ __volatile__("wrpr %0, 0x0, %%pstate"
					     : : "r" (pstate));
			return;
		}
		stuck -= 1;
		if(stuck == 0)
			break;
	} while(result & 0x1);
	__asm__ __volatile__("wrpr %0, 0x0, %%pstate"
			     : : "r" (pstate));
	if(stuck == 0) {
#ifdef XCALL_DEBUG
		printk("CPU[%d]: mondo stuckage result[%016lx]\n",
		       smp_processor_id(), result);
#endif
	} else {
#ifdef XCALL_DEBUG
		printk("CPU[%d]: Penguin %d NACK's master.\n", smp_processor_id(), cpu);
#endif
		udelay(2);
		goto again;
	}
}

void smp_cross_call(unsigned long *func, u32 ctx, u64 data1, u64 data2)
{
	if(smp_processors_ready) {
		unsigned long mask = (cpu_present_map & ~(1UL<<smp_processor_id()));
		u64 pstate, data0 = (((u64)ctx)<<32 | (((u64)func) & 0xffffffff));
		int i, ncpus = smp_num_cpus - 1;

		__asm__ __volatile__("rdpr %%pstate, %0" : "=r" (pstate));
		for(i = 0; i < NR_CPUS; i++) {
			if(mask & (1UL << i)) {
				xcall_deliver(data0, data1, data2, pstate, i);
				ncpus--;
			}
			if (!ncpus) break;
		}
		/* NOTE: Caller runs local copy on master. */
	}
}

struct call_data_struct {
	void (*func) (void *info);
	void *info;
	atomic_t finished;
	int wait;
};

extern unsigned long xcall_call_function;

int smp_call_function(void (*func)(void *info), void *info,
		      int nonatomic, int wait)
{
	struct call_data_struct data;
	int cpus = smp_num_cpus - 1;

	if (!cpus)
		return 0;

	data.func = func;
	data.info = info;
	atomic_set(&data.finished, 0);
	data.wait = wait;

	smp_cross_call(&xcall_call_function,
		       0, (u64) &data, 0);
	if (wait) {
		while (atomic_read(&data.finished) != cpus)
			barrier();
	}

	return 0;
}

void smp_call_function_client(struct call_data_struct *call_data)
{
	call_data->func(call_data->info);
	if (call_data->wait)
		atomic_inc(&call_data->finished);
}

extern unsigned long xcall_flush_tlb_page;
extern unsigned long xcall_flush_tlb_mm;
extern unsigned long xcall_flush_tlb_range;
extern unsigned long xcall_flush_tlb_all;
extern unsigned long xcall_tlbcachesync;
extern unsigned long xcall_flush_cache_all;
extern unsigned long xcall_report_regs;
extern unsigned long xcall_receive_signal;

void smp_receive_signal(int cpu)
{
	if(smp_processors_ready &&
	   (cpu_present_map & (1UL<<cpu)) != 0) {
		u64 pstate, data0 = (((u64)&xcall_receive_signal) & 0xffffffff);
		__asm__ __volatile__("rdpr %%pstate, %0" : "=r" (pstate));
		xcall_deliver(data0, 0, 0, pstate, cpu);
	}
}

void smp_report_regs(void)
{
	smp_cross_call(&xcall_report_regs, 0, 0, 0);
}

void smp_flush_cache_all(void)
{
	smp_cross_call(&xcall_flush_cache_all, 0, 0, 0);
	__flush_cache_all();
}

void smp_flush_tlb_all(void)
{
	smp_cross_call(&xcall_flush_tlb_all, 0, 0, 0);
	__flush_tlb_all();
}

/* We know that the window frames of the user have been flushed
 * to the stack before we get here because all callers of us
 * are flush_tlb_*() routines, and these run after flush_cache_*()
 * which performs the flushw.
 *
 * XXX I diked out the fancy flush avoidance code for the
 * XXX swapping cases for now until the new MM code stabilizes. -DaveM
 *
 * The SMP TLB coherency scheme we use works as follows:
 *
 * 1) mm->cpu_vm_mask is a bit mask of which cpus an address
 *    space has (potentially) executed on, this is the heuristic
 *    we use to avoid doing cross calls.
 *
 * 2) TLB context numbers are shared globally across all processors
 *    in the system, this allows us to play several games to avoid
 *    cross calls.
 *
 *    One invariant is that when a cpu switches to a process, and
 *    that processes tsk->active_mm->cpu_vm_mask does not have the
 *    current cpu's bit set, that tlb context is flushed locally.
 *
 *    If the address space is non-shared (ie. mm->count == 1) we avoid
 *    cross calls when we want to flush the currently running process's
 *    tlb state.  This is done by clearing all cpu bits except the current
 *    processor's in current->active_mm->cpu_vm_mask and performing the
 *    flush locally only.  This will force any subsequent cpus which run
 *    this task to flush the context from the local tlb if the process
 *    migrates to another cpu (again).
 *
 * 3) For shared address spaces (threads) and swapping we bite the
 *    bullet for most cases and perform the cross call.
 *
 *    The performance gain from "optimizing" away the cross call for threads is
 *    questionable (in theory the big win for threads is the massive sharing of
 *    address space state across processors).
 *
 *    For the swapping case the locking is difficult to get right, we'd have to
 *    enforce strict ordered access to mm->cpu_vm_mask via a spinlock for example.
 *    Then again one could argue that when you are swapping, the cost of a cross
 *    call won't even show up on the performance radar.  But in any case we do get
 *    rid of the cross-call when the task has a dead context or the task has only
 *    ever run on the local cpu.
 */
void smp_flush_tlb_mm(struct mm_struct *mm)
{
	if (CTX_VALID(mm->context)) {
		u32 ctx = CTX_HWBITS(mm->context);
		int cpu = smp_processor_id();

		if (mm == current->active_mm && atomic_read(&mm->mm_users) == 1) {
			/* See smp_flush_tlb_page for info about this. */
			mm->cpu_vm_mask = (1UL << cpu);
			goto local_flush_and_out;
		}

		smp_cross_call(&xcall_flush_tlb_mm, ctx, 0, 0);

	local_flush_and_out:
		__flush_tlb_mm(ctx, SECONDARY_CONTEXT);
	}
}

void smp_flush_tlb_range(struct mm_struct *mm, unsigned long start,
			 unsigned long end)
{
	if (CTX_VALID(mm->context)) {
		u32 ctx = CTX_HWBITS(mm->context);
		int cpu = smp_processor_id();

		start &= PAGE_MASK;
		end   &= PAGE_MASK;

		if (mm == current->active_mm && atomic_read(&mm->mm_users) == 1) {
			mm->cpu_vm_mask = (1UL << cpu);
			goto local_flush_and_out;
		}

		smp_cross_call(&xcall_flush_tlb_range, ctx, start, end);

	local_flush_and_out:
		__flush_tlb_range(ctx, start, SECONDARY_CONTEXT, end, PAGE_SIZE, (end-start));
	}
}

void smp_flush_tlb_page(struct mm_struct *mm, unsigned long page)
{
	if (CTX_VALID(mm->context)) {
		u32 ctx = CTX_HWBITS(mm->context);
		int cpu = smp_processor_id();

		page &= PAGE_MASK;
		if (mm == current->active_mm && atomic_read(&mm->mm_users) == 1) {
			/* By virtue of being the current address space, and
			 * having the only reference to it, the following operation
			 * is safe.
			 *
			 * It would not be a win to perform the xcall tlb flush in
			 * this case, because even if we switch back to one of the
			 * other processors in cpu_vm_mask it is almost certain that
			 * all TLB entries for this context will be replaced by the
			 * time that happens.
			 */
			mm->cpu_vm_mask = (1UL << cpu);
			goto local_flush_and_out;
		} else {
			/* By virtue of running under the mm->page_table_lock,
			 * and mmu_context.h:switch_mm doing the same, the following
			 * operation is safe.
			 */
			if (mm->cpu_vm_mask == (1UL << cpu))
				goto local_flush_and_out;
		}

		/* OK, we have to actually perform the cross call.  Most likely
		 * this is a cloned mm or kswapd is kicking out pages for a task
		 * which has run recently on another cpu.
		 */
		smp_cross_call(&xcall_flush_tlb_page, ctx, page, 0);

	local_flush_and_out:
		__flush_tlb_page(ctx, page, SECONDARY_CONTEXT);
	}
}

/* CPU capture. */
/* #define CAPTURE_DEBUG */
extern unsigned long xcall_capture;

static atomic_t smp_capture_depth = ATOMIC_INIT(0);
static atomic_t smp_capture_registry = ATOMIC_INIT(0);
static unsigned long penguins_are_doing_time = 0;

void smp_capture(void)
{
	if (smp_processors_ready) {
		int result = __atomic_add(1, &smp_capture_depth);

		membar("#StoreStore | #LoadStore");
		if(result == 1) {
			int ncpus = smp_num_cpus;

#ifdef CAPTURE_DEBUG
			printk("CPU[%d]: Sending penguins to jail...",
			       smp_processor_id());
#endif
			penguins_are_doing_time = 1;
			membar("#StoreStore | #LoadStore");
			atomic_inc(&smp_capture_registry);
			smp_cross_call(&xcall_capture, 0, 0, 0);
			while(atomic_read(&smp_capture_registry) != ncpus)
				membar("#LoadLoad");
#ifdef CAPTURE_DEBUG
			printk("done\n");
#endif
		}
	}
}

void smp_release(void)
{
	if(smp_processors_ready) {
		if(atomic_dec_and_test(&smp_capture_depth)) {
#ifdef CAPTURE_DEBUG
			printk("CPU[%d]: Giving pardon to imprisoned penguins\n",
			       smp_processor_id());
#endif
			penguins_are_doing_time = 0;
			membar("#StoreStore | #StoreLoad");
			atomic_dec(&smp_capture_registry);
		}
	}
}

/* Imprisoned penguins run with %pil == 15, but PSTATE_IE set, so they
 * can service tlb flush xcalls...
 */
extern void prom_world(int);
extern void save_alternate_globals(unsigned long *);
extern void restore_alternate_globals(unsigned long *);
void smp_penguin_jailcell(void)
{
	unsigned long global_save[24];

	__asm__ __volatile__("flushw");
	save_alternate_globals(global_save);
	prom_world(1);
	atomic_inc(&smp_capture_registry);
	membar("#StoreLoad | #StoreStore");
	while(penguins_are_doing_time)
		membar("#LoadLoad");
	restore_alternate_globals(global_save);
	atomic_dec(&smp_capture_registry);
	prom_world(0);
}

extern unsigned long xcall_promstop;

void smp_promstop_others(void)
{
	if (smp_processors_ready)
		smp_cross_call(&xcall_promstop, 0, 0, 0);
}

extern void sparc64_do_profile(unsigned long pc, unsigned long o7);

static unsigned long current_tick_offset;

#define prof_multiplier(__cpu)		cpu_data[(__cpu)].multiplier
#define prof_counter(__cpu)		cpu_data[(__cpu)].counter

void smp_percpu_timer_interrupt(struct pt_regs *regs)
{
	unsigned long compare, tick, pstate;
	int cpu = smp_processor_id();
	int user = user_mode(regs);

	/*
	 * Check for level 14 softint.
	 */
	if (!(get_softint() & (1UL << 0))) {
		extern void handler_irq(int, struct pt_regs *);

		handler_irq(14, regs);
		return;
	}

	clear_softint((1UL << 0));
	do {
		if (!user)
			sparc64_do_profile(regs->tpc, regs->u_regs[UREG_RETPC]);
		if (!--prof_counter(cpu)) {
			if (cpu == boot_cpu_id) {
				irq_enter(cpu, 0);

				kstat.irqs[cpu][0]++;
				timer_tick_interrupt(regs);

				irq_exit(cpu, 0);
			}

			update_process_times(user);

			prof_counter(cpu) = prof_multiplier(cpu);
		}

		/* Guarentee that the following sequences execute
		 * uninterrupted.
		 */
		__asm__ __volatile__("rdpr	%%pstate, %0\n\t"
				     "wrpr	%0, %1, %%pstate"
				     : "=r" (pstate)
				     : "i" (PSTATE_IE));

		/* Workaround for Spitfire Errata (#54 I think??), I discovered
		 * this via Sun BugID 4008234, mentioned in Solaris-2.5.1 patch
		 * number 103640.
		 *
		 * On Blackbird writes to %tick_cmpr can fail, the
		 * workaround seems to be to execute the wr instruction
		 * at the start of an I-cache line, and perform a dummy
		 * read back from %tick_cmpr right after writing to it. -DaveM
		 *
		 * Just to be anal we add a workaround for Spitfire
		 * Errata 50 by preventing pipeline bypasses on the
		 * final read of the %tick register into a compare
		 * instruction.  The Errata 50 description states
		 * that %tick is not prone to this bug, but I am not
		 * taking any chances.
		 */
		__asm__ __volatile__("rd	%%tick_cmpr, %0\n\t"
				     "ba,pt	%%xcc, 1f\n\t"
				     " add	%0, %2, %0\n\t"
				     ".align	64\n"
				  "1: wr	%0, 0x0, %%tick_cmpr\n\t"
				     "rd	%%tick_cmpr, %%g0\n\t"
				     "rd	%%tick, %1\n\t"
				     "mov	%1, %1"
				     : "=&r" (compare), "=r" (tick)
				     : "r" (current_tick_offset));

		/* Restore PSTATE_IE. */
		__asm__ __volatile__("wrpr	%0, 0x0, %%pstate"
				     : /* no outputs */
				     : "r" (pstate));
	} while (tick >= compare);
}

static void __init smp_setup_percpu_timer(void)
{
	int cpu = smp_processor_id();
	unsigned long pstate;

	prof_counter(cpu) = prof_multiplier(cpu) = 1;

	/* Guarentee that the following sequences execute
	 * uninterrupted.
	 */
	__asm__ __volatile__("rdpr	%%pstate, %0\n\t"
			     "wrpr	%0, %1, %%pstate"
			     : "=r" (pstate)
			     : "i" (PSTATE_IE));

	/* Workaround for Spitfire Errata (#54 I think??), I discovered
	 * this via Sun BugID 4008234, mentioned in Solaris-2.5.1 patch
	 * number 103640.
	 *
	 * On Blackbird writes to %tick_cmpr can fail, the
	 * workaround seems to be to execute the wr instruction
	 * at the start of an I-cache line, and perform a dummy
	 * read back from %tick_cmpr right after writing to it. -DaveM
	 */
	__asm__ __volatile__("
		rd	%%tick, %%g1
		ba,pt	%%xcc, 1f
		 add	%%g1, %0, %%g1
		.align	64
	1:	wr	%%g1, 0x0, %%tick_cmpr
		rd	%%tick_cmpr, %%g0"
	: /* no outputs */
	: "r" (current_tick_offset)
	: "g1");

	/* Restore PSTATE_IE. */
	__asm__ __volatile__("wrpr	%0, 0x0, %%pstate"
			     : /* no outputs */
			     : "r" (pstate));
}

void __init smp_tick_init(void)
{
	int i;
	
	boot_cpu_id = hard_smp_processor_id();
	current_tick_offset = timer_tick_offset;
	cpu_present_map = 0;
	for(i = 0; i < linux_num_cpus; i++)
		cpu_present_map |= (1UL << linux_cpus[i].mid);
	for(i = 0; i < NR_CPUS; i++) {
		__cpu_number_map[i] = -1;
		__cpu_logical_map[i] = -1;
	}
	__cpu_number_map[boot_cpu_id] = 0;
	prom_cpu_nodes[boot_cpu_id] = linux_cpus[0].prom_node;
	__cpu_logical_map[0] = boot_cpu_id;
	current->processor = boot_cpu_id;
	prof_counter(boot_cpu_id) = prof_multiplier(boot_cpu_id) = 1;
}

static inline unsigned long find_flush_base(unsigned long size)
{
	struct page *p = mem_map;
	unsigned long found, base;

	size = PAGE_ALIGN(size);
	found = size;
	base = (unsigned long) page_address(p);
	while(found != 0) {
		/* Failure. */
		if(p >= (mem_map + max_mapnr))
			return 0UL;
		if(PageReserved(p)) {
			found = size;
			base = (unsigned long) page_address(p);
		} else {
			found -= PAGE_SIZE;
		}
		p++;
	}
	return base;
}

cycles_t cacheflush_time;

static void __init smp_tune_scheduling (void)
{
	unsigned long orig_flush_base, flush_base, flags, *p;
	unsigned int ecache_size, order;
	cycles_t tick1, tick2, raw;

	/* Approximate heuristic for SMP scheduling.  It is an
	 * estimation of the time it takes to flush the L2 cache
	 * on the local processor.
	 *
	 * The ia32 chooses to use the L1 cache flush time instead,
	 * and I consider this complete nonsense.  The Ultra can service
	 * a miss to the L1 with a hit to the L2 in 7 or 8 cycles, and
	 * L2 misses are what create extra bus traffic (ie. the "cost"
	 * of moving a process from one cpu to another).
	 */
	printk("SMP: Calibrating ecache flush... ");
	ecache_size = prom_getintdefault(linux_cpus[0].prom_node,
					 "ecache-size", (512 * 1024));
	if (ecache_size > (4 * 1024 * 1024))
		ecache_size = (4 * 1024 * 1024);
	orig_flush_base = flush_base =
		__get_free_pages(GFP_KERNEL, order = get_order(ecache_size));

	if (flush_base != 0UL) {
		__save_and_cli(flags);

		/* Scan twice the size once just to get the TLB entries
		 * loaded and make sure the second scan measures pure misses.
		 */
		for (p = (unsigned long *)flush_base;
		     ((unsigned long)p) < (flush_base + (ecache_size<<1));
		     p += (64 / sizeof(unsigned long)))
			*((volatile unsigned long *)p);

		/* Now the real measurement. */
		__asm__ __volatile__("
		b,pt	%%xcc, 1f
		 rd	%%tick, %0

		.align	64
1:		ldx	[%2 + 0x000], %%g1
		ldx	[%2 + 0x040], %%g2
		ldx	[%2 + 0x080], %%g3
		ldx	[%2 + 0x0c0], %%g5
		add	%2, 0x100, %2
		cmp	%2, %4
		bne,pt	%%xcc, 1b
		 nop
	
		rd	%%tick, %1"
		: "=&r" (tick1), "=&r" (tick2), "=&r" (flush_base)
		: "2" (flush_base), "r" (flush_base + ecache_size)
		: "g1", "g2", "g3", "g5");

		__restore_flags(flags);

		raw = (tick2 - tick1);

		/* Dampen it a little, considering two processes
		 * sharing the cache and fitting.
		 */
		cacheflush_time = (raw - (raw >> 2));

		free_pages(orig_flush_base, order);
	} else {
		cacheflush_time = ((ecache_size << 2) +
				   (ecache_size << 1));
	}

	printk("Using heuristic of %d cycles.\n",
	       (int) cacheflush_time);
}

/* /proc/profile writes can call this, don't __init it please. */
int setup_profiling_timer(unsigned int multiplier)
{
	unsigned long flags;
	int i;

	if((!multiplier) || (timer_tick_offset / multiplier) < 1000)
		return -EINVAL;

	save_and_cli(flags);
	for(i = 0; i < NR_CPUS; i++) {
		if(cpu_present_map & (1UL << i))
			prof_multiplier(i) = multiplier;
	}
	current_tick_offset = (timer_tick_offset / multiplier);
	restore_flags(flags);

	return 0;
}
