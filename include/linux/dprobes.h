#ifndef _LINUX_DPROBES_H
#define _LINUX_DPROBES_H

/*
 * IBM Dynamic Probes
 * Copyright (c) International Business Machines Corp., 2000
 *
 */

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <sys/types.h>
#endif

#define DP_MAJOR_VER	5
#define DP_MINOR_VER	0
#define DP_PATCH_VER	0

/* main command codes */
#define DP_CMD_MASK		0x000000ff
#define DP_INSERT		0x00000001
#define DP_REMOVE		0x00000002
#define DP_GETVARS		0x00000004
#define DP_QUERY		0x00000008
#define DP_HELP			0x00000010
#define DP_VERSION		0x00000020
#define DP_BUILDPPDF		0x00000040
#define DP_APPLYPPDF		0x00000080

/* command modifiers */
#define DP_FLAGS_MASK		0xff000000
#define DP_ALL			0x80000000

/* insert cmd modifiers */
#define DP_MERGE		0x01000000
#define DP_REPLACE		0x02000000
#define DP_STOP_CPUS		0x04000000
#define DP_DONT_VERIFY_OPCODES	0x08000000
#define DP_AUTOSTACKTRACE	0x10000000

/* getvars cmd modifiers */
#define DP_GETVARS_INDEX	0x01000000
#define DP_GETVARS_RESET	0x02000000
#define DP_GETVARS_LOCAL	0x04000000
#define DP_GETVARS_GLOBAL	0x08000000

/* query cmd modifiers */
#define DP_QUERY_EXTENDED	0x01000000

/* pgm->flags: default data that will be part of each log record */
#define DP_LOG_MASK		0x0000ff00
#define DP_LOG_PROCNAME		0x00000100
#define DP_LOG_PID		0x00000200
#define DP_LOG_UID		0x00000400
#define DP_LOG_NIP             0x00000800
#define DP_LOG_PSW             0x00000800
#define DP_LOG_UPSW            0x00001000
#define DP_LOG_SS_ESP		0x00000800
#define DP_LOG_CS_EIP		0x00001000
#define DP_LOG_TSC		0x00002000
#define DP_LOG_CPU		0x00004000
#define DP_LOG_ALL		0x0000ff00

/* pgm->flags: default log target */
#define DP_LOG_TARGET_MASK	0x00ff0000
#define DP_LOG_TARGET_KLOG	0x00010000
#define DP_LOG_TARGET_LTT	0x00020000
#define DP_LOG_TARGET_COM1	0x00040000
#define DP_LOG_TARGET_COM2	0x00080000
#define DP_LOG_TARGET_EVL	0x00100000
#define DP_UNFORMATTED_OUTPUT	0x00200000

/* exit facilities for use with "exit_to" instruction */
#define DP_EXIT_TO_SGI_KDB	0x01
#define DP_EXIT_TO_SGI_VMDUMP	0x02
#define DP_EXIT_TO_CORE_DUMP	0x03

/* bit flags to indicate elements present in the trace buffer header. */
#define DP_HDR_MAJOR		0x00000001
#define DP_HDR_MINOR		0x00000002
#define DP_HDR_CPU		0x00000004
#define DP_HDR_PID		0x00000008
#define DP_HDR_UID		0x00000010
#define DP_HDR_CS		0x00000020
#define DP_HDR_EIP		0x00000040
#define DP_HDR_SS		0x00000080
#define DP_HDR_ESP		0x00000100
#define DP_HDR_TSC		0x00000200
#define DP_HDR_PROCNAME		0x00000400

typedef unsigned char byte_t;
/* FIXME: Well, we're working on getting this in arch headers */
#if defined(CONFIG_X86) || defined(CONFIG_IA64)
typedef byte_t opcode_t;
#endif

/*
 * This captures the header information of each probe point. It also contains
 * links to the rpn code of this dprobe handler.
 *
 * maxhits: This field indicates the number of times this probe will be
 * executed before being disabled. A negative value here means that the
 * probe will not be disabled automatically.
 *
 * count: This field keeps track of number of times this probe is hit,
 * and executed successfully. There is a reason this is "long" and not
 * "unsigned long". We would implement "pass-count" feature that specifies
 * the number of times we pass over this probe with actually executing the
 * probe handler, using the fact that count is a signed value.
 */
struct dp_point_struct {
	loff_t offset; 	/* file offset or absolute address */
	unsigned short address_flag;
	opcode_t opcode;
	opcode_t actual_opcode; /* in case of opcode mismatch */
	unsigned short major;
	unsigned short minor;
	unsigned short group;
	unsigned short type;
	unsigned long rpn_offset; /* offset into rpn_code of dp_pgm_struct */
	unsigned long rpn_end;
	unsigned short probe; /* watchpoint info, probe and access types */
	unsigned long len; 	/* range of the watchpoint probe */
	int dbregno;	/* the debug reg used for this watchpoint */
	long maxhits;
	long passcount;
	unsigned char logonfault;
	unsigned short ex_mask;
	unsigned long heap_size; /* no of heap elements specified by this probe */
};

#define DP_ADDRESS_ABSOLUTE	0x0001

/*
 * Flags related to watchpoint probes.
 */
#define DP_PROBE_BREAKPOINT	0x8000
#define DP_PROBE_WATCHPOINT	0x4000
#define DP_PROBE_MASK		0xc000
#define DP_MAX_WATCHPOINT	0x4 /* maximum number of watchpoint probes */

#define DP_WATCHTYPE_EXECUTE	0x0000
#define DP_WATCHTYPE_WRITE	0x0001
#define DP_WATCHTYPE_IO		0x0002
#define DP_WATCHTYPE_RDWR	0x0003
#define DP_WATCHTYPE_MASK	0x0003

/*
 * This captures the information specified in the dprobe program file header.
 *
 * User application passes the details of the pgm header to the system call in
 * this structure. It is also used in the kernel as part of dp_module_struct.
 */
struct dp_pgm_struct {
	unsigned char *name; /* module for which the probe program is written */
	unsigned long flags;
	unsigned short major;
	unsigned long id;
	unsigned short jmpmax;
	unsigned short logmax;
	unsigned short ex_logmax; /* length of exception stack trace buffer */ 
	unsigned short num_lv;	/* num of local variables */
	unsigned short num_gv;	/* num of global variables */
	unsigned long heapmax; /* num of heap elements required 
				   by this probe program */

	unsigned short rpn_length; /* rpn code */
	unsigned char autostacktrace;
	byte_t *rpn_code;

	unsigned short align; /* kernel module alignment */
	unsigned short num_points;
	struct dp_point_struct *point;
};

/* pgm flags */
#define DP_MODTYPE_MASK		0x000f
#define DP_MODTYPE_USER		0x0001
#define DP_MODTYPE_KERNEL	0x0002
#define DP_MODTYPE_KMOD		0x0004

#define LOG_SIZE		1024
#define EX_LOG_SIZE		1024
#define JMP_MAX			65535
#define MAX_MAXHITS		0x7fffffff
#define HEAPMAX			16*1024

#define DEFAULT_LOGMAX		LOG_SIZE
#define DEFAULT_EX_LOG_MAX	EX_LOG_SIZE
#define DEFAULT_JMPMAX		JMP_MAX
#define DEFAULT_MAXHITS		MAX_MAXHITS
#define DEFAULT_HEAPMAX		HEAPMAX

/* compiled in limits */
#define DP_MAX_LVARS		256
#define DP_MAX_GVARS		256

/* exception codes */
#define EX_INVALID_ADDR         0x0001
#define EX_SEG_FAULT            0x0002
#define EX_MAX_JMPS             0x0004
#define EX_CALL_STACK_OVERFLOW  0x0010
#define EX_DIV_BY_ZERO          0x0020
#define EX_INVALID_OPERAND      0x0040
#define EX_INVALID_OPCODE       0x0080
#define EX_HEAP_NOMEM		0x0100
#define EX_HEAP_INVALID_HANDLE	0x0200
#define EX_HEAP_INVALID_OFFSET	0x0400
#define EX_LOG_OVERFLOW         0x1000
#define EX_RPN_STACK_WRAP       0x2000
#define EX_USER                 0x8000
#define DEFAULT_EX_MASK         0x0fff

/* If a non-maskable ex is masked out, the interpreter will be terminated. */
#define EX_NON_MASKABLE_EX      0x0fff

#define EX_NO_HANDLER		0xffffffff

/*
 * This defines the Dynamic Probe getvars Request Packet, passed in from
 * the user for subfunction DP_GETVARS.
 */
struct  dp_getvars_in_struct  {
	unsigned char *name;    /* loadable module name */
	unsigned short from;
	unsigned short to;
	unsigned long allocated; /* Allocated size of result pkt */
	unsigned long returned; /* Returned size of result pkt */
};

/*
 * The output from getvars command is returned in this format. First all
 * global variables will be there as dp_getvars_global_out_struct followed
 * by local variables for each module in form of dp_getvars_local_out_struct.
 */
struct  dp_getvars_global_out_struct  {
	unsigned long num_vars; /* number of the variables to follow */
	/* variables */
};

struct dp_getvars_local_out_struct {
	unsigned long length;  /* length of this element */
	unsigned long num_vars; /* number of the variables to follow */
	/* variables followed by the name of loadable module*/
};

/*
 * This defines the Dynamic Probe query Request Packet passed in
 * from the user for subfunction DP_QUERY.
 */
struct dp_query_in_struct {
	unsigned char *name; /* loadable module name */
	unsigned long allocated; /* Allocated size of result pkt */
	unsigned long returned; /* Returned size of result pkt */
};

struct dp_outrec_struct {
	unsigned long status;
	long count;
	int dbregno;	/* the debug reg used for this watchpoint */
	struct dp_point_struct point;
};

/*
 * flags
 *
 * Initially a probe record starts out with zero flags.
 *
 * DP_REC_STATUS_COMPILED: A probe record is compiled, after successfully
 * validating the rpn code.
 *
 * DP_REC_STATUS_ACTIVE: A probe record becomes valid after it is verified
 * that the opcode as specified in the probe program matches with the one
 * at the specified location. We would probably do this verification the
 * first time this probe is inserted.
 *
 * DP_REC_STATUS_MISMATCH: This means the opcode specified by the probe
 * does not match with the one present when we try to insert it.
 *
 * DP_REC_STATUS_DISABLED: A probe will be disabled after being executed
 * for max-hit number of times automatically.
 *
 * DP_REC_STATUS_REMOVED: The status of a probe will be removed if user
 * explicitly removes it using dprobe --remove --major-minor command.
 * Note that if a specific probe is removed by its major-minor, we
 * don't remove the dp_record_struct from memory as it is too much
 * work. We simply note that fact in the status flags and leave the
 * dp_record_struct alone. But, if an entire probe program is removed,
 * all its structures will be removed permanently.
 *
 * DP_REC_STATUS_INVALID_OFFSET: Set if the address specified for probe insertion
 * is out of bounds for that executable module. 
 *
 * DP_REC_STATUS_DEBUGREG_UNAVAIL: Set if the probe is of type watchpoint and
 * no free debug registers are free for use.
 * 
 * DP_REC_STATUS_WATCHPOINT_LEN_INVALID: Set if the probe is of type watchpoint
 * and the range of address specified is not valid.
 */
#define DP_REC_STATUS_COMPILED  		0x00000001
#define DP_REC_STATUS_ACTIVE			0x00000002
#define DP_REC_STATUS_MISMATCH			0x00000004
#define DP_REC_STATUS_DISABLED  		0x00000008
#define DP_REC_STATUS_REMOVED   		0x00000010
#define DP_REC_STATUS_INVALID_OFFSET		0x00000020
#define DP_REC_STATUS_DEBUGREG_UNAVAIL		0x00000040
#define DP_REC_STATUS_WATCHPOINT_LEN_INVALID	0x00000080
#define DP_REC_STATUS_DEBUGREG_PATCH_NEEDED	0x00000100
#define DP_REC_STATUS_KPROBE_ERR		0x00000200
#define DP_REC_STATUS_EXCLUDED			0x00000400
#define DP_REC_ARCH_FLAGS			0xff000000

struct dp_outmod_struct {
	struct dp_outmod_struct *next;  /* link */
	unsigned long flags; 	/* cmdline switches */
	struct dp_outrec_struct * rec; 	/* array of dp_outrec_structs. */
	unsigned long * lv;	/* local variables */
	struct dp_pgm_struct pgm;
	unsigned long base;
	unsigned long end;
};

struct dp_ioctl_arg {
	void * input;
	void * output;
	unsigned long cmd;
};

/* 
 * This should ideally be 2*HEAP_HDR_SIZE, but the latter is 
 *  __KERNEL__  specific. Hence hardcoded here.
 */
#define MIN_HEAP_SIZE	40

#ifdef __KERNEL__

#include <linux/sched.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/mm.h>
#include <linux/kprobes.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>

struct dp_record_struct {
	struct kprobe kp;
	struct uprobe up;
	struct dp_module_struct *mod;
	spinlock_t lock;
	unsigned long status;
	long count;
	int dbregno;	/* the debug reg used for this watchpoint */
	struct dp_point_struct point;
};

/* 
 * When Dprobes stores it's log in a trace buffer, the log data is preceded
 * by the header information as in this structure, followed by optional 
 * header elements.
 */
#define DP_TRACE_HDR_ID 1
struct dp_trace_hdr_struct {
	unsigned short facility_id;
	unsigned short len;
	unsigned long mask;
	unsigned short major;
	unsigned short minor;
};

#ifdef CONFIG_SMP
#define MIN_ST_SIZE	sizeof(struct dp_trace_hdr_struct) + sizeof(int)
#else
#define MIN_ST_SIZE	sizeof(struct dp_trace_hdr_struct)
#endif

/* Major number 0 is reserved for stack trace log record */
#define DP_ST_MAJOR	0x0000
#define DP_ST_MINOR	0x0000

/*
 * This is the data structure that we maintain in kernel for each module
 * that has any probes applied on it.
 */
struct dp_module_struct {
	struct dp_module_struct *next;  /* link */
	unsigned long flags;
	struct dp_record_struct * rec; 	/* array of dp_record_structs. */
	unsigned long * lv;	/* local variables */
	struct inode * inode; 	/* for quicker access to inode */
	int trace_id;		/* ltt trace event id */
	struct dp_trace_hdr_struct hdr;
	struct dp_pgm_struct pgm;
	unsigned long base; /* used to store kmod base address */
	unsigned long end; /* used to store kmod base address */
	struct nameidata nd; /* to hold path/dentry etc. */
	struct address_space_operations * ori_a_ops;
	struct address_space_operations dp_a_ops;
};

/* flags used to keep track of allocations */
#define DP_MOD_FLAGS_LARGE_REC	0x01

#ifdef CONFIG_SMP
extern struct dprobes_struct dprobes_set[];
#define dprobes dprobes_set[smp_processor_id()]
#else 
extern struct dprobes_struct dprobes;
#define dprobes_set (&dprobes)
#endif /* CONFIG_SMP */

/*extern char _stext, _etext;  kernel start and end */

/*
 * global variables stuff
 */
extern unsigned long *dp_gv;
extern unsigned long dp_num_gv;
extern rwlock_t dp_gv_lock;

extern byte_t *dp_heap;
extern unsigned long dp_num_heap;
extern rwlock_t dp_heap_lock;

extern int dp_insmod(struct module *kmod);
extern int dp_remmod(struct module *kmod);
extern int dp_readpage(struct file *, struct page *);
extern int dp_writepage(struct page *page);

#define IS_COW_PAGE(page, inode) (!(page->mapping) || \
	(page->mapping->host != (void *)inode)) 

#include <linux/list.h>
struct heap_hdr {
	byte_t *addr;
	unsigned char flags;
	unsigned long size;
	struct list_head list;
};

#define HEAP_HDR_SIZE	sizeof(struct heap_hdr)
#define HEAP_FREE	1
#define	HEAP_ALLOCATED	2

#include <asm/dprobes.h>

#endif /* __KERNEL__ */

#endif
