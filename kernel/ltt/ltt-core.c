/*
 * ltt-core.c
 *
 * (C) Copyright, 1999, 2000, 2001, 2002, 2003, 2004 -
 *              Karim Yaghmour (karim@opersys.com)
 *
 * Contains the kernel code for the Linux Trace Toolkit.
 *
 * Author:
 *	Karim Yaghmour (karim@opersys.com)
 *
 * Changelog:
 *	24/01/04, Revamped tracer to rely entirely on relayfs, no sys_trace.
 *		Renamed all relevant files and functions from trace* to ltt*.
 *	14/03/03, Modified to use relayfs (zanussi@us.ibm.com)
 *	15/10/02, Changed tracer from device to kernel subsystem and added
 *		custom trace system call (sys_trace).
 *	01/10/02, Coding style change to fit with kernel coding style.
 *	16/02/02, Added Tom Zanussi's implementation of K42's lockless logging.
 *		K42 tracing guru Robert Wisniewski participated in the
 *		discussions surrounding this implementation. A big thanks to
 *		the IBM folks.
 *	03/12/01, Added user event support.
 *	05/01/01, Modified PPC bit manipulation functions for x86
 *		compatibility (andy_lowe@mvista.com).
 *	15/11/00, Finally fixed memory allocation and remapping method. Now
 *		using BTTV-driver-inspired code.
 *	13/03/00, Modified tracer so that the daemon mmaps the tracer's buffers
 *		in it's address space rather than use "read".
 *	26/01/00, Added support for standardized buffer sizes and extensibility
 *		of events.
 *	01/10/99, Modified tracer in order to used double-buffering.
 *	28/09/99, Adding tracer configuration support.
 *	09/09/99, Changing the format of an event record in order to reduce the
 *		size of the traces.
 *	04/03/99, Initial typing.
 *
 * Note:
 *	The sizes of the variables used to store the details of an event are
 *	planned for a system who gets at least one clock tick every 10 
 *	milli-seconds. There has to be at least one event every 2^32-1
 *	microseconds, otherwise the size of the variable holding the time
 *	doesn't work anymore.
 */

#include <linux/init.h>
#include <linux/ltt.h>
#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>

#include <asm/io.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>
#include <asm/relay.h>
#include <asm/ltt.h>

/* Tracer configuration */
static int		num_cpus;

/* System call tracing behavior */
static int		use_syscall_eip_bounds;
static int		lower_eip_bound_set;
static int		upper_eip_bound_set;
static void*		lower_eip_bound;
static void*		upper_eip_bound;
static int		syscall_eip_depth_set;
static int		syscall_eip_depth;

static int		fetch_syscall_eip_use_depth;
static int		fetch_syscall_eip_use_bounds;
static void*		syscall_lower_eip_bound;
static void*		syscall_upper_eip_bound;

/* Data buffer management */
static struct trace_struct	current_traces[NR_TRACES];
static u32			start_reserve = TRACER_FIRST_EVENT_SIZE;
static u32			end_reserve = TRACER_LAST_EVENT_SIZE;
static u32			trace_start_reserve = TRACER_START_TRACE_EVENT_SIZE;
static struct rchan_callbacks	ltt_callbacks;		/* relayfs callbacks */
static struct ltt_arch_info	ltt_arch_info;
static struct buf_control_info	shared_buf_ctl;
static char *			user_event_data = NULL;

/* Timer management */
static struct timer_list	heartbeat_timer;
static struct timer_list	percpu_timer[NR_CPUS] __cacheline_aligned;

/* /proc variables */
static struct proc_dir_entry *	ltt_proc_root_entry; /* proc/ltt */
static int			tmp_rchan_handles[NR_CPUS];
static char			relayfs_path[PATH_MAX];	/* path to attribs */
static char			relay_file_name[PATH_MAX]; /* scratch area */
static int			control_channel; /* LTT control channel */

/* Forward declarations */
static struct proc_dir_entry *create_handle_proc_dir(unsigned trace_handle);
static void remove_handle_proc_dir(struct proc_dir_entry *handle_dir,
				   unsigned trace_handle);

/* Size of statically defined events */
int event_struct_size[TRACE_EV_MAX + 1] =
{
	sizeof(trace_start),
	sizeof(trace_syscall_entry),
	0,				/* TRACE_SYSCALL_EXIT */
	sizeof(trace_trap_entry),
	0,				/* TRACE_TRAP_EXIT */
	sizeof(trace_irq_entry),
	0,				/* TRACE_IRQ_EXIT */
	sizeof(trace_schedchange),
	0,				/* TRACE_KERNEL_TIMER */
	sizeof(trace_soft_irq),
	sizeof(trace_process),
	sizeof(trace_file_system),
	sizeof(trace_timer),
	sizeof(trace_memory),
	sizeof(trace_socket),
	sizeof(trace_ipc),
	sizeof(trace_network),
	sizeof(trace_buffer_start),
	sizeof(trace_buffer_end),
	sizeof(trace_new_event),
	sizeof(trace_custom),
	sizeof(trace_change_mask),
	0				/* TRACE_HEARTBEAT */
};

/* Custom event description */
struct custom_event_desc {
	trace_new_event event;

	pid_t owner_pid;

	struct custom_event_desc *next;
	struct custom_event_desc *prev;
};

/* Custom event management */
static int			next_event_id = TRACE_EV_MAX + 1;
static rwlock_t			custom_list_lock = RW_LOCK_UNLOCKED;
static rwlock_t			trace_handle_table_lock = RW_LOCK_UNLOCKED;
static struct custom_event_desc	custom_events_head;
static struct custom_event_desc	*custom_events = NULL;

/* Handle management */
struct trace_handle_struct{
	struct task_struct	*owner;
};
static struct trace_handle_struct trace_handle_table[TRACE_MAX_HANDLES];

/**
 *	set_waiting_for_cpu_async: - Utility function for setting wait flags
 *	@cpu_id: which CPU to set flag on
 *	@bit: which bit to set
 */
static inline void set_waiting_for_cpu_async(unsigned int trace_handle, u8 cpu_id, int bit)
{
	atomic_set(&waiting_for_cpu_async(trace_handle, cpu_id), 
		   atomic_read(&waiting_for_cpu_async(trace_handle, cpu_id)) | bit);
}

/**
 *	clear_waiting_for_cpu_async: - Utility function for clearing wait flags
 *	@cpu_id: which CPU to clear flag on
 *	@bit: which bit to clear
 */
static inline void clear_waiting_for_cpu_async(unsigned int trace_handle, u8 cpu_id, int bit)
{
	atomic_set(&waiting_for_cpu_async(trace_handle, cpu_id), 
		   atomic_read(&waiting_for_cpu_async(trace_handle, cpu_id)) & ~bit);
}


/**
 *	init_trace: - Initialize a trace/flight recorder instance
 *	@trace_struct: trace/flight recorder struct
 *
 *	Initialize a trace instance to default values.
 */
static void init_trace(struct trace_struct *trace)
{
	trace->trace_handle = 0;
	
	trace->active = NULL;
	trace->paused = 0;
	trace->flight_recorder = 1;
	trace->daemon_task_struct = NULL;
	trace->trace_start_data = NULL;
	
	trace->tracer_started = 0;
	trace->tracer_stopping = 0;
	trace->proc_dir_entry = NULL;
	trace->traced_events = 0;
	trace->log_event_details_mask = 0;
	trace->log_cpuid = 0;
	trace->tracing_pid = 0;
	trace->tracing_pgrp = 0;
	trace->tracing_gid = 0;
	trace->tracing_uid = 0;
	trace->traced_pid = 0;
	trace->traced_pgrp = 0;
	trace->traced_gid = 0;
	trace->traced_uid = 0;
	trace->use_locking = 1;
	trace->n_buffers = 0;
	trace->buf_size = 0;
	trace->using_tsc = 0;

	trace->buffer_switches_pending = 0;
}

/*
 * Trace heartbeat
 */

/**
 *	write_heartbeat_event: - Timer function generating hearbeat event.
 *	@data: unused
 *
 *	Guarantees at least 1 event is logged before low word of TSC wraps.
 */
static void write_heartbeat_event(unsigned long data)
{
	unsigned long int flags;
	int i, j;
	
	local_irq_save(flags);
	for (i = 0; i < NR_TRACES; i++) {
		if (current_traces[i].active && current_traces[i].using_tsc) {
			for (j =  0; j < num_cpus; j++)
				set_waiting_for_cpu_async(i, j, LTT_TRACE_HEARTBEAT);
		}
	}
	local_irq_restore(flags);

	del_timer(&heartbeat_timer);
	heartbeat_timer.expires = jiffies + 0xffffffffUL/loops_per_jiffy - 1;
	add_timer(&heartbeat_timer);
}

/**
 *	syscall_active: - If any active trace is logging syscalls, return 1
 *	@syscall_type: either SYSCALL_ENTRY or SYSCALL_EXIT
 *
 *	Returns 1 if any channel is tracing syscalls, 0 otherwise
 *
 *	Needed for setting/clearing the global syscall...active variables
 *	in order to reflect the needs of all traces.
 */
int syscall_active(int syscall_type)
{
	int i, retval = 0;
	struct trace_struct *trace;
	
	for (i = 0; i < NR_TRACES; i++) {
		trace = &current_traces[i];
		if (!trace->active)
			continue;
		if(ltt_test_bit(syscall_type, &trace->traced_events))
			retval = 1;
	}

	return retval;
}

/**
 *	need_heartbeat: - If any active trace uses TSC timestamping, return 1
 *
 *	Returns the number of active traces using TSC timestamping
 *
 *	Needed for starting/stopping the heartbeat timer depending on whether
 *	any trace needs it or not.
 */
int need_heartbeat(void)
{
	int i, retval = 0;
	struct trace_struct *trace;
	
	for (i = 0; i < NR_TRACES; i++) {
		trace = &current_traces[i];
		if(trace->active && trace->using_tsc)
			retval++;
	}

	return retval;
}

/**
 *	init_heartbeat_timer: - Start timer generating hearbeat events.
 */
static void init_heartbeat_timer(void)
{
	if (loops_per_jiffy > 0) {
		init_timer(&heartbeat_timer);
		heartbeat_timer.function = write_heartbeat_event;
		heartbeat_timer.expires = jiffies 
			+ 0xffffffffUL/loops_per_jiffy - 1;
		add_timer(&heartbeat_timer);
	}
	else
		printk(KERN_ALERT "LTT: No TSC for heartbeat timer - continuing without one \n");
}

/*
 * Tasks and timers for trace finalization
 */

/**
 *	all_channels_finalized: - Verify that all channels have been finalized.
 *	@trace_handle: the trace containing the channels to be tested
 *
 *	Returns 1 if channels on all CPUs are complete, 0 otherwise.
 */
static int all_channels_finalized(unsigned int trace_handle)
{
	int i;
	
	for (i = 0; i < num_cpus; i++)
		if (atomic_read(&waiting_for_cpu_async(trace_handle, i)) & LTT_FINALIZE_TRACE)
			return 0;

	return 1;
}

/**
 *	active_traces: - The number of currently active traces
 *
 *	Returns the number of active traces
 */
static inline int active_traces(void)
{
	int i, nr_active = 0;
	
	for (i = 0; i < NR_TRACES; i++)
		if (current_traces[i].active)
			nr_active++;
		
	return nr_active;
}

/**
 *	del_percpu_timers: - Delete all per_cpu timers.
 */
static inline void del_percpu_timers(void)
{
	int i;

	for (i =  0; i < num_cpus; i++)
		del_timer_sync(&percpu_timer[i]);
}

/**
 *	remove_readers: - Remove all map readers.
 *	@trace_handle: the trace containing the readers to be removed
 */
static inline void remove_readers(unsigned int trace_handle)
{
	int i;
	
	for (i = 0; i < num_cpus; i++) {
		remove_map_reader(trace_channel_reader(trace_handle, i));
		trace_channel_reader(trace_handle, i) = NULL;
	}
}

/**
 *	do_waiting_async_tasks: - perform asynchronous per-CPU tasks.
 *	@cpu_id: the CPU the tasks should be executed on
 */
static void do_waiting_async_tasks(unsigned int trace_handle, u8 cpu_id)
{
	unsigned long int flags;
	int tasks;
	struct trace_struct *trace;
	
	trace = &current_traces[trace_handle];

	local_irq_save(flags);
	tasks = atomic_read(&waiting_for_cpu_async(trace_handle, cpu_id));

	if (tasks == 0) {
		local_irq_restore(flags);
		return;
	}

	if (trace->using_tsc && trace->tracer_started && (tasks & LTT_TRACE_HEARTBEAT)) {
                clear_waiting_for_cpu_async(trace_handle, cpu_id, LTT_TRACE_HEARTBEAT);
		TRACE_HEARTBEAT();
	}

	if (trace->tracer_stopping && (tasks & LTT_FINALIZE_TRACE)) {
                clear_waiting_for_cpu_async(trace_handle, cpu_id, LTT_FINALIZE_TRACE);
		if (relay_close(trace_channel_handle(trace_handle, cpu_id)) != 0)
			printk(KERN_ALERT "LTT: Couldn't close trace channel %d\n", trace_channel_handle(trace_handle, cpu_id));

		set_bit(cpu_id, &trace->buffer_switches_pending);

		if (all_channels_finalized(trace_handle)) {
			remove_readers(trace_handle);
			trace->tracer_stopping = 0;
		}
	}

	local_irq_restore(flags);
}

/**
 *	check_waiting_async_tasks: - Timer function checking for async tasks.
 *	@data: unused
 */
static void check_waiting_async_tasks(unsigned long data)
{
	int i;
	int cpu = smp_processor_id();
	
	for (i = 0; i < NR_TRACES; i++) {
		if (atomic_read(&waiting_for_cpu_async(i, cpu)) != 0)
			do_waiting_async_tasks(i, cpu);
	}

	del_timer(&percpu_timer[cpu]);
	percpu_timer[cpu].expires = jiffies + LTT_PERCPU_TIMER_FREQ;
	add_timer(&percpu_timer[cpu]);
}

/**
 *	_init_percpu_timer: - Start timer checking for async tasks.
 */
void _init_percpu_timer(void * dummy)
{
	int cpu = smp_processor_id();

	init_timer(&percpu_timer[cpu]);
	percpu_timer[cpu].function = check_waiting_async_tasks;
	percpu_timer[cpu].expires = jiffies + LTT_PERCPU_TIMER_FREQ;
	add_timer(&percpu_timer[cpu]);
}

static inline void init_percpu_timers(void)
{
	_init_percpu_timer(NULL);

	if (smp_call_function(_init_percpu_timer, NULL, 1, 1) != 0)
		printk(KERN_ALERT "LTT: Couldn't initialize all per-CPU timers\n");
}

/*
 * User-kernel interface for tracer
 */

/**
 *	update_shared_buffer_control: - prepare for GET_BUFFER_CONTROL ioctl
 *	@cpu_id: the CPU associated with the ioctl
 */
static inline void update_shared_buffer_control(struct trace_struct *trace, u8 cpu_id)
{
	struct rchan_info channel_info;
	int i;
	int channel_handle = trace_channel_handle(trace->trace_handle, cpu_id);
	
	if (relay_info(channel_handle, &channel_info) == -1) {
		shared_buf_ctl.buffer_control_valid = 0;
		return;
	}

	shared_buf_ctl.cpu_id =				cpu_id;
	shared_buf_ctl.buffer_switches_pending =	trace->buffer_switches_pending & ~(1UL << cpu_id);
	shared_buf_ctl.buffer_control_valid =		1;
	shared_buf_ctl.buf_size =			channel_info.buf_size;
	shared_buf_ctl.n_buffers =			channel_info.n_bufs;
	shared_buf_ctl.cur_idx =			channel_info.cur_idx;
	shared_buf_ctl.buffers_produced =		channel_info.bufs_produced;
	shared_buf_ctl.buffers_consumed =		channel_info.bufs_consumed;

	if (channel_info.flags & RELAY_SCHEME_LOCKLESS) {
		for (i = 0; i < channel_info.n_bufs; i++) {
			shared_buf_ctl.buffer_complete[i] = 
				channel_info.buffer_complete[i];
		}
	}
}

/**
 *	ltt_set_flight_recorder_config: - set flight recorder defaults
 *	@trace: the trace struct
 */
static void ltt_set_flight_recorder_config(struct trace_struct *trace)
{
	trace->traced_events = 0;
	trace->log_event_details_mask = 0;
	
	ltt_set_bit(TRACE_EV_BUFFER_START, &trace->traced_events);
	ltt_set_bit(TRACE_EV_BUFFER_START, &trace->log_event_details_mask);
	ltt_set_bit(TRACE_EV_START, &trace->traced_events);
	ltt_set_bit(TRACE_EV_START, &trace->log_event_details_mask);
	ltt_set_bit(TRACE_EV_CHANGE_MASK, &trace->traced_events);
	ltt_set_bit(TRACE_EV_CHANGE_MASK, &trace->log_event_details_mask);

	ltt_set_bit(TRACE_EV_SYSCALL_ENTRY, &trace->traced_events);
	ltt_set_bit(TRACE_EV_SYSCALL_ENTRY, &trace->log_event_details_mask);
	ltt_set_bit(TRACE_EV_SYSCALL_EXIT, &trace->traced_events);
	ltt_set_bit(TRACE_EV_SYSCALL_EXIT, &trace->log_event_details_mask);
	ltt_set_bit(TRACE_EV_TRAP_ENTRY, &trace->traced_events);
	ltt_set_bit(TRACE_EV_TRAP_ENTRY, &trace->log_event_details_mask);
	ltt_set_bit(TRACE_EV_TRAP_EXIT, &trace->traced_events);
	ltt_set_bit(TRACE_EV_TRAP_EXIT, &trace->log_event_details_mask);
	ltt_set_bit(TRACE_EV_IRQ_ENTRY, &trace->traced_events);
	ltt_set_bit(TRACE_EV_IRQ_ENTRY, &trace->log_event_details_mask);
	ltt_set_bit(TRACE_EV_IRQ_EXIT, &trace->traced_events);
	ltt_set_bit(TRACE_EV_IRQ_EXIT, &trace->log_event_details_mask);
	ltt_set_bit(TRACE_EV_SCHEDCHANGE, &trace->traced_events);
	ltt_set_bit(TRACE_EV_SCHEDCHANGE, &trace->log_event_details_mask);

	ltt_set_bit(TRACE_EV_KERNEL_TIMER, &trace->traced_events);
	ltt_set_bit(TRACE_EV_KERNEL_TIMER, &trace->log_event_details_mask);
	ltt_set_bit(TRACE_EV_SOFT_IRQ, &trace->traced_events);
	ltt_set_bit(TRACE_EV_SOFT_IRQ, &trace->log_event_details_mask);
	ltt_set_bit(TRACE_EV_PROCESS, &trace->traced_events);
	ltt_set_bit(TRACE_EV_PROCESS, &trace->log_event_details_mask);
}

/**
 *	tracer_get_status: - fill in a status struct covering all traces
 *	@tracer_status: the tracer_status struct
 */
static int tracer_get_status(struct tracer_status *tracer_status)
{
	int i, j, rchan_handle, retval = 0;
	struct trace_struct *trace;
	struct trace_info *info;
	struct rchan_info rchan_info;
	
	tracer_status->num_cpus = num_cpus;

	for (i = 0; i < NR_TRACES; i++) {
		trace = &current_traces[i];
		info = &tracer_status->traces[i];
		info->active = trace->active && trace->tracer_started ? 1 : 0;
		if (!info->active)
			continue;
		info->trace_handle = trace->trace_handle;
		info->paused = trace->paused;
		info->flight_recorder = trace->flight_recorder;
		info->use_locking = trace->use_locking;
		info->using_tsc = trace->using_tsc;
		info->n_buffers = trace->n_buffers;
		info->buf_size = trace->buf_size;
		info->traced_events = trace->traced_events;
		info->log_event_details_mask = trace->log_event_details_mask;
		for (j = 0; j < num_cpus; j++) {
			rchan_handle = trace_channel_handle(trace->trace_handle, j);
			retval = relay_info(rchan_handle, &rchan_info);
			if (retval)
				return retval;
			info->buffers_produced[j] = rchan_info.bufs_produced;
		}
	}

	return retval;
}

/**
 *      ltt_ioctl: - Tracing kernel-user control interface
 *
 *      @rchan_id: rchan id ioctl occurred on
 *      @tracer_command: command given by the caller
 *      @command_arg: argument to the command
 *
 *      Returns:
 *      >0, In case the caller requested the number of events lost.
 *      0, Everything went OK
 *      -ENOSYS, no such command
 *      -EINVAL, tracer not properly configured
 *      -EBUSY, tracer can't be reconfigured while in operation
 *      -ENOMEM, no more memory
 *      -EFAULT, unable to access user space memory
 *      -EACCES, invalid tracer handle
 */
static int ltt_ioctl(int rchan_id,
		     unsigned int tracer_command,
		     unsigned long arg)
{
	int retval;
	int new_user_event_id;
	unsigned long int flags;
	u8 cpu_id;
	u8 i;
	u32 buffers_consumed;
	trace_custom user_event;
	trace_change_mask trace_mask;
	trace_new_event new_user_event;
	struct buffers_committed buffers_committed;
	struct trace_struct *trace = NULL;
	struct tracer_status tracer_status;
	unsigned int tracer_handle;
	unsigned long command_arg;

	if (copy_from_user(&tracer_handle, (void *)arg, sizeof(unsigned int)))
		return -EFAULT;

	if (copy_from_user(&command_arg, (void*)(arg + sizeof(tracer_handle)), sizeof(unsigned long)))
		return -EFAULT;

	if (tracer_command == TRACER_ALLOC_HANDLE)
		return ltt_alloc_trace_handle(tracer_handle);

	if (!ltt_valid_trace_handle(tracer_handle))
		return -EACCES;

	if (tracer_handle < NR_TRACES)
		trace = &current_traces[tracer_handle];
	else if (tracer_handle >= NR_TRACES) {
		if (current_traces[TRACE_HANDLE].active)
			trace = &current_traces[TRACE_HANDLE];
		if (trace == NULL && tracer_command != TRACER_GET_STATUS)
			return -EACCES;
	}

	if ((tracer_handle < NR_TRACES)
	    && (trace->tracer_started == 1)
	    && (tracer_command != TRACER_STOP)
	    && (tracer_command != TRACER_PAUSE)
	    && (tracer_command != TRACER_UNPAUSE)
	    && (tracer_command != TRACER_DATA_COMITTED)
	    && (tracer_command != TRACER_GET_ARCH_INFO)
	    && (tracer_command != TRACER_GET_BUFFER_CONTROL)
	    && (tracer_command != TRACER_GET_START_INFO))
		return -EBUSY;

	if ((tracer_handle >= NR_TRACES)
	    && (tracer_command != TRACER_CREATE_USER_EVENT)
	    && (tracer_command != TRACER_DESTROY_USER_EVENT)
	    && (tracer_command != TRACER_TRACE_USER_EVENT)
	    && (tracer_command != TRACER_GET_STATUS)
	    && (tracer_command != TRACER_SET_EVENT_MASK)
	    && (tracer_command != TRACER_GET_EVENT_MASK))
		return -ENOSYS;

	switch (tracer_command) {
	case TRACER_START:
		if (trace->using_tsc && (need_heartbeat() == 1))
			init_heartbeat_timer();
		if (active_traces() == 1)
			init_percpu_timers();

		if (((use_syscall_eip_bounds == 1)
		     && (syscall_eip_depth_set == 1))
		    || ((use_syscall_eip_bounds == 1)
			&& ((lower_eip_bound_set != 1)
			    || (upper_eip_bound_set != 1)))
		    || ((trace->tracing_pid == 1)
			&& (trace->tracing_pgrp == 1)))
			return -EINVAL;

		if (ltt_set_trace_config(syscall_eip_depth_set,
					 use_syscall_eip_bounds,
					 syscall_eip_depth,
					 lower_eip_bound,
					 upper_eip_bound) < 0)
			return -EINVAL;

		if (trace->flight_recorder)
			ltt_set_flight_recorder_config(trace);
		
		ltt_set_bit(TRACE_EV_BUFFER_START, &trace->traced_events);
		ltt_set_bit(TRACE_EV_BUFFER_START, &trace->log_event_details_mask);
		ltt_set_bit(TRACE_EV_START, &trace->traced_events);
		ltt_set_bit(TRACE_EV_START, &trace->log_event_details_mask);
		ltt_set_bit(TRACE_EV_CHANGE_MASK, &trace->traced_events);
		ltt_set_bit(TRACE_EV_CHANGE_MASK, &trace->log_event_details_mask);

		/* Enable hooks for the events we are interested in. */
		change_traced_events(&trace->traced_events);

		trace->tracer_stopping = 0;
		trace->tracer_started = 1;

		ltt_reregister_custom_events();
		break;

	case TRACER_STOP:
		if (trace->flight_recorder) {
			for (i = 0; i < num_cpus; i++)
				tmp_rchan_handles[i] = trace_channel_handle(tracer_handle, i);
			ltt_free_all_handles(NULL);
		} else {
			trace->tracer_stopping = 1;
			trace->tracer_started = 0;
		}

		/* Disable hooks. */
		change_traced_events(NULL);

		if (trace->flight_recorder) {
			for (i = 0; i < num_cpus; i++) {
				if (relay_close(tmp_rchan_handles[i]) != 0)
					printk(KERN_ALERT "LTT: Couldn't close trace channel %d\n", trace_channel_handle(tracer_handle, i));
			}
			remove_readers(tracer_handle);
		} else {
			for (i = 0; i < num_cpus; i++)
				set_waiting_for_cpu_async(tracer_handle, i, LTT_FINALIZE_TRACE);
		}
		break;

	case TRACER_PAUSE:
		trace->paused = 1;
		break;

	case TRACER_UNPAUSE:
		trace->paused = 0;
		break;

	case TRACER_CONFIG_DEFAULT:
		ltt_set_default_config(trace);
		break;

	case TRACER_CONFIG_MEMORY_BUFFERS:
		if (trace->use_locking == 1) {
			if (command_arg < TRACER_MIN_BUF_SIZE)
				return -EINVAL;
		} else {
			if ((command_arg < TRACER_LOCKLESS_MIN_BUF_SIZE) || 
			    (command_arg > TRACER_LOCKLESS_MAX_BUF_SIZE))
				return -EINVAL;
		}
		return ltt_set_buffer_size(trace, command_arg, relayfs_path);
		break;

	case TRACER_CONFIG_N_MEMORY_BUFFERS:
		if (command_arg < TRACER_MIN_BUFFERS || 
		    command_arg > TRACER_MAX_BUFFERS)
			return -EINVAL;

		return ltt_set_n_buffers(trace, command_arg);
		break;

	case TRACER_CONFIG_USE_LOCKING:
		trace->use_locking = command_arg;

		if ((trace->use_locking == 0) && (have_cmpxchg() == 0))
			return -EINVAL;
		break;

	case TRACER_CONFIG_EVENTS:
		if (copy_from_user(&trace->traced_events, (void *)command_arg, sizeof(trace->traced_events)))
			return -EFAULT;
		break;

	case TRACER_CONFIG_TIMESTAMP:
		trace->using_tsc = command_arg;

		if ((trace->using_tsc == 1) && (have_tsc() == 0)) {
			trace->using_tsc = 0;
			return -EINVAL;
		}
		break;

	case TRACER_CONFIG_DETAILS:
		if (copy_from_user(&trace->log_event_details_mask, (void *)command_arg, sizeof(trace->log_event_details_mask)))
			return -EFAULT;
		 /* Enable hooks for the events we are interested in. */

		if (trace->tracer_started) {
			change_traced_events(&trace->traced_events);
		}

		break;

	case TRACER_CONFIG_CPUID:
		trace->log_cpuid = 1;
		break;

	case TRACER_CONFIG_PID:
		trace->tracing_pid = 1;
		trace->traced_pid = command_arg;
		break;

	case TRACER_CONFIG_PGRP:
		trace->tracing_pgrp = 1;
		trace->traced_pgrp = command_arg;
		break;

	case TRACER_CONFIG_GID:
		trace->tracing_gid = 1;
		trace->traced_gid = command_arg;
		break;

	case TRACER_CONFIG_UID:
		trace->tracing_uid = 1;
		trace->traced_uid = command_arg;
		break;

	case TRACER_CONFIG_SYSCALL_EIP_DEPTH:
		syscall_eip_depth_set = 1;
		syscall_eip_depth = command_arg;
		break;

	case TRACER_CONFIG_SYSCALL_EIP_LOWER:
		use_syscall_eip_bounds = 1;
		lower_eip_bound = (void *)command_arg;
		lower_eip_bound_set = 1;
		break;

	case TRACER_CONFIG_SYSCALL_EIP_UPPER:
		use_syscall_eip_bounds = 1;
		upper_eip_bound = (void *)command_arg;
		upper_eip_bound_set = 1;
		break;

	case TRACER_DATA_COMITTED:
		if (copy_from_user(&buffers_committed, (void *)command_arg, 
				   sizeof(buffers_committed)))
			return -EFAULT;

		cpu_id = buffers_committed.cpu_id;
		buffers_consumed = buffers_committed.buffers_consumed;
		clear_bit(cpu_id, &trace->buffer_switches_pending);

		local_irq_save(flags);
		relay_buffers_consumed(trace_channel_reader(tracer_handle, cpu_id), 
				       buffers_consumed);
		local_irq_restore(flags);

		break;

	case TRACER_GET_EVENTS_LOST:
		return events_lost(tracer_handle, command_arg);
		break;

	case TRACER_CREATE_USER_EVENT:
		if (copy_from_user(&new_user_event, (void *)command_arg, sizeof(new_user_event)))
			return -EFAULT;

		new_user_event_id = ltt_create_owned_event(new_user_event.type,
							     new_user_event.desc,
							     new_user_event.format_type,
							     new_user_event.form,
							     current->pid);
		if (new_user_event_id >= 0) {
			new_user_event.id = new_user_event_id;
			if (copy_to_user((void *)command_arg, &new_user_event, sizeof(new_user_event))) {
				ltt_destroy_event(new_user_event_id);
				return -EFAULT;
			}
		} else
			return new_user_event_id;
		break;

	case TRACER_DESTROY_USER_EVENT:
		ltt_destroy_event((int)command_arg);
		break;

	case TRACER_TRACE_USER_EVENT:
		if (copy_from_user(&user_event, (void *)command_arg, sizeof(user_event)))
			return -EFAULT;
		
		if ((user_event_data == NULL) 
		    && (user_event_data = vmalloc(CUSTOM_EVENT_MAX_SIZE)) < 0)
			return -ENOMEM;

		if (copy_from_user(user_event_data, user_event.data, user_event.data_size))
			return -EFAULT;

		retval = ltt_log_raw_event(user_event.id,
					   user_event.data_size,
					   user_event_data);
		if (retval < 0)
			return retval;
		break;

	case TRACER_SET_EVENT_MASK:
		if (copy_from_user(&(trace_mask.mask), (void *)command_arg, sizeof(trace_mask.mask)))
			return -EFAULT;

		retval = _ltt_log_event(trace,
					TRACE_EV_CHANGE_MASK,
					&trace_mask,
					smp_processor_id());

		memcpy(&trace->traced_events, &(trace_mask.mask), sizeof(trace_mask.mask));

		ltt_set_bit(TRACE_EV_BUFFER_START, &trace->traced_events);
		ltt_set_bit(TRACE_EV_START, &trace->traced_events);
		ltt_set_bit(TRACE_EV_CHANGE_MASK, &trace->traced_events);

		/* Enable hooks for the events we are interested in. */
		if (trace->tracer_started) {
			change_traced_events(&trace->traced_events);
		}

		return retval;
		break;

	case TRACER_GET_EVENT_MASK:
		if (copy_to_user((void *)command_arg, &trace->traced_events, sizeof(trace->traced_events)))
			return -EFAULT;
		break;

	case TRACER_GET_ARCH_INFO:
		ltt_arch_info.n_cpus = num_cpus;
		ltt_arch_info.page_shift = PAGE_SHIFT;

		if (copy_to_user((void *)command_arg,
				 &ltt_arch_info,
				 sizeof(ltt_arch_info))) {
			return -EFAULT;
		}
		break;

	case TRACER_GET_START_INFO:
		if (trace->trace_start_data) {
			if (copy_to_user((void *)command_arg,
					 trace->trace_start_data,
					 sizeof(trace_start)))
				return -EFAULT;
		} else
			return -EINVAL;
		break;

	case TRACER_GET_STATUS:
		if (tracer_get_status(&tracer_status))
			return -EINVAL;
		
		if (copy_to_user((void *)command_arg, 
				 &tracer_status,
				 sizeof(struct tracer_status)))
			return -EFAULT;
		break;

	case TRACER_GET_BUFFER_CONTROL:
		if (copy_from_user(&shared_buf_ctl, (void *)command_arg, sizeof(shared_buf_ctl)))
			return -EFAULT;

		if (shared_buf_ctl.cpu_id == -1) {
			for (i = 0; i < num_cpus; i++) {
				if (trace->buffer_switches_pending & (1UL << i)) {
					update_shared_buffer_control(trace, i);
					if (copy_to_user((void *)command_arg,
							 &shared_buf_ctl,
							 sizeof(struct buf_control_info)))
						return -EFAULT;
					return 0;
				}
			}
		} else {
			update_shared_buffer_control(trace, (u8)shared_buf_ctl.cpu_id);
			if (copy_to_user((void *)command_arg,
					 &shared_buf_ctl,
					 sizeof(struct buf_control_info)))
				return -EFAULT;
			return 0;
		}

		shared_buf_ctl.cpu_id = 0;
		shared_buf_ctl.buffer_control_valid = 0;

		if (copy_to_user((void *) command_arg,
				&shared_buf_ctl,
				sizeof(struct buf_control_info)))
			return -EFAULT;
		break;

	case TRACER_FREE_HANDLE:
		return ltt_free_trace_handle(tracer_handle);
		break;

	case TRACER_FREE_DAEMON_HANDLE:
		return ltt_free_daemon_handle(trace);
		break;

	case TRACER_FREE_ALL_HANDLES:
		ltt_free_all_handles(current);
		break;

	case TRACER_MAP_BUFFER:
		return -EFAULT;
		break;

	default:
		return -ENOSYS;
		break;
	}

	return 0;
}

/*
 * Trace Handles
 */

/**
 *	ltt_valid_trace_handle: - Validate tracer handle.
 *	@tracer_handle: handle to be validated
 *
 *	Returns:
 *	1, if handle is valid
 *	0, if handle is invalid
 */
int ltt_valid_trace_handle(unsigned int tracer_handle)
{
	int retval = 0;
	struct trace_struct *trace;

	if (tracer_handle < NR_TRACES) {
		trace = &current_traces[tracer_handle];
		if (!trace->active)
			retval = 0;
		else if (!trace->flight_recorder) {
			if (trace->daemon_task_struct == current)
				retval = 1;
		} else
			retval = 1;
	} else {
		read_lock(&trace_handle_table_lock);
		if (trace_handle_table[tracer_handle - NR_TRACES].owner == current)
			retval = 1;
		else
			retval = 0;
		read_unlock(&trace_handle_table_lock);
	}

	return retval;
}

/**
 *	ltt_alloc_trace_handle: - Allocate trace handle to caller.
 *	@tracer_handle: handle requested by process
 *
 *	Returns:
 *	Handle ID, everything went OK
 *	-ENODEV, no more free handles.
 *	-EBUSY, daemon handle already in use.
 */
int ltt_alloc_trace_handle(unsigned int tracer_handle)
{
	int i;
	int retval;
	struct trace_struct *trace = NULL;
	
	if (tracer_handle < NR_TRACES) {
		trace = &current_traces[tracer_handle];
		if (trace->active)
			return -EBUSY;
	}

	if (tracer_handle == NR_TRACES) {
		write_lock(&trace_handle_table_lock);
		for (i = 0; i < TRACE_MAX_HANDLES; i++)
			if (trace_handle_table[i].owner == NULL) {
				trace_handle_table[i].owner = current;
				break;
			}
		write_unlock(&trace_handle_table_lock);
		if (i == TRACE_MAX_HANDLES)
			retval = -ENODEV;
		else
			retval = (i + NR_TRACES);
	} else {
		trace->active = trace;
		trace->tracer_started = 0;
		trace->tracer_stopping = 0;
		if (tracer_handle == TRACE_HANDLE) {
			trace->flight_recorder = 0;
			trace->daemon_task_struct = current;
		} else {
			if ((trace->trace_start_data = (struct _trace_start *) kmalloc(sizeof(struct _trace_start), GFP_ATOMIC)) == NULL)
				return -ENOMEM;
		}

		trace->proc_dir_entry = create_handle_proc_dir(tracer_handle);
		ltt_set_default_config(trace);
		retval = trace->trace_handle = tracer_handle;
	}
	
	return retval;
}

/**
 *	ltt_free_trace_handle: - Free a single handle.
 *	tracer_handle: handle to be freed.
 *
 *	Returns: 
 *	0, everything went OK
 *	-ENODEV, no such handle.
 *	-EACCES, handle doesn't belong to caller.
 */
int ltt_free_trace_handle(unsigned int tracer_handle)
{
	int retval;

	if ((tracer_handle < NR_TRACES) || (tracer_handle >= TRACE_MAX_HANDLES))
		return -ENODEV;

	write_lock(&trace_handle_table_lock);

	if (trace_handle_table[tracer_handle - NR_TRACES].owner == current) {
		trace_handle_table[tracer_handle - NR_TRACES].owner = NULL;
		retval = 0;
	}
	else
		retval = -EACCES;

	write_unlock(&trace_handle_table_lock);

	return retval;
}

/**
 *	ltt_free_daemon_handle: - Free the daemon's handle.
 *
 *	Returns: 
 *	0, everything went OK
 *	-EACCES, handle doesn't belong to caller.
 *	-EBUSY, there are still event writes in progress so the buffer can't
 *	be released.
 */
int ltt_free_daemon_handle(struct trace_struct *trace)
{
	int i;

	if (!trace->flight_recorder) {
		if (trace->daemon_task_struct != current)
			return -EACCES;

		for (i = 0; i < num_cpus; i++) {
			if (events_lost(trace->trace_handle, i) > 0)
				printk(KERN_ALERT "LTT: Lost %d events on cpu %d\n",
				       events_lost(trace->trace_handle, i), i);
		}
		trace->daemon_task_struct = NULL;
	}
	
	trace->active = NULL;

	if (!active_traces())
		del_percpu_timers();

	if(!need_heartbeat()) 
		del_timer(&heartbeat_timer);

	for (i = 0; i < num_cpus; i++) {
		if (trace_channel_handle(trace->trace_handle, i) != -1) {
				trace_channel_handle(trace->trace_handle, i) = -1;
				trace_channel_reader(trace->trace_handle, i) = NULL;
		}
	}

	remove_handle_proc_dir(trace->proc_dir_entry, 0);

	trace->use_locking = 1;
	ltt_set_default_config(trace);
	trace->tracer_started = 0;
	trace->tracer_stopping = 0;
	if (trace->trace_start_data)
		kfree(trace->trace_start_data);

	return 0;
}

/**
 *	ltt_free_all_handles: - Free all handles taken.
 *	@task_ptr: pointer to exiting task.
 *
 *	Free all handles taken against a given channel.  If task_ptr is NULL,
 *	it means there is no daemon, i.e. free all handles taken agains the
 *	flight recorder channel, otherwise task_ptr refers to a trace daemon.
 */
void ltt_free_all_handles(struct task_struct* task_ptr)
{
	int i;
	struct trace_struct *trace;

	if (task_ptr == NULL) {
		if (current_traces[FLIGHT_HANDLE].active) {
			ltt_free_daemon_handle(&current_traces[FLIGHT_HANDLE]);
			return;
		}
	} else {
		trace = &current_traces[TRACE_HANDLE];
		if (trace->active && trace->daemon_task_struct == task_ptr)
			ltt_free_daemon_handle(trace);
	}

	write_lock(&trace_handle_table_lock);
	for (i = 0; i < TRACE_MAX_HANDLES; i++)
		if (trace_handle_table[i].owner == current)
			trace_handle_table[i].owner = NULL;
	write_unlock(&trace_handle_table_lock);
} /* * Tracer Configuration
 */

/**
 *	init_channel_data: - Init channel-associated data for new tracing run.
 *	@buf_ctrl: buffer control struct to be initialized
 *	@use_lockless: which tracing scheme to use, 1 for lockless
 *	@buffer_number_bits: number of bits in index word for buffer number
 *	@offset_bits: number of bits in index word to use for buffer offset
 */
static void init_channel_data(struct trace_struct *trace)
{
	unsigned i;
	
	trace->buffer_switches_pending = 0;

	for (i = 0; i < num_cpus; i++) {
		trace_channel_handle(trace->trace_handle, i) = -1;
		trace_channel_reader(trace->trace_handle, i) = NULL;
		atomic_set(&waiting_for_cpu_async(trace->trace_handle, i), LTT_NOTHING_TO_DO);
		events_lost(trace->trace_handle, i) = 0;
	}
}

/**
 *	ltt_set_n_buffers: - Sets the number of buffers.
 *	@no_buffers: number of buffers.
 *
 *	For lockless only, must be a power of 2.
 *
 *	Returns:
 *
 *	0, Size setting went OK
 *	-EINVAL, not a power of 2
 */
int ltt_set_n_buffers(struct trace_struct *trace, int no_buffers)
{
	if (hweight32(no_buffers) != 1)
		return -EINVAL;

	trace->n_buffers = no_buffers;

	return 0;
}

/**
 *	ltt_set_buffer_size: - Sets size of and creates buffers.
 *	@buf_size: Size of sub-buffers
 *	@dirname: name of the relayfs directory to contain trace files
 *
 *	Note: dirname should be well-formed before it gets here e.g.
 *	trailing slashes should be removed.
 *
 *	Returns:
 *	0, Size setting went OK
 *	-ENOMEM, unable to get a hold of memory for tracer
 *	-EINVAL, tracer not properly configured
 */
int ltt_set_buffer_size(struct trace_struct *trace, int buffer_size, char * dirname)
{
	int i;
	u32 flags;

	if (trace->flight_recorder)
		flags = RELAY_DELIVERY_BULK | RELAY_USAGE_SMP | RELAY_MODE_CONTINUOUS;
	else
		flags = RELAY_DELIVERY_BULK | RELAY_USAGE_SMP | RELAY_MODE_NO_OVERWRITE;

	if ((dirname == NULL) || (strlen(dirname) == 0))
		return  -EINVAL;

	if (trace->using_tsc)
		flags |= RELAY_TIMESTAMP_TSC;
	else
		flags |= RELAY_TIMESTAMP_GETTIMEOFDAY;
	
	if (trace->use_locking)
		flags |= RELAY_SCHEME_LOCKING;
	else
		flags |= RELAY_SCHEME_LOCKLESS;
	
	num_cpus = num_online_cpus();

	init_channel_data(trace);

	trace->buf_size = buffer_size;

	for (i = 0; i < num_cpus; i++) {
		sprintf(relay_file_name, "%s/cpu%d", dirname, i);
		trace_channel_handle(trace->trace_handle, i) = relay_open(relay_file_name,
							  buffer_size,
							  trace->n_buffers,
							  flags,
							  &ltt_callbacks,
							  start_reserve,
							  end_reserve,
							  trace_start_reserve,
							  0,
							  0,
							  0,
							  NULL,
							  0);
		if (trace_channel_handle(trace->trace_handle, i) < 0)
			return -ENOMEM;
	}
	
	return 0;
}

/**
 *	ltt_set_default_config: - Sets the tracer in its default config
 *
 *	Returns:
 *	0, everything went OK
 *	-ENOMEM, unable to get a hold of memory for tracer
 */
int ltt_set_default_config(struct trace_struct *trace)
{
	int i;
	int retval = 0;

	trace->traced_events = 0;

	for (i = 0; i <= TRACE_EV_MAX; i++) {
		ltt_set_bit(i, &trace->traced_events);
		ltt_set_bit(i, &trace->log_event_details_mask);
	}


	trace->log_cpuid = 0;
	trace->tracing_pid = 0;
	trace->tracing_pgrp = 0;
	trace->tracing_gid = 0;
	trace->tracing_uid = 0;
	trace->using_tsc = 0;

	syscall_eip_depth_set = 0;
	use_syscall_eip_bounds = 0;
	lower_eip_bound_set = 0;
	upper_eip_bound_set = 0;

	ltt_set_trace_config(syscall_eip_depth_set,
			 use_syscall_eip_bounds,
			 0,
			 0,
			 0);

	/* Enable hooks for the events we are interested in. */
	if (trace->tracer_started) {
		change_traced_events(&trace->traced_events);
	}

	return retval;
}

/**
 *	ltt_set_trace_config: - Set the tracing configuration
 *	@do_syscall_depth: Use depth to fetch eip
 *	@do_syscall_bounds: Use bounds to fetch eip
 *	@eip_depth: Detph to fetch eip
 *	@eip_lower_bound: Lower bound eip address
 *	@eip_upper_bound: Upper bound eip address
 *
 *	Returns: 
 *	0, all is OK 
 *	-ENOMEDIUM, there isn't a registered tracer
 *	-ENXIO, wrong tracer
 *	-EINVAL, invalid configuration
 */
int ltt_set_trace_config(int do_syscall_depth,
		     int do_syscall_bounds,
		     int eip_depth,
		     void *eip_lower_bound,
		     void *eip_upper_bound)
{
	if ((do_syscall_depth && do_syscall_bounds)
	    || (eip_lower_bound > eip_upper_bound)
	    || (eip_depth < 0))
		return -EINVAL;

	fetch_syscall_eip_use_depth = do_syscall_depth;
	fetch_syscall_eip_use_bounds = do_syscall_bounds;

	syscall_eip_depth = eip_depth;
	syscall_lower_eip_bound = eip_lower_bound;
	syscall_upper_eip_bound = eip_upper_bound;

	return 0;
}

/**
 *	ltt_get_trace_config: - Get the tracing configuration
 *	@do_syscall_depth: Use depth to fetch eip
 *	@do_syscall_bounds: Use bounds to fetch eip
 *	@eip_depth: Detph to fetch eip
 *	@eip_lower_bound: Lower bound eip address
 *	@eip_upper_bound: Upper bound eip address
 *
 *	Returns:
 *	0, all is OK 
 *	-ENOMEDIUM, there isn't a registered tracer
 */
int ltt_get_trace_config(int *do_syscall_depth,
		     int *do_syscall_bounds,
		     int *eip_depth,
		     void **eip_lower_bound,
		     void **eip_upper_bound)
{
	*do_syscall_depth = fetch_syscall_eip_use_depth;
	*do_syscall_bounds = fetch_syscall_eip_use_bounds;
	*eip_depth = syscall_eip_depth;
	*eip_lower_bound = syscall_lower_eip_bound;
	*eip_upper_bound = syscall_upper_eip_bound;

	return 0;
}


/*
 * Custom Events
 */

/**
 *	init_custom_events: - Initialize custom events
 */
static inline void init_custom_events(void)
{
	custom_events = &custom_events_head;
	custom_events->next = custom_events;
	custom_events->prev = custom_events;
}

/**
 *	_ltt_create_event: - Create a new traceable event type
 *	@event_type: string describing event type
 *	@event_desc: string used for standard formatting
 *	@format_type: type of formatting used to log event data
 *	@format_data: data specific to format
 *	@owner_pid: PID of event's owner (0 if none)
 *
 *	Returns:
 *	New Event ID if all is OK
 *	-ENOMEM, Unable to allocate new event
 */
int _ltt_create_event(char *event_type,
			char *event_desc,
			int format_type,
			char *format_data,
			pid_t owner_pid)
{
	trace_new_event *new_event;
	struct custom_event_desc *new_event_desc;

	if ((new_event_desc = (struct custom_event_desc *) kmalloc(sizeof(struct custom_event_desc), GFP_ATOMIC)) == NULL)
		 return -ENOMEM;

	new_event = &(new_event_desc->event);
	new_event->type[0] = '\0';
	new_event->desc[0] = '\0';
	new_event->form[0] = '\0';

	if (event_type != NULL)
		strncpy(new_event->type, event_type, CUSTOM_EVENT_TYPE_STR_LEN);
	if (event_desc != NULL)
		strncpy(new_event->desc, event_desc, CUSTOM_EVENT_DESC_STR_LEN);
	if (format_data != NULL)
		strncpy(new_event->form, format_data, CUSTOM_EVENT_FORM_STR_LEN);

	new_event->type[CUSTOM_EVENT_TYPE_STR_LEN - 1] = '\0';
	new_event->desc[CUSTOM_EVENT_DESC_STR_LEN - 1] = '\0';
	new_event->form[CUSTOM_EVENT_FORM_STR_LEN - 1] = '\0';

	new_event->format_type = format_type;
	new_event->id = next_event_id;

	next_event_id++;

	new_event_desc->owner_pid = owner_pid;

	write_lock(&custom_list_lock);

	if (custom_events == NULL)
		init_custom_events();

	new_event_desc->next = custom_events;
	new_event_desc->prev = custom_events->prev;
	custom_events->prev->next = new_event_desc;
	custom_events->prev = new_event_desc;
	write_unlock(&custom_list_lock);

	ltt_log_event(TRACE_EV_NEW_EVENT, &(new_event_desc->event));

	return new_event->id;
}

int ltt_create_event(char *event_type,
		       char *event_desc,
		       int format_type,
		       char *format_data)
{
	return _ltt_create_event(event_type, event_desc, format_type, format_data, 0);
}

int ltt_create_owned_event(char *event_type,
			     char *event_desc,
			     int format_type,
			     char *format_data,
			     pid_t owner_pid)
{
	return _ltt_create_event(event_type, event_desc, format_type, format_data, owner_pid);
}

/**
 *	ltt_destroy_event: - Destroy a created event type
 *	@event_id, the Id returned by ltt_create_event()
 */
void ltt_destroy_event(int event_id)
{
	struct custom_event_desc *event_desc;

	write_lock(&custom_list_lock);

	if (custom_events == NULL)
		init_custom_events();

	for (event_desc = custom_events->next;
	     event_desc != custom_events;
	     event_desc = event_desc->next)
		if (event_desc->event.id == event_id)
			break;

	if (event_desc != custom_events) {
		event_desc->next->prev = event_desc->prev;
		event_desc->prev->next = event_desc->next;
		kfree(event_desc);
	}

	write_unlock(&custom_list_lock);
}

/**
 *	ltt_destroy_owners_events: Destroy an owner's events
 *	@owner_pid: the PID of the owner who's events are to be deleted.
 */
void ltt_destroy_owners_events(pid_t owner_pid)
{
	struct custom_event_desc *temp_event;
	struct custom_event_desc *event_desc;

	write_lock(&custom_list_lock);

	if (custom_events == NULL)
		init_custom_events();

	event_desc = custom_events->next;

	while (event_desc != custom_events) {
		temp_event = event_desc->next;
		if (event_desc->owner_pid == owner_pid) {
			event_desc->next->prev = event_desc->prev;
			event_desc->prev->next = event_desc->next;
			kfree(event_desc);
		}
		event_desc = temp_event;
	}

	write_unlock(&custom_list_lock);
}

/**
 *	ltt_reregister_custom_events: - Relogs event creations.
 */
void ltt_reregister_custom_events(void)
{
	struct custom_event_desc *event_desc;

	read_lock(&custom_list_lock);

	if (custom_events == NULL)
		init_custom_events();

	for (event_desc = custom_events->next;
	     event_desc != custom_events;
	     event_desc = event_desc->next)
		ltt_log_event(TRACE_EV_NEW_EVENT, &(event_desc->event));

	read_unlock(&custom_list_lock);
}

/*
 * Event logging primitives
 */

/**
 *	_ltt_log_event: - Tracing function per se.
 *	@event_id: ID of event as defined in linux/ltt.h
 *	@event_struct: struct describing the event
 *	@cpu_id: the CPU associated with the event
 *
 *	Returns: 
 *	0, if everything went OK (event got registered)
 *	-ENODEV, no tracing daemon opened the driver.
 *	-ENOMEM, no more memory to store events.
 *	-EBUSY, tracer not started yet.
 */
int _ltt_log_event(struct trace_struct *trace,
		   u8 event_id,
		   void *event_struct,
		   u8 cpu_id)
{
	int var_data_len = 0;
	void *var_data_beg = NULL;
	uint16_t data_size;
	struct task_struct *incoming_process = NULL;
	unsigned long flags;
	char * reserved;
	int bytes_written = 0;
	int reserve_code, interrupting;
	struct timeval time_stamp;
	u32 time_delta;
	int channel_handle;
	struct rchan *rchan;
	unsigned int tracer_handle;
	
	if (!trace)
		return -ENOMEDIUM;

	if (trace->paused)
		return -EBUSY;

	tracer_handle = trace->trace_handle;
	
	if (!trace->flight_recorder && (trace->daemon_task_struct == NULL))
		return -ENODEV;

	channel_handle = trace_channel_handle(tracer_handle, cpu_id);

	if ((trace->tracer_started == 1) || (event_id == TRACE_EV_START) || (event_id == TRACE_EV_BUFFER_START))
		goto trace_event;

	return -EBUSY;

trace_event:
	if (!ltt_test_bit(event_id, &trace->traced_events))
		return 0;

	if ((event_id != TRACE_EV_START) && (event_id != TRACE_EV_BUFFER_START)) {
		if (event_id == TRACE_EV_SCHEDCHANGE)
			incoming_process = (struct task_struct *) (((trace_schedchange *) event_struct)->in);
		if ((trace->tracing_pid == 1) && (current->pid != trace->traced_pid)) {
			if (incoming_process == NULL)
				return 0;
			else if (incoming_process->pid != trace->traced_pid)
				return 0;
		}
		if ((trace->tracing_pgrp == 1) && (process_group(current) != trace->traced_pgrp)) {
			if (incoming_process == NULL)
				return 0;
			else if (process_group(incoming_process) != trace->traced_pgrp)
				return 0;
		}
		if ((trace->tracing_gid == 1) && (current->egid != trace->traced_gid)) {
			if (incoming_process == NULL)
				return 0;
			else if (incoming_process->egid != trace->traced_gid)
				return 0;
		}
		if ((trace->tracing_uid == 1) && (current->euid != trace->traced_uid)) {
			if (incoming_process == NULL)
				return 0;
			else if (incoming_process->euid != trace->traced_uid)
				return 0;
		}
		if (event_id == TRACE_EV_SCHEDCHANGE)
			(((trace_schedchange *) event_struct)->in) = incoming_process->pid;
	}

	data_size = sizeof(event_id) + sizeof(time_delta) + sizeof(data_size);

	if (ltt_test_bit(event_id, &trace->log_event_details_mask)) {
		data_size += event_struct_size[event_id];
		switch (event_id) {
		case TRACE_EV_FILE_SYSTEM:
			if ((((trace_file_system *) event_struct)->event_sub_id == TRACE_EV_FILE_SYSTEM_EXEC)
			    || (((trace_file_system *) event_struct)->event_sub_id == TRACE_EV_FILE_SYSTEM_OPEN)) {
				var_data_beg = ((trace_file_system *) event_struct)->file_name;
				var_data_len = ((trace_file_system *) event_struct)->event_data2 + 1;
				data_size += (uint16_t) var_data_len;
			}
			break;
		case TRACE_EV_CUSTOM:
			var_data_beg = ((trace_custom *) event_struct)->data;
			var_data_len = ((trace_custom *) event_struct)->data_size;
			data_size += (uint16_t) var_data_len;
			break;
		}
	}

	if ((trace->log_cpuid == 1) && (event_id != TRACE_EV_START) && (event_id != TRACE_EV_BUFFER_START))
		data_size += sizeof(cpu_id);

	rchan = rchan_get(channel_handle);
	if (rchan == NULL)
		return -ENODEV;
	relay_lock_channel(rchan, flags); /* nop for lockless */
	reserved = relay_reserve(rchan, data_size, &time_stamp, &time_delta, &reserve_code, &interrupting);
	
	if (reserve_code & RELAY_WRITE_DISCARD) {
		events_lost(trace->trace_handle, cpu_id)++;
		bytes_written = 0;
		goto check_buffer_switch;
	}
	if ((trace->log_cpuid == 1) && (event_id != TRACE_EV_START) 
	    && (event_id != TRACE_EV_BUFFER_START))
		relay_write_direct(reserved,
				   &cpu_id,
				   sizeof(cpu_id));

	relay_write_direct(reserved,
			   &event_id,
			   sizeof(event_id));

	relay_write_direct(reserved,
			   &time_delta,
			   sizeof(time_delta));

	if (ltt_test_bit(event_id, &trace->log_event_details_mask)) {
		relay_write_direct(reserved,
				   event_struct,
				   event_struct_size[event_id]);
		if (var_data_len)
			relay_write_direct(reserved,
					   var_data_beg,
					   var_data_len);
	}

	relay_write_direct(reserved,
			   &data_size,
			   sizeof(data_size));

	bytes_written = data_size;

check_buffer_switch:
	if ((event_id == TRACE_EV_SCHEDCHANGE) && (tracer_handle == TRACE_HANDLE) && current_traces[FLIGHT_HANDLE].active)
		(((trace_schedchange *) event_struct)->in) = (u32)incoming_process;
	
	/* We need to commit even if we didn't write anything because
	   that's how the deliver callback is invoked. */
	relay_commit(rchan, reserved, bytes_written, reserve_code, interrupting);

	relay_unlock_channel(rchan, flags);
	rchan_put(rchan);

	return 0;
}

/**
 *	ltt_log_event: - Trace an event
 *	@event_id, the event's ID (check out ltt.h)
 *	@event_struct, the structure describing the event
 *
 *	Returns:
 *	Trace fct return code if OK.
 *	-ENOMEDIUM, there is no registered tracer
 *	-ENOMEM, couldn't access ltt_info
 */
int ltt_log_event(u8 event_id,
		void *event_struct)
{
	int i;
	static int err[NR_TRACES];
	struct trace_struct *trace;
	u32 cpu = smp_processor_id();

	for (i = 0; i < NR_TRACES; i++) {
		trace = current_traces[i].active;
		err[i] = _ltt_log_event(trace,
				     event_id,
				     event_struct, 
				     cpu);
	}

	return err[0] == -ENOMEDIUM ? err[1] : err[0];
}

/**
 *	ltt_log_std_formatted_event: - Trace a formatted event
 *	@event_id: the event Id provided upon creation
 *	@...: printf-like data that will be used to fill the event string.
 *
 *	Returns:
 *	Trace fct return code if OK.
 *	-ENOMEDIUM, there is no registered tracer or event doesn't exist.
 */
int ltt_log_std_formatted_event(int event_id,...)
{
	int string_size;
	char final_string[CUSTOM_EVENT_FINAL_STR_LEN];
	va_list vararg_list;
	trace_custom custom_event;
	struct custom_event_desc *event_desc;

	read_lock(&custom_list_lock);

	if (custom_events == NULL)
		init_custom_events();

	for (event_desc = custom_events->next;
	     event_desc != custom_events;
	     event_desc = event_desc->next)
		if (event_desc->event.id == event_id)
			break;

	if (event_desc == custom_events) {
		read_unlock(&custom_list_lock);
		return -ENOMEDIUM;
	}

	custom_event.id = event_id;

	va_start(vararg_list, event_id);
	string_size = vsprintf(final_string, event_desc->event.desc, vararg_list);
	read_unlock(&custom_list_lock);
	va_end(vararg_list);

	custom_event.data_size = (u32) (string_size + 1);
	custom_event.data = final_string;

	return ltt_log_event(TRACE_EV_CUSTOM, &custom_event);
}

/**
 *	ltt_log_raw_event: - Trace a raw event
 *	@event_id, the event Id provided upon creation
 *	@event_size, the size of the data provided
 *	@event_data, data buffer describing event
 *
 *	Returns:
 *	Trace fct return code if OK.
 *	-ENOMEDIUM, there is no registered tracer or event doesn't exist.
 */
int ltt_log_raw_event(int event_id, int event_size, void *event_data)
{
	trace_custom custom_event;
	struct custom_event_desc *event_desc;

	read_lock(&custom_list_lock);

	if (custom_events == NULL)
		init_custom_events();

	for (event_desc = custom_events->next;
	     event_desc != custom_events;
	     event_desc = event_desc->next)
		if (event_desc->event.id == event_id)
			break;

	read_unlock(&custom_list_lock);

	if (event_desc == custom_events)
		return -ENOMEDIUM;

	custom_event.id = event_id;

	if (event_size <= CUSTOM_EVENT_MAX_SIZE)
		custom_event.data_size = (u32) event_size;
	else
		custom_event.data_size = (u32) CUSTOM_EVENT_MAX_SIZE;

	custom_event.data = event_data;

	return ltt_log_event(TRACE_EV_CUSTOM, &custom_event);
}

/*
 * Relayfs callback implementations.
 */

/**
 *	_ltt_channel_cpuid: - Get CPU id given channel handle, for given trace.
 *	@tracer_handle: trace handle.
 *	@channel_handle: relay channel handle.
 *
 *	Returns:
 *
 *	CPU id
 *	-1, channel_handle, thus CPU id, not found
 */
static int _ltt_channel_cpuid(int tracer_handle, int channel_handle)
{
	int i;
	
	for (i = 0; i < num_cpus; i++)
		if (trace_channel_handle(tracer_handle, i) == channel_handle)
			return i;
	
	return -1;
}

/**
 *	ltt_channel_cpuid: - Get CPU id given channel handle.
 *	@channel_handle: relay channel handle.
 *
 *	Returns:
 *
 *	CPU id
 *	-1, channel_handle, thus CPU id, not found
 */
static int ltt_channel_cpuid(int channel_handle)
{
	int i, cpuid;
	
	for (i = 0; i < NR_TRACES; i++) {
		cpuid = _ltt_channel_cpuid(i, channel_handle);
		if (cpuid != -1)
			return cpuid;
	}
	
	return -1;
}

/**
 *	ltt_channel_trace: - Get trace struct given channel handle.
 *	@channel_handle: relay channel handle.
 *
 *	Returns:
 *
 *	trace struct *
 *	NULL, channel_handle, thus trace_struct *, not found
 */
static struct trace_struct *ltt_channel_trace(int channel_handle)
{
	int i;
	
	for (i = 0; i < NR_TRACES; i++) {
		if (_ltt_channel_cpuid(i, channel_handle) != -1)
			return &current_traces[i];
	}
	
	return NULL;
}

/**
 *	ltt_channel_trace_handle: - Get trace handle given channel handle.
 *	@channel_handle: relay channel handle.
 *
 *	Returns:
 *
 *	trace handle
 *	-1, channel_handle, thus trace handle, not found
 */
static int ltt_channel_trace_handle(int channel_handle)
{
	unsigned int i;
	
	for (i = 0; i < NR_TRACES; i++)
		if (_ltt_channel_cpuid(i, channel_handle) != -1)
			return i;
	
	return -1;
}

/**
 *	write_start_event: - Initialize a trace session for a given CPU.
 *	@cpu_id: the CPU id to initialize a trace for
 */
static inline int write_start_event(struct trace_struct *trace,
				    int channel, 
				    char * current_write_pos,
				    u32 start_tsc,
				    int using_tsc)
{
	struct rchan_info channel_info;
	u32 time_delta;
	trace_start start_event;
	u8 event_id;
	uint16_t data_size;

	relay_info(channel, &channel_info);

	start_event.magic_number =	TRACER_MAGIC_NUMBER;
	start_event.arch_type =		TRACE_ARCH_TYPE;
	start_event.arch_variant =	TRACE_ARCH_VARIANT;
	start_event.system_type =	TRACE_SYS_TYPE_VANILLA_LINUX;
	start_event.major_version =	TRACER_VERSION_MAJOR;
	start_event.minor_version =	TRACER_VERSION_MINOR;
	start_event.buffer_size =	channel_info.buf_size;
	start_event.event_mask = 	trace->traced_events;
	start_event.details_mask =	trace->log_event_details_mask;
	start_event.log_cpuid =		trace->log_cpuid;
	start_event.use_tsc =		trace->using_tsc;
	start_event.flight_recorder =	trace->flight_recorder;

	event_id = TRACE_EV_START;
	relay_write_direct(current_write_pos,
			   &event_id,
			   sizeof(event_id));

	time_delta = switch_time_delta(start_tsc, using_tsc);
	relay_write_direct(current_write_pos,
			   &time_delta,
			   sizeof(time_delta));

	relay_write_direct(current_write_pos,
			   &start_event,
			   sizeof(trace_start));

	data_size = sizeof(event_id)
		+ sizeof(time_delta)
		+ sizeof(trace_start)
		+ sizeof(data_size);

	relay_write_direct(current_write_pos,
			   &data_size,
			   sizeof(data_size));

	if (trace->trace_start_data)
		memcpy(trace->trace_start_data, &start_event, sizeof(start_event));

	return (int)data_size;
}

/**
 *	buffer_start_callback: - Write start-buffer event to start of buffer.
 *	@channel_handle: the channel id
 *	@current_write_pos: position in sub-buffer client should write to
 *	@buffer_id: the id of the new sub-buffer
 *	@start_time: the timestamp associated with the start of sub-buffer
 *	@start_tsc: the TSC associated with the timestamp, if using_tsc
 *	@using_tsc: boolean, indicates whether start_tsc is valid
 *
 *	This is the relayfs buffer_start() callback implementation for
 *	the tracer.  We write the start event directly to the address
 *	contained in the current_write_pos param.  If this is the first
 *	sub-buffer, we also write the start event.  Of course we reserved
 *	the number of bytes we're writing when we opened the channel, which
 *	is the number we return.
 */
static int buffer_start_callback(int channel_handle,
				 char * current_write_pos,
				 u32 buffer_id,
				 struct timeval start_time,
				 u32 start_tsc,
				 int using_tsc) 
{
	trace_buffer_start start_buffer_event;
	u8 event_id;
	u32 time_delta;
	uint16_t data_size;
	struct trace_struct *trace = ltt_channel_trace(channel_handle);

	if (!trace)
		return 0;
	
	start_buffer_event.id = buffer_id;
	start_buffer_event.time = start_time;
	start_buffer_event.tsc = start_tsc;

	event_id = TRACE_EV_BUFFER_START;
	relay_write_direct(current_write_pos,
			   &event_id,
			   sizeof(event_id));

	time_delta = switch_time_delta(start_tsc, using_tsc);
	relay_write_direct(current_write_pos,
			   &time_delta,
			   sizeof(time_delta));

	relay_write_direct(current_write_pos,
			   &start_buffer_event,
			   sizeof(start_buffer_event));

	data_size = sizeof(event_id)
	    + sizeof(time_delta)
	    + sizeof(start_buffer_event)
	    + sizeof(data_size);

	relay_write_direct(current_write_pos,
			   &data_size,
			   sizeof(data_size));

	if (buffer_id == 0) /* first buffer */
		data_size += write_start_event(trace, channel_handle, current_write_pos, start_tsc, using_tsc);
	
	return (int)data_size;
}

/**
 *	buffer_end_callback - called at the end of a sub-buffer
 *	@channel_handle: the channel id
 *	@current_write_pos: position in sub-buffer of end of data
 *	@end_of_buffer: the position of the end of the sub-buffer
 *	@end_time: the timestamp associated with the end of the sub-buffer
 *	@end_tsc: the TSC associated with the end_time, if using_tsc
 *	@using_tsc: boolean, indicates whether end_tsc is valid
 *
 *	This is the relayfs buffer_end() callback implementation for
 *	the tracer.  We write the end event directly to the address
 *	contained in the current_write_pos param.  We also calculate
 *	the 'size_lost' or unused bytes at the end of the sub-buffer
 *	and write that value to the very end of the sub-buffer for
 *	post-processing.  Of course we reserved	the number of bytes
 *	we're writing when we opened the channel, which is the number
 *	we return.
 */
static int buffer_end_callback(int channel_handle,
			       char * current_write_pos,
			       char * end_of_buffer,
			       struct timeval end_time,
			       u32 end_tsc,
			       int using_tsc) 
{
 	trace_buffer_end end_buffer_event;
	u8 event_id;
	u32 time_delta;
	char* init_write_pos = current_write_pos;
	uint16_t data_size;
	u32 size_lost;
	u8 cpu_id;
	struct trace_struct *trace;

	end_buffer_event.time = end_time;
	end_buffer_event.tsc = end_tsc;

	cpu_id = (u8)ltt_channel_cpuid(channel_handle);
	trace = ltt_channel_trace(channel_handle);
	if (!trace)
		return 0;

	if (trace->log_cpuid == 1)
		relay_write_direct(current_write_pos,
				   &cpu_id,
				   sizeof(cpu_id));

	event_id = TRACE_EV_BUFFER_END;
	relay_write_direct(current_write_pos,
			   &event_id,
			   sizeof(event_id));

	time_delta = switch_time_delta(end_tsc, using_tsc);
	relay_write_direct(current_write_pos,
			   &time_delta,
			   sizeof(time_delta));

	relay_write_direct(current_write_pos,
			   &end_buffer_event,
			   sizeof(end_buffer_event));

	data_size = sizeof(event_id)
		+ sizeof(time_delta)
		+ sizeof(end_buffer_event)
		+ sizeof(data_size);

	relay_write_direct(current_write_pos,
			   &data_size,
			   sizeof(data_size));

	/* size lost includes size of end buffer event */
	size_lost = end_of_buffer - init_write_pos;
	*((u32 *) (end_of_buffer - sizeof(size_lost))) = size_lost;

	return (int)data_size;
}

/**
 *	deliver_callback - called when data is ready for the tracer
 *	@channel_handle: the channel id
 *	@from: the start of the delivered data
 *	@len: the length of the delivered data
 *
 *	This is the relayfs deliver() callback implementation for
 *	the tracer.  We simply set the send_signal flag, which will
 *	be checked when the current write is finished, at which 
 *	point the daemon will be signaled to read the buffer.
 */
void deliver_callback(int channel_handle,
		      char * from,
		      u32 len)
{
	struct trace_struct *trace;
	int cpu_id;

	trace = ltt_channel_trace(channel_handle);
	if (!trace)
		return;
	
	cpu_id = ltt_channel_cpuid(channel_handle);
	if (cpu_id == -1)
		return;

	set_bit(cpu_id, &trace->buffer_switches_pending);
}

/**
 *	fileop_notify - called when change to trace file status 
 *	@rchan_id: the rchan id
 *	@filp: the file
 *	@fileop: the file operation
 *
 *	This is the relayfs fileop_notify() callback implementation for
 *	the tracer.  We use it to take care of trace file mapping and
 *	unmapping tasks.
 */
static int fileop_notify(int rchan_id,
			 struct file *filp,
			 enum relay_fileop fileop)
{
	struct rchan_reader *map_reader;
	struct rchan_reader *open_file_reader;
	struct rchan *rchan;
	u8 cpu_id;
	int trace_handle;

	trace_handle = ltt_channel_trace_handle(rchan_id);
	if (trace_handle == -1)
		return 0;
	
	if (fileop == RELAY_FILE_MAP) {
		cpu_id = (u8)ltt_channel_cpuid(rchan_id);
		open_file_reader = (struct rchan_reader *)filp->private_data;
		rchan = open_file_reader->rchan;
		if (atomic_read(&rchan->mapped))
			return -EBUSY;
		map_reader = add_map_reader(rchan_id);
		trace_channel_reader(trace_handle, cpu_id) = map_reader;
	} else if (fileop == RELAY_FILE_UNMAP) {
		cpu_id = (u8)ltt_channel_cpuid(rchan_id);
		remove_map_reader(trace_channel_reader(trace_handle, cpu_id));
		trace_channel_reader(trace_handle, cpu_id) = NULL;
	}

	return 0;
}

static struct rchan_callbacks ltt_callbacks = {
	.buffer_start = buffer_start_callback,
	.buffer_end = buffer_end_callback,
	.deliver = deliver_callback,
	.fileop_notify = fileop_notify,
	.ioctl = ltt_ioctl,
};

/*
 * Procfs kernel-user interface
 */

/**
 *	proc_read_relayfs_path - procfs read callback for relayfs_path attr 
 */
static int proc_read_relayfs_path(char *page, char **start, off_t off, 
				  int count, int *eof, void *data)
{
	return sprintf(page, "%s", relayfs_path);
}

/**
 *	proc_write_relayfs_path - procfs write callback for relayfs_path attr 
 *
 *	Sets the path to the trace files within relayfs for the current trace.
 */
static int proc_write_relayfs_path(struct file *filp, const char *buffer,
				   unsigned long count, void *data)
{
	unsigned long len;

	if (count > PATH_MAX)
		len = PATH_MAX;
	else
		len = count;
	
	if (copy_from_user(relayfs_path, buffer, len))
		return -EFAULT;

	if (len != PATH_MAX)
		relayfs_path[len] = '\0';

	return len;
}

/**
 *	populate_handle_proc_dir - populate proc dir with trace attributes
 *	@trace_handle: the trace handle for this trace run
 *	@handle_dir: the directory to populate
 *
 *	This function populates the handle dir with attribute files.
 *
 *	Returns 0 if successful, negative if not.
 */
static int populate_handle_proc_dir(unsigned int trace_handle,
				    struct proc_dir_entry *handle_dir)
{
	struct proc_dir_entry * file_entry;
	int err = 0;

	file_entry = create_proc_entry("relayfs_path", 0666, handle_dir);

	if (file_entry == NULL) {
		err = -ENOMEM;
		return err;
	}

	file_entry->read_proc = proc_read_relayfs_path;
	file_entry->write_proc = proc_write_relayfs_path;
	file_entry->data = (void *)trace_handle;
	file_entry->owner = THIS_MODULE;

	return err;
}

/**
 *	create_handle_proc_dir - create proc dir for trace attributes
 *	@trace_handle: the trace handle for this trace run
 *
 *	This function creates a proc dir to communicate trace attribute
 *	values between the daemon and the tracer.  It also populates the
 *	new dir with the attribute files.
 *
 *	Retruns the proc dir entry if successful, NULL otherwise.
 */
static struct proc_dir_entry *create_handle_proc_dir(unsigned int trace_handle)
{
	char handle_dir_name[22];
	struct proc_dir_entry *handle_dir;

	sprintf(handle_dir_name, "%u", trace_handle);

	handle_dir = proc_mkdir(handle_dir_name, ltt_proc_root_entry);

	if (handle_dir == NULL) {
		return NULL;
	}
	else
		handle_dir->owner = THIS_MODULE;
	
	if (populate_handle_proc_dir(trace_handle, handle_dir)) {
		remove_proc_entry(handle_dir_name, ltt_proc_root_entry);
		handle_dir = NULL;
	}
		
	return handle_dir;
}

/**
 *	depopulate_handle_proc_dir - remove proc dir entries for handle_dir
 *	@handle_dir: the directory to depopulate
 *
 *	This function removes the attribute files from the handle dir.
 */
static void depopulate_handle_proc_dir(struct proc_dir_entry *handle_dir)
{
	remove_proc_entry("relayfs_path", handle_dir);
}

/**
 *	remove_handle_proc_dir - remove proc dir for trace attributes
 *	@handle_dir: the directory
 *	@trace_handle: the trace handle for this trace run
 *
 *	This function removes a trace handle's proc dir.  It first
 *	depopulates the dir of attribute files.
 */
static void remove_handle_proc_dir(struct proc_dir_entry *handle_dir, 
				   unsigned int trace_handle)
{
	char handle_dir_name[22];

	depopulate_handle_proc_dir(handle_dir);
	
	sprintf(handle_dir_name, "%u", trace_handle);
	remove_proc_entry(handle_dir_name, ltt_proc_root_entry);
}

/*
 * Initialization and finalization
 */

static struct rchan_callbacks control_callbacks = {
	.ioctl = ltt_ioctl,
};

/**
 *	create_control_channel - creates channel /mnt/relay/ltt/control
 *
 *	Returns channel id on success, negative otherwise.
 */
static int
create_control_channel(void)
{
	u32 bufsize, nbufs;
	u32 channel_flags;
	int control;

	sprintf(relay_file_name, "%s/%s", TRACE_RELAYFS_ROOT, TRACE_CONTROL_FILE);

	channel_flags = RELAY_DELIVERY_PACKET | RELAY_USAGE_GLOBAL;
	channel_flags |= RELAY_SCHEME_ANY | RELAY_TIMESTAMP_ANY;

	bufsize = 4096;
	nbufs = 4;

	control = relay_open(relay_file_name,
			     bufsize,
			     nbufs,
			     channel_flags,
			     &control_callbacks,
			     0,
			     0,
			     0,
			     0,
			     0,
			     0,
			     NULL,
			     0);

	return control;
}

/**
 *	proc_read_init_ltt - procfs read callback for init attr 
 */
static int proc_read_init_ltt(char *page, char **start, off_t off, 
			      int count, int *eof, void *data)
{
	return sprintf(page, "%d", control_channel == -1 ? 0 : 1);
}

/**
 *	proc_write_init_ltt - procfs write callback for init attr 
 */
static int proc_write_init_ltt(struct file *filp, const char *buffer,
			       unsigned long count, void *data)
{
	if (control_channel == -1) {
		control_channel = create_control_channel();
	
		if (control_channel < 0)
			printk("LTT control channel creation failed, errcode: %d\n", control_channel);
		else
			printk("LTT control channel created\n");
	}

	return 1;
}

/**
 *	remove_control_channel - destroys channel /mnt/relay/ltt/control
 *
 *	Returns 0, negative otherwise.
 */
static int
remove_control_channel(void)
{
	if (control_channel != -1)
		return relay_close(control_channel);

	return -ENODEV;
}

static int __init init_ltt(void)
{
	int i;
	int err = 0;
	struct proc_dir_entry *init_entry;

	ltt_proc_root_entry = proc_mkdir("ltt", NULL);

	if (ltt_proc_root_entry == NULL)
		err = -ENOMEM;
	else
		ltt_proc_root_entry->owner = THIS_MODULE;

	control_channel = -1;
	
	init_entry = create_proc_entry("init", 0666, ltt_proc_root_entry);
	if (init_entry == NULL) {
		err = -ENOMEM;
		return err;
	}

	init_entry->read_proc = proc_read_init_ltt;
	init_entry->write_proc = proc_write_init_ltt;
	init_entry->owner = THIS_MODULE;

	for (i = 0; i < NR_TRACES; i++)
		init_trace(&current_traces[i]);
		
	return err;
}

static void __exit exit_ltt(void)
{
	remove_proc_entry("init", ltt_proc_root_entry);
	remove_proc_entry("ltt", NULL);

	remove_control_channel();
}

module_init(init_ltt)
module_exit(exit_ltt)

EXPORT_SYMBOL(ltt_set_trace_config);
EXPORT_SYMBOL(ltt_get_trace_config);
EXPORT_SYMBOL(ltt_create_event);
EXPORT_SYMBOL(ltt_create_owned_event);
EXPORT_SYMBOL(ltt_destroy_event);
EXPORT_SYMBOL(ltt_destroy_owners_events);
EXPORT_SYMBOL(ltt_log_std_formatted_event);
EXPORT_SYMBOL(ltt_log_raw_event);
EXPORT_SYMBOL(ltt_log_event);

MODULE_AUTHOR("Karim Yaghmour, Tom Zanussi, Bob Wisniewski")
MODULE_DESCRIPTION("Linux Trace Toolkit kernel core") 
MODULE_LICENSE("GPL");
