extern struct semaphore device_sem;
extern struct semaphore devclass_sem;

extern int bus_add_device(struct device * dev);
extern void bus_remove_device(struct device * dev);

extern int bus_add_driver(struct device_driver *);
extern void bus_remove_driver(struct device_driver *);

extern int devclass_add_device(struct device *);
extern void devclass_remove_device(struct device *);

extern int devclass_add_driver(struct device_driver *);
extern void devclass_remove_driver(struct device_driver *);

extern int interface_add_dev(struct device *);
extern void interface_remove_dev(struct device *);


#ifdef CONFIG_HOTPLUG
extern int class_hotplug(struct device *dev, const char *action);
#else
static inline int class_hotplug(struct device *dev, const char *action)
{
	return 0;
}
#endif

