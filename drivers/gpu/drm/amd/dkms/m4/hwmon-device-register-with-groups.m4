dnl # commit bab2243ce1897865e31ea6d59b0478391f51812b
dnl # hwmon: Introduce hwmon_device_register_with_groups
dnl # hwmon_device_register_with_groups() lets callers register a hwmon device
dnl # together with all sysfs attributes in a single call.
dnl # When using hwmon_device_register_with_groups(), hwmon attributes are attached
dnl # to the hwmon device directly and no longer with its parent device.
AC_DEFUN([AC_AMDGPU_HWMON_DEVICE_REGISTER_WITH_GROUPS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/stddef.h>
			#include <linux/hwmon.h>
		], [
			hwmon_device_register_with_groups(NULL, NULL, NULL, NULL);
		], [hwmon_device_register_with_groups], [drivers/hwmon/hwmon.c], [
			AC_DEFINE(HAVE_HWMON_DEVICE_REGISTER_WITH_GROUPS, 1,
				[hwmon_device_register_with_groups() is available])
		])
	])
])
