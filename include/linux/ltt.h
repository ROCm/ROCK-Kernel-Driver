/*
 * linux/include/linux/ltt.h
 *
 * Copyright (C) 1999-2004 Karim Yaghmour (karim@opersys.com)
 *
 * This contains the necessary definitions for the Linux Trace Toolkit.
 */

#ifndef _LINUX_TRACE_H
#define _LINUX_TRACE_H

#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>

#include <linux/relayfs_fs.h>

/* Is kernel tracing enabled  */
#if defined(CONFIG_LTT) 
/* Don't set this to "1" unless you really know what you're doing */
#define LTT_UNPACKED_STRUCTS	0

/* Structure packing within the trace */
#if LTT_UNPACKED_STRUCTS
#define LTT_PACKED_STRUCT
#else
#define LTT_PACKED_STRUCT __attribute__ ((packed))
#endif

typedef u64 trace_event_mask;

#define CUSTOM_EVENT_MAX_SIZE		8192
#define CUSTOM_EVENT_TYPE_STR_LEN	20
#define CUSTOM_EVENT_DESC_STR_LEN	100
#define CUSTOM_EVENT_FORM_STR_LEN	256
#define CUSTOM_EVENT_FINAL_STR_LEN	200

#define CUSTOM_EVENT_FORMAT_TYPE_NONE	0
#define CUSTOM_EVENT_FORMAT_TYPE_STR	1
#define CUSTOM_EVENT_FORMAT_TYPE_HEX	2
#define CUSTOM_EVENT_FORMAT_TYPE_XML	3
#define CUSTOM_EVENT_FORMAT_TYPE_IBM	4

#define TRACE_MAX_HANDLES		256

/* In the ltt root directory lives the trace control file, used for
   kernel-user communication. */
#define TRACE_RELAYFS_ROOT		"ltt"
#define TRACE_CONTROL_FILE		"control"

/* We currently support 2 traces, normal trace and flight recorder */
#define NR_TRACES			2
#define TRACE_HANDLE			0
#define FLIGHT_HANDLE			1

/* Convenience accessors */
#define waiting_for_cpu_async(trace_handle, cpu) (current_traces[trace_handle].relay_data[cpu].waiting_for_cpu_async)
#define trace_channel_handle(trace_handle, cpu) (current_traces[trace_handle].relay_data[cpu].channel_handle)
#define trace_channel_reader(trace_handle, cpu) (current_traces[trace_handle].relay_data[cpu].reader)
#define trace_buffers_full(cpu) (daemon_relay_data[cpu].buffers_full)
#define events_lost(trace_handle, cpu) (current_traces[trace_handle].relay_data[cpu].events_lost)

/* System types */
#define TRACE_SYS_TYPE_VANILLA_LINUX	1

/* Architecture types */
#define TRACE_ARCH_TYPE_I386			1
#define TRACE_ARCH_TYPE_PPC			2
#define TRACE_ARCH_TYPE_SH			3
#define TRACE_ARCH_TYPE_S390			4
#define TRACE_ARCH_TYPE_MIPS			5
#define TRACE_ARCH_TYPE_ARM			6

/* Standard definitions for variants */
#define TRACE_ARCH_VARIANT_NONE             0   /* Main architecture implementation */

/* The maximum number of CPUs the kernel might run on */
#define MAX_NR_CPUS 32

/* Per-CPU channel information */
struct channel_data
{
	int channel_handle;
	struct rchan_reader *reader;
	atomic_t waiting_for_cpu_async;
	u32 events_lost;
};

/* Per-trace status info */
struct trace_info
{
	int			active;
	unsigned int		trace_handle;
	int			paused;
	int			flight_recorder;
	int			use_locking;
	int			using_tsc;
	u32			n_buffers;
	u32			buf_size;
	trace_event_mask	traced_events;
	trace_event_mask	log_event_details_mask;
	u32			buffers_produced[MAX_NR_CPUS];
};

/* Status info for all traces */
struct tracer_status
{
	int num_cpus;
	struct trace_info traces[NR_TRACES];
};

/* Per-trace information - each trace/flight recorder represented by one */
struct trace_struct
{
	unsigned int		trace_handle;	/* For convenience */
	struct trace_struct	*active;	/* 'this' if active, or NULL */
	int			paused;		/* Not currently logging */
	struct channel_data relay_data[NR_CPUS];/* Relayfs handles, by CPU */
	int			flight_recorder;/* i.e. this is not a trace */
	struct task_struct	*daemon_task_struct;/* Daemon associated with trace */
	struct _trace_start	*trace_start_data; /* Trace start event data, for flight recorder */
	int			tracer_started;
	int			tracer_stopping;
	struct proc_dir_entry *	proc_dir_entry;	/* proc/ltt/0..1 */
	trace_event_mask	traced_events;
	trace_event_mask	log_event_details_mask;
	u32			n_buffers;	/* Number of sub-buffers */
	u32			buf_size;	/* Size of sub-buffer */
	int			use_locking;
	int			using_tsc;
	int			log_cpuid;
	int			tracing_pid;
	int			tracing_pgrp;
	int			tracing_gid;
	int			tracing_uid;
	pid_t			traced_pid;
	pid_t			traced_pgrp;
	gid_t			traced_gid;
	uid_t			traced_uid;
	unsigned long		buffer_switches_pending;/* For trace */  
};


extern int ltt_set_trace_config(
	int		do_syscall_depth,
	int		do_syscall_bounds,
	int		eip_depth,
	void		*eip_lower_bound,
	void		*eip_upper_bound);
extern int ltt_get_trace_config(
	int		*do_syscall_depth,
	int		*do_syscall_bounds,
	int		*eip_depth,
	void		**eip_lower_bound,
	void		**eip_upper_bound);
extern int ltt_create_event(
	char		*event_type,
	char		*event_desc,
	int		format_type,
	char		*format_data);
extern int ltt_create_owned_event(
	char		*event_type,
	char		*event_desc,
	int		format_type,
	char		*format_data,
	pid_t		owner_pid);
extern void ltt_destroy_event(
	int		event_id);
extern void ltt_destroy_owners_events(
	pid_t		owner_pid);
extern void ltt_reregister_custom_events(void);
extern int ltt_log_std_formatted_event(
	int		event_id,
	...);
extern int ltt_log_raw_event(
	int		event_id,
	int		event_size,
	void		*event_data);
extern int _ltt_log_event(
	struct trace_struct *trace,
	u8		event_id,
	void		*event_struct,
	u8		cpu_id);
extern int ltt_log_event(
	u8		event_id,
	void		*event_struct);
extern int ltt_valid_trace_handle(
	unsigned int	tracer_handle);
extern int ltt_alloc_trace_handle(
	unsigned int	tracer_handle);
extern int ltt_free_trace_handle(
	unsigned int	tracer_handle);
extern int ltt_free_daemon_handle(
	struct trace_struct *trace);
extern void ltt_free_all_handles(
	struct task_struct*	task_ptr);
extern int ltt_set_buffer_size(
	struct trace_struct *trace,
	int		buffers_size, 
	char *		dirname);
extern int ltt_set_n_buffers(
	struct trace_struct *trace,
	int		no_buffers);
extern int ltt_set_default_config(
	struct trace_struct *trace);

static inline void TRACE_EVENT(u8 event_id, void* data)
{
	ltt_log_event(event_id, data);
}

/* Traced events */
enum {
	TRACE_EV_START = 0,	/* This is to mark the trace's start */
	TRACE_EV_SYSCALL_ENTRY,	/* Entry in a given system call */
	TRACE_EV_SYSCALL_EXIT,	/* Exit from a given system call */
	TRACE_EV_TRAP_ENTRY,	/* Entry in a trap */
	TRACE_EV_TRAP_EXIT,	/* Exit from a trap */
	TRACE_EV_IRQ_ENTRY,	/* Entry in an irq */
	TRACE_EV_IRQ_EXIT,	/* Exit from an irq */
	TRACE_EV_SCHEDCHANGE,	/* Scheduling change */
	TRACE_EV_KERNEL_TIMER,	/* The kernel timer routine has been called */
	TRACE_EV_SOFT_IRQ,	/* Hit key part of soft-irq management */
	TRACE_EV_PROCESS,	/* Hit key part of process management */
	TRACE_EV_FILE_SYSTEM,	/* Hit key part of file system */
	TRACE_EV_TIMER,		/* Hit key part of timer management */
	TRACE_EV_MEMORY,	/* Hit key part of memory management */
	TRACE_EV_SOCKET,	/* Hit key part of socket communication */
	TRACE_EV_IPC,		/* Hit key part of System V IPC */
	TRACE_EV_NETWORK,	/* Hit key part of network communication */
	TRACE_EV_BUFFER_START,	/* Mark the begining of a trace buffer */
	TRACE_EV_BUFFER_END,	/* Mark the ending of a trace buffer */
	TRACE_EV_NEW_EVENT,	/* New event type */
	TRACE_EV_CUSTOM,	/* Custom event */
	TRACE_EV_CHANGE_MASK,	/* Change in event mask */
	TRACE_EV_HEARTBEAT	/* Heartbeat event */
};

/* Number of traced events */
#define TRACE_EV_MAX           TRACE_EV_HEARTBEAT

/* Information logged when a trace is started */
#define TRACER_MAGIC_NUMBER     0x00D6B7ED
#define TRACER_VERSION_MAJOR    2
#define TRACER_VERSION_MINOR    2
typedef struct _trace_start {
	u32 magic_number;
	u32 arch_type;
	u32 arch_variant;
	u32 system_type;
	u8 major_version;
	u8 minor_version;

	u32 buffer_size;
	trace_event_mask event_mask;
	trace_event_mask details_mask;
	u8 log_cpuid;
	u8 use_tsc;
	u8 flight_recorder;
} LTT_PACKED_STRUCT trace_start;

/*  TRACE_SYSCALL_ENTRY */
typedef struct _trace_syscall_entry {
	u8 syscall_id;		/* Syscall entry number in entry.S */
	u32 address;		/* Address from which call was made */
} LTT_PACKED_STRUCT trace_syscall_entry;

/*  TRACE_TRAP_ENTRY */
#ifndef __s390__
typedef struct _trace_trap_entry {
	u16 trap_id;		/* Trap number */
	u32 address;		/* Address where trap occured */
} LTT_PACKED_STRUCT trace_trap_entry;
static inline void TRACE_TRAP_ENTRY(u16 trap_id, u32 address)
#else
typedef u64 trapid_t;
typedef struct _trace_trap_entry {
	trapid_t trap_id;	/* Trap number */
	u32 address;		/* Address where trap occured */
} LTT_PACKED_STRUCT trace_trap_entry;
static inline void TRACE_TRAP_ENTRY(trapid_t trap_id, u32 address)
#endif
{
	trace_trap_entry trap_event;

	trap_event.trap_id = trap_id;
	trap_event.address = address;

	ltt_log_event(TRACE_EV_TRAP_ENTRY, &trap_event);
}

/*  TRACE_TRAP_EXIT */
static inline void TRACE_TRAP_EXIT(void)
{
	ltt_log_event(TRACE_EV_TRAP_EXIT, NULL);
}

/*  TRACE_IRQ_ENTRY */
typedef struct _trace_irq_entry {
	u8 irq_id;		/* IRQ number */
	u8 kernel;		/* Are we executing kernel code */
} LTT_PACKED_STRUCT trace_irq_entry;
static inline void TRACE_IRQ_ENTRY(u8 irq_id, u8 in_kernel)
{
	trace_irq_entry irq_entry;

	irq_entry.irq_id = irq_id;
	irq_entry.kernel = in_kernel;

	ltt_log_event(TRACE_EV_IRQ_ENTRY, &irq_entry);
}

/*  TRACE_IRQ_EXIT */
static inline void TRACE_IRQ_EXIT(void)
{
	ltt_log_event(TRACE_EV_IRQ_EXIT, NULL);
}

/*  TRACE_SCHEDCHANGE */
typedef struct _trace_schedchange {
	u32 out;		/* Outgoing process */
	u32 in;			/* Incoming process */
	u32 out_state;		/* Outgoing process' state */
} LTT_PACKED_STRUCT trace_schedchange;
static inline void TRACE_SCHEDCHANGE(task_t * task_out, task_t * task_in)
{
	trace_schedchange sched_event;

	sched_event.out = (u32) task_out->pid;
	sched_event.in = (u32) task_in;
	sched_event.out_state = (u32) task_out->state;

	ltt_log_event(TRACE_EV_SCHEDCHANGE, &sched_event);
}

/*  TRACE_SOFT_IRQ */
enum {
	TRACE_EV_SOFT_IRQ_BOTTOM_HALF = 1,	/* Conventional bottom-half */
	TRACE_EV_SOFT_IRQ_SOFT_IRQ,		/* Real soft-irq */
	TRACE_EV_SOFT_IRQ_TASKLET_ACTION,	/* Tasklet action */
	TRACE_EV_SOFT_IRQ_TASKLET_HI_ACTION	/* Tasklet hi-action */
};
typedef struct _trace_soft_irq {
	u8 event_sub_id;	/* Soft-irq event Id */
	u32 event_data;
} LTT_PACKED_STRUCT trace_soft_irq;
static inline void TRACE_SOFT_IRQ(u8 ev_id, u32 data)
{
	trace_soft_irq soft_irq_event;

	soft_irq_event.event_sub_id = ev_id;
	soft_irq_event.event_data = data;

	ltt_log_event(TRACE_EV_SOFT_IRQ, &soft_irq_event);
}

/*  TRACE_PROCESS */
enum {
	TRACE_EV_PROCESS_KTHREAD = 1,	/* Creation of a kernel thread */
	TRACE_EV_PROCESS_FORK,		/* A fork or clone occured */
	TRACE_EV_PROCESS_EXIT,		/* An exit occured */
	TRACE_EV_PROCESS_WAIT,		/* A wait occured */
	TRACE_EV_PROCESS_SIGNAL,	/* A signal has been sent */
	TRACE_EV_PROCESS_WAKEUP		/* Wake up a process */
};
typedef struct _trace_process {
	u8 event_sub_id;	/* Process event ID */
	u32 event_data1;
	u32 event_data2;
} LTT_PACKED_STRUCT trace_process;
static inline void TRACE_PROCESS(u8 ev_id, u32 data1, u32 data2)
{
	trace_process proc_event;

	proc_event.event_sub_id = ev_id;
	proc_event.event_data1 = data1;
	proc_event.event_data2 = data2;

	ltt_log_event(TRACE_EV_PROCESS, &proc_event);
}
static inline void TRACE_PROCESS_EXIT(u32 data1, u32 data2)
{
	trace_process proc_event;

	proc_event.event_sub_id = TRACE_EV_PROCESS_EXIT;

	/**** WARNING ****/
	/* Regardless of whether this trace statement is active or not, these
	two function must be called, otherwise there will be inconsistencies
	in the kernel's structures. */
/*
	ltt_destroy_owners_events(current->pid);
	ltt_free_all_handles(current);
*/
	ltt_log_event(TRACE_EV_PROCESS, &proc_event);
}

/*  TRACE_FILE_SYSTEM */
enum {
	TRACE_EV_FILE_SYSTEM_BUF_WAIT_START = 1,	/* Starting to wait for a data buffer */
	TRACE_EV_FILE_SYSTEM_BUF_WAIT_END,		/* End to wait for a data buffer */
	TRACE_EV_FILE_SYSTEM_EXEC,			/* An exec occured */
	TRACE_EV_FILE_SYSTEM_OPEN,			/* An open occured */
	TRACE_EV_FILE_SYSTEM_CLOSE,			/* A close occured */
	TRACE_EV_FILE_SYSTEM_READ,			/* A read occured */
	TRACE_EV_FILE_SYSTEM_WRITE,			/* A write occured */
	TRACE_EV_FILE_SYSTEM_SEEK,			/* A seek occured */
	TRACE_EV_FILE_SYSTEM_IOCTL,			/* An ioctl occured */
	TRACE_EV_FILE_SYSTEM_SELECT,			/* A select occured */
	TRACE_EV_FILE_SYSTEM_POLL			/* A poll occured */
};
typedef struct _trace_file_system {
	u8 event_sub_id;	/* File system event ID */
	u32 event_data1;
	u32 event_data2;
	char *file_name;	/* Name of file operated on */
} LTT_PACKED_STRUCT trace_file_system;
static inline void TRACE_FILE_SYSTEM(u8 ev_id, u32 data1, u32 data2, const unsigned char *file_name)
{
	trace_file_system fs_event;

	fs_event.event_sub_id = ev_id;
	fs_event.event_data1 = data1;
	fs_event.event_data2 = data2;
	fs_event.file_name = (char*) file_name;

	ltt_log_event(TRACE_EV_FILE_SYSTEM, &fs_event);
}

/*  TRACE_TIMER */
enum {
	TRACE_EV_TIMER_EXPIRED = 1,	/* Timer expired */
	TRACE_EV_TIMER_SETITIMER,	/* Setting itimer occurred */
	TRACE_EV_TIMER_SETTIMEOUT	/* Setting sched timeout occurred */
};
typedef struct _trace_timer {
	u8 event_sub_id;	/* Timer event ID */
	u8 event_sdata;		/* Short data */
	u32 event_data1;
	u32 event_data2;
} LTT_PACKED_STRUCT trace_timer;
static inline void TRACE_TIMER(u8 ev_id, u8 sdata, u32 data1, u32 data2)
{
	trace_timer timer_event;

	timer_event.event_sub_id = ev_id;
	timer_event.event_sdata = sdata;
	timer_event.event_data1 = data1;
	timer_event.event_data2 = data2;

	ltt_log_event(TRACE_EV_TIMER, &timer_event);
}

/*  TRACE_MEMORY */
enum {
	TRACE_EV_MEMORY_PAGE_ALLOC = 1,		/* Allocating pages */
	TRACE_EV_MEMORY_PAGE_FREE,		/* Freing pages */
	TRACE_EV_MEMORY_SWAP_IN,		/* Swaping pages in */
	TRACE_EV_MEMORY_SWAP_OUT,		/* Swaping pages out */
	TRACE_EV_MEMORY_PAGE_WAIT_START,	/* Start to wait for page */
	TRACE_EV_MEMORY_PAGE_WAIT_END		/* End to wait for page */
};
typedef struct _trace_memory {
	u8 event_sub_id;	/* Memory event ID */
	u32 event_data;
} LTT_PACKED_STRUCT trace_memory;
static inline void TRACE_MEMORY(u8 ev_id, u32 data)
{
	trace_memory memory_event;

	memory_event.event_sub_id = ev_id;
	memory_event.event_data = data;

	ltt_log_event(TRACE_EV_MEMORY, &memory_event);
}

/*  TRACE_SOCKET */
enum {
	TRACE_EV_SOCKET_CALL = 1,	/* A socket call occured */
	TRACE_EV_SOCKET_CREATE,		/* A socket has been created */
	TRACE_EV_SOCKET_SEND,		/* Data was sent to a socket */
	TRACE_EV_SOCKET_RECEIVE		/* Data was read from a socket */
};
typedef struct _trace_socket {
	u8 event_sub_id;	/* Socket event ID */
	u32 event_data1;
	u32 event_data2;
} LTT_PACKED_STRUCT trace_socket;
static inline void TRACE_SOCKET(u8 ev_id, u32 data1, u32 data2)
{
	trace_socket socket_event;

	socket_event.event_sub_id = ev_id;
	socket_event.event_data1 = data1;
	socket_event.event_data2 = data2;

	ltt_log_event(TRACE_EV_SOCKET, &socket_event);
}

/*  TRACE_IPC */
enum {
	TRACE_EV_IPC_CALL = 1,		/* A System V IPC call occured */
	TRACE_EV_IPC_MSG_CREATE,	/* A message queue has been created */
	TRACE_EV_IPC_SEM_CREATE,	/* A semaphore was created */
	TRACE_EV_IPC_SHM_CREATE		/* A shared memory segment has been created */
};
typedef struct _trace_ipc {
	u8 event_sub_id;	/* IPC event ID */
	u32 event_data1;
	u32 event_data2;
} LTT_PACKED_STRUCT trace_ipc;
static inline void TRACE_IPC(u8 ev_id, u32 data1, u32 data2)
{
	trace_ipc ipc_event;

	ipc_event.event_sub_id = ev_id;
	ipc_event.event_data1 = data1;
	ipc_event.event_data2 = data2;

	ltt_log_event(TRACE_EV_IPC, &ipc_event);
}

/*  TRACE_NETWORK */
enum {
	TRACE_EV_NETWORK_PACKET_IN = 1,	/* A packet came in */
	TRACE_EV_NETWORK_PACKET_OUT	/* A packet was sent */
};
typedef struct _trace_network {
	u8 event_sub_id;	/* Network event ID */
	u32 event_data;
} LTT_PACKED_STRUCT trace_network;
static inline void TRACE_NETWORK(u8 ev_id, u32 data)
{
	trace_network net_event;

	net_event.event_sub_id = ev_id;
	net_event.event_data = data;

	ltt_log_event(TRACE_EV_NETWORK, &net_event);
}

/* Start of trace buffer information */
typedef struct _trace_buffer_start {
	struct timeval time;	/* Time stamp of this buffer */
	u32 tsc;   /* TSC of this buffer, if applicable */
	u32 id;			/* Unique buffer ID */
} LTT_PACKED_STRUCT trace_buffer_start;

/* End of trace buffer information */
typedef struct _trace_buffer_end {
	struct timeval time;	/* Time stamp of this buffer */
	u32 tsc;   /* TSC of this buffer, if applicable */
} LTT_PACKED_STRUCT trace_buffer_end;

/* Custom declared events */
/* ***WARNING*** These structures should never be used as is, use the 
   provided custom event creation and logging functions. */
typedef struct _trace_new_event {
	/* Basics */
	u32 id;					/* Custom event ID */
	char type[CUSTOM_EVENT_TYPE_STR_LEN];	/* Event type description */
	char desc[CUSTOM_EVENT_DESC_STR_LEN];	/* Detailed event description */

	/* Custom formatting */
	u32 format_type;			/* Type of formatting */
	char form[CUSTOM_EVENT_FORM_STR_LEN];	/* Data specific to format */
} LTT_PACKED_STRUCT trace_new_event;
typedef struct _trace_custom {
	u32 id;			/* Event ID */
	u32 data_size;		/* Size of data recorded by event */
	void *data;		/* Data recorded by event */
} LTT_PACKED_STRUCT trace_custom;

/* TRACE_CHANGE_MASK */
typedef struct _trace_change_mask {
	trace_event_mask mask;	/* Event mask */
} LTT_PACKED_STRUCT trace_change_mask;


/*  TRACE_HEARTBEAT */
static inline void TRACE_HEARTBEAT(void)
{
	ltt_log_event(TRACE_EV_HEARTBEAT, NULL);
}

/* Tracer properties */
#define TRACER_DEFAULT_BUF_SIZE   50000
#define TRACER_MIN_BUF_SIZE        1000
#define TRACER_MAX_BUF_SIZE      500000
#define TRACER_MIN_BUFFERS            2
#define TRACER_MAX_BUFFERS          256

#define TRACER_FIRST_EVENT_SIZE   (sizeof(u8) + sizeof(u32) + sizeof(trace_buffer_start) + sizeof(uint16_t))
#define TRACER_START_TRACE_EVENT_SIZE   (sizeof(u8) + sizeof(u32) + sizeof(trace_start) + sizeof(uint16_t))
#define TRACER_LAST_EVENT_SIZE   (sizeof(u8) \
				  + sizeof(u8) \
				  + sizeof(u32) \
				  + sizeof(trace_buffer_end) \
				  + sizeof(uint16_t) \
				  + sizeof(u32))

/* The configurations possible */
enum {
	TRACER_START = TRACER_MAGIC_NUMBER,	/* Start tracing events using the current configuration */
	TRACER_STOP,				/* Stop tracing */
	TRACER_CONFIG_DEFAULT,			/* Set the tracer to the default configuration */
	TRACER_CONFIG_MEMORY_BUFFERS,		/* Set the memory buffers the daemon wants us to use */
	TRACER_CONFIG_EVENTS,			/* Trace the given events */
	TRACER_CONFIG_DETAILS,			/* Record the details of the event, or not */
	TRACER_CONFIG_CPUID,			/* Record the CPUID associated with the event */
	TRACER_CONFIG_PID,			/* Trace only one process */
	TRACER_CONFIG_PGRP,			/* Trace only the given process group */
	TRACER_CONFIG_GID,			/* Trace the processes of a given group of users */
	TRACER_CONFIG_UID,			/* Trace the processes of a given user */
	TRACER_CONFIG_SYSCALL_EIP_DEPTH,	/* Set the call depth at which the EIP should be fetched on syscall */
	TRACER_CONFIG_SYSCALL_EIP_LOWER,	/* Set the lowerbound address from which EIP is recorded on syscall */
	TRACER_CONFIG_SYSCALL_EIP_UPPER,	/* Set the upperbound address from which EIP is recorded on syscall */
	TRACER_DATA_COMITTED,			/* The daemon has comitted the last trace */
	TRACER_GET_EVENTS_LOST,			/* Get the number of events lost */
	TRACER_CREATE_USER_EVENT,		/* Create a user tracable event */
	TRACER_DESTROY_USER_EVENT,		/* Destroy a user tracable event */
	TRACER_TRACE_USER_EVENT,		/* Trace a user event */
	TRACER_SET_EVENT_MASK,			/* Set the trace event mask */
	TRACER_GET_EVENT_MASK,			/* Get the trace event mask */
	TRACER_GET_BUFFER_CONTROL,		/* Get the buffer control data for the lockless schem*/
	TRACER_CONFIG_N_MEMORY_BUFFERS,		/* Set the number of memory buffers the daemon wants us to use */
	TRACER_CONFIG_USE_LOCKING,		/* Set the locking scheme to use */
	TRACER_CONFIG_TIMESTAMP,		/* Set the timestamping method to use */
	TRACER_GET_ARCH_INFO,			/* Get information about the CPU configuration */
	TRACER_ALLOC_HANDLE,			/* Allocate a tracer handle */
	TRACER_FREE_HANDLE,			/* Free a single handle */
	TRACER_FREE_DAEMON_HANDLE,		/* Free the daemon's handle */
	TRACER_FREE_ALL_HANDLES,		/* Free all handles */
	TRACER_MAP_BUFFER,			/* Map buffer to process-space */
	TRACER_PAUSE,				/* Pause tracing */
	TRACER_UNPAUSE,				/* Unpause tracing */
	TRACER_GET_START_INFO,			/* trace start data */
	TRACER_GET_STATUS			/* status of traces */
};

/* Lockless scheme definitions */
#define TRACER_LOCKLESS_MIN_BUF_SIZE CUSTOM_EVENT_MAX_SIZE + 8192
#define TRACER_LOCKLESS_MAX_BUF_SIZE 0x1000000

/* Flags used for per-CPU tasks */
#define LTT_NOTHING_TO_DO      0x00
#define LTT_FINALIZE_TRACE     0x02
#define LTT_TRACE_HEARTBEAT    0x08

/* How often the LTT per-CPU timers fire */
#define LTT_PERCPU_TIMER_FREQ  (HZ/10);

/* Used for sharing per-buffer information between driver and daemon */
struct buf_control_info
{
	s16 cpu_id;
	u32 buffer_switches_pending;
	u32 buffer_control_valid;

	u32 buf_size;
	u32 n_buffers;
	u32 cur_idx;
	u32 buffers_produced;
	u32 buffers_consumed;
	int buffer_complete[TRACER_MAX_BUFFERS];
};

/* Used for sharing buffer-commit information between driver and daemon */
struct buffers_committed
{
	u8 cpu_id;
	u32 buffers_consumed;
};

/* Used for specifying size/cpu id pair between driver and daemon */
struct cpu_mmap_data
{
	u8 cpu_id;
	unsigned long map_size;
};

/* Used for sharing architecture-specific info between driver and daemon */
struct ltt_arch_info
{
	int n_cpus;
	int page_shift;
};

extern __inline__ int ltt_set_bit(int nr, void *addr)
{
	unsigned char *p = addr;
	unsigned char mask = 1 << (nr & 7);
	unsigned char old;

	p += nr >> 3;
	old = *p;
	*p |= mask;

	return ((old & mask) != 0);
}

extern __inline__ int ltt_clear_bit(int nr, void *addr)
{
	unsigned char *p = addr;
	unsigned char mask = 1 << (nr & 7);
	unsigned char old;

	p += nr >> 3;
	old = *p;
	*p &= ~mask;

	return ((old & mask) != 0);
}

extern __inline__ int ltt_test_bit(int nr, void *addr)
{
	unsigned char *p = addr;
	unsigned char mask = 1 << (nr & 7);

	p += nr >> 3;

	return ((*p & mask) != 0);
}

/**
 *	switch_time_delta: - Utility function getting buffer switch time delta.
 *	@time_delta: previously calculated or retrieved time delta 
 *
 *	Returns the time_delta passed in if we're using TSC or 0 otherwise.
 */
static inline u32 switch_time_delta(u32 time_delta,
				    int using_tsc)
{
	if((using_tsc == 1) && cpu_has_tsc)
		return time_delta;
	else
		return 0;
}

static inline void TRACE_CLEANUP(void) {
	ltt_destroy_owners_events(current->pid);
	ltt_free_all_handles(current);
}

extern void change_traced_events(trace_event_mask *);
#else				/* Kernel is configured without tracing */
#define TRACE_EVENT(ID, DATA)
#define TRACE_TRAP_ENTRY(ID, EIP)
#define TRACE_TRAP_EXIT()
#define TRACE_IRQ_ENTRY(ID, KERNEL)
#define TRACE_IRQ_EXIT()
#define TRACE_SCHEDCHANGE(OUT, IN)
#define TRACE_SOFT_IRQ(ID, DATA)
#define TRACE_PROCESS(ID, DATA1, DATA2)
#define TRACE_PROCESS_EXIT(DATA1, DATA2)
#define TRACE_FILE_SYSTEM(ID, DATA1, DATA2, FILE_NAME)
#define TRACE_TIMER(ID, SDATA, DATA1, DATA2)
#define TRACE_MEMORY(ID, DATA)
#define TRACE_SOCKET(ID, DATA1, DATA2)
#define TRACE_IPC(ID, DATA1, DATA2)
#define TRACE_NETWORK(ID, DATA)
#define TRACE_HEARTBEAT()
static inline void TRACE_CLEANUP(void)	{}
#endif				/* defined(CONFIG_LTT) */
#endif				/* _LINUX_TRACE_H */



