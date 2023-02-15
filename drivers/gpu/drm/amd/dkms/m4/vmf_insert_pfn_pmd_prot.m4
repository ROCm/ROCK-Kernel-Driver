dnl #
dnl # v5.6-rc5-1489-g314b6580adc5 drm/ttm, drm/vmwgfx: Support huge TTM pagefaults
dnl # v5.6-rc5-1488-g9a9731b18c9b mm: Add vmf_insert_pfn_xxx_prot() for huge page-table entries
dnl #
AC_DEFUN([AC_AMDGPU_VMF_INSERT_PFN_PMD_PROT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_CHECK_SYMBOL_EXPORT([vmf_insert_pfn_pmd_prot],
			[mm/huge_memory.c], [
			AC_DEFINE(HAVE_VMF_INSERT_PFN_PMD_PROT,
				1,
				[vmf_insert_pfn_{pmd,pud}_prot() is available])
		], [
		dnl #
		dnl # v5.1-10136-gfce86ff5802b
		dnl # mm/huge_memory: fix vmf_insert_pfn_{pmd, pud}() crash, handle unaligned addresses
		dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <linux/mm.h>
			], [
				pfn_t *pfn = NULL;
				vmf_insert_pfn_pmd(NULL, *pfn, 0);
			], [
				AC_DEFINE(HAVE_VMF_INSERT_PFN_PMD_3ARGS,
					1,
					[vmf_insert_pfn_{pmd,pud}() wants 3 args])
			])
		])
	])
])
