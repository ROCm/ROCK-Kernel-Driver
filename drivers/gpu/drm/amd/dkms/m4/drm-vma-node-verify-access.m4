dnl #
dnl # commit 5488dc16fde74595a40c5d20ae52d978313f0b4e
dnl # drm: introduce pipe color correction properties
dnl #
AC_DEFUN([AC_AMDGPU_DRM_VMA_NODE_VERIFY_ACCESS],
	[AC_MSG_CHECKING([whether drm_vma_node_verify_access() 2nd argument is drm_file])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_vma_manager.h>
	], [
		struct drm_vma_offset_node *node = NULL;
		struct drm_file *tag = NULL;
		drm_vma_node_verify_access(node, tag);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(DRM_VMA_NODE_VERIFY_ACCESS_HAS_DRM_FILE, 1, [drm_vma_node_verify_access() 2nd argument is drm_file])
	], [
		AC_MSG_RESULT(no)
	])
])
