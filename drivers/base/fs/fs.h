
extern int device_create_dir(struct driver_dir_entry * dir, struct driver_dir_entry * parent);

int get_devpath_length(struct device * dev);

void fill_devpath(struct device * dev, char * path, int length);

