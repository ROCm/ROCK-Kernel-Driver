extern int bus_add_device(struct device * dev);
extern void bus_remove_device(struct device * dev);

extern int bus_add_driver(struct device_driver *);
extern void bus_remove_driver(struct device_driver *);

