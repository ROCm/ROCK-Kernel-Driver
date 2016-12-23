dnl #
dnl # commit 156d7d4120e1c860fde667fc30eeae84bc3e7a25
dnl # vga_switcheroo: Add handler flags infrastructure
dnl #
AC_DEFUN([AC_AMDGPU_VGA_SWITCHEROO_REGISTER_HANDLER], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/vga_switcheroo.h>
		], [
			vga_switcheroo_register_handler(NULL, 0);
		], [
			AC_DEFINE(HAVE_VGA_SWITCHEROO_REGISTER_HANDLER_PC_E, 1,
				[vga_switeroo_register_handler() has p,e interface])
			AC_DEFINE(HAVE_VGA_SWITCHEROO_HANDLER_FLAGS_T_ENUM, 1,
				[enum vga_switcheroo_handler_flags_t is available])
		], [
			AC_KERNEL_TRY_COMPILE([
				#include <linux/vga_switcheroo.h>
			], [
				vga_switcheroo_register_handler((const struct vga_switcheroo_handler *)NULL);
			], [
				AC_DEFINE(HAVE_VGA_SWITCHEROO_REGISTER_HANDLER_PC, 1,
					[vga_switeroo_register_handler() p_c interface])
			])
		])
	])
])
