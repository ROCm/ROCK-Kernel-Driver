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
		],[
			dnl #
			dnl # commit 07da1223ec939982497db3caccd6215b55acc35c
			dnl # v5.9-rc8-3-g07da1223ec93
			dnl # lib/scatterlist: Add support in dynamic allocation of SG table from pages
			dnl #
			AC_KERNEL_TRY_COMPILE_SYMBOL([
				#include <linux/scatterlist.h>
			], [
				__sg_alloc_table_from_pages(NULL,NULL,0,0,0,0,NULL,0,GFP_KERNEL);
			], [__sg_alloc_table_from_pages],[lib/scatterlist.c], [
				AC_DEFINE(HAVE___SG_ALLOC_TABLE_FROM_PAGES_9ARGS, 1,
					[__sg_alloc_table_from_pages() has 9 args])
			])
		])
	])
])
