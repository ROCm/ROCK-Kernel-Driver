#ifndef __LINUX_TRIGEVENT_HOOKS_H
#define __LINUX_TRIGEVENT_HOOKS_H
/*
 * Kernel Hooks Interface.
 * 
 * Authors: Richard J Moore <richardj_moore@uk.ibm.com>
 *	    Vamsi Krishna S. <vamsi_krishna@in.ibm.com>
 */
#include <linux/hook.h>
#include <linux/module.h>

#ifdef CONFIG_TRIGEVENT_HOOKS
#define TRIG_EVENT(name, args...) GENERIC_HOOK(name , ##args)
#define DECLARE_TRIG_EVENT(name)	DECLARE_GENERIC_HOOK(name)

USE_HOOK(ipc_call_hook);
USE_HOOK(ipc_msg_create_hook);
USE_HOOK(ipc_sem_create_hook);
USE_HOOK(ipc_shm_create_hook);

USE_HOOK(irq_entry_hook);
USE_HOOK(irq_exit_hook);

USE_HOOK(kernel_timer_hook);

USE_HOOK(kthread_hook);
USE_HOOK(exec_hook);
USE_HOOK(fork_hook);
USE_HOOK(process_exit_hook);
USE_HOOK(process_wait_hook);
USE_HOOK(process_wakeup_hook);
USE_HOOK(sched_switch_hook);
USE_HOOK(sched_dispatch_hook);
USE_HOOK(signal_hook);

USE_HOOK(open_hook);
USE_HOOK(close_hook);
USE_HOOK(llseek_hook);
USE_HOOK(lseek_hook);
USE_HOOK(ioctl_hook);
USE_HOOK(poll_hook);
USE_HOOK(select_hook);
USE_HOOK(read_hook);
USE_HOOK(write_hook);
USE_HOOK(buf_wait_end_hook);
USE_HOOK(buf_wait_start_hook);

USE_HOOK(mmap_hook);
USE_HOOK(mm_page_alloc_hook);
USE_HOOK(mm_page_free_hook);
USE_HOOK(mm_swap_in_hook);
USE_HOOK(mm_swap_out_hook);
USE_HOOK(page_wait_end_hook);
USE_HOOK(page_wait_start_hook);

USE_HOOK(net_pkt_in_hook);
USE_HOOK(net_pkt_out_hook);

USE_HOOK(sk_call_hook);
USE_HOOK(sk_create_hook);
USE_HOOK(sk_receive_hook);
USE_HOOK(sk_send_hook);

USE_HOOK(softirq_hook);
USE_HOOK(tasklet_action_hook);
USE_HOOK(tasklet_hi_action_hook);
USE_HOOK(bh_hook);

USE_HOOK(timer_expired_hook);
USE_HOOK(setitimer_hook);
USE_HOOK(settimeout_hook);

USE_HOOK(trap_entry_hook);
USE_HOOK(trap_exit_hook);

USE_HOOK(timer_hook);

#ifdef CONFIG_MODULES
USE_HOOK(module_init_hook);
USE_HOOK(module_init_failed_hook);
USE_HOOK(free_module_hook);
#endif

#ifdef CONFIG_TRIGEVENT_SYSCALL_HOOK
USE_HOOK(pre_syscall_hook);
USE_HOOK(post_syscall_hook);

extern int pre_syscall_enabled;
extern int post_syscall_enabled;

extern void enable_pre_syscall(void);
extern void enable_post_syscall(void);
extern void disable_pre_syscall(void);
extern void disable_post_syscall(void);
#endif /* CONFIG_TRIGEVENT_SYSCALL_HOOK */
#else
#define TRIG_EVENT(name, ...)
#endif /* CONFIG_TRIGEVENT_HOOKS */

/* this needs to be done properly */
#ifdef __s390__
typedef uint64_t trapid_t;
#endif

#endif /* __LINUX_TRIGEVENT_HOOKS_H */
