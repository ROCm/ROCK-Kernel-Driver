/*
 * RAS Instrumentation hooks.
 *
 * Most of these hooks are for Linux Trace Toolkit. They may also be
 * used by any other tool.
 * 
 * Author: Vamsi Krishna S. <vamsi_krishna@in.ibm.com>
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/hook.h>
#include <linux/trigevent_hooks.h>
#include <linux/ptrace.h>

DECLARE_TRIG_EVENT(ipc_call_hook);
DECLARE_TRIG_EVENT(ipc_msg_create_hook);
DECLARE_TRIG_EVENT(ipc_sem_create_hook);
DECLARE_TRIG_EVENT(ipc_shm_create_hook);

DECLARE_TRIG_EVENT(irq_entry_hook);
DECLARE_TRIG_EVENT(irq_exit_hook);

DECLARE_TRIG_EVENT(kernel_timer_hook);

DECLARE_TRIG_EVENT(kthread_hook);
DECLARE_TRIG_EVENT(exec_hook);
DECLARE_TRIG_EVENT(fork_hook);
DECLARE_TRIG_EVENT(process_exit_hook);
DECLARE_TRIG_EVENT(process_wait_hook);
DECLARE_TRIG_EVENT(process_wakeup_hook);
DECLARE_TRIG_EVENT(sched_switch_hook);
DECLARE_TRIG_EVENT(sched_dispatch_hook);
DECLARE_TRIG_EVENT(signal_hook);

DECLARE_TRIG_EVENT(open_hook);
DECLARE_TRIG_EVENT(close_hook);
DECLARE_TRIG_EVENT(llseek_hook);
DECLARE_TRIG_EVENT(lseek_hook);
DECLARE_TRIG_EVENT(ioctl_hook);
DECLARE_TRIG_EVENT(poll_hook);
DECLARE_TRIG_EVENT(select_hook);
DECLARE_TRIG_EVENT(read_hook);
DECLARE_TRIG_EVENT(write_hook);
DECLARE_TRIG_EVENT(buf_wait_end_hook);
DECLARE_TRIG_EVENT(buf_wait_start_hook);


DECLARE_TRIG_EVENT(mmap_hook);
DECLARE_TRIG_EVENT(mm_page_alloc_hook);
DECLARE_TRIG_EVENT(mm_page_free_hook);
DECLARE_TRIG_EVENT(mm_swap_in_hook);
DECLARE_TRIG_EVENT(mm_swap_out_hook);
DECLARE_TRIG_EVENT(page_wait_end_hook);
DECLARE_TRIG_EVENT(page_wait_start_hook);

DECLARE_TRIG_EVENT(net_pkt_in_hook);
DECLARE_TRIG_EVENT(net_pkt_out_hook);

DECLARE_TRIG_EVENT(sk_call_hook);
DECLARE_TRIG_EVENT(sk_create_hook);
DECLARE_TRIG_EVENT(sk_receive_hook);
DECLARE_TRIG_EVENT(sk_send_hook);

DECLARE_TRIG_EVENT(softirq_hook);
DECLARE_TRIG_EVENT(tasklet_action_hook);
DECLARE_TRIG_EVENT(tasklet_hi_action_hook);
DECLARE_TRIG_EVENT(bh_hook);

DECLARE_TRIG_EVENT(timer_expired_hook);
DECLARE_TRIG_EVENT(setitimer_hook);
DECLARE_TRIG_EVENT(settimeout_hook);

DECLARE_TRIG_EVENT(trap_entry_hook);
DECLARE_TRIG_EVENT(trap_exit_hook);

DECLARE_TRIG_EVENT(timer_hook);

#ifdef CONFIG_MODULES
DECLARE_TRIG_EVENT(module_init_hook);
DECLARE_TRIG_EVENT(module_init_failed_hook);
DECLARE_TRIG_EVENT(free_module_hook);
#endif

#ifdef CONFIG_TRIGEVENT_SYSCALL_HOOK
DECLARE_TRIG_EVENT(pre_syscall_hook);
DECLARE_TRIG_EVENT(post_syscall_hook);
/* 
 * as syscalls are very sensitive, we have extra steps to enable 
 * pre/post syscall hooks. Besides registering and arming hooks
 * one has to call enable/disable_pre/post_syscall_hook().
 */
int pre_syscall_enabled;
int post_syscall_enabled;

void enable_pre_syscall(void)
{
	pre_syscall_enabled = 1;
}
EXPORT_SYMBOL(enable_pre_syscall);

void enable_post_syscall(void)
{
	post_syscall_enabled = 1;
}
EXPORT_SYMBOL(enable_post_syscall);

void disable_pre_syscall(void)
{
	pre_syscall_enabled = 0;
}
EXPORT_SYMBOL(disable_pre_syscall);

void disable_post_syscall(void)
{
	post_syscall_enabled = 0;
}
EXPORT_SYMBOL(disable_post_syscall);

asmlinkage void pre_syscall(struct pt_regs * regs)
{
	TRIG_EVENT(pre_syscall_hook, regs);
}

asmlinkage void post_syscall(void)
{
	TRIG_EVENT(post_syscall_hook);
}
#endif /* CONFIG_TRIGEVENT_SYSCALL_HOOK */
