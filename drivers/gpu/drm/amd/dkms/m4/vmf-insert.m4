dnl #
dnl # commit 34c0fd540e79fb49ef9ce864dae1058cca265780
dnl # mm, dax, pmem: introduce pfn_t
dnl #
AC_DEFUN([AC_AMDGPU_VMF_INSERT], [
	AC_MSG_CHECKING([whether pfn_t type is defined])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/mm.h>
	], [
		pfn_t pfn;
		pfn.val = 0;
	], [
		dnl #
		dnl # commit 1c8f422059ae5da07db7406ab916203f9417e396
		dnl # mm: change return type to vm_fault_t
		dnl #
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PFN_T, 1, [pfn_t is defined])

		AC_MSG_CHECKING([whether vmf_insert_*() functions are available])
		AC_KERNEL_TRY_COMPILE([
			#include <linux/mm.h>
		], [
			pfn_t pfn = {};
			vmf_insert_mixed(NULL, 0, pfn);
			vmf_insert_pfn(NULL, 0, 0);
		], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_VMF_INSERT, 1, [vmf_insert_*() are available])
		], [
			dnl #
			dnl # commit 01c8f1c44b83a0825b573e7c723b033cece37b86
			dnl # mm, dax, gpu: convert vm_insert_mixed to pfn_t
			dnl #
			AC_MSG_RESULT(no)
			AC_MSG_CHECKING([whether vm_insert_mixed() function wants pfn_t or unsigned long arg])
			AC_KERNEL_TRY_COMPILE_SYMBOL([
				#include <linux/mm.h>
			], [
				pfn_t pfn = {};
				vm_insert_mixed(NULL, 0, pfn);
			], [vm_insert_mixed], [mm/memory.c], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_PFN_T_VM_INSERT_MIXED, 1, [vm_insert_mixed() wants pfn_t arg])
			], [
				AC_MSG_RESULT(no)
			])
		])
	], [
		AC_MSG_RESULT(no)
	])
])
