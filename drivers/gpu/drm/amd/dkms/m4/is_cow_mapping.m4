dnl #
dnl # commit 97a7e4733b9b221d012ae68fcd3b3251febf6341
dnl # mm: introduce page_needs_cow_for_dma() for deciding whether cow
dnl #
AC_DEFUN([AC_AMDGPU_IS_COW_MAPPING], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/mm.h>
		], [
			is_cow_mapping(VM_SHARED);
		], [
			AC_DEFINE(HAVE_IS_COW_MAPPING, 1, [is_cow_mapping() is available])
		])
	])
])
