#ifndef AMDKCL_VGA_SWITCHEROO_H
#define AMDKCL_VGA_SWITCHEROO_H

#include <linux/vga_switcheroo.h>

#if !defined(HAVE_3ARGS_VGA_SWITCHEROO_CLIENT_OPS_VGA_SWITCHEROO_REGISTER_CLIENT) &&\
	!defined(HAVE_2ARGS_VGA_SWITCHEROO_REGISTER_CLIENT)
struct vga_switcheroo_client_ops {
       void (*set_gpu_state)(struct pci_dev *dev, enum vga_switcheroo_state);
       void (*reprobe)(struct pci_dev *dev);
       bool (*can_switch)(struct pci_dev *dev);
};
#endif

static inline int kcl_vga_switcheroo_register_client(struct pci_dev *dev,
						     const struct vga_switcheroo_client_ops *ops,
						     bool driver_power_control)
{
#if defined(HAVE_3ARGS_VGA_SWITCHEROO_CLIENT_OPS_VGA_SWITCHEROO_REGISTER_CLIENT)
	return vga_switcheroo_register_client(dev, ops, driver_power_control);
#elif defined(HAVE_2ARGS_VGA_SWITCHEROO_REGISTER_CLIENT)
	return vga_switcheroo_register_client(dev, ops);
#elif defined(HAVE_4ARGS_VGA_SWITCHEROO_REGISTER_CLIENT)
	return vga_switcheroo_register_client(dev, ops->set_gpu_state, ops->reprobe, ops->can_switch);
#else
	return vga_switcheroo_register_client(dev, ops->set_gpu_state, ops->can_switch);
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
