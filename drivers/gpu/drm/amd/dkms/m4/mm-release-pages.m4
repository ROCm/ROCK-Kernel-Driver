dnl #
dnl # commit c6f92f9fbe7dbcc8903a67229aa88b4077ae4422
dnl # mm: remove cold parameter for release_pages
dnl #
AC_DEFUN([AC_AMDGPU_2ARGS_MM_RELEASE_PAGES],
	[AC_MSG_CHECKING([whether release_pages() wants 2 args])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/pagemap.h>
	], [
		struct page **pages = NULL;
		int nr = 0;

		release_pages(pages, nr);
	], [release_pages], [mm/swap.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_MM_RELEASE_PAGES, 1, [release_pages() wants 2 args])
	], [
		AC_MSG_RESULT(no)
	])
])
