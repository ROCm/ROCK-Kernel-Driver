dnl #
dnl # commit v5.13-rc3-1382-gf425821b9468
dnl # drm/vma: Add a driver_private member to vma_node.
dnl #
AC_DEFUN([AC_AMDGPU_DRM_VMA_OFFSET_NODE_READONLY_FIELD], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_vma_manager.h>
		], [
			struct drm_vma_offset_node *node = NULL;
			node->readonly = false;
		], [
			AC_DEFINE(HAVE_DRM_VMA_OFFSET_NODE_READONLY_FIELD, 1, [struct drm_vma_offset_node has readonly field])
		])
	])
])
