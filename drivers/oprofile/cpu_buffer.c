/**
 * @file cpu_buffer.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 *
 * Each CPU has a local buffer that stores PC value/event
 * pairs. We also log context switches when we notice them.
 * Eventually each CPU's buffer is processed into the global
 * event buffer by sync_buffer().
 *
 * We use a local buffer for two reasons: an NMI or similar
 * interrupt cannot synchronise, and high sampling rates
 * would lead to catastrophic global synchronisation if
 * a global buffer was used.
 */

#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/errno.h>
 
#include "cpu_buffer.h"
#include "buffer_sync.h"
#include "oprof.h"

struct oprofile_cpu_buffer cpu_buffer[NR_CPUS] __cacheline_aligned;

static void wq_sync_buffer(void *);
static void timer_ping(unsigned long data);
#define DEFAULT_TIMER_EXPIRE (HZ / 2)
int timers_enabled;

static void __free_cpu_buffers(int num)
{
	int i;
 
	for_each_online_cpu(i) {
		if (cpu_buffer[i].buffer)
			vfree(cpu_buffer[i].buffer);
	}
}
 
 
int alloc_cpu_buffers(void)
{
	int i;
 
	unsigned long buffer_size = fs_cpu_buffer_size;
 
	for_each_online_cpu(i) {
		struct oprofile_cpu_buffer * b = &cpu_buffer[i];
 
		b->buffer = vmalloc(sizeof(struct op_sample) * buffer_size);
		if (!b->buffer)
			goto fail;
 
		b->last_task = NULL;
		b->last_is_kernel = -1;
		b->buffer_size = buffer_size;
		b->tail_pos = 0;
		b->head_pos = 0;
		b->sample_received = 0;
		b->sample_lost_overflow = 0;
		b->cpu = i;
		init_timer(&b->timer);
		b->timer.function = timer_ping;
		b->timer.data = i;
		b->timer.expires = jiffies + DEFAULT_TIMER_EXPIRE;
		INIT_WORK(&b->work, wq_sync_buffer, b);
	}
	return 0;

fail:
	__free_cpu_buffers(i);
	return -ENOMEM;
}
 

void free_cpu_buffers(void)
{
	__free_cpu_buffers(NR_CPUS);
}


void start_cpu_timers(void)
{
	int i;

	timers_enabled = 1;

	for_each_online_cpu(i) {
		struct oprofile_cpu_buffer * b = &cpu_buffer[i];

		add_timer_on(&b->timer, i);
	}
}


void end_cpu_timers(void)
{
	int i;

	timers_enabled = 0;

	for_each_online_cpu(i) {
		struct oprofile_cpu_buffer * b = &cpu_buffer[i];

		del_timer_sync(&b->timer);
	}

	flush_scheduled_work();
}


/* compute number of available slots in cpu_buffer queue */
static unsigned long nr_available_slots(struct oprofile_cpu_buffer const * b)
{
	unsigned long head = b->head_pos;
	unsigned long tail = b->tail_pos;

	if (tail > head)
		return (tail - head) - 1;

	return tail + (b->buffer_size - head) - 1;
}


static void increment_head(struct oprofile_cpu_buffer * b)
{
	unsigned long new_head = b->head_pos + 1;

	/* Ensure anything written to the slot before we
	 * increment is visible */
	wmb();

	if (new_head < (b->buffer_size))
		b->head_pos = new_head;
	else
		b->head_pos = 0;
}


/* This must be safe from any context. It's safe writing here
 * because of the head/tail separation of the writer and reader
 * of the CPU buffer.
 *
 * is_kernel is needed because on some architectures you cannot
 * tell if you are in kernel or user space simply by looking at
 * eip. We tag this in the buffer by generating kernel enter/exit
 * events whenever is_kernel changes
 */
void oprofile_add_sample(unsigned long eip, unsigned int is_kernel, 
	unsigned long event, int cpu)
{
	struct oprofile_cpu_buffer * cpu_buf = &cpu_buffer[cpu];
	struct task_struct * task;

	is_kernel = !!is_kernel;

	cpu_buf->sample_received++;
 

	if (nr_available_slots(cpu_buf) < 3) {
		cpu_buf->sample_lost_overflow++;
		return;
	}

	task = current;

	/* notice a switch from user->kernel or vice versa */
	if (cpu_buf->last_is_kernel != is_kernel) {
		cpu_buf->last_is_kernel = is_kernel;
		cpu_buf->buffer[cpu_buf->head_pos].eip = ~0UL;
		cpu_buf->buffer[cpu_buf->head_pos].event = is_kernel;
		increment_head(cpu_buf);
	}

	/* notice a task switch */
	if (cpu_buf->last_task != task) {
		cpu_buf->last_task = task;
		cpu_buf->buffer[cpu_buf->head_pos].eip = ~0UL;
		cpu_buf->buffer[cpu_buf->head_pos].event = (unsigned long)task;
		increment_head(cpu_buf);
	}
 
	cpu_buf->buffer[cpu_buf->head_pos].eip = eip;
	cpu_buf->buffer[cpu_buf->head_pos].event = event;
	increment_head(cpu_buf);
}


/* Resets the cpu buffer to a sane state. */
void cpu_buffer_reset(struct oprofile_cpu_buffer * cpu_buf)
{
	/* reset these to invalid values; the next sample
	 * collected will populate the buffer with proper
	 * values to initialize the buffer
	 */
	cpu_buf->last_is_kernel = -1;
	cpu_buf->last_task = NULL;
}


/* FIXME: not guaranteed to be on our CPU */
static void wq_sync_buffer(void * data)
{
	struct oprofile_cpu_buffer * b = (struct oprofile_cpu_buffer *)data;
	if (b->cpu != smp_processor_id()) {
		printk("WQ on CPU%d, prefer CPU%d\n",
		       smp_processor_id(), b->cpu);
	}
	sync_buffer(b->cpu);

	/* don't re-add the timer if we're shutting down */
	if (timers_enabled) {
		del_timer_sync(&b->timer);
		add_timer_on(&b->timer, b->cpu);
	}
}


/* This serves to avoid cpu buffer overflow, and makes sure
 * the task mortuary progresses
 */
static void timer_ping(unsigned long data)
{
	struct oprofile_cpu_buffer * b = &cpu_buffer[data];
	if (b->cpu != smp_processor_id()) {
		printk("Timer on CPU%d, prefer CPU%d\n",
		       smp_processor_id(), b->cpu);
	}
	schedule_work(&b->work);
	/* work will re-enable our timer */
}
