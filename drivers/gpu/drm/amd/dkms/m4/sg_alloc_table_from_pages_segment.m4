dnl #
dnl # commit 90e7a6de62781c27d6a111fccfb19b807f9b6887
dnl # v5.14-rc6-1-g90e7a6de6278
dnl # lib/scatterlist: Provide a dedicated function to support table append
dnl #
AC_DEFUN([AC_AMDGPU_SG_ALLOC_TABLE_FROM_PAGES_SEGMENT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/scatterlist.h>
		], [
			sg_alloc_table_from_pages_segment(NULL,NULL,0,0,0,0,GFP_KERNEL);
		], [sg_alloc_table_from_pages_segment],[lib/scatterlist.c], [
			AC_DEFINE(HAVE_SG_ALLOC_TABLE_FROM_PAGES_SEGMENT, 1,
				[sg_alloc_table_from_pages_segment() is available])
		])
	])
])
