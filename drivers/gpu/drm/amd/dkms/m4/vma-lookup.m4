dnl #
dnl # v5.13-105-gce6d42f2e4a2
dnl # mm: add vma_lookup(), update find_vma_intersection() comments
dnl #
AC_DEFUN([AC_AMDGPU_VMA_LOOKUP], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/mm.h>
		], [
			vma_lookup(NULL, 0);
		], [
			AC_DEFINE(HAVE_VMA_LOOKUP, 1,
				[vma_lookup() is available])
		])
	])
])

dnl #
dnl # v6.2-rc4-446-gbc292ab00f6c
dnl # mm: introduce vma->vm_flags wrapper functions
dnl #
AC_DEFUN([AC_AMDGPU_VM_FLAGS_SET], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/mm.h>
		], [
			vm_flags_set(NULL, 0);
			vm_flags_clear(NULL, 0);
		], [
			AC_DEFINE(HAVE_VM_FLAGS_SET, 1,
				[vm_flags_{set, clear}  is available])
		])
	])
])
