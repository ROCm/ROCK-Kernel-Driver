dnl #
dnl # commit: v5.1-rc3-1-g817b4d64da03
dnl # ACPI / utils: Introduce acpi_dev_get_first_match_dev() helper
dnl #
AC_DEFUN([AC_AMDGPU_ACPI_DEV_GET_FIRST_MATCH_DEV], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/acpi.h>
		],[
			acpi_dev_get_first_match_dev(NULL, NULL, 0);
		],[acpi_dev_get_first_match_dev],[drivers/acpi/utils.c], [
			AC_DEFINE(HAVE_ACPI_DEV_GET_FIRST_MATCH_DEV, 1,
				[acpi_dev_get_first_match_dev() is available])
		])
	])
])
