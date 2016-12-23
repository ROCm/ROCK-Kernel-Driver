#ifndef AMDKCL_VGA_SWITCHEROO_H
#define AMDKCL_VGA_SWITCHEROO_H

#include <linux/vga_switcheroo.h>

#if defined(CONFIG_VGA_SWITCHEROO)
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
static inline int kcl_vga_switcheroo_register_handler(struct vga_switcheroo_handler *handler,
						      int handler_flags)
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
static inline int kcl_vga_switcheroo_register_handler(const struct vga_switcheroo_handler *handler,
						      int handler_flags)
#else
static inline int kcl_vga_switcheroo_register_handler(const struct vga_switcheroo_handler *handler,
		enum vga_switcheroo_handler_flags_t handler_flags)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
	return vga_switcheroo_register_handler(handler);
#else
	/* the value fo handler_flags is enumerated in vga_switcheroo_handler_flags_t
	 * in vga_switheroo.h */
	return vga_switcheroo_register_handler(handler, handler_flags);
#endif
}
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
static inline int kcl_vga_switcheroo_register_handler(struct vga_switcheroo_handler *handler,
						      int handler_flags)
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
static inline int kcl_vga_switcheroo_register_handler(const struct vga_switcheroo_handler *handler,
						      int handler_flags)
#else
static inline int kcl_vga_switcheroo_register_handler(const struct vga_switcheroo_handler *handler,
		enum vga_switcheroo_handler_flags_t handler_flags)
#endif
	{ return 0; }
#endif /* defined(CONFIG_VGA_SWITCHEROO) */

#endif /* AMDKCL_VGA_SWITCHEROO_H */
