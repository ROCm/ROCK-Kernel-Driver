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
 * event buffer by sync_cpu_buffers().
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
#include "oprof.h"

struct oprofile_cpu_buffer cpu_buffer[NR_CPUS] __cacheline_aligned;

static void __free_cpu_buffers(int num)
{
	int i;
 
	for (i=0; i < num; ++i) {
		struct oprofile_cpu_buffer * b = &cpu_buffer[i];
 
		if (!cpu_possible(i)) 
			continue;
 
		vfree(b->buffer);
	}
}
 
 
int alloc_cpu_buffers(void)
{
	int i;
 
	unsigned long buffer_size = fs_cpu_buffer_size;
 
	for (i=0; i < NR_CPUS; ++i) {
		struct oprofile_cpu_buffer * b = &cpu_buffer[i];
 
		if (!cpu_possible(i)) 
			continue;
 
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
		b->sample_lost_task_exit = 0;
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
		if (!(task->flags & PF_EXITING)) {
			cpu_buf->buffer[cpu_buf->head_pos].eip = ~0UL;
			cpu_buf->buffer[cpu_buf->head_pos].event = (unsigned long)task;
			increment_head(cpu_buf);
		}
	}
 
	/* If the task is exiting it's not safe to take a sample
	 * as the task_struct is about to be freed. We can't just
	 * notify at release_task() time because of CLONE_DETACHED
	 * tasks that release_task() themselves.
	 */
	if (task->flags & PF_EXITING) {
		cpu_buf->sample_lost_task_exit++;
		return;
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
