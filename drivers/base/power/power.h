
/*
 * Used to synchronize global power management operations.
 */
extern struct semaphore dpm_sem;

/* 
 * The PM lists.
 */
extern struct list_head dpm_active;
extern struct list_head dpm_suspended;
extern struct list_head dpm_off;
extern struct list_head dpm_off_irq;


static inline struct dev_pm_info * to_pm_info(struct list_head * entry)
{
	return container_of(entry,struct dev_pm_info,entry);
}

static inline struct device * to_device(struct list_head * entry)
{
	return container_of(to_pm_info(entry),struct device,power);
}


/*
 * sysfs.c
 */

extern int dpm_sysfs_add(struct device *);
extern void dpm_sysfs_remove(struct device *);

/*
 * resume.c 
 */
extern int dpm_resume(void);
extern void dpm_power_up(void);
extern void dpm_power_up_irq(void);
extern void power_up_device(struct device *);
extern int resume_device(struct device *);

/*
 * suspend.c
 */
extern int suspend_device(struct device *, u32);
extern int power_down_device(struct device *, u32);


/*
 * runtime.c
 */

extern int dpm_runtime_suspend(struct device *, u32);
extern void dpm_runtime_resume(struct device *);

