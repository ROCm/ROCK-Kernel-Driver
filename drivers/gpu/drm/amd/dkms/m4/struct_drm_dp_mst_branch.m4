dnl #
dnl # v5.6-rc7-127-gd01cd62400b3
dnl # uuid: Add inline helpers to import / export UUIDs
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_MST_BRANCH_GUID_T], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <drm/display/drm_dp_mst_helper.h>
                ],[
                        struct drm_dp_mst_branch mst_primary;
                        const guid_t guid;
                        guid_copy(&mst_primary.guid, &guid);
                ],[
                        AC_DEFINE(HAVE_DRM_DP_MST_BRANCH_GUID_T, 1,
                                [the guid of struct drm_dp_mst_branch is guid_t])
                ])
        ])
])



