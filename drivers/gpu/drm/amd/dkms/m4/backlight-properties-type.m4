dnl #
dnl # commit bb7ca747f8d6243b3943c5b133048652020f4a50
dnl # backlight: add backlight type
dnl #
AC_DEFUN([AC_AMDGPU_BACKLIGHT_PROPERTIES_TYPE],
	[AC_MSG_CHECKING([whether backlight_properties->type is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/backlight.h>
	],[
		struct backlight_properties *ptest = NULL;
		ptest->type = BACKLIGHT_RAW;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BACKLIGHT_PROPERTIES_TYPE, 1, [backlight_properties->type is available])
	],[
		AC_MSG_RESULT(no)
	])
])
