
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
 * resume.c 
 */
extern int dpm_resume(void);
extern void dpm_power_up(void);
extern void dpm_power_up_irq(void);


