/* Functions local to drivers/usb/core/ */

extern void usb_create_driverfs_dev_files (struct usb_device *dev);
extern void usb_create_driverfs_intf_files (struct usb_interface *intf);
extern int usb_probe_interface (struct device *dev);
extern int usb_unbind_interface (struct device *dev);
