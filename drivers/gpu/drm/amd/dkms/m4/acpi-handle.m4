dnl #
dnl # 3.13 API added ACPI_HANDLE
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
