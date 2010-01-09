/*
 * perfmon_res.c:  perfmon2 resource allocations
 *
 * This file implements the perfmon2 interface which
 * provides access to the hardware performance counters
 * of the host processor.
 *
 * The initial version of perfmon.c was written by
 * Ganesh Venkitachalam, IBM Corp.
 *
 * Then it was modified for perfmon-1.x by Stephane Eranian and
 * David Mosberger, Hewlett Packard Co.
 *
 * Version Perfmon-2.x is a complete rewrite of perfmon-1.x
 * by Stephane Eranian, Hewlett Packard Co.
 *
 * Copyright (c) 1999-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *                David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * More information about perfmon available at:
 * 	http://perfmon2.sf.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/perfmon_kern.h>
#include "perfmon_priv.h"

/*
 * global information about all sessions
 * mostly used to synchronize between system wide and per-process
 */
struct pfm_resources {
	size_t		smpl_buf_mem_cur;/* current smpl buf mem usage */
	cpumask_t	sys_cpumask;     /* bitmask of used cpus */
	u32		thread_sessions; /* #num loaded per-thread sessions */
};

static struct pfm_resources pfm_res;

static __cacheline_aligned_in_smp DEFINE_SPINLOCK(pfm_res_lock);

/**
 * pfm_smpl_buf_space_acquire - check memory resource usage for sampling buffer
 * @ctx: context of interest
 * @size: size fo requested buffer
 *
 * sampling buffer allocated by perfmon must be
 * checked against max locked memory usage thresholds
 * for security reasons.
 *
 * The first level check is against the system wide limit
 * as indicated by the system administrator in /sys/kernel/perfmon
 *
 * The second level check is on a per-process basis using
 * RLIMIT_MEMLOCK limit.
 *
 * Operating on the current task only.
 */
int pfm_smpl_buf_space_acquire(struct pfm_context *ctx, size_t size)
{
	struct mm_struct *mm;
	unsigned long locked;
	unsigned long buf_mem, buf_mem_max;
	unsigned long flags;

	spin_lock_irqsave(&pfm_res_lock, flags);

	/*
	 * check against global buffer limit
	 */
	buf_mem_max = pfm_controls.smpl_buffer_mem_max;
	buf_mem = pfm_res.smpl_buf_mem_cur + size;

	if (buf_mem <= buf_mem_max) {
		pfm_res.smpl_buf_mem_cur = buf_mem;

		PFM_DBG("buf_mem_max=%lu current_buf_mem=%lu",
			buf_mem_max,
			buf_mem);
	}

	spin_unlock_irqrestore(&pfm_res_lock, flags);

	if (buf_mem > buf_mem_max) {
		PFM_DBG("smpl buffer memory threshold reached");
		return -ENOMEM;
	}

	/*
	 * check against per-process RLIMIT_MEMLOCK
	 */
	mm = get_task_mm(current);

	down_write(&mm->mmap_sem);

	locked  = mm->locked_vm << PAGE_SHIFT;
	locked += size;

	if (locked > rlimit(RLIMIT_MEMLOCK)) {

		PFM_DBG("RLIMIT_MEMLOCK reached ask_locked=%lu rlim_cur=%lu",
			locked,
			rlimit(RLIMIT_MEMLOCK));

		up_write(&mm->mmap_sem);
		mmput(mm);
		goto unres;
	}

	mm->locked_vm = locked >> PAGE_SHIFT;

	up_write(&mm->mmap_sem);

	mmput(mm);

	return 0;

unres:
	/*
	 * remove global buffer memory allocation
	 */
	spin_lock_irqsave(&pfm_res_lock, flags);

	pfm_res.smpl_buf_mem_cur -= size;

	spin_unlock_irqrestore(&pfm_res_lock, flags);

	return -ENOMEM;
}
/**
 * pfm_smpl_buf_space_release - release resource usage for sampling buffer
 * @ctx: perfmon context of interest
 *
 * There exist multiple paths leading to this function. We need to
 * be very careful withlokcing on the mmap_sem as it may already be
 * held by the time we come here.
 * The following paths exist:
 *
 * exit path:
 * sys_exit_group
 *    do_group_exit
 *     do_exit
 *      exit_mm
 *       mmput
 *        exit_mmap
 *         remove_vma
 *          fput
 *           __fput
 *            pfm_close
 *             __pfm_close
 *              pfm_context_free
 * 	         pfm_release_buf_space
 * munmap path:
 * sys_munmap
 *  do_munmap
 *   remove_vma
 *    fput
 *     __fput
 *      pfm_close
 *       __pfm_close
 *        pfm_context_free
 *         pfm_release_buf_space
 *
 * close path:
 * sys_close
 *  filp_close
 *   fput
 *    __fput
 *     pfm_close
 *      __pfm_close
 *       pfm_context_free
 *        pfm_release_buf_space
 *
 * The issue is that on the munmap() path, the mmap_sem is already held
 * in write-mode by the time we come here. To avoid the deadlock, we need
 * to know where we are coming from and skip down_write(). If is fairly
 * difficult to know this because of the lack of good hooks and
 * the fact that, there may not have been any mmap() of the sampling buffer
 * (i.e. create_context() followed by close() or exit()).
 *
 * We use a set flag ctx->flags.mmap_nlock which is toggled in the vm_ops
 * callback in remove_vma() which is called systematically for the call, so
 * on all but the pure close() path. The exit path does not already hold
 * the lock but this is exit so there is no task->mm by the time we come here.
 *
 * The mmap_nlock is set only when unmapping and this is the LAST reference
 * to the file (i.e., close() followed by munmap()).
 */
void pfm_smpl_buf_space_release(struct pfm_context *ctx, size_t size)
{
	unsigned long flags;
	struct mm_struct *mm;

	mm = get_task_mm(current);
	if (mm) {
		if (ctx->flags.mmap_nlock == 0) {
			PFM_DBG("doing down_write");
			down_write(&mm->mmap_sem);
		}

		mm->locked_vm -= size >> PAGE_SHIFT;

		PFM_DBG("size=%zu locked_vm=%lu", size, mm->locked_vm);

		if (ctx->flags.mmap_nlock == 0)
			up_write(&mm->mmap_sem);

		mmput(mm);
	}

	spin_lock_irqsave(&pfm_res_lock, flags);

	pfm_res.smpl_buf_mem_cur -= size;

	spin_unlock_irqrestore(&pfm_res_lock, flags);
}

/**
 * pfm_session_acquire - reserve a per-thread or per-cpu session
 * @is_system: true if per-cpu session
 * @cpu: cpu number for per-cpu session
 *
 * return:
 * 	 0    : success
 * 	-EBUSY: if conflicting session exist
 */
int pfm_session_acquire(int is_system, u32 cpu)
{
	unsigned long flags;
	u32 nsys_cpus;
	int ret = 0;

	/*
	 * validy checks on cpu_mask have been done upstream
	 */
	spin_lock_irqsave(&pfm_res_lock, flags);

	nsys_cpus = cpus_weight(pfm_res.sys_cpumask);

	PFM_DBG("in  sys=%u task=%u is_sys=%d cpu=%u",
		nsys_cpus,
		pfm_res.thread_sessions,
		is_system,
		cpu);

	if (is_system) {
		/*
		 * cannot mix system wide and per-task sessions
		 */
		if (pfm_res.thread_sessions > 0) {
			PFM_DBG("%u conflicting thread_sessions",
				pfm_res.thread_sessions);
			ret = -EBUSY;
			goto abort;
		}

		if (cpu_isset(cpu, pfm_res.sys_cpumask)) {
			PFM_DBG("conflicting session on CPU%u", cpu);
			ret = -EBUSY;
			goto abort;
		}

		PFM_DBG("reserved session on CPU%u", cpu);

		cpu_set(cpu, pfm_res.sys_cpumask);
		nsys_cpus++;
	} else {
		if (nsys_cpus) {
			ret = -EBUSY;
			goto abort;
		}
		pfm_res.thread_sessions++;
	}

	PFM_DBG("out sys=%u task=%u is_sys=%d cpu=%u",
		nsys_cpus,
		pfm_res.thread_sessions,
		is_system,
		cpu);

abort:
	spin_unlock_irqrestore(&pfm_res_lock, flags);

	return ret;
}

/**
 * pfm_session_release - release a per-cpu or per-thread session
 * @is_system: true if per-cpu session
 * @cpu: cpu number for per-cpu session
 *
 * called from __pfm_unload_context()
 */
void pfm_session_release(int is_system, u32 cpu)
{
	unsigned long flags;

	spin_lock_irqsave(&pfm_res_lock, flags);

	PFM_DBG("in sys_sessions=%u thread_sessions=%u syswide=%d cpu=%u",
		cpus_weight(pfm_res.sys_cpumask),
		pfm_res.thread_sessions,
		is_system, cpu);

	if (is_system)
		cpu_clear(cpu, pfm_res.sys_cpumask);
	else
		pfm_res.thread_sessions--;

	PFM_DBG("out sys_sessions=%u thread_sessions=%u syswide=%d cpu=%u",
		cpus_weight(pfm_res.sys_cpumask),
		pfm_res.thread_sessions,
		is_system, cpu);

	spin_unlock_irqrestore(&pfm_res_lock, flags);
}

/**
 * pfm_session_allcpus_acquire - acquire per-cpu sessions on all available cpus
 *
 * currently used by Oprofile on X86
 */
int pfm_session_allcpus_acquire(void)
{
	unsigned long flags;
	int ret = -EBUSY;

	spin_lock_irqsave(&pfm_res_lock, flags);

	if (!cpus_empty(pfm_res.sys_cpumask)) {
		PFM_DBG("already some system-wide sessions");
		goto abort;
	}
	/*
	 * cannot mix system wide and per-task sessions
	 */
	if (pfm_res.thread_sessions) {
		PFM_DBG("%u conflicting thread_sessions",
			pfm_res.thread_sessions);
		goto abort;
	}
	/*
	 * we need to set all bits to avoid issues
	 * with HOTPLUG, and cpus showing up while
	 * there is already an allcpu session
	 */
	cpus_setall(pfm_res.sys_cpumask);

	ret = 0;
abort:
	spin_unlock_irqrestore(&pfm_res_lock, flags);

	return ret;
}
EXPORT_SYMBOL(pfm_session_allcpus_acquire);

/**
 * pfm_session_allcpus_release - relase per-cpu sessions on all cpus
 *
 * currently used by Oprofile code
 */
void pfm_session_allcpus_release(void)
{
	unsigned long flags;

	spin_lock_irqsave(&pfm_res_lock, flags);

	cpus_clear(pfm_res.sys_cpumask);

	spin_unlock_irqrestore(&pfm_res_lock, flags);
}
EXPORT_SYMBOL(pfm_session_allcpus_release);

/**
 * pfm_sysfs_res_show - return currnt resourcde usage for sysfs
 * @buf: buffer to hold string in return
 * @sz: size of buf
 * @what: what to produce
 *        what=0 : thread_sessions
 *        what=1 : cpus_weight(sys_cpumask)
 *        what=2 : smpl_buf_mem_cur
 *        what=3 : pmu model name
 *
 * called from perfmon_sysfs.c
 * return number of bytes written into buf (up to sz)
 */
ssize_t pfm_sysfs_res_show(char *buf, size_t sz, int what)
{
	unsigned long flags;
	cpumask_t mask;

	spin_lock_irqsave(&pfm_res_lock, flags);

	switch (what) {
	case 0: snprintf(buf, sz, "%u\n", pfm_res.thread_sessions);
		break;
	case 1:
		cpus_and(mask, pfm_res.sys_cpumask, cpu_online_map);
		snprintf(buf, sz, "%d\n", cpus_weight(mask));
		break;
	case 2: snprintf(buf, sz, "%zu\n", pfm_res.smpl_buf_mem_cur);
		break;
	case 3:
		snprintf(buf, sz, "%s\n",
			pfm_pmu_conf ?	pfm_pmu_conf->pmu_name
				     :	"unknown\n");
	}
	spin_unlock_irqrestore(&pfm_res_lock, flags);
	return strlen(buf);
}
