/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2004 Silicon Graphics, Inc.  All Rights Reserved.
 */


/*
 * Cross Partition Communication (XPC) support - standard version.
 *
 *	XPC provides a message passing capability that crosses partition
 *	boundaries. This module is made up of two parts:
 *
 *	    partition	This part detects the presence/absence of other
 *			partitions. It provides a heartbeat and monitors
 *			the heartbeats of other partitions.
 *
 *	    channel	This part manages the channels and sends/receives
 *			messages across them to/from other partitions.
 *
 *	There are a couple of additional functions residing in XP, which
 *	provide an interface to XPC for its users (XPMEM and XPNET).
 *
 *
 *	Caveats:
 *
 *	  . We currently have no way to determine which nasid an IPI came
 *	    from. Thus, xpc_IPI_send() does a remote AMO write followed by
 *	    a IPI. The AMO indicates where data is to be pulled from, so
 *	    after the IPI arrives, the remote partition checks the AMO words.
 *	    The IPI can actually arrive before the AMO however, so other code
 *	    must periodically check for this case. Also, remote AMO operations
 *	    do not reliably time out. Thus we do a remote PIO read solely to
 *	    know whether the remote partition is down and whether we should
 *	    stop sending IPIs to it. This remote PIO read operation is set up
 *	    in a special nofault region so SAL knows to ignore (and cleanup)
 *	    any errors due to the remote AMO write, PIO read, and/or PIO 
 *	    write operations.
 *
 *	    If/when new hardware solves this IPI problem, we should abandon
 *	    the current approach.
 *
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <asm/sn/intr.h>
#include <asm/sn/sn_sal.h>
#include <asm/uaccess.h>
#define __KERNEL_SYSCALLS__		/* needed for waitpid() */
#include <asm/unistd.h>			/* needed for waitpid() */
#include "xpc.h"


/* once Linux 2.4 is no longer supported, eliminate these gyrations */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0) && \
    LINUX_VERSION_CODE <  KERNEL_VERSION(2,5,0)

#define IRQ_HANDLED

#define THREAD_INFO  task_struct
#define CURRENT_THREAD_INFO()  current

#define NASID_SLICE_TO_CPUID_ERROR  smp_num_cpus

#define sys_sched_setscheduler sched_setscheduler
static inline _syscall3(long, sched_setscheduler, pid_t, pid,
			int, policy, struct sched_param *, param);

#define DAEMONIZE(name...)  { daemonize(); sprintf(current->comm, name); }
#define DEQUEUE_SIGNAL(task, mask, info)  dequeue_signal(mask, info)

#define cpumask_of_cpu(cpu) \
	({ \
		cpumask_t __cpu_mask = CPU_MASK_NONE; \
		__set_bit(cpu, &__cpu_mask); \
		&__cpu_mask; \
	})
#define cpumask_of_allcpus() \
	({ \
		cpumask_t __cpu_mask = CPU_MASK_ALL; \
		&__cpu_mask; \
	})

#else /* LINUX_VERSION_CODE == Linux 2.4 */

#include <linux/syscalls.h>

#define EXPORT_NO_SYMBOLS

#define THREAD_INFO  thread_info
#define CURRENT_THREAD_INFO  current_thread_info

#define NASID_SLICE_TO_CPUID_ERROR  NR_CPUS

#define DAEMONIZE(name...)  daemonize(name)
#define DEQUEUE_SIGNAL(task, mask, info)  dequeue_signal(task, mask, info)

#define set_migratable_thread()

#define cpumask_of_allcpus() \
	({ \
		cpumask_t __cpu_mask = CPU_MASK_ALL; \
		__cpu_mask; \
	})

#endif /* LINUX_VERSION_CODE == Linux 2.4 */


/* define two DPRINTK dbgtk message buffers */

DECLARE_DPRINTK(xpc_part, 1000, XPC_DBG_P_DEFCAPTURE_SETS,
		XPC_DBG_P_DEFCONSOLE_SETS, XPC_DBG_P_SET_DESCRIPTION);

DECLARE_DPRINTK(xpc_chan, 2000, XPC_DBG_C_DEFCAPTURE_SETS,
		XPC_DBG_C_DEFCONSOLE_SETS, XPC_DBG_C_SET_DESCRIPTION);


/* systune related variables for /proc/sys directories */

static int xpc_hb_min = 1;
static int xpc_hb_max = 10;

static int xpc_hb_check_min = 10;
static int xpc_hb_check_max = 120;

static ctl_table xpc_sys_xpc_hb_dir[] = {
	{
		1,
		"hb_interval",
		&xpc_hb_interval,
		sizeof(int),
		0644,
		NULL,
		&proc_dointvec_minmax,
		&sysctl_intvec,
		NULL,
		&xpc_hb_min, &xpc_hb_max
	},
	{
		2,
		"hb_check_interval",
		&xpc_hb_check_interval,
		sizeof(int),
		0644,
		NULL,
		&proc_dointvec_minmax,
		&sysctl_intvec,
		NULL,
		&xpc_hb_check_min, &xpc_hb_check_max
	},
	{0}
};
static ctl_table xpc_sys_xpc_dir[] = {
	{
		1,
		"hb",
		NULL,
		0,
		0555,
		xpc_sys_xpc_hb_dir
	},
	{0}
};
static ctl_table xpc_sys_dir[] = {
	{
		1,
		"xpc",
		NULL,
		0,
		0555,
		xpc_sys_xpc_dir
	},
	{0}
};
static struct ctl_table_header *xpc_sysctl;


/* #of IRQs received */
static atomic_t xpc_act_IRQ_rcvd;

/* IRQ handler notifies this wait queue on receipt of an IRQ */
static DECLARE_WAIT_QUEUE_HEAD(xpc_act_IRQ_wq);

volatile static unsigned long xpc_hb_check_timeout;

/* xpc_hb_checker thread exited notification */
static DECLARE_MUTEX_LOCKED(xpc_hb_checker_exited);

/* xpc_discovery thread exited notification */
static DECLARE_MUTEX_LOCKED(xpc_discovery_exited);


static struct timer_list xpc_hb_timer;


static void xpc_kthread_waitmsgs(xpc_partition_t *, xpc_channel_t *);


// >>> XPC is going to try to keep all activity for a given partition
// >>> constrained to a single CPU. And if we allocate all structures for
// >>> that partition from that CPU we will have memory affinity. We should
// >>> minimize the pulling of cachelines from one CPU to another.
// >>>
// >>> This will require that heartbeat portion of XPC create a partition/CPU
// >>> mapping for each of the other partitions with respect to CPUs in this
// >>> partition.  For performance it would be nice if each partition could get
// >>> a unique CPU, but the enabled hardware may not allow for that. The
// >>> heartbeat code will ensure that when it calls xpc_partition_up() for a
// >>> given partition that it is running on the CPU mapped to that partition.
// >>> This will allow XPC to allocate data structures for that partition that
// >>> will always have affinity when subsequently referenced from that same
// >>> CPU. When XPC interrupts another partition it will interrupt the
// >>> mapped CPU.  We will need to decide whether the kernel threads that
// >>> the IRQ handler will wakeup should be pinned to the same CPU as the IRQ
// >>> handler.
// >>>
// >>> This scheme will allow XPC to run in parallel for different partitions,
// >>> but all channels within a partition will be serialized. Is this a good
// >>> or a bad thing?


// >>> Make sure that all waits/sleeps/blocks are interruptible!


/*
 * Notify the heartbeat check thread that an IRQ has been received.
 */
static irqreturn_t
xpc_act_IRQ_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	atomic_inc(&xpc_act_IRQ_rcvd);
	wake_up_interruptible(&xpc_act_IRQ_wq);
	return IRQ_HANDLED;
}


/*
 * Timer to produce the heartbeat.  The timer structures function is
 * already set when this is initially called.  A tunable is used to
 * specify when the next timeout should occur.
 */
static void
xpc_hb_beater(unsigned long dummy)
{
	xpc_vars->heartbeat++;

	if (XPC_TICKS >= xpc_hb_check_timeout) {
		wake_up_interruptible(&xpc_act_IRQ_wq);
	}

	xpc_hb_timer.expires = XPC_TICKS +
					(xpc_hb_interval * XPC_TICKS_PER_SEC);
	add_timer(&xpc_hb_timer);
}


/*
 * This thread is responsible for nearly all of the partition
 * activation/deactivation.
 */
static int
xpc_hb_checker(void *ignore)
{
	int last_IRQ_count = 0;
	int new_IRQ_count;
	int force_IRQ=0;


	/* this thread was marked active by xpc_hb_init() */

	DAEMONIZE(XPC_HB_CHECK_THREAD_NAME);

	set_user_nice(current, 19);

	spin_lock_irq(&current->sighand->siglock);
	/*
	 * The following allows SIGCHLD to be delivered.  Without it,
	 * SIGCHLD gets dropped on the floor.
	 */
	current->sighand->action[SIGCHLD-1].sa.sa_handler = SIG_IGN;
	siginitsetinv(&current->blocked, sigmask(SIGCHLD) | sigmask(SIGHUP));
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);


	set_cpus_allowed(current, cpumask_of_cpu(XPC_HB_CHECK_CPU));

	xpc_hb_check_timeout = XPC_TICKS +
				(xpc_hb_check_interval * XPC_TICKS_PER_SEC);

	while (!xpc_exiting) {

		/* wait for IRQ or timeout */

		(void) wait_event_interruptible(xpc_act_IRQ_wq,
			    (last_IRQ_count < atomic_read(&xpc_act_IRQ_rcvd) ||
					XPC_TICKS >= xpc_hb_check_timeout ||
						xpc_exiting));

		DPRINTK(xpc_part, XPC_DBG_P_HEARTBEATV,
			"woke up with %d ticks rem; %d IRQs have been "
			"received\n", (int) (xpc_hb_check_timeout - XPC_TICKS),
			atomic_read(&xpc_act_IRQ_rcvd) - last_IRQ_count);


		while (signal_pending(current)) {
			unsigned long sig;
			siginfo_t sig_info;

			spin_lock_irq(&current->sighand->siglock);
			sig = DEQUEUE_SIGNAL(current, &current->blocked,
								&sig_info);
			spin_unlock_irq(&current->sighand->siglock);

			DPRINTK(xpc_part, XPC_DBG_P_HEARTBEAT,
				"received signal %lu\n", sig);

			if (sig == SIGCHLD) {
				while((waitpid(-1, NULL, WNOHANG)) > 0) {
					/* empty */
				}
					
			}
			/* SIGHUP is ignored, all other signals are blocked */
		}


		/* checking of remote heartbeats is skewed by IRQ handling */
		if (XPC_TICKS >= xpc_hb_check_timeout) {
			DPRINTK(xpc_part, XPC_DBG_P_HEARTBEATV,
				"checking remote heartbeats\n");
			xpc_check_remote_hb();

			/*
			 * We need to periodically recheck to ensure no
			 * IPI/AMO pairs have been missed.  That check
			 * must always reset xpc_hb_check_timeout.
			 */
			force_IRQ = 1;
		}


		new_IRQ_count = atomic_read(&xpc_act_IRQ_rcvd);
		if (last_IRQ_count < new_IRQ_count || force_IRQ != 0) {
			force_IRQ = 0;

			DPRINTK(xpc_part, XPC_DBG_P_HEARTBEATV,
			        "found an IRQ to process; will be resetting "
				"xpc_hb_check_timeout\n");

			last_IRQ_count += xpc_identify_act_IRQ_sender();
			if (last_IRQ_count < new_IRQ_count) {
				/* retry once to help avoid missing AMO */
				(void) xpc_identify_act_IRQ_sender();
			}
			last_IRQ_count = new_IRQ_count;

			xpc_hb_check_timeout = XPC_TICKS +
				   (xpc_hb_check_interval * XPC_TICKS_PER_SEC);
		}
	}

	DPRINTK(xpc_part, XPC_DBG_P_HEARTBEAT,
		"heartbeat checker is exiting\n");


	/* mark this thread as inactive */
	up(&xpc_hb_checker_exited);
	return 0;
}


/*
 * This thread will attempt to discover other partitions to activate
 * based on info provided by SAL. This new thread is short lived and
 * will exit once discovery is complete.
 */
static int
xpc_initiate_discovery(void *ignore)
{
	DAEMONIZE(XPC_DISCOVERY_THREAD_NAME);

	xpc_discovery();

	DPRINTK(xpc_part, XPC_DBG_P_DISCOVERY,
		"discovery thread is exiting\n");

	/* mark this thread as inactive */
	up(&xpc_discovery_exited);
	return 0;
}


/*
 * Orders cpus and attempts a round-robin assignment of cpus. Each
 * time it is called, it will give the next in the order.
 *
 * Ordering starts at compact node ID 1 (second node in the
 * partition) and grabs the second cpu on that node. Next is the
 * second cpu on cnode 2. This continues for all cnodes and then
 * wraps to the first cpu for each cnode.
 *
 * There seems to be some inconsistency in the meaning and usage of
 * the word 'slice'.  Some places equate slice with CPU, others seem to
 * equate it with the Front Side Bus slot. There are two CPUs (numbered 0 & 1)
 * and two FSB slots (numbered 0 & 2) per nasid. The function
 * nasid_slice_to_cpuid() equates slice to the FSB slot.
 */
static int
xpc_next_cpu(void)
{
	static int last_cnodeid = 0;
	static int last_slice = 2;
	int next_cpu;
	nasid_t nasid;


	next_cpu = -1;
	while (next_cpu == -1) {
		if (++last_cnodeid >= numnodes) {
			last_cnodeid = 0;
			last_slice = (last_slice + 2) % 4;
		}

		/* skip headless nodes */
		if (is_headless_node(last_cnodeid)) {
			continue;
		}

		nasid = cnodeid_to_nasid(last_cnodeid);
		next_cpu = nasid_slice_to_cpuid(nasid, last_slice);
		if (next_cpu == NASID_SLICE_TO_CPUID_ERROR) {
			next_cpu = -1;
		}
	}
	return next_cpu;
}


/*
 * Establish first contact with the remote partititon. This involves pulling
 * the XPC per partition variables from the remote partition and waiting for
 * the remote partition to pull ours.
 */
static xpc_t
xpc_make_first_contact(xpc_partition_t *part)
{
	xpc_t ret;


	while ((ret = xpc_pull_remote_vars_part(part)) != xpcSuccess) {
		if (ret != xpcRetry) {
			XPC_DEACTIVATE_PARTITION(part, ret);
			return ret;
		}

		DPRINTK(xpc_chan, XPC_DBG_C_SETUP,
			"waiting to make first contact with partition %d\n",
			XPC_PARTID(part));

		/* wait a 1/4 of a second or so */
		set_current_state(TASK_INTERRUPTIBLE);
		(void) schedule_timeout(0.25 * HZ);

		if (part->act_state == XPC_P_DEACTIVATING) {
			return part->reason;
		}
	}

	return xpc_mark_partition_active(part);
}


/*
 * The first kthread assigned to a newly activated partition is the one
 * created by XPC HB with which it calls xpc_partition_up(). XPC hangs on to
 * that kthread until the partition is brought down, at which time that kthread
 * returns back to XPC HB. (The return of that kthread will signify to XPC HB
 * that XPC has dismantled all communication infrastructure for the associated
 * partition.) This kthread becomes the channel manager for that partition.
 *
 * Each active partition has a channel manager, who, besides connecting and
 * disconnecting channels, will ensure that each of the partition's connected
 * channels has the required number of assigned kthreads to get the work done.
 */
static void
xpc_channel_mgr(xpc_partition_t *part)
{
	unsigned long sig;
	siginfo_t sig_info;


	while (part->act_state != XPC_P_DEACTIVATING ||
				atomic_read(&part->nchannels_active) > 0) {

		xpc_process_channel_activity(part);


		/*
		 * Wait until we've been requested to activate kthreads or
		 * all of the channel's message queues have been torn down or
		 * a signal is pending.
		 * 
		 * The channel_mgr_requests is set to 1 after being awakened,
		 * This is done to prevent the channel mgr from making one pass
		 * through the loop for each request, since he will
		 * be servicing all the requests in one pass. The reason it's
		 * set to 1 instead of 0 is so that other kthreads will know
		 * that the channel mgr is running and won't bother trying to
		 * wake him up.
		 */
		atomic_dec(&part->channel_mgr_requests);
		(void) wait_event_interruptible(part->channel_mgr_wq,
				(atomic_read(&part->channel_mgr_requests) > 0 ||
				part->local_IPI_amo != 0 ||
				(part->act_state == XPC_P_DEACTIVATING &&
				atomic_read(&part->nchannels_active) == 0)));
		atomic_set(&part->channel_mgr_requests, 1);

		// >>> Does it need to wakeup periodically as well? In case we
		// >>> miscalculated the #of kthreads to wakeup or create?


		/*
		 * Reap any children that may have exited.
		 * >>> Re-package the following into a common function that
		 * >>> both xpc_partition and xpc_channel can use?
		 */
		while (signal_pending(current)) {

			spin_lock_irq(&current->sighand->siglock);
			sig = DEQUEUE_SIGNAL(current, &current->blocked,
								&sig_info);
			spin_unlock_irq(&current->sighand->siglock);

			DPRINTK(xpc_chan, XPC_DBG_C_KTHREAD,
				"signal_pending, sig=%ld\n", sig);

			// >>> at the moment we're ignoring all but SIGCHLD

			if (sig == SIGCHLD) {
				while (waitpid(-1, NULL, WNOHANG) > 0) {
					/* nothing more to do */
				}
			}
		}
	}
}


/*
 * When XPC HB determines that a partition has come up, it will create a new
 * kthread and that kthread will call this function to attempt to set up the
 * basic infrastructure used for Cross Partition Communication with the newly
 * upped partition.
 *
 * The kthread that was created by XPC HB and which setup the XPC
 * infrastructure will remain assigned to the partition until the partition
 * goes down. At which time the kthread will teardown the XPC infrastructure
 * and then exit.
 *
 * XPC HB will put the remote partition's XPC per partition specific variables
 * physical address into xpc_partitions[partid].remote_vars_part_pa prior to
 * calling xpc_partition_up().
 */
static void
xpc_partition_up(xpc_partition_t *part)
{
	XP_ASSERT(part->channels == NULL);

	DPRINTK(xpc_chan, XPC_DBG_C_SETUP,
		"activating partition %d\n", XPC_PARTID(part));

	if (xpc_setup_infrastructure(part) != xpcSuccess) {
		return;
	}

	/*
	 * The kthread that XPC HB called us with will become the
	 * channel manager for this partition. It will not return
	 * back to XPC HB until the partition's XPC infrastructure
	 * has been dismantled.
	 */

	/* allow the channel mgr and its children to run on any CPU */
	set_migratable_thread();
	set_cpus_allowed(current, cpumask_of_allcpus());

	(void) XPC_PART_REF(part);	/* this will always succeed */

	if (xpc_make_first_contact(part) == xpcSuccess) {
		xpc_channel_mgr(part);
	}

	XPC_PART_DEREF(part);

	xpc_teardown_infrastructure(part);
}


/*
 *
 */
static int
xpc_activating(void *__partid)
{
	partid_t partid = (u64) __partid;
	xpc_partition_t *part = &xpc_partitions[partid];
	unsigned long irq_flags;
	struct sched_param param = { sched_priority: MAX_USER_RT_PRIO - 1 };
	mm_segment_t saved_addr_limit;
	struct THREAD_INFO *thread_info = CURRENT_THREAD_INFO();


	XP_ASSERT(partid > 0 && partid < MAX_PARTITIONS);


	/* indicate the thread is activating */

	spin_lock_irqsave(&part->act_lock, irq_flags);

	if (part->act_state == XPC_P_DEACTIVATING) {
		part->act_state = XPC_P_INACTIVE;
		spin_unlock_irqrestore(&part->act_lock, irq_flags);
		part->remote_rp_pa = 0;
		return 0;
	}

	XP_ASSERT(part->act_state == XPC_P_ACTIVATION_REQ);
	part->act_state = XPC_P_ACTIVATING;

	XPC_SET_REASON(part, 0, 0);
	spin_unlock_irqrestore(&part->act_lock, irq_flags);


	part->act_cpu = xpc_next_cpu();
	DPRINTK(xpc_part, XPC_DBG_P_ACT,
		"bringing partition %d up; pinning to %d\n",
		partid, part->act_cpu);

	XP_ASSERT(cpu_online(part->act_cpu));

	DAEMONIZE("xpc%02d", partid);

	/*
	 * The XPC tasks need to become realtime tasks to prevent a significant
	 * performance degradation.  We will use the setscheduler system call
	 * for this.
	 *
	 * The changing of addr_limit to KERNEL_DS is necessary to ensure the
	 * kernel sched_param (&param) is recognized as being from kernel
	 * address space and not user.
	 */
	memcpy(&saved_addr_limit, &thread_info->addr_limit,
							sizeof(mm_segment_t));
	thread_info->addr_limit.seg = KERNEL_DS.seg;
	sys_sched_setscheduler(current->pid, SCHED_FIFO, &param);
	memcpy(&thread_info->addr_limit, &saved_addr_limit,
							sizeof(mm_segment_t));

	set_cpus_allowed(current, cpumask_of_cpu(part->act_cpu));

	/*
	 * Register the remote partition's AMOs with SAL so it can handle
	 * and cleanup errors within that address range should the remote
	 * partition go down. We don't unregister this range because it is
	 * difficult to tell when outstanding writes to the remote partition
	 * are finished and thus when it is safe to unregister. This should
	 * not result in wasted space in the SAL xp_addr_region table because
	 * we should get the same page for remote_amos_page_pa after module
	 * reloads and system reboots.
	 */
	if (sn_register_xp_addr_region(part->remote_amos_page_pa,
							PAGE_SIZE, 1) < 0) {
		DPRINTK_ALWAYS(xpc_part, XPC_DBG_P_ACT,
			"xpc_partition_up(%d) failed to register xp_addr "
			"region\n", partid);

		spin_lock_irqsave(&part->act_lock, irq_flags);
		part->act_state = XPC_P_INACTIVE;
		XPC_SET_REASON(part, xpcPhysAddrRegFailed, __LINE__);
		spin_unlock_irqrestore(&part->act_lock, irq_flags);
		part->remote_rp_pa = 0;
		return 0;
	}

	XPC_ALLOW_HB(partid, xpc_vars);
	XPC_IPI_SEND_ACTIVATED(part);


	/*
	 * xpc_partition_up() holds this thread and marks this partition as
	 * XPC_P_ACTIVE by calling xpc_hb_mark_active().
	 */
	(void) xpc_partition_up(part);

	xpc_mark_partition_inactive(part);

	if (part->reason == xpcReactivating) {
		/* interrupting ourselves results in activating partition */
		XPC_IPI_SEND_REACTIVATE(part);
	}

	return 0;
}


/*
 * >>>
 */
void
xpc_activate_partition(xpc_partition_t *part)
{
	partid_t partid = XPC_PARTID(part);
	unsigned long irq_flags;
	pid_t pid;


	spin_lock_irqsave(&part->act_lock, irq_flags);

	pid = kernel_thread(xpc_activating, (void *) ((u64) partid), SIGCHLD);

	XP_ASSERT(part->act_state == XPC_P_INACTIVE);

	if (pid > 0) {
		part->act_state = XPC_P_ACTIVATION_REQ;
		XPC_SET_REASON(part, xpcCloneKThread, __LINE__);
	} else {
		XPC_SET_REASON(part, xpcCloneKThreadFailed, __LINE__);
	}

	spin_unlock_irqrestore(&part->act_lock, irq_flags);
}


/*
 * Handle the receipt of a SGI_XPC_NOTIFY IRQ by seeing whether the specified
 * partition actually sent it. Since SGI_XPC_NOTIFY IRQs may be shared by more
 * than one partition, we use an AMO_t structure per partition to indicate
 * whether a partition has sent an IPI or not.  >>> If it has, then wake up the
 * associated kthread to handle it.
 *
 * All SGI_XPC_NOTIFY IRQs received by XPC are the result of IPIs sent by XPC
 * running on other partitions.
 *
 * Noteworthy Arguments:
 *
 *	irq - Interrupt ReQuest number. NOT USED.
 *
 *	dev_id - partid of IPI's potential sender.
 *
 *	regs - processor's context before the processor entered
 *	       interrupt code. NOT USED.
 */
irqreturn_t
xpc_notify_IRQ_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	partid_t partid = (partid_t) (u64) dev_id;
	xpc_partition_t *part = &xpc_partitions[partid];


	XP_ASSERT(partid > 0 && partid < MAX_PARTITIONS);

	if (XPC_PART_REF(part)) {
		xpc_check_for_channel_activity(part);

		XPC_PART_DEREF(part);
	}
	return IRQ_HANDLED;
}


/*
 * Check to see if xpc_notify_IRQ_handler() dropped any IPIs on the floor
 * because the write to their associated IPI amo completed after the IRQ/IPI
 * was received.
 */
void
xpc_dropped_IPI_check(xpc_partition_t *part)
{
	if (XPC_PART_REF(part)) {
		xpc_check_for_channel_activity(part);

		part->dropped_IPI_timer.expires = XPC_TICKS +
							XPC_P_DROPPED_IPI_WAIT;
		add_timer(&part->dropped_IPI_timer);
		XPC_PART_DEREF(part);
	}
}


void
xpc_activate_kthreads(xpc_channel_t *ch, int needed)
{
	int idle = atomic_read(&ch->kthreads_idle);
	int assigned = atomic_read(&ch->kthreads_assigned);
	int wakeup;


	XP_ASSERT(needed > 0);

	if (idle > 0) {
		wakeup = (needed > idle) ? idle : needed;
		needed -= wakeup;

		DPRINTK(xpc_chan, XPC_DBG_C_KTHREAD,
			"wakeup %d idle kthreads, partid=%d, channel=%d\n",
			wakeup, ch->partid, ch->number);

		/* only wakeup the requested number of kthreads */
		wake_up_nr(&ch->idle_wq, wakeup);
	}

	if (needed <= 0) {
		return;
	}

	if (needed + assigned > ch->kthreads_assigned_limit) {
		needed = ch->kthreads_assigned_limit - assigned;
		// >>>should never be less than 0
		if (needed <= 0) {
			return;
		}
	}

	DPRINTK(xpc_chan, XPC_DBG_C_KTHREAD,
		"create %d new kthreads, partid=%d, channel=%d\n",
		needed, ch->partid, ch->number);

	xpc_create_kthreads(ch, needed);
}


/*
 * This function is where XPC's kthreads wait for messages to deliver.
 */
static void
xpc_kthread_waitmsgs(xpc_partition_t *part, xpc_channel_t *ch)
{
	do {
		/* deliver messages to their intended recipients */

		while (ch->w_local_GP.get < ch->w_remote_GP.put &&
					!(ch->flags & XPC_C_DISCONNECTING)) {
			xpc_deliver_msg(ch);
		}

		if (atomic_inc_return(&ch->kthreads_idle) >
						ch->kthreads_idle_limit) {
			/* too many idle kthreads on this channel */
			atomic_dec(&ch->kthreads_idle);
			break;
		}

		DPRINTK(xpc_chan, XPC_DBG_C_IPI,
			"idle kthread calling "
			"wait_event_exclusive_interruptible()\n");

		(void) wait_event_exclusive_interruptible(ch->idle_wq,
					(ch->w_local_GP.get <
						ch->w_remote_GP.put ||
					(ch->flags & XPC_C_DISCONNECTING)));

		atomic_dec(&ch->kthreads_idle);

	} while (!(ch->flags & XPC_C_DISCONNECTING));
}


static int
xpc_daemonize_kthread(void *args)
{
	partid_t partid = XPC_UNPACK_ARG1(args);
	u16 ch_number = XPC_UNPACK_ARG2(args);
	xpc_partition_t *part = &xpc_partitions[partid];
	xpc_channel_t *ch;
	int n_needed;


	DAEMONIZE("xpc%02dc%d", partid, ch_number);

	DPRINTK(xpc_chan, XPC_DBG_C_KTHREAD,
		"kthread starting, partid=%d, channel=%d\n", partid, ch_number);

	ch = &part->channels[ch_number];

	if (!(ch->flags & XPC_C_DISCONNECTING)) {
		XP_ASSERT(ch->flags & XPC_C_CONNECTED);

		/* let registerer know that connection has been established */

		if (atomic_read(&ch->kthreads_assigned) == 1) {
			xpc_connected_callout(ch);

			/*
			 * It is possible that while the callout was being
			 * made that the remote partition sent some messages.
			 * If that is the case, we may need to activate
			 * additional kthreads to help deliver them. We only
			 * need one less than total #of messages to deliver.
			 */
			n_needed = ch->w_remote_GP.put - ch->w_local_GP.get - 1;
			if (n_needed > 0 &&
					!(ch->flags & XPC_C_DISCONNECTING)) {
				xpc_activate_kthreads(ch, n_needed);
			}
		}

		xpc_kthread_waitmsgs(part, ch);
	}

	if (atomic_dec_return(&ch->kthreads_assigned) == 0 && 
			((ch->flags & XPC_C_CONNECTCALLOUT) ||
				(ch->reason != xpcUnregistering &&
					ch->reason != xpcOtherUnregistering))) {
		xpc_disconnected_callout(ch);
	}


	XPC_MSGQUEUE_DEREF(ch);

	DPRINTK(xpc_chan, XPC_DBG_C_KTHREAD,
		"kthread exiting, partid=%d, channel=%d\n", partid, ch_number);

	XPC_PART_DEREF(part);
	return 0;
}


/*
 * For each partition that XPC has established communications with, there is
 * a minimum of one kernel thread assigned to perform any operation that
 * may potentially sleep or block (basically the callouts to the asynchronous
 * functions registered via xpc_connect()).
 *
 * Additional kthreads are created and destroyed by XPC as the workload
 * demands.
 *
 * A kthread is assigned to one of the active channels that exists for a given
 * partition.
 */
void
xpc_create_kthreads(xpc_channel_t *ch, int needed)
{
	unsigned long irq_flags;
	pid_t pid;
	xpc_args_t args = XPC_PACK_ARGS(ch->partid, ch->number);


	while (needed-- > 0) {
		pid = kernel_thread(xpc_daemonize_kthread,
						(void *) args, SIGCHLD);
		if (pid < 0) {
			/* the fork failed */

			if (atomic_read(&ch->kthreads_assigned) <
						ch->kthreads_idle_limit) {
				/*
				 * Flag this as an error only if we have an
				 * insufficient #of kthreads for the channel
				 * to function.
				 *
				 * No XPC_MSGQUEUE_REF() is needed here since
				 * the channel mgr is doing this.
				 */
				spin_lock_irqsave(&ch->lock, irq_flags);
				XPC_DISCONNECT_CHANNEL(ch, xpcLackOfResources,
								&irq_flags);
				spin_unlock_irqrestore(&ch->lock, irq_flags);
			}
			break;
		}

		/*
		 * The following is done on behalf of the newly created
		 * kthread. That kthread is responsible for doing the
		 * counterpart to the following before it exits.
		 */
		(void) XPC_PART_REF(&xpc_partitions[ch->partid]);
		XPC_MSGQUEUE_REF(ch);
		atomic_inc(&ch->kthreads_assigned);
		ch->kthreads_created++;	// >>> temporary debug only!!!
	}
}


void
xpc_disconnect_wait(int ch_number)
{
	partid_t partid;
	xpc_partition_t *part;
	xpc_channel_t *ch;


	/* now wait for all callouts to the caller's function to cease */
	for (partid = 1; partid < MAX_PARTITIONS; partid++) {
		part = &xpc_partitions[partid];

		if (XPC_PART_REF(part)) {
			ch = &part->channels[ch_number];

// >>> how do we keep from falling into the window between our check and going
// >>> down and coming back up where sema is re-inited?
			if (ch->flags & XPC_C_SETUP) {
				(void) down(&ch->teardown_sema);
			}

			XPC_PART_DEREF(part);
		}
	}
}


void __exit
xpc_exit(void)
{
	partid_t partid;
	int active_part_count;
	xpc_partition_t *part;


	/* now it's time to eliminate our heartbeat */
	del_timer_sync(&xpc_hb_timer);
	xpc_vars->heartbeating_to_mask = 0;

	/* indicate to others that our reserved page is uninitialized */
	xpc_rsvd_page->vars_pa = 0;

	/*
	 * Ignore all incoming interrupts. Without interupts the heartbeat
	 * checker won't activate any new partitions that may come up.
	 */
	free_irq(SGI_XPC_ACTIVATE, NULL);

	/*
	 * Cause the heartbeat checker and the discovery threads to exit.
	 * We don't want them attempting to activate new partitions as we
	 * try to deactivate the existing ones.
	 */
	xpc_exiting = 1;
	wake_up_interruptible(&xpc_act_IRQ_wq);

	/* wait for the heartbeat checker thread to mark itself inactive */
	down(&xpc_hb_checker_exited);

	/* wait for the discovery thread to mark itself inactive */
	down(&xpc_discovery_exited);


	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(0.3 * HZ);
	set_current_state(TASK_RUNNING);


	/* wait for all partitions to become inactive */

	do {
		active_part_count = 0;

		for (partid = 1; partid < MAX_PARTITIONS; partid++) {
			part = &xpc_partitions[partid];
			if (part->act_state != XPC_P_INACTIVE) {
				active_part_count++;

				XPC_DEACTIVATE_PARTITION(part, xpcUnloading);
			}
		}

		if (active_part_count) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(0.3 * HZ);
			set_current_state(TASK_RUNNING);
		}

	} while (active_part_count > 0);


	/* close down protections for IPI operations */
	xpc_restrict_IPI_ops();


	/* clear the interface to XPC's functions */
	xpc_clear_interface();

	if (xpc_sysctl) {
		unregister_sysctl_table(xpc_sysctl);
	}

	XPC_KDB_UNREGISTER();
	UNREG_DPRINTK(xpc_chan);
	UNREG_DPRINTK(xpc_part);
}
module_exit(xpc_exit);


int __init
xpc_init(void)
{
	int ret;
	partid_t partid;
	xpc_partition_t *part;
	pid_t pid;


	XP_ASSERT(L1_CACHE_ALIGNED(xpc_remote_copy_buffer));
	XP_ASSERT(XPC_VARS_ALIGNED_SIZE <= XPC_RSVD_PAGE_ALIGNED_SIZE);

	REG_DPRINTK(xpc_part);
	REG_DPRINTK(xpc_chan);
	XPC_KDB_REGISTER();

	xpc_sysctl = register_sysctl_table(xpc_sys_dir, 1);

	/*
	 * The first few fields of each entry of xpc_partitions[] need to
	 * be initialized now so that calls to xpc_connect() and
	 * xpc_disconnect() can be made prior to the activation of any remote
	 * partition. NOTE THAT NONE OF THE OTHER FIELDS BELONGING TO THESE
	 * ENTRIES ARE MEANINGFUL UNTIL AFTER AN ENTRY'S CORRESPONDING
	 * PARTITION HAS BEEN ACTIVATED.
	 */
	for (partid = 1; partid < MAX_PARTITIONS; partid++) {
		part = &xpc_partitions[partid];

		XP_ASSERT(L1_CACHE_ALIGNED(part));

		part->act_IRQ_rcvd = 0;
		spin_lock_init(&part->act_lock);
		part->act_state = XPC_P_INACTIVE;
		XPC_SET_REASON(part, 0, 0);
		part->setup_state = XPC_P_UNSET;
		init_waitqueue_head(&part->teardown_wq);
		atomic_set(&part->references, 0);
	}

	/*
	 * Open up protections for IPI operations (and AMO operations on
	 * Shub 1.1 systems).
	 */
	xpc_allow_IPI_ops();

	/*
	 * Interrupts being processed will increment this atomic variable and
	 * awaken the heartbeat thread which will process the interrupts.
	 */
	atomic_set(&xpc_act_IRQ_rcvd, 0);

	/*
	 * This is safe to do before the xpc_hb_checker thread has started
	 * because the handler releases a wait queue.  If an interrupt is
	 * received before the thread is waiting, it will not go to sleep,
	 * but rather immediately process the interrupt.
	 */
	ret = request_irq(SGI_XPC_ACTIVATE, xpc_act_IRQ_handler, 0,
							"xpc hb", NULL);
	if (ret != 0) {
		DPRINTK_ALWAYS(xpc_part, (XPC_DBG_P_INIT | XPC_DBG_P_ERROR),
			KERN_ERR "XPC: can't register ACTIVATE IRQ handler, "
			"errno=%d\n", -ret);

		xpc_restrict_IPI_ops();

		if (xpc_sysctl) {
			unregister_sysctl_table(xpc_sysctl);
		}

		XPC_KDB_UNREGISTER();
		UNREG_DPRINTK(xpc_chan);
		UNREG_DPRINTK(xpc_part);
		return -EBUSY;
	}

	/*
	 * Fill the partition reserved page with the information needed by
	 * other partitions to discover we are alive and establish initial
	 * communications.
	 */
	xpc_rsvd_page = xpc_rsvd_page_init();
	if (xpc_rsvd_page == NULL) {
		DPRINTK_ALWAYS(xpc_part, (XPC_DBG_P_INIT | XPC_DBG_P_ERROR),
			KERN_ERR "XPC: SAL could not locate a reserved page\n");

		free_irq(SGI_XPC_ACTIVATE, NULL);
		xpc_restrict_IPI_ops();

		if (xpc_sysctl) {
			unregister_sysctl_table(xpc_sysctl);
		}

		XPC_KDB_UNREGISTER();
		UNREG_DPRINTK(xpc_chan);
		UNREG_DPRINTK(xpc_part);
		return -EBUSY;
	}


	/*
	 * Set the beating to other partitions into motion.  This is
	 * the last requirement for other partitions' discovery to
	 * initiate communications with us.
	 */
	init_timer(&xpc_hb_timer);
	xpc_hb_timer.function = xpc_hb_beater;
	xpc_hb_beater(0);


	/*
	 * The real work-horse behind xpc.  This processes incoming
	 * interrupts and monitors remote heartbeats.
	 */
	pid = kernel_thread(xpc_hb_checker, NULL, SIGCHLD);
	if (pid < 0) {
		DPRINTK_ALWAYS(xpc_part, (XPC_DBG_P_INIT | XPC_DBG_P_ERROR),
		       KERN_ERR "XPC: failed while forking hb check thread\n");

		/* indicate to others that our reserved page is uninitialized */
		xpc_rsvd_page->vars_pa = 0;

		del_timer_sync(&xpc_hb_timer);
		free_irq(SGI_XPC_ACTIVATE, NULL);
		xpc_restrict_IPI_ops();

		if (xpc_sysctl) {
			unregister_sysctl_table(xpc_sysctl);
		}

		XPC_KDB_UNREGISTER();
		UNREG_DPRINTK(xpc_chan);
		UNREG_DPRINTK(xpc_part);
		return -EBUSY;
	}


	/*
	 * Startup a thread that will attempt to discover other partitions to
	 * activate based on info provided by SAL. This new thread is short
	 * lived and will exit once discovery is complete.
	 */
	pid = kernel_thread(xpc_initiate_discovery, NULL, SIGCHLD);
	if (pid < 0) {
		DPRINTK_ALWAYS(xpc_part, (XPC_DBG_P_INIT | XPC_DBG_P_ERROR),
		      KERN_ERR "XPC: failed while forking discovery thread\n");

		/* mark this new thread as a non-starter */
		up(&xpc_discovery_exited);

		xpc_exit();
		return -EBUSY;
	}


	/* set the interface to point at XPC's functions */
	xpc_set_interface(xpc_initiate_connect, xpc_initiate_disconnect,
			  xpc_allocate, xpc_send, xpc_send_notify, xpc_received,
			  xpc_partid_to_nasids);

	return 0;
}
module_init(xpc_init);


MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION("Cross Partition Communication (XPC) support");
MODULE_LICENSE("GPL");

MODULE_PARM(xpc_hb_interval, "1i");
MODULE_PARM_DESC(xpc_hb_interval, "Number of seconds between "
		"heartbeat increments.");

MODULE_PARM(xpc_hb_check_interval, "1i");
MODULE_PARM_DESC(xpc_hb_check_interval, "Number of seconds between "
		"heartbeat checks.");

EXPORT_NO_SYMBOLS;

