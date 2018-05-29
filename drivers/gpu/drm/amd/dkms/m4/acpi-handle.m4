dnl #
dnl # commit 3a83f992490f8235661b768e53bd5f14915420ac
dnl # Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
dnl # Date:   Thu Nov 14 23:17:21 2013 +0100
dnl # ACPI: Eliminate the DEVICE_ACPI_HANDLE() macro
dnl #
AC_DEFUN([AC_AMDGPU_ACPI_HANDLE],
	[AC_MSG_CHECKING([whether ACPI_HANDLE is defined])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/acpi.h>
	], [
		#if !defined(ACPI_HANDLE)
		#error ACPI_HANDLE not #defined
		#endif
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ACPI_HANDLE, 1,
		[ACPI_HANDLE is defined])
	],[
		AC_MSG_RESULT(no)
	])
])
