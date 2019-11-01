#ifndef AMDKCL_VGA_SWITCHEROO_H
#define AMDKCL_VGA_SWITCHEROO_H

#include <linux/vga_switcheroo.h>

#if !defined(HAVE_VGA_SWITCHEROO_REGISTER_CLIENT_OPS)
struct vga_switcheroo_client_ops {
       void (*set_gpu_state)(struct pci_dev *dev, enum vga_switcheroo_state);
       void (*reprobe)(struct pci_dev *dev);
       bool (*can_switch)(struct pci_dev *dev);
};
#endif

#if !defined(HAVE_VGA_SWITCHEROO_REGISTER_CLIENT_P_P_B)
static inline int _kcl_vga_switcheroo_register_client(struct pci_dev *dev,
						     const struct vga_switcheroo_client_ops *ops,
						     bool driver_power_control)
{
#if defined(HAVE_VGA_SWITCHEROO_REGISTER_CLIENT_P_P)
	return vga_switcheroo_register_client(dev, ops);
#elif defined(HAVE_VGA_SWITCHEROO_REGISTER_CLIENT_P_P_P_P)
	return vga_switcheroo_register_client(dev, ops->set_gpu_state, ops->reprobe, ops->can_switch);
#else
	return vga_switcheroo_register_client(dev, ops->set_gpu_state, ops->can_switch);
#endif
}
#endif

#if !defined(HAVE_VGA_SWITCHEROO_HANDLER_FLAGS_T_ENUM)
enum vga_switcheroo_handler_flags_t {
	VGA_SWITCHEROO_CAN_SWITCH_DDC	= (1 << 0),
	VGA_SWITCHEROO_NEEDS_EDP_CONFIG	= (1 << 1),
};
#endif

#if !defined(HAVE_VGA_SWITCHEROO_REGISTER_HANDLER_PC_E)
static inline int _kcl_vga_switcheroo_register_handler(
			  const struct vga_switcheroo_handler *handler,
			  enum vga_switcheroo_handler_flags_t handler_flags)
{
#if defined(HAVE_VGA_SWITCHEROO_REGISTER_HANDLER_PC)
	return vga_switcheroo_register_handler(handler);
#else
	return vga_switcheroo_register_handler((struct vga_switcheroo_handler *)handler);
#endif
}
#endif
#endif /* AMDKCL_VGA_SWITCHEROO_H */
