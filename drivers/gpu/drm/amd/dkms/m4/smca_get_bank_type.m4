dnl #
dnl #
dnl # v5.15-rc2-452-gf38ce910d8df x86/MCE/AMD: Export smca_get_bank_type symbol
dnl #
AC_DEFUN([AC_AMDGPU_SMCA_GET_BANK_TYPE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_CHECK_SYMBOL_EXPORT([smca_get_bank_type],
		[arch/x86/kernel/cpu/mce/amd.c], [
			AC_DEFINE(HAVE_SMCA_GET_BANK_TYPE, 1,
				[smca_get_bank_type() is available])
		], [
			dnl #
			dnl #
			dnl # v4.9-rc4-4-g79349f529ab1 x86/RAS: Simplify SMCA bank descriptor struct
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <asm/mce.h>
			], [
				struct smca_bank *b = NULL;
				b->id = 0;
			], [
				AC_DEFINE(HAVE_STRUCT_SMCA_BANK, 1,
					[struct smca_bank is available])
			])
		])
	])
])
