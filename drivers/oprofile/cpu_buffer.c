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

static unsigned long buffer_size;
 
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
 
	buffer_size = fs_cpu_buffer_size;
 
	for (i=0; i < NR_CPUS; ++i) {
		struct oprofile_cpu_buffer * b = &cpu_buffer[i];
 
		if (!cpu_possible(i)) 
			continue;
 
		b->buffer = vmalloc(sizeof(struct op_sample) * buffer_size);
		if (!b->buffer)
			goto fail;
 
		spin_lock_init(&b->int_lock);
		b->pos = 0;
		b->last_task = 0;
		b->last_is_kernel = -1;
		b->sample_received = 0;
		b->sample_lost_locked = 0;
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

 
/* Note we can't use a semaphore here as this is supposed to
 * be safe from any context. Instead we trylock the CPU's int_lock.
 * int_lock is taken by the processing code in sync_cpu_buffers()
 * so we avoid disturbing that.
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
 
	if (!spin_trylock(&cpu_buf->int_lock)) {
		cpu_buf->sample_lost_locked++;
		return;
	}

	if (cpu_buf->pos > buffer_size - 2) {
		cpu_buf->sample_lost_overflow++;
		goto out;
	}

	task = current;

	/* notice a switch from user->kernel or vice versa */
	if (cpu_buf->last_is_kernel != is_kernel) {
		cpu_buf->last_is_kernel = is_kernel;
		cpu_buf->buffer[cpu_buf->pos].eip = ~0UL;
		cpu_buf->buffer[cpu_buf->pos].event = is_kernel;
		cpu_buf->pos++;
	}

	/* notice a task switch */
	if (cpu_buf->last_task != task) {
		cpu_buf->last_task = task;
		if (!(task->flags & PF_EXITING)) {
			cpu_buf->buffer[cpu_buf->pos].eip = ~0UL;
			cpu_buf->buffer[cpu_buf->pos].event = (unsigned long)task;
			cpu_buf->pos++;
		}
	}
 
	/* If the task is exiting it's not safe to take a sample
	 * as the task_struct is about to be freed. We can't just
	 * notify at release_task() time because of CLONE_DETACHED
	 * tasks that release_task() themselves.
	 */
	if (task->flags & PF_EXITING) {
		cpu_buf->sample_lost_task_exit++;
		goto out;
	}
 
	cpu_buf->buffer[cpu_buf->pos].eip = eip;
	cpu_buf->buffer[cpu_buf->pos].event = event;
	cpu_buf->pos++;
out:
	spin_unlock(&cpu_buf->int_lock);
}

/* resets the cpu buffer to a sane state - should be called with 
 * cpu_buf->int_lock held
 */
void cpu_buffer_reset(struct oprofile_cpu_buffer *cpu_buf)
{
	cpu_buf->pos = 0;

	/* reset these to invalid values; the next sample
	 * collected will populate the buffer with proper
	 * values to initialize the buffer
	 */
	cpu_buf->last_is_kernel = -1;
	cpu_buf->last_task = 0;
}

