/*
 * drivers/trace/trace_hooks.c
 *
 * This file registers and arms all the instrumentation hooks that are
 * required by Linux Trace Toolkit. It also contains the hook exit
 * routines.
 *
 * Author: Vamsi Krishna S. (vamsi_krishna@in.ibm.com)
 *         The hook exit routines are based on LTT sources.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/ptrace.h>
#include <linux/bitops.h>
#include <linux/trigevent_hooks.h>
#include <linux/ltt.h>

#include <asm/io.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>


/*
 * Register hook exits. LTT maintains a bit in sTracedEvents for
 * the status of a set of similar events for eg: TRACE_IPC for
 * a number of IPC events etc. We need to arm and disarm all
 * the hooks that correspond to a given bit in sTracedEvents.
 */

/*
 * Helper macro to declare hook record structs. It takes as an argument 
 * name of the hook. If hook_name is passed in, it declares hook_rec
 * ltt_hook_name_rec and initialises the exit routine to
 * ltt_hook_name.
 */
#define DECLARE_HOOK_REC(name) \
static struct hook_rec ltt_##name##_rec = { \
	.hook_exit      = ltt_##name, \
};

/*
 * Helper macros to register/unregister/arm/disarm hooks.
 */
#define ltt_hook_register(name) \
	hook_exit_register(&name, &ltt_##name##_rec)
#define ltt_hook_unregister(name) \
	hook_exit_deregister(&ltt_##name##_rec)
#define ltt_hook_arm(name) \
	hook_exit_arm(&ltt_##name##_rec)
#define ltt_hook_remove(name) \
	hook_exit_disarm(&ltt_##name##_rec); \
	hook_exit_deregister(&ltt_##name##_rec)

#ifdef CONFIG_TRIGEVENT_SYSCALL_HOOK
/* TRACE_SYSCALL_ENTRY */
extern void ltt_pre_syscall(struct pt_regs *);
static void ltt_pre_syscall_hook(struct hook *h, struct pt_regs *regs)
{
	ltt_pre_syscall(regs);
}
DECLARE_HOOK_REC(pre_syscall_hook);

static int enable_pre_syscall_hooks(void)
{
	int rc;

	ltt_pre_syscall_hook_rec.hook_exit_name = "pre_syscall";
	if ((rc = ltt_hook_register(pre_syscall_hook)))
		return rc;
	
	ltt_hook_arm(pre_syscall_hook);
	enable_pre_syscall();
	return rc;
}

static int disable_pre_syscall_hooks(void)
{
	disable_pre_syscall();
	ltt_hook_remove(pre_syscall_hook);
	return 0;
}

/* TRACE_SYSCALL_EXIT */
static void ltt_post_syscall_hook(struct hook *h, struct pt_regs *regs)
{
	ltt_log_event(TRACE_EV_SYSCALL_EXIT, NULL);
}
DECLARE_HOOK_REC(post_syscall_hook);

static int enable_post_syscall_hooks(void)
{
	int rc;

	ltt_post_syscall_hook_rec.hook_exit_name = "post_syscall";
	if ((rc = ltt_hook_register(post_syscall_hook)))
		return rc;
	
	ltt_hook_arm(post_syscall_hook);
	enable_post_syscall();
	return rc;
}

static int disable_post_syscall_hooks(void)
{
	disable_post_syscall();
	ltt_hook_remove(post_syscall_hook);
	return 0;
}
#endif
/* TRACE_TRAP_ENTRY */
static void ltt_trap_entry_hook(struct hook *h, int trapnr, unsigned long eip, struct pt_regs *regs)
{
	TRACE_TRAP_ENTRY(trapnr, eip);
}
DECLARE_HOOK_REC(trap_entry_hook);

static int enable_trap_entry_hooks(void)
{
	int rc;

	ltt_trap_entry_hook_rec.hook_exit_name = "trap_entry";
	if ((rc = ltt_hook_register(trap_entry_hook)))
		return rc;
	
	ltt_hook_arm(trap_entry_hook);
	return rc;
}

static int disable_trap_entry_hooks(void)
{
	ltt_hook_remove(trap_entry_hook);
	return 0;
}

/* TRACE_TRAP_EXIT */
static void ltt_trap_exit_hook(struct hook *h)
{
	TRACE_TRAP_EXIT();
}

DECLARE_HOOK_REC(trap_exit_hook);

static int enable_trap_exit_hooks(void)
{
	int rc;
	
	ltt_trap_exit_hook_rec.hook_exit_name = "trap_exit";
	if ((rc = ltt_hook_register(trap_exit_hook)))
		return rc;
	
	ltt_hook_arm(trap_exit_hook);
	return rc;
}

static int disable_trap_exit_hooks(void)
{
	ltt_hook_remove(trap_exit_hook);
	return 0;
}

/* TRACE_IRQ_ENTRY */
static void ltt_irq_entry_hook(struct hook *h, unsigned int irq, struct pt_regs *regs, int irq_in_kernel)
{
	TRACE_IRQ_ENTRY(irq, irq_in_kernel);
}

DECLARE_HOOK_REC(irq_entry_hook);

static int enable_irq_entry_hooks(void)
{
	int rc;

	ltt_irq_entry_hook_rec.hook_exit_name = "irq_entry";
	if ((rc = ltt_hook_register(irq_entry_hook)))
		return rc;
	
	ltt_hook_arm(irq_entry_hook);
	return rc;
}

static int disable_irq_entry_hooks(void)
{
	ltt_hook_remove(irq_entry_hook);
	return 0;
}

/* TRACE_IRQ_EXIT */
static void ltt_irq_exit_hook(struct hook *h)
{
	TRACE_IRQ_EXIT();
}

DECLARE_HOOK_REC(irq_exit_hook);

static int enable_irq_exit_hooks(void)
{
	int rc;

	ltt_irq_exit_hook_rec.hook_exit_name = "irq_exit";
	if ((rc = ltt_hook_register(irq_exit_hook)))
		return rc;
	
	ltt_hook_arm(irq_exit_hook);
	return rc;
}

static int disable_irq_exit_hooks(void)
{
	ltt_hook_remove(irq_exit_hook);
	return 0;
}

/* TRACE_SCHEDCHANGE */
static void ltt_sched_switch_hook(struct hook *h, struct task_struct *prev, struct task_struct *next)
{
	TRACE_SCHEDCHANGE(prev, next);
}

DECLARE_HOOK_REC(sched_switch_hook);

static int enable_schedchange_hooks(void)
{
	int rc;
	
	ltt_sched_switch_hook_rec.hook_exit_name = "sched_switch";
	if ((rc = ltt_hook_register(sched_switch_hook)))
		return rc;
	
	ltt_hook_arm(sched_switch_hook);
	return rc;
}

static int disable_schedchange_hooks(void)
{
	ltt_hook_remove(sched_switch_hook);
	return 0;
}


/* TRACE_KERNEL_TIMER */
static void ltt_kernel_timer_hook(struct hook *h, unsigned long nr)
{
	TRACE_EVENT(TRACE_EV_KERNEL_TIMER, NULL);
}

DECLARE_HOOK_REC(kernel_timer_hook);

static int enable_kernel_timer_hooks(void)
{
	int rc;

	ltt_kernel_timer_hook_rec.hook_exit_name = "kernel_timer";
	if ((rc = ltt_hook_register(kernel_timer_hook)))
		return rc;
	
	ltt_hook_arm(kernel_timer_hook);
	return rc;
}

static int disable_kernel_timer_hooks(void)
{
	ltt_hook_remove(kernel_timer_hook);
	return 0;
}

static void ltt_softirq_hook(struct hook *h, int index)
{
	TRACE_SOFT_IRQ(TRACE_EV_SOFT_IRQ_SOFT_IRQ, index);
}
static void ltt_tasklet_action_hook(struct hook *h, unsigned long fn)
{
	TRACE_SOFT_IRQ(TRACE_EV_SOFT_IRQ_TASKLET_ACTION, fn);
}
static void ltt_tasklet_hi_action_hook(struct hook *h, unsigned long fn)
{
	TRACE_SOFT_IRQ(TRACE_EV_SOFT_IRQ_TASKLET_HI_ACTION, fn);
}

DECLARE_HOOK_REC(softirq_hook);
DECLARE_HOOK_REC(tasklet_action_hook);
DECLARE_HOOK_REC(tasklet_hi_action_hook);

static int enable_softirq_hooks(void)
{
	int rc;

	
	ltt_softirq_hook_rec.hook_exit_name = "ltt_softirq";
	if ((rc = ltt_hook_register(softirq_hook)))
		goto err;
	ltt_tasklet_action_hook_rec.hook_exit_name = "tasklet_action";
	if ((rc = ltt_hook_register(tasklet_action_hook)))
		goto err1;
	ltt_tasklet_hi_action_hook_rec.hook_exit_name = "tasklet_hi_action";
	if ((rc = ltt_hook_register(tasklet_hi_action_hook)))
		goto err2;
	
	ltt_hook_arm(softirq_hook);
	ltt_hook_arm(tasklet_action_hook);
	ltt_hook_arm(tasklet_hi_action_hook);
	return rc;

err2:	ltt_hook_unregister(tasklet_action_hook);
err1:	ltt_hook_unregister(softirq_hook);
err:	return rc;

}

static int disable_softirq_hooks(void)
{
	ltt_hook_remove(softirq_hook);
	ltt_hook_remove(tasklet_action_hook);
	ltt_hook_remove(tasklet_hi_action_hook);
	return 0;
}

/* TRACE_PROCESS */
static void ltt_kthread_hook(struct hook *h, unsigned int ret, unsigned int fn)
{
	TRACE_PROCESS(TRACE_EV_PROCESS_KTHREAD, ret, fn);
}
static void ltt_process_exit_hook(struct hook *h, pid_t pid)
{
	TRACE_PROCESS_EXIT(0, 0);
}
static void ltt_process_wait_hook(struct hook *h, pid_t pid)
{
	TRACE_PROCESS(TRACE_EV_PROCESS_WAIT, pid, 0);
}
static void ltt_fork_hook(struct hook *h, unsigned long clone_flags, struct task_struct *p, int ret)
{
	TRACE_PROCESS(TRACE_EV_PROCESS_FORK, ret, 0);
}
static void ltt_process_wakeup_hook(struct hook *h, pid_t pid, unsigned long state)
{
	TRACE_PROCESS(TRACE_EV_PROCESS_WAKEUP, pid, state);
}
static void ltt_signal_hook(struct hook *h, int sig, pid_t pid)
{
	TRACE_PROCESS(TRACE_EV_PROCESS_SIGNAL, sig, pid);
}

DECLARE_HOOK_REC(kthread_hook);
DECLARE_HOOK_REC(process_exit_hook);
DECLARE_HOOK_REC(process_wait_hook);
DECLARE_HOOK_REC(fork_hook);
DECLARE_HOOK_REC(process_wakeup_hook);
DECLARE_HOOK_REC(signal_hook);

static int enable_process_hooks(void)
{
	int rc;

	ltt_kthread_hook_rec.hook_exit_name = "ltt_kthread";
	if ((rc = ltt_hook_register(kthread_hook)))
		goto err;
	ltt_process_exit_hook_rec.hook_exit_name = "ltt_process_exit";
	if ((rc = ltt_hook_register(process_exit_hook)))
		goto err1;
	ltt_process_wait_hook_rec.hook_exit_name = "ltt_process_wait";
	if ((rc = ltt_hook_register(process_wait_hook)))
		goto err2;
	ltt_fork_hook_rec.hook_exit_name = "ltt_fork";
	if ((rc = ltt_hook_register(fork_hook)))
		goto err3;
	ltt_signal_hook_rec.hook_exit_name = "ltt_signal";
	if ((rc = ltt_hook_register(signal_hook)))
		goto err4;
	ltt_process_wakeup_hook_rec.hook_exit_name = "ltt_process_wakeup";
	if ((rc = ltt_hook_register(process_wakeup_hook)))
		goto err5;
	
	ltt_hook_arm(kthread_hook);
	ltt_hook_arm(process_exit_hook);
	ltt_hook_arm(process_wait_hook);
	ltt_hook_arm(fork_hook);
	ltt_hook_arm(signal_hook);
	ltt_hook_arm(process_wakeup_hook);
	return rc;

err5:	ltt_hook_unregister(signal_hook);
err4:	ltt_hook_unregister(fork_hook);
err3:	ltt_hook_unregister(process_wait_hook);
err2:	ltt_hook_unregister(process_exit_hook);
err1:	ltt_hook_unregister(kthread_hook);
err:	return rc;

}

static int disable_process_hooks(void)
{
	ltt_hook_remove(kthread_hook);
	ltt_hook_remove(process_exit_hook);
	ltt_hook_remove(process_wait_hook);
	ltt_hook_remove(fork_hook);
	ltt_hook_remove(signal_hook);
	ltt_hook_remove(process_wakeup_hook);
	return 0;
}

/* TRACE_FILE_SYSTEM */
static void ltt_buf_wait_start_hook(struct hook *h, struct buffer_head *bh)
{
	TRACE_FILE_SYSTEM(TRACE_EV_FILE_SYSTEM_BUF_WAIT_START, 0, 0, NULL);
}
static void ltt_buf_wait_end_hook(struct hook *h, struct buffer_head *bh)
{
	TRACE_FILE_SYSTEM(TRACE_EV_FILE_SYSTEM_BUF_WAIT_END, 0, 0, NULL);
}
static void ltt_exec_hook(struct hook *h, int len, char *name, struct pt_regs *regs)
{
	TRACE_FILE_SYSTEM(TRACE_EV_FILE_SYSTEM_EXEC, 0, len, name);
}
static void ltt_ioctl_hook(struct hook *h, unsigned int fd, unsigned int cmd)
{
	TRACE_FILE_SYSTEM(TRACE_EV_FILE_SYSTEM_IOCTL, fd, cmd, NULL);
}
static void ltt_open_hook(struct hook *h, unsigned int fd, unsigned int len, char *name)
{
	TRACE_FILE_SYSTEM(TRACE_EV_FILE_SYSTEM_OPEN, fd, len, name);
}
static void ltt_close_hook(struct hook *h, unsigned int fd)
{
	TRACE_FILE_SYSTEM(TRACE_EV_FILE_SYSTEM_CLOSE, fd, 0, NULL);
}
static void ltt_lseek_hook(struct hook *h, unsigned int fd, off_t offset)
{
	TRACE_FILE_SYSTEM(TRACE_EV_FILE_SYSTEM_SEEK, fd, offset, NULL);
}
static void ltt_llseek_hook(struct hook *h, unsigned int fd, loff_t offset)
{
	TRACE_FILE_SYSTEM(TRACE_EV_FILE_SYSTEM_SEEK, fd, offset, NULL);
}
static void ltt_read_hook(struct hook *h, unsigned int fd, size_t count)
{
	TRACE_FILE_SYSTEM(TRACE_EV_FILE_SYSTEM_READ, fd, count, NULL);
}
static void ltt_write_hook(struct hook *h, unsigned int fd, size_t count)
{
	TRACE_FILE_SYSTEM(TRACE_EV_FILE_SYSTEM_WRITE, fd, count, NULL);
}
static void ltt_select_hook(struct hook *h, unsigned int fd, long timeout)
{
	TRACE_FILE_SYSTEM(TRACE_EV_FILE_SYSTEM_SELECT, fd, timeout, NULL);
}
static void ltt_poll_hook(struct hook *h, unsigned int fd)
{
	TRACE_FILE_SYSTEM(TRACE_EV_FILE_SYSTEM_POLL, fd, 0, NULL);
}

DECLARE_HOOK_REC(buf_wait_start_hook);
DECLARE_HOOK_REC(buf_wait_end_hook);
DECLARE_HOOK_REC(exec_hook);
DECLARE_HOOK_REC(ioctl_hook);
DECLARE_HOOK_REC(open_hook);
DECLARE_HOOK_REC(close_hook);
DECLARE_HOOK_REC(lseek_hook);
DECLARE_HOOK_REC(llseek_hook);
DECLARE_HOOK_REC(read_hook);
DECLARE_HOOK_REC(write_hook);
DECLARE_HOOK_REC(select_hook);
DECLARE_HOOK_REC(poll_hook);

static int enable_fs_hooks(void)
{
	int rc;

	ltt_buf_wait_start_hook_rec.hook_exit_name = "ltt_buf_wait_start";
	if ((rc = ltt_hook_register(buf_wait_start_hook)))
		goto err;
	ltt_buf_wait_end_hook_rec.hook_exit_name = "ltt_buf_wait_end";
	if ((rc = ltt_hook_register(buf_wait_end_hook)))
		goto err1;
	ltt_exec_hook_rec.hook_exit_name = "ltt_exec";
	if ((rc = ltt_hook_register(exec_hook)))
		goto err2;
	ltt_ioctl_hook_rec.hook_exit_name = "ltt_ioctl";
	if ((rc = ltt_hook_register(ioctl_hook)))
		goto err3;
	ltt_open_hook_rec.hook_exit_name = "ltt_open";
	if ((rc = ltt_hook_register(open_hook)))
		goto err4;
	ltt_close_hook_rec.hook_exit_name = "ltt_close";
	if ((rc = ltt_hook_register(close_hook)))
		goto err5;
	ltt_lseek_hook_rec.hook_exit_name = "ltt_lseek";
	if ((rc = ltt_hook_register(lseek_hook)))
		goto err6;
	ltt_llseek_hook_rec.hook_exit_name = "ltt_llseek";
	if ((rc = ltt_hook_register(llseek_hook)))
		goto err7;
	ltt_read_hook_rec.hook_exit_name = "ltt_read";
	if ((rc = ltt_hook_register(read_hook)))
		goto err8;
	ltt_write_hook_rec.hook_exit_name = "ltt_write";
	if ((rc = ltt_hook_register(write_hook)))
		goto err9;
	ltt_select_hook_rec.hook_exit_name = "ltt_select";
	if ((rc = ltt_hook_register(select_hook)))
		goto err10;
	ltt_poll_hook_rec.hook_exit_name = "ltt_poll";
	if ((rc = ltt_hook_register(poll_hook)))
		goto err11;
	
	ltt_hook_arm(buf_wait_start_hook);
	ltt_hook_arm(buf_wait_end_hook);
	ltt_hook_arm(exec_hook);
	ltt_hook_arm(ioctl_hook);
	ltt_hook_arm(open_hook);
	ltt_hook_arm(close_hook);
	ltt_hook_arm(lseek_hook);
	ltt_hook_arm(llseek_hook);
	ltt_hook_arm(read_hook);
	ltt_hook_arm(write_hook);
	ltt_hook_arm(select_hook);
	ltt_hook_arm(poll_hook);
	return rc;

err11:	ltt_hook_unregister(select_hook);
err10:	ltt_hook_unregister(write_hook);
err9:	ltt_hook_unregister(read_hook);
err8:	ltt_hook_unregister(llseek_hook);
err7:	ltt_hook_unregister(lseek_hook);
err6:	ltt_hook_unregister(close_hook);
err5:	ltt_hook_unregister(open_hook);
err4:	ltt_hook_unregister(ioctl_hook);
err3:	ltt_hook_unregister(exec_hook);
err2:	ltt_hook_unregister(buf_wait_end_hook);
err1:	ltt_hook_unregister(buf_wait_start_hook);
err:	return rc;

}

static int disable_fs_hooks(void)
{
	ltt_hook_remove(buf_wait_start_hook);
	ltt_hook_remove(buf_wait_end_hook);
	ltt_hook_remove(exec_hook);
	ltt_hook_remove(ioctl_hook);
	ltt_hook_remove(open_hook);
	ltt_hook_remove(close_hook);
	ltt_hook_remove(lseek_hook);
	ltt_hook_remove(llseek_hook);
	ltt_hook_remove(read_hook);
	ltt_hook_remove(write_hook);
	ltt_hook_remove(select_hook);
	ltt_hook_remove(poll_hook);
	return 0;
}

/* TRACE_TIMER */
static void ltt_timer_expired_hook(struct hook *h, struct task_struct *p)
{
	TRACE_TIMER(TRACE_EV_TIMER_EXPIRED, 0, 0, 0);
}
static void ltt_setitimer_hook(struct hook *h, int which, unsigned long interval, unsigned long value)
{
	TRACE_TIMER(TRACE_EV_TIMER_SETITIMER, which, interval, value);
}
static void ltt_settimeout_hook(struct hook *h, unsigned long timeout)
{
	TRACE_TIMER(TRACE_EV_TIMER_SETTIMEOUT, 0, timeout, 0);
}

DECLARE_HOOK_REC(setitimer_hook);
DECLARE_HOOK_REC(timer_expired_hook);
DECLARE_HOOK_REC(settimeout_hook);

static int enable_timer_hooks(void)
{
	int rc;

	ltt_setitimer_hook_rec.hook_exit_name = "ltt_setitimer";
	if ((rc = ltt_hook_register(setitimer_hook)))
		goto err;
	ltt_timer_expired_hook_rec.hook_exit_name = "ltt_timer_expired";
	if ((rc = ltt_hook_register(timer_expired_hook)))
		goto err1;
	ltt_settimeout_hook_rec.hook_exit_name = "ltt_settimeout";
	if ((rc = ltt_hook_register(settimeout_hook)))
		goto err2;
	
	ltt_hook_arm(setitimer_hook);
	ltt_hook_arm(timer_expired_hook);
	ltt_hook_arm(settimeout_hook);
	return rc;

err2:	ltt_hook_unregister(timer_expired_hook);
err1:	ltt_hook_unregister(setitimer_hook);
err:	return rc;

}

static int disable_timer_hooks(void)
{
	ltt_hook_remove(setitimer_hook);
	ltt_hook_remove(timer_expired_hook);
	ltt_hook_remove(settimeout_hook);
	return 0;
}


/* TRACE_MEMORY */
static void ltt_mm_page_alloc_hook(struct hook *h, unsigned int order)
{
	TRACE_MEMORY(TRACE_EV_MEMORY_PAGE_ALLOC, order);
}
static void ltt_mm_page_free_hook(struct hook *h, unsigned int order)
{
	TRACE_MEMORY(TRACE_EV_MEMORY_PAGE_FREE, order);
}
static void ltt_mm_swap_in_hook(struct hook *h, unsigned long address)
{
	TRACE_MEMORY(TRACE_EV_MEMORY_SWAP_IN, address);
}
static void ltt_mm_swap_out_hook(struct hook *h, struct page *page)
{
	TRACE_MEMORY(TRACE_EV_MEMORY_SWAP_OUT, ((unsigned long) page));
}
static void ltt_page_wait_start_hook(struct hook *h, struct page *page)
{
	TRACE_MEMORY(TRACE_EV_MEMORY_PAGE_WAIT_START, 0);
}
static void ltt_page_wait_end_hook(struct hook *h, struct page *page)
{
	TRACE_MEMORY(TRACE_EV_MEMORY_PAGE_WAIT_END, 0);
}

DECLARE_HOOK_REC(page_wait_start_hook);
DECLARE_HOOK_REC(page_wait_end_hook);
DECLARE_HOOK_REC(mm_page_free_hook);
DECLARE_HOOK_REC(mm_page_alloc_hook);
DECLARE_HOOK_REC(mm_swap_in_hook);
DECLARE_HOOK_REC(mm_swap_out_hook);

static int enable_mm_hooks(void)
{
	int rc;

	ltt_page_wait_start_hook_rec.hook_exit_name = "ltt_page_wait_start";
	if ((rc = ltt_hook_register(page_wait_start_hook)))
		goto err;
	ltt_page_wait_end_hook_rec.hook_exit_name = "ltt_page_wait_end";
	if ((rc = ltt_hook_register(page_wait_end_hook)))
		goto err1;
	ltt_mm_page_free_hook_rec.hook_exit_name = "ltt_mm_page_free";
	if ((rc = ltt_hook_register(mm_page_free_hook)))
		goto err2;
	ltt_mm_page_alloc_hook_rec.hook_exit_name = "ltt_mm_page_alloc";
	if ((rc = ltt_hook_register(mm_page_alloc_hook)))
		goto err3;
	ltt_mm_swap_in_hook_rec.hook_exit_name = "ltt_mm_swap_in";
	if ((rc = ltt_hook_register(mm_swap_in_hook)))
		goto err4;
	ltt_mm_swap_out_hook_rec.hook_exit_name = "ltt_mm_swap_out";
	if ((rc = ltt_hook_register(mm_swap_out_hook)))
		goto err5;
	
	ltt_hook_arm(page_wait_start_hook);
	ltt_hook_arm(page_wait_end_hook);
	ltt_hook_arm(mm_page_free_hook);
	ltt_hook_arm(mm_page_alloc_hook);
	ltt_hook_arm(mm_swap_in_hook);
	ltt_hook_arm(mm_swap_out_hook);
	return rc;

err5:	ltt_hook_unregister(mm_swap_in_hook);
err4:	ltt_hook_unregister(mm_page_alloc_hook);
err3:	ltt_hook_unregister(mm_page_free_hook);
err2:	ltt_hook_unregister(page_wait_end_hook);
err1:	ltt_hook_unregister(page_wait_start_hook);
err:	return rc;

}

static int disable_mm_hooks(void)
{
	ltt_hook_remove(page_wait_start_hook);
	ltt_hook_remove(page_wait_end_hook);
	ltt_hook_remove(mm_page_free_hook);
	ltt_hook_remove(mm_page_alloc_hook);
	ltt_hook_remove(mm_swap_in_hook);
	ltt_hook_remove(mm_swap_out_hook);
	return 0;
}
/* TRACE_SOCKET */
static void ltt_sk_send_hook(struct hook *h, int type, int size)
{
	TRACE_SOCKET(TRACE_EV_SOCKET_SEND, type, size);
}

static void ltt_sk_receive_hook(struct hook *h, int type, int size)
{
	TRACE_SOCKET(TRACE_EV_SOCKET_RECEIVE, type, size);
}

static void ltt_sk_create_hook(struct hook *h, int retval, int type)
{
	TRACE_SOCKET(TRACE_EV_SOCKET_CREATE, retval, type);
}

static void ltt_sk_call_hook(struct hook *h, int call, unsigned long a0)
{
	TRACE_SOCKET(TRACE_EV_SOCKET_CALL, call, a0);
}

DECLARE_HOOK_REC(sk_send_hook);
DECLARE_HOOK_REC(sk_receive_hook);
DECLARE_HOOK_REC(sk_create_hook);
DECLARE_HOOK_REC(sk_call_hook);

static int enable_socket_hooks(void)
{
	int rc;

	ltt_sk_send_hook_rec.hook_exit_name = "ltt_sk_send";
	if ((rc = ltt_hook_register(sk_send_hook)))
		goto err;
	ltt_sk_receive_hook_rec.hook_exit_name = "ltt_sk_receive";
	if ((rc = ltt_hook_register(sk_receive_hook)))
		goto err1;
	ltt_sk_create_hook_rec.hook_exit_name = "ltt_sk_create";
	if ((rc = ltt_hook_register(sk_create_hook)))
		goto err2;
	ltt_sk_call_hook_rec.hook_exit_name = "ltt_sk_call";
	if ((rc = ltt_hook_register(sk_call_hook)))
		goto err3;
	
	ltt_hook_arm(sk_send_hook);
	ltt_hook_arm(sk_receive_hook);
	ltt_hook_arm(sk_create_hook);
	ltt_hook_arm(sk_call_hook);
	return rc;

err3:	ltt_hook_unregister(sk_create_hook);
err2:	ltt_hook_unregister(sk_receive_hook);
err1:	ltt_hook_unregister(sk_send_hook);
err:	return rc;

}

static int disable_socket_hooks(void)
{
	ltt_hook_remove(sk_send_hook);
	ltt_hook_remove(sk_receive_hook);
	ltt_hook_remove(sk_create_hook);
	ltt_hook_remove(sk_call_hook);
	return 0;
}

/* TRACE_IPC */
static void ltt_ipc_call_hook(struct hook *h, unsigned int call, int first)
{
	TRACE_IPC(TRACE_EV_IPC_CALL, call, first);
}
static void ltt_ipc_msg_create_hook(struct hook *h, int err, int flag)
{
	TRACE_IPC(TRACE_EV_IPC_MSG_CREATE, err, flag);
}
static void ltt_ipc_sem_create_hook(struct hook *h, int err, int flag)
{
	TRACE_IPC(TRACE_EV_IPC_SEM_CREATE, err, flag);
}
static void ltt_ipc_shm_create_hook(struct hook *h, int err, int flag)
{
	TRACE_IPC(TRACE_EV_IPC_SHM_CREATE, err, flag);
}

DECLARE_HOOK_REC(ipc_call_hook);
DECLARE_HOOK_REC(ipc_msg_create_hook);
DECLARE_HOOK_REC(ipc_sem_create_hook);
DECLARE_HOOK_REC(ipc_shm_create_hook);

static int enable_ipc_hooks(void)
{
	int rc;

	ltt_ipc_call_hook_rec.hook_exit_name = "ltt_ipc_call";
	if ((rc = ltt_hook_register(ipc_call_hook)))
		goto err;
	ltt_ipc_msg_create_hook_rec.hook_exit_name = "ltt_ipc_msg_create";
	if ((rc = ltt_hook_register(ipc_msg_create_hook)))
		goto err1;
	ltt_ipc_sem_create_hook_rec.hook_exit_name = "ltt_ipc_sem_create";
	if ((rc = ltt_hook_register(ipc_sem_create_hook)))
		goto err2;
	ltt_ipc_shm_create_hook_rec.hook_exit_name = "ltt_ipc_shm_create";
	if ((rc = ltt_hook_register(ipc_shm_create_hook)))
		goto err3;
	
	ltt_hook_arm(ipc_call_hook);
	ltt_hook_arm(ipc_msg_create_hook);
	ltt_hook_arm(ipc_sem_create_hook);
	ltt_hook_arm(ipc_shm_create_hook);
	return rc;

err3:	ltt_hook_unregister(ipc_sem_create_hook);
err2:	ltt_hook_unregister(ipc_msg_create_hook);
err1:	ltt_hook_unregister(ipc_call_hook);
err:	return rc;

}

static int disable_ipc_hooks(void)
{
	ltt_hook_remove(ipc_call_hook);
	ltt_hook_remove(ipc_msg_create_hook);
	ltt_hook_remove(ipc_sem_create_hook);
	ltt_hook_remove(ipc_shm_create_hook);
	return 0;
}

/* TRACE_NETWORK */
static void ltt_net_pkt_out_hook(struct hook *h, unsigned short protocol)
{
	TRACE_NETWORK(TRACE_EV_NETWORK_PACKET_OUT, protocol);
}

static void ltt_net_pkt_in_hook(struct hook *h, unsigned short protocol)
{
	TRACE_NETWORK(TRACE_EV_NETWORK_PACKET_IN, protocol);
}

DECLARE_HOOK_REC(net_pkt_out_hook);
DECLARE_HOOK_REC(net_pkt_in_hook);

static int enable_net_hooks(void)
{
	int rc;

	ltt_net_pkt_out_hook_rec.hook_exit_name = "ltt_net_pkt_out";
	if ((rc = ltt_hook_register(net_pkt_out_hook)))
		goto err;
	ltt_net_pkt_in_hook_rec.hook_exit_name = "ltt_net_pkt_in";
	if ((rc = ltt_hook_register(net_pkt_in_hook)))
		goto err1;
	
	ltt_hook_arm(net_pkt_out_hook);
	ltt_hook_arm(net_pkt_in_hook);
	return rc;

err1:	ltt_hook_unregister(net_pkt_out_hook);
err:	return rc;

}

static int disable_net_hooks(void)
{
	ltt_hook_remove(net_pkt_out_hook);
	ltt_hook_remove(net_pkt_in_hook);
	return 0;
}

typedef int (*enable_fn_t)(void);

static enable_fn_t enable[TRACE_EV_MAX + 1][2] =
{
	{ NULL, NULL},							/* TRACE_START */
#ifdef CONFIG_TRIGEVENT_SYSCALL_HOOK
	{ disable_pre_syscall_hooks, enable_pre_syscall_hooks},		/* TRACE_SYSCALL_ENTRY */
	{ disable_post_syscall_hooks, enable_post_syscall_hooks},	/* TRACE_SYSCALL_EXIT */
#endif
	{ disable_trap_entry_hooks, enable_trap_entry_hooks},		/* TRACE_TRAP_ENTRY */
	{ disable_trap_exit_hooks, enable_trap_exit_hooks},		/* TRACE_TRAP_EXIT */
	{ disable_irq_entry_hooks, enable_irq_entry_hooks},		/* TRACE_IRQ_ENTRY */
	{ disable_irq_exit_hooks, enable_irq_exit_hooks},		/* TRACE_IRQ_EXIT */
	{ disable_schedchange_hooks, enable_schedchange_hooks},		/* TRACE_SCHEDCHANGE */
	{ disable_kernel_timer_hooks, enable_kernel_timer_hooks},	/* TRACE_KERNEL_TIMER */
	{ disable_softirq_hooks, enable_softirq_hooks},			/* TRACE_SOFT_IRQ */
	{ disable_process_hooks, enable_process_hooks},			/* TRACE_PROCESS */
	{ disable_fs_hooks, enable_fs_hooks},				/* TRACE_FILE_SYSTEM */
	{ disable_timer_hooks, enable_timer_hooks},			/* TRACE_MEMORY */
	{ disable_mm_hooks, enable_mm_hooks},				/* TRACE_MEMORY */
	{ disable_socket_hooks, enable_socket_hooks},			/* TRACE_SOCKET */
	{ disable_ipc_hooks, enable_ipc_hooks},				/* TRACE_IPC */
	{ disable_net_hooks, enable_net_hooks},				/* TRACE_NETWORK */
	{ NULL, NULL},							/* TRACE_BUFFER_START */
	{ NULL, NULL},							/* TRACE_BUFFER_END */
	{ NULL, NULL},							/* TRACE_NEW_EVENT */
	{ NULL, NULL},							/* TRACE_CUSTOM */
	{ NULL, NULL}							/* TRACE_CHANGE_MASK */
};

static trace_event_mask prev_mask;
void change_traced_events(trace_event_mask *mask)
{
	int i = 0;
	enable_fn_t fn;
	
	trace_event_mask changes;	/* zero bit indicates change */

	if (!mask) {
		/* disable all existing hooks, tracing is being stopped */
		changes = ~prev_mask;
		memset(&prev_mask, 0, sizeof(prev_mask));
		mask = &prev_mask;
	} else {
		changes = ~(prev_mask ^ (*mask));
		memcpy(&prev_mask, mask, sizeof(*mask));
	}

repeat:
	i = find_next_zero_bit((const unsigned long *)&changes, sizeof(changes), i);
	if (i <= TRACE_EV_MAX) {
		if (test_bit(i, (const unsigned long *)mask)) {
			fn = enable[i][1];
		} else {
			fn = enable[i][0];
		}
		if (fn) {
			fn();
		}
		i++;
		goto repeat;
	}

#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT)
	/*
	 * Wait to ensure that no other processor (or process, for
	 * CONFIG_PREEMPT) could possibly be inside any of our hook
	 * exit functions.
	 *
	 * sychornize_kernel is available on UL kernels. We need an
	 * equivalent on other 2.4 kernels.
	 */
	synchronize_kernel();
#endif
	return;
}
