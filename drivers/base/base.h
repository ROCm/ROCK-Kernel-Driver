#undef DEBUG

#ifdef DEBUG
# define DBG(x...) printk(x)
#else
# define DBG(x...)
#endif

extern struct list_head global_device_list;
extern spinlock_t device_lock;

extern struct device * get_device_locked(struct device *);

extern int bus_add_device(struct device * dev);
extern void bus_remove_device(struct device * dev);

extern int device_make_dir(struct device * dev);
extern void device_remove_dir(struct device * dev);

extern int bus_make_dir(struct bus_type * bus);
extern void bus_remove_dir(struct bus_type * bus);

extern int driver_make_dir(struct device_driver * drv);
extern void driver_remove_dir(struct device_driver * drv);

extern int device_bus_link(struct device * dev);
extern void device_remove_symlink(struct driver_dir_entry * dir, const char * name);

extern int devclass_make_dir(struct device_class *);
extern void devclass_remove_dir(struct device_class *);

extern int devclass_drv_link(struct device_driver *);
extern void devclass_drv_unlink(struct device_driver *);

extern int devclass_dev_link(struct device_class *, struct device *);
extern void devclass_dev_unlink(struct device_class *, struct device *);

extern int devclass_add_device(struct device *);
extern void devclass_remove_device(struct device *);

extern int intf_make_dir(struct device_interface *);
extern void intf_remove_dir(struct device_interface *);

extern int intf_dev_link(struct intf_data *);
extern void intf_dev_unlink(struct intf_data *);

extern int interface_add(struct device_class *, struct device *);
extern void interface_remove(struct device_class *, struct device *);


extern int driver_attach(struct device_driver * drv);
extern void driver_detach(struct device_driver * drv);

#ifdef CONFIG_HOTPLUG
extern int dev_hotplug(struct device *dev, const char *action);
#else
static inline int dev_hotplug(struct device *dev, const char *action)
{
	return 0;
}
#endif

