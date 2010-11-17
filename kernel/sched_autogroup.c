#ifdef CONFIG_SCHED_AUTOGROUP

unsigned int __read_mostly sysctl_sched_autogroup_enabled = 1;

struct autogroup {
	struct kref		kref;
	struct task_group	*tg;
};

static struct autogroup autogroup_default;

static void autogroup_init(struct task_struct *init_task)
{
	autogroup_default.tg = &init_task_group;
	kref_init(&autogroup_default.kref);
	init_task->signal->autogroup = &autogroup_default;
}

static inline void autogroup_destroy(struct kref *kref)
{
	struct autogroup *ag = container_of(kref, struct autogroup, kref);
	struct task_group *tg = ag->tg;

	kfree(ag);
	sched_destroy_group(tg);
}

static inline void autogroup_kref_put(struct autogroup *ag)
{
	kref_put(&ag->kref, autogroup_destroy);
}

static inline struct autogroup *autogroup_kref_get(struct autogroup *ag)
{
	kref_get(&ag->kref);
	return ag;
}

static inline struct autogroup *autogroup_create(void)
{
	struct autogroup *ag = kmalloc(sizeof(*ag), GFP_KERNEL);

	if (!ag)
		goto out_fail;

	ag->tg = sched_create_group(&init_task_group);
	kref_init(&ag->kref);

	if (!(IS_ERR(ag->tg)))
		return ag;

out_fail:
	if (ag) {
		kfree(ag);
		WARN_ON(1);
	} else
		WARN_ON(1);

	return autogroup_kref_get(&autogroup_default);
}

static inline struct task_group *
autogroup_task_group(struct task_struct *p, struct task_group *tg)
{
	int enabled = ACCESS_ONCE(sysctl_sched_autogroup_enabled);

	enabled &= (tg == &root_task_group);
	enabled &= (p->sched_class == &fair_sched_class);
	enabled &= (!(p->flags & PF_EXITING));

	if (enabled)
		return p->signal->autogroup->tg;

	return tg;
}

static void
autogroup_move_group(struct task_struct *p, struct autogroup *ag)
{
	struct autogroup *prev;
	struct task_struct *t;
	struct rq *rq;
	unsigned long flags;

	rq = task_rq_lock(p, &flags);
	prev = p->signal->autogroup;
	if (prev == ag) {
		task_rq_unlock(rq, &flags);
		return;
	}

	p->signal->autogroup = autogroup_kref_get(ag);
	__sched_move_task(p, rq);
	task_rq_unlock(rq, &flags);

	rcu_read_lock();
	list_for_each_entry_rcu(t, &p->thread_group, thread_group) {
		sched_move_task(t);
	}
	rcu_read_unlock();

	autogroup_kref_put(prev);
}

void sched_autogroup_create_attach(struct task_struct *p)
{
	struct autogroup *ag = autogroup_create();

	autogroup_move_group(p, ag);
	/* drop extra refrence added by autogroup_create() */
	autogroup_kref_put(ag);
}
EXPORT_SYMBOL(sched_autogroup_create_attach);

/* currently has no users */
void sched_autogroup_detach(struct task_struct *p)
{
	autogroup_move_group(p, &autogroup_default);
}
EXPORT_SYMBOL(sched_autogroup_detach);

void sched_autogroup_fork(struct signal_struct *sig)
{
	sig->autogroup = autogroup_kref_get(current->signal->autogroup);
}

void sched_autogroup_exit(struct signal_struct *sig)
{
	autogroup_kref_put(sig->autogroup);
}

static int __init setup_autogroup(char *str)
{
	sysctl_sched_autogroup_enabled = 0;

	return 1;
}

__setup("noautogroup", setup_autogroup);
#endif
