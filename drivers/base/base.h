#undef DEBUG

#ifdef DEBUG
# define DBG(x...) printk(x)
#else
# define DBG(x...)
#endif

extern struct device device_root;
extern spinlock_t device_lock;

extern int bus_add_device(struct device * dev);
extern void bus_remove_device(struct device * dev);

extern int device_create_dir(struct driver_dir_entry * dir, struct driver_dir_entry * parent);
extern int device_make_dir(struct device * dev);
extern void device_remove_dir(struct device * dev);

extern int device_bus_link(struct device * dev);
