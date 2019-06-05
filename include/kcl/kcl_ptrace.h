#ifndef _KCL_PTRACE_H
#define _KCL_PTRACE_H

#include <linux/ptrace.h>

#if !defined(HAVE_PTRACE_PARENT)
static inline struct task_struct *ptrace_parent(struct task_struct *task)
{
	if (unlikely(task->ptrace))
		return rcu_dereference(task->parent);
	return NULL;
}
#endif

#endif
