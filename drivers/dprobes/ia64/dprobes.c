/*
 * IBM Dynamic Probes
 * Copyright (c) International Business Machines Corp., 2000
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/dprobes.h>
#include <linux/vmalloc.h>
#include <linux/serial_reg.h>
#include <linux/kprobes.h>
#include <linux/module.h>

#include <asm/uaccess.h>
#include <asm/io.h>

void dprobes_asm_code_start(void) { }

/*
 * Format strings for printing out the log.
 */
#define DP_LOG_HDR_FMT		"dprobes(%d,%d) "
#define DP_LOG_CPU_FMT		"cpu=%d "
#define DP_LOG_PROCNAME_FMT	"name=%s "
#define DP_LOG_PID_FMT		"pid=%d "
#define DP_LOG_UID_FMT		"uid=%d "
#define DP_LOG_TSC_FMT		"tsc=%08lx:%08lx "
#define DP_LOG_NEWLINE_FMT	"\n"
#define DP_LOG_DATA_FMT		"%x "

static void log_stack_trace(struct dprobes_struct *dp)
{
	unsigned long i;
	byte_t *ex_log;
	unsigned char *tmp = dp->log_hdr;
	unsigned long ex_log_len = dp->ex_log_len;

	if (ex_log_len > MIN_ST_SIZE) {
		tmp += sprintf(tmp, DP_LOG_HDR_FMT, 0, 0);

#ifdef CONFIG_SMP
		tmp += sprintf(tmp, DP_LOG_CPU_FMT, smp_processor_id());
#endif
		printk("%s ", dp->log_hdr);

		ex_log_len -= MIN_ST_SIZE;
		ex_log = dp->ex_log + MIN_ST_SIZE;
		for (i = 0; i < ex_log_len; i++)
			printk(DP_LOG_DATA_FMT, ex_log[i]);
		printk(DP_LOG_NEWLINE_FMT);
	}
}

static void fmt_log_hdr(struct dprobes_struct *dp)
{
	unsigned char *tmp = dp->log_hdr;
	unsigned long log_flags = dp->mod->pgm.flags & DP_LOG_MASK;

	tmp += sprintf(tmp, DP_LOG_HDR_FMT, dp->major, dp->minor);

#ifdef CONFIG_SMP
	tmp += sprintf(tmp, DP_LOG_CPU_FMT, smp_processor_id());
#endif
	if (log_flags & DP_LOG_PID)
		tmp += sprintf(tmp, DP_LOG_PID_FMT, current->pid);
	if (log_flags & DP_LOG_UID)
		tmp += sprintf(tmp, DP_LOG_UID_FMT, current->uid);
	if (log_flags & DP_LOG_TSC) {
		struct timeval ts;
		do_gettimeofday(&ts);
		tmp += sprintf(tmp, DP_LOG_TSC_FMT, ts.tv_sec, ts.tv_usec);
	}
	if (log_flags & DP_LOG_PROCNAME)
		tmp += sprintf(tmp, DP_LOG_PROCNAME_FMT, current->comm);
	tmp += sprintf(tmp, DP_LOG_NEWLINE_FMT);

	tmp += sprintf(tmp, DP_LOG_HDR_FMT, dp->major, dp->minor);
	*tmp = '\0';

	return;
}

/*
 * Saves (writes) the log to the standard kernel log.
 */
static int log_to_klog(struct dprobes_struct *dp)
{
	unsigned long i;
	byte_t *log = dp->log + dp->mod->hdr.len;
	unsigned long log_len = dp->log_len - dp->mod->hdr.len;

	fmt_log_hdr(dp);
	printk("%s ", dp->log_hdr);

	for (i = 0; i < log_len; i++)
		printk(DP_LOG_DATA_FMT, log[i]);
	printk(DP_LOG_NEWLINE_FMT);

	return 0;
}	

/*
 * The order in which the optional header elements is written out has to be the
 * same order in which the DP_HDR_* constants are defined in 
 * include/linux/dprobes.h. The formatter is expected to look for the optional
 * elements in the same order.
 */
static void fmt_log_hdr_raw(struct dprobes_struct *dp)
{
	unsigned char *tmp = dp->log;
	unsigned long mask = dp->mod->hdr.mask;

	dp->mod->hdr.major = dp->major;
	dp->mod->hdr.minor = dp->minor;
	*(struct dp_trace_hdr_struct *)tmp = dp->mod->hdr;	
	tmp += sizeof(dp->mod->hdr);

#ifdef CONFIG_SMP
	*(unsigned int *)tmp = smp_processor_id();
	tmp += sizeof(int);
#endif
	if (mask & DP_HDR_PID) {
		*(pid_t *)tmp = current->pid;
		tmp += sizeof(pid_t);
	}
	if (mask & DP_HDR_UID) {
		*(uid_t *)tmp = current->uid;
		tmp += sizeof(uid_t);
	}
	if (mask & DP_HDR_TSC) {
		struct timeval ts;
		do_gettimeofday(&ts);
		*(struct timeval *)tmp = ts;
		tmp += sizeof(ts);
	}
	if (mask & DP_HDR_PROCNAME) {
		memcpy(tmp, current->comm, 16);
		tmp += 16;
	}
	return;
}

/*
 * dp->mod->hdr is changed here. Hence should be called only after
 * fmt_log_hdr_raw().
 */
static void fmt_st_hdr_raw(struct dprobes_struct *dp)
{
	unsigned char *tmp = dp->ex_log;
	dp->mod->hdr.mask = DP_HDR_MAJOR | DP_HDR_MINOR;
	dp->mod->hdr.major = DP_ST_MAJOR;
	dp->mod->hdr.minor = DP_ST_MINOR;
	dp->mod->hdr.len = MIN_ST_SIZE;
	*(struct dp_trace_hdr_struct *)tmp = dp->mod->hdr;	
	tmp += sizeof(dp->mod->hdr);
#ifdef CONFIG_SMP
	dp->mod->hdr.mask |= DP_HDR_CPU;
	*(unsigned int *)tmp = smp_processor_id();
	tmp += sizeof(int);
	dp->mod->hdr.len += sizeof(int);
#endif
}

static int log_to_ltt(struct dprobes_struct *dp)
{
	printk(KERN_WARNING "dprobes: Linux Trace Toolkit not available.\n");
	return 0;
}

static int log_st_to_ltt(struct dprobes_struct *dp)
{
	printk(KERN_WARNING "dprobes: Linux Trace Toolkit not available.\n");
	return 0;
}

#ifdef CONFIG_EVLOG
#include <linux/evl_log.h>
static int log_to_evl(struct dprobes_struct *dp)
{
	/*
	 * We will need to modify this in future to create a DPROBES facility
	 * type and use dp->id instead of (major,minor) for event type. Even the
	 * priority ought to be something other than LOG_KERN.
	 */
	posix_log_write(LOG_KERN, 
			(dp->major << 16) || dp->minor, /* dp->id */
			LOG_INFO,
			(dp->log + dp->mod->hdr.len),
			(dp->log_len - dp->mod->hdr.len),
			POSIX_LOG_BINARY,
			0);
	return 0;
}
#else
static int log_to_evl(struct dprobes_struct *dp)
{
	printk(KERN_WARNING "dprobes: POSIX Event Logging not available.\n");
	return 0;
}
#endif

#define BOTH_EMPTY_TR 	(UART_LSR_TEMT | UART_LSR_THRE)
#define COM1_ADDR	0x3f8
#define COM2_ADDR	0x2f8

/*
 * Wait for the transmitter buffer to become empty.
 */  
static inline void wait_for_xmitr_empty(int port)
{
	int lsr;
	unsigned int tmout = 1000000;
	do {
               	lsr = inb(port + UART_LSR);
               	if (--tmout == 0) break;
       	} while ((lsr & BOTH_EMPTY_TR) != BOTH_EMPTY_TR);
}

/*
 * This routine sends the raw binary data to com port. The receiving
 * end should be capable of handling raw bytes.
 */  
static int log_to_com_port(struct dprobes_struct *dp, int port)
{
	int i, ier;
	byte_t *log = dp->log;
	byte_t *ex_log = dp->ex_log;
	unsigned long log_len = dp->log_len;
	unsigned long ex_log_len = dp->ex_log_len;
	
	/* store UART configuration and program it in polled mode */
	ier = inb(port + UART_IER);	
	outb(0x00, port + UART_IER);

	/* send the log data to com port */
	fmt_log_hdr_raw(dp);
	for (i = 0; i < log_len; i++) {
		wait_for_xmitr_empty(port);
		outb(log[i], port + UART_TX);
	}
	
	/* send CR and LF characters */
	wait_for_xmitr_empty(port);
	outb(0x0d, port + UART_TX);
	wait_for_xmitr_empty(port);
	outb(0x0a, port + UART_TX);

	/* send the stack trace data(if present) to com port */
	if (ex_log_len > MIN_ST_SIZE) {
		fmt_st_hdr_raw(dp);
		for (i = 0; i < ex_log_len; i++) {
			wait_for_xmitr_empty(port);
			outb(ex_log[i], port + UART_TX);
		}

		/* send CR and LF characters */
		wait_for_xmitr_empty(port);
		outb(0x0d, port + UART_TX);
		wait_for_xmitr_empty(port);
		outb(0x0a, port + UART_TX);
	}	

	/* restore the configuration of UART */
	wait_for_xmitr_empty(port);
	outb(ier, port + UART_IER);
	return 0;
}

static inline void save_log(struct dprobes_struct *dp)
{
	switch(dp->mod->pgm.flags & DP_LOG_TARGET_MASK) {
	case DP_LOG_TARGET_COM1:
		log_to_com_port(dp, COM1_ADDR);
		break;
	case DP_LOG_TARGET_COM2:
		log_to_com_port(dp, COM2_ADDR);
		break;
	case DP_LOG_TARGET_LTT:
		fmt_log_hdr_raw(dp);
		log_to_ltt(dp);
		if (dp->ex_log_len > MIN_ST_SIZE) {
			fmt_st_hdr_raw(dp);
			log_st_to_ltt(dp);
		}
		break;
	case DP_LOG_TARGET_EVL:
		log_to_evl(dp);
		break;
	default:
		log_to_klog(dp);
		log_stack_trace(dp);
		break;
	}
	return;
}

/*
 * Exits to the selected facility after the original instruction is
 * successfully executed.
 */
void dp_post_handler(struct kprobe *kp, struct pt_regs *regs, unsigned long flags)
{
	struct dprobes_struct *dp = &dprobes;
	dp->rec->count++;
	if (dp->rec->count >= dp->rec->point.maxhits) {
		dp->rec->status &= ~DP_REC_STATUS_ACTIVE;
	       	dp->rec->status |= DP_REC_STATUS_DISABLED;
	}
	if (dp->status & DP_STATUS_ABORT)
		return;
	save_log(dp);
	return;
}

/* TODO - Implement watchpoint probes */ 
inline void delete_watchpoint(unsigned int cond, struct pt_regs *regs, struct dp_record_struct *rec) 
{
	return;
}

static inline void set_rf(struct dp_record_struct *rec, struct pt_regs *regs) 
{
	return; 
}

inline int dr_trap_type(unsigned int cond) 
{
	return 0;
}

static void dp_pre_handler(struct kprobe *kp, struct pt_regs *regs, struct switch_stack *sw)
{
	register struct dprobes_struct *dp = &dprobes;
	struct dp_record_struct *rec = container_of(kp, typeof(*rec), kp);


	if (rec->status & (DP_REC_STATUS_DISABLED | DP_REC_STATUS_REMOVED)) {
		return;
       	}

	dp->rec = rec;
	dp->probe_addr = (unsigned long) rec->kp.addr;

	dp->regs = regs;
	dp->mod = rec->mod;
	dp->major = rec->point.major;
	dp->minor = rec->point.minor;
	dp->sw = sw;
	if (user_mode(regs)) {
		dp->status |= DP_USER_PROBE;
		dp->uregs = regs;
	} else {
		dp->status |= DP_KERNEL_PROBE;
		dp->uregs = ia64_task_regs(current);
	}
	dp->status |= DP_STATUS_FIRSTFPU;

	/* rec->count > 0 for passcount */
	if (rec->status & DP_REC_STATUS_ACTIVE && rec->count >= 0) {
		dp_interpreter();
		if (rec->status & (DP_REC_STATUS_DISABLED | DP_REC_STATUS_REMOVED)) {
			if (rec->status & DP_REC_STATUS_ACTIVE)
				rec->status &= ~DP_REC_STATUS_ACTIVE;
				rec->status |= DP_REC_STATUS_DISABLED;

		}
	} else {
		dp->status |= DP_STATUS_ABORT;
	}
	dp->status |= DP_STATUS_SS;
	return;
}

/*
 *
 * Return values 0 means that this fault is NOT handled by us. Any other
 * return value indicated otherwise.
 */
int dp_fault_handler(struct kprobe *kp, struct pt_regs * regs, int trapnr)
{
	register struct dprobes_struct *dp = &dprobes;
	struct exception_table_entry *fix;

	if (dp->status & DP_STATUS_INTERPRETER) {
		dp->status |= DP_STATUS_GPF;
		/* Are we prepared to handle this kernel fault?  */
		fix = (struct exception_table_entry *)search_exception_tables((unsigned long) (regs->cr_iip + ia64_psr(regs)->ri));
		if (fix->cont) {
			ia64_handle_exception(regs, fix);
			return 1;
		}

		/*
		 * Major problem. We are in the interpreter, got a page fault
		 * and don't have a local exception handler. Let the default
		 * fault handler take effect, which will oops.
		 */
		return 0;
	}
	else if (dp->status & DP_STATUS_SS) {
		if (dp->rec->point.logonfault) {
			dp->rec->count++;
			save_log(dp);
		}
	}
	return 0;
}

int dr_alloc(int regnum, int flag) 
{
	return -1;
}

int dr_free(int regnum)
{
	return -1;
}

static inline int remove_wp(byte_t *addr, struct dp_record_struct *rec)
{
	return 0;
}

static inline int insert_wp(byte_t *addr, struct dp_record_struct *rec,
	struct dp_module_struct *m, struct page * page)
{
	rec->status |= DP_REC_STATUS_DEBUGREG_PATCH_NEEDED;
	return 0;
}

/* 
 * Writing the breakpoint instruction should be the last thing done
 * in this function to ensure that all the structures that
 * are needed to locate the probe are in place before a probe can
 * be hit.
 */
static inline int insert_bp(byte_t *addr, struct dp_record_struct *rec,
	struct dp_module_struct *m, struct page * page)
{
	
	if (!(m->pgm.flags & DP_MODTYPE_USER) &&
		!(((unsigned long)addr) <= m->base && 
		((unsigned long)addr) < (m->end + m->base))) {
		rec->status |= DP_REC_STATUS_INVALID_OFFSET;
		return -1;
	}
	if (*addr != rec->point.opcode)  {
		if (m->pgm.flags & DP_DONT_VERIFY_OPCODES) {
			rec->point.opcode = *addr;
		} else {
			if (!(m->pgm.flags & DP_MODTYPE_USER) || 
			    !IS_COW_PAGE(page, m->inode)) {
				rec->point.actual_opcode = *addr;
				rec->status |= DP_REC_STATUS_MISMATCH;
			}
			return -1;
		}
	}

	/* all okay, register the probe */
	rec->kp.addr = (kprobe_opcode_t *) addr;
	rec->kp.pre_handler = dp_pre_handler;
	rec->kp.post_handler = dp_post_handler;
	rec->kp.fault_handler = dp_fault_handler;
	
	if (register_kprobe(&rec->kp)) {
		rec->status |= DP_REC_STATUS_KPROBE_ERR;
		return -1;
	}
	rec->status |= DP_REC_STATUS_ACTIVE;
	return 0;
}

int __insert_probe(byte_t *addr, struct dp_record_struct *rec, 
	struct dp_module_struct *m, struct page * page)
{ 
	if (rec->point.probe & DP_PROBE_BREAKPOINT) {
		return insert_bp(addr, rec, m, page);
	}
	else {
		return insert_wp(addr, rec, m, page);
	}
}

static inline void remove_bp(byte_t *addr, struct dp_record_struct *rec)
{
	unregister_kprobe(&rec->kp);
}

int __remove_probe(byte_t * addr, struct dp_record_struct *rec)
{
	if (rec->point.probe & DP_PROBE_BREAKPOINT)
		remove_bp(addr, rec);
	else 
		remove_wp(addr, rec);
	return 0;
}

inline int insert_probe_userspace(byte_t *addr, struct dp_record_struct *rec,
	struct page *page, struct vm_area_struct *vma) { return ENOSYS; }
inline int remove_probe_userspace(byte_t *addr, struct dp_record_struct *rec,
	struct page *page, struct vm_area_struct *vma) { return ENOSYS; }
int register_userspace_probes(struct dp_record_struct *rec) {return ENOSYS; }
void unregister_userspace_probes(struct dp_record_struct *rec) { }

void dprobes_asm_code_end(void) { }
