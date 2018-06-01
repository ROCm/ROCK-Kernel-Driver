dnl #
dnl # commit 5e19b013f55a884c59a14391b22138899d1cc4cc
dnl # lib: bitmap: add alignment offset for bitmap_find_next_zero_area()
dnl #
AC_DEFUN([AC_AMDGPU_BITMAP_FIND_NEXT_ZERO_AREA_OFF],
	[AC_MSG_CHECKING([whether bitmap_find_next_zero_area_off() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/bitmap.h>
	], [
		bitmap_find_next_zero_area_off(NULL, 0, 0, 0, 0, 0);
	], [bitmap_find_next_zero_area_off], [lib/bitmap.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BITMAP_FIND_NEXT_ZERO_AREA_OFF, 1, [bitmap_find_next_zero_area_off() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
