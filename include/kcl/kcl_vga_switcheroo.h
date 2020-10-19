/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_VGA_SWITCHEROO_H
#define AMDKCL_VGA_SWITCHEROO_H

#include <linux/vga_switcheroo.h>

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
