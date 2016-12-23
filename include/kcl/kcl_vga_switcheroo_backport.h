#ifndef AMDKCL_VGA_SWITCHEROO_BACKPORT_H
#define AMDKCL_VGA_SWITCHEROO_BACKPORT_H
#include <kcl/kcl_vga_switcheroo.h>
#include <linux/vga_switcheroo.h>

#if !defined(HAVE_VGA_SWITCHEROO_REGISTER_CLIENT_P_P_B)
#define vga_switcheroo_register_client _kcl_vga_switcheroo_register_client
#endif

#if !defined(HAVE_VGA_SWITCHEROO_REGISTER_HANDLER_PC_E)
#define vga_switcheroo_register_handler _kcl_vga_switcheroo_register_handler
#endif
#endif
