#undef DEBUG

#ifdef DEBUG
# define DBG(x...) printk(x)
#else
# define DBG(x...)
#endif

extern struct device device_root;
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

extern int driver_attach(struct device_driver * drv);
extern void driver_detach(struct device_driver * drv);
