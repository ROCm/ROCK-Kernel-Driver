#ifdef CONFIG_SCHED_AUTOGROUP

static void __sched_move_task(struct task_struct *tsk, struct rq *rq);

static inline struct task_group *
autogroup_task_group(struct task_struct *p, struct task_group *tg);

#else /* !CONFIG_SCHED_AUTOGROUP */

static inline void autogroup_init(struct task_struct *init_task) {  }

static inline struct task_group *
autogroup_task_group(struct task_struct *p, struct task_group *tg)
{
	return tg;
}

#endif /* CONFIG_SCHED_AUTOGROUP */
