#ifndef _LINUX_PID_H
#define _LINUX_PID_H

enum pid_type
{
	PIDTYPE_PID,
	PIDTYPE_PGID,
	PIDTYPE_SID,
	PIDTYPE_MAX
};

struct pid
{
	int nr;
	atomic_t count;
	struct task_struct *task;
	struct list_head task_list;
	struct list_head hash_chain;
};

struct pid_link
{
	struct list_head pid_chain;
	struct pid *pidptr;
	struct pid pid;
};

#define pid_task(elem, type) \
	list_entry(elem, struct task_struct, pids[type].pid_chain)

/*
 * attach_pid() must be called with the tasklist_lock write-held.
 *
 * It might unlock the tasklist_lock for allocation, so this
 * function must be called after installing all other links of
 * a new task.
 */
extern int FASTCALL(attach_pid(struct task_struct *, enum pid_type, int));

/*
 * detach_pid() must be called with the tasklist_lock write-held.
 */
extern void FASTCALL(detach_pid(struct task_struct *task, enum pid_type));

/*
 * look up a PID in the hash table. Must be called with the tasklist_lock
 * held.
 */
extern struct pid *FASTCALL(find_pid(enum pid_type, int));

extern int alloc_pidmap(void);
extern void FASTCALL(free_pidmap(int));

#define for_each_task_pid(who, type, task, elem, pid)		\
	if ((pid = find_pid(type, who)))			\
	        for (elem = pid->task_list.next,			\
			prefetch(elem->next),				\
			task = pid_task(elem, type);			\
			elem != &pid->task_list;			\
			elem = elem->next, prefetch(elem->next), 	\
			task = pid_task(elem, type))

#endif /* _LINUX_PID_H */
