/*
 * linux/kernel/capability.c
 *
 * Copyright (C) 1997  Andrew Main <zefram@fysh.org>
 *
 * Integrated into 2.1.97+,  Andrew G. Morgan <morgan@transmeta.com>
 * 30 May 2002:	Cleanup, Robert M. Love <rml@tech9.net>
 */ 

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/security.h>
#include <asm/uaccess.h>

unsigned securebits = SECUREBITS_DEFAULT; /* systemwide security settings */
kernel_cap_t cap_bset = CAP_INIT_EFF_SET;

EXPORT_SYMBOL(securebits);
EXPORT_SYMBOL(cap_bset);

/*
 * This global lock protects task->cap_* for all tasks including current.
 * Locking rule: acquire this prior to tasklist_lock.
 */
spinlock_t task_capability_lock = SPIN_LOCK_UNLOCKED;

/*
 * For sys_getproccap() and sys_setproccap(), any of the three
 * capability set pointers may be NULL -- indicating that that set is
 * uninteresting and/or not to be changed.
 */

/*
 * sys_capget - get the capabilities of a given process.
 */
asmlinkage long sys_capget(cap_user_header_t header, cap_user_data_t dataptr)
{
     int ret = 0;
     pid_t pid;
     __u32 version;
     task_t *target;
     struct __user_cap_data_struct data;

     if (get_user(version, &header->version))
	     return -EFAULT;

     if (version != _LINUX_CAPABILITY_VERSION) {
	     if (put_user(_LINUX_CAPABILITY_VERSION, &header->version))
		     return -EFAULT; 
             return -EINVAL;
     }

     if (get_user(pid, &header->pid))
	     return -EFAULT;

     if (pid < 0) 
             return -EINVAL;

     spin_lock(&task_capability_lock);
     read_lock(&tasklist_lock); 

     if (pid && pid != current->pid) {
	     target = find_task_by_pid(pid);
	     if (!target) {
	          ret = -ESRCH;
	          goto out;
	     }
     } else
	     target = current;

     ret = security_capget(target, &data.effective, &data.inheritable, &data.permitted);

out:
     read_unlock(&tasklist_lock); 
     spin_unlock(&task_capability_lock);

     if (!ret && copy_to_user(dataptr, &data, sizeof data))
          return -EFAULT; 

     return ret;
}

/*
 * cap_set_pg - set capabilities for all processes in a given process
 * group.  We call this holding task_capability_lock and tasklist_lock.
 */
static inline void cap_set_pg(int pgrp, kernel_cap_t *effective,
			      kernel_cap_t *inheritable,
			      kernel_cap_t *permitted)
{
	task_t *g, *target;
	struct list_head *l;
	struct pid *pid;

	for_each_task_pid(pgrp, PIDTYPE_PGID, g, l, pid) {
		target = g;
		while_each_thread(g, target)
			security_capset_set(target, effective, inheritable, permitted);
	}
}

/*
 * cap_set_all - set capabilities for all processes other than init
 * and self.  We call this holding task_capability_lock and tasklist_lock.
 */
static inline void cap_set_all(kernel_cap_t *effective,
			       kernel_cap_t *inheritable,
			       kernel_cap_t *permitted)
{
     task_t *g, *target;

     do_each_thread(g, target) {
             if (target == current || target->pid == 1)
                     continue;
	     security_capset_set(target, effective, inheritable, permitted);
     } while_each_thread(g, target);
}

/*
 * sys_capset - set capabilities for a given process, all processes, or all
 * processes in a given process group.
 *
 * The restrictions on setting capabilities are specified as:
 *
 * [pid is for the 'target' task.  'current' is the calling task.]
 *
 * I: any raised capabilities must be a subset of the (old current) permitted
 * P: any raised capabilities must be a subset of the (old current) permitted
 * E: must be set to a subset of (new target) permitted
 */
asmlinkage long sys_capset(cap_user_header_t header, const cap_user_data_t data)
{
     kernel_cap_t inheritable, permitted, effective;
     __u32 version;
     task_t *target;
     int ret;
     pid_t pid;

     if (get_user(version, &header->version))
	     return -EFAULT; 

     if (version != _LINUX_CAPABILITY_VERSION) {
	     if (put_user(_LINUX_CAPABILITY_VERSION, &header->version))
		     return -EFAULT; 
             return -EINVAL;
     }

     if (get_user(pid, &header->pid))
	     return -EFAULT; 

     if (pid && !capable(CAP_SETPCAP))
             return -EPERM;

     if (copy_from_user(&effective, &data->effective, sizeof(effective)) ||
	 copy_from_user(&inheritable, &data->inheritable, sizeof(inheritable)) ||
	 copy_from_user(&permitted, &data->permitted, sizeof(permitted)))
	     return -EFAULT; 

     spin_lock(&task_capability_lock);
     read_lock(&tasklist_lock);

     if (pid > 0 && pid != current->pid) {
          target = find_task_by_pid(pid);
          if (!target) {
               ret = -ESRCH;
               goto out;
          }
     } else
               target = current;

     ret = -EPERM;

     if (security_capset_check(target, &effective, &inheritable, &permitted))
	     goto out;

     if (!cap_issubset(inheritable, cap_combine(target->cap_inheritable,
                       current->cap_permitted)))
             goto out;

     /* verify restrictions on target's new Permitted set */
     if (!cap_issubset(permitted, cap_combine(target->cap_permitted,
                       current->cap_permitted)))
             goto out;

     /* verify the _new_Effective_ is a subset of the _new_Permitted_ */
     if (!cap_issubset(effective, permitted))
             goto out;

     ret = 0;

     /* having verified that the proposed changes are legal,
           we now put them into effect. */
     if (pid < 0) {
             if (pid == -1)  /* all procs other than current and init */
                     cap_set_all(&effective, &inheritable, &permitted);

             else            /* all procs in process group */
                     cap_set_pg(-pid, &effective, &inheritable, &permitted);
     } else {
	     security_capset_set(target, &effective, &inheritable, &permitted);
     }

out:
     read_unlock(&tasklist_lock);
     spin_unlock(&task_capability_lock);

     return ret;
}
