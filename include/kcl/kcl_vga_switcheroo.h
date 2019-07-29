#ifndef AMDKCL_VGA_SWITCHEROO_H
#define AMDKCL_VGA_SWITCHEROO_H

#include <linux/vga_switcheroo.h>

/**
 * arg change in mainline kernel 3.12
 * but only affect RHEL6 without backport
 */
static inline int kcl_vga_switcheroo_register_client(struct pci_dev *dev,
						     const struct vga_switcheroo_client_ops *ops,
						     bool driver_power_control)
{
#if defined(OS_NAME_RHEL_6)
	return vga_switcheroo_register_client(dev, ops);
#else
	return vga_switcheroo_register_client(dev, ops, driver_power_control);
#endif
}

#if defined(HAVE_2ARGS_VGA_SWITCHEROO_REGISTER_HANDLER)
static inline int kcl_vga_switcheroo_register_handler(const struct vga_switcheroo_handler *handler,
								   enum vga_switcheroo_handler_flags_t handler_flags)
#elif defined(HAVE_1ARG_CONST_VGA_SWITCHEROO_REGISTER_HANDLER)
static inline int kcl_vga_switcheroo_register_handler(const struct vga_switcheroo_handler *handler,
						      int handler_flags)
#else
static inline int kcl_vga_switcheroo_register_handler(struct vga_switcheroo_handler *handler,
						      int handler_flags)
#endif
{
#if defined(HAVE_2ARGS_VGA_SWITCHEROO_REGISTER_HANDLER)
	return vga_switcheroo_register_handler(handler, handler_flags);
#else
	return vga_switcheroo_register_handler(handler);
#endif
}
#endif /* AMDKCL_VGA_SWITCHEROO_H */
