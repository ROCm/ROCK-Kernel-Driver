#undef DEBUG

#ifdef DEBUG
# define DBG(x...) printk(x)
#else
# define DBG(x...)
#endif

extern struct device device_root;
extern spinlock_t device_lock;

extern int device_make_dir(struct device * dev);
extern void device_remove_dir(struct device * dev);

