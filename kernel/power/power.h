

/* With SUSPEND_CONSOLE defined, it suspend looks *really* cool, but
   we probably do not take enough locks for switching consoles, etc,
   so bad things might happen.
*/
#if defined(CONFIG_VT) && defined(CONFIG_VT_CONSOLE)
#define SUSPEND_CONSOLE	(MAX_NR_CONSOLES-1)
#endif


#ifdef CONFIG_PM_DISK
extern int pm_suspend_disk(void);

#else
static inline int pm_suspend_disk(void)
{
	return -EPERM;
}
#endif

extern struct semaphore pm_sem;
#define power_attr(_name) \
static struct subsys_attribute _name##_attr = {	\
	.attr	= {				\
		.name = __stringify(_name),	\
		.mode = 0644,			\
	},					\
	.show	= _name##_show,			\
	.store	= _name##_store,		\
}

extern struct subsystem power_subsys;

extern int freeze_processes(void);
extern void thaw_processes(void);

extern int pm_prepare_console(void);
extern void pm_restore_console(void);
