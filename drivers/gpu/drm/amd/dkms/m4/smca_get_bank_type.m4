dnl #
dnl # v5.16-rc1-22-g91f75eb481cf x86/MCE/AMD, EDAC/mce_amd: Support non-uniform MCA bank type enumeration
dnl #
AC_DEFUN([AC_AMDGPU_SMCA_GET_BANK_TYPE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/limits.h>
			#include <asm/mce.h>
		],[    
			unsigned int a = 0, b = 0;
			enum smca_bank_types bank_type;
			bank_type = smca_get_bank_type(a, b);
		],[
			AC_DEFINE(HAVE_SMCA_GET_BANK_TYPE_WITH_TWO_ARGUMENTS, 1,
				[whether smca_get_bank_type(x, x) is available])
		],[
			dnl #
			dnl # v5.15-rc2-452-gf38ce910d8df x86/MCE/AMD: Export smca_get_bank_type symbol
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <linux/limits.h>
				#include <asm/mce.h>
			],[
				unsigned int a = 0;
				enum smca_bank_types bank_type;
				bank_type = smca_get_bank_type(a);
			],[
				AC_DEFINE(HAVE_SMCA_GET_BANK_TYPE_WITH_ONE_ARGUMENT, 1,
					[smca_get_bank_type(x) is available])
			],[
				dnl #
				dnl # v4.9-rc4-4-g79349f529ab1 x86/RAS: Simplify SMCA bank descriptor struct
				dnl #
				AC_KERNEL_TRY_COMPILE([
					#include <linux/limits.h>
					#include <asm/mce.h>
				],[
					struct smca_bank *b = NULL;
					b->id = 0;
				], [
					AC_DEFINE(HAVE_STRUCT_SMCA_BANK, 1,
						[struct smca_bank is available])
				])
			
			])
		])
	
	])
])
