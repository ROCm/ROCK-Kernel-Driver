dnl #
dnl # commit 156d7d4120e1c860fde667fc30eeae84bc3e7a25
dnl # vga_switcheroo: Add handler flags infrastructure
dnl #
AC_DEFUN([AC_AMDGPU_VGA_SWITCHEROO_REGISTER_HANDLER], [
	AC_MSG_CHECKING([whether vga_switeroo_register_handler() with 2 args is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/vga_switcheroo.h>
	], [
		vga_switcheroo_register_handler(NULL, 0);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_VGA_SWITCHEROO_REGISTER_HANDLER, 1, [vga_switeroo_register_handler() with 2 args])
	], [
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether vga_switeroo_register_handler() with 1 const arg])
		AC_KERNEL_TRY_COMPILE([
			#include <linux/vga_switcheroo.h>
		], [
			vga_switcheroo_register_handler((const struct vga_switcheroo_handler *)NULL);
		], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_1ARG_CONST_VGA_SWITCHEROO_REGISTER_HANDLER, 1, [vga_switeroo_register_handler() with 1 const arg])
		], [
			AC_MSG_RESULT(no)
		])
	])
])
