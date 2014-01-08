/*
 * Code for supporting irq vector tracepoints.
 *
 * Copyright (C) 2013 Seiji Aguchi <seiji.aguchi@hds.com>
 *
 */
#include <asm/desc.h>
#include <linux/atomic.h>

atomic_t trace_trap_table_ctr = ATOMIC_INIT(0);

static int trace_trap_table_refcount;
static DEFINE_MUTEX(trap_table_mutex);

static void set_trace_trap_table_ctr(int val)
{
	atomic_set(&trace_trap_table_ctr, val);
	/* Ensure the trace_trap_table_ctr is set before sending IPI */
	wmb();
}

static void switch_trap_table(void *arg)
{
	unsigned long flags;

	local_irq_save(flags);
	load_current_trap_table();
	local_irq_restore(flags);
}

void trace_irq_vector_regfunc(void)
{
	mutex_lock(&trap_table_mutex);
	if (!trace_trap_table_refcount) {
		set_trace_trap_table_ctr(1);
		smp_call_function(switch_trap_table, NULL, 0);
		switch_trap_table(NULL);
	}
	trace_trap_table_refcount++;
	mutex_unlock(&trap_table_mutex);
}

void trace_irq_vector_unregfunc(void)
{
	mutex_lock(&trap_table_mutex);
	trace_trap_table_refcount--;
	if (!trace_trap_table_refcount) {
		set_trace_trap_table_ctr(0);
		smp_call_function(switch_trap_table, NULL, 0);
		switch_trap_table(NULL);
	}
	mutex_unlock(&trap_table_mutex);
}
