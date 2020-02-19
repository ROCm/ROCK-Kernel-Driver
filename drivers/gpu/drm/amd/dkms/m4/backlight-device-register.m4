dnl #
dnl # commit a19a6ee6cad2b20292a774c2f56ba8039b0fac9c
dnl # backlight: Allow properties to be passed at registration
dnl #
AC_DEFUN([AC_AMDGPU_BACKLIGHT_DEVICE_REGISTER_WITH_5ARGS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/backlight.h>
		], [
			backlight_device_register(NULL, NULL, NULL, NULL, NULL);
		], [
			AC_DEFINE(HAVE_BACKLIGHT_DEVICE_REGISTER_WITH_5ARGS, 1,
				[backlight_device_register() with 5 args is available])
		])
	])
])
