dnl #
dnl # commit v5.3-rc1-675-g8806cd3aa025
dnl # drm: Rename HDMI colorspace property creation function
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CONNECT_ATTACH_COLORSPACE_PROPERTY], [
    AC_KERNEL_DO_BACKGROUND([
        AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_connector.h>
		],[
			drm_connector_attach_colorspace_property(NULL);
		],[drm_connector_attach_colorspace_property], [drivers/gpu/drm/drm_connector.c], [
            AC_DEFINE(HAVE_DRM_CONNECT_ATTACH_COLORSPACE_PROPERTY, 1,
                [drm_connector_attach_colorspace_property() is available])
		])
    ])
])

dnl #
dnl # commit v6.1-5783-g08383039cd19
dnl # drm/connector: Allow drivers to pass list of supported colorspaces
dnl #
AC_DEFUN([AC_AMDGPU_DRM_MODE_CREATE_HDMI_COLORSPACE_PROPERTY], [
	AC_KERNEL_TRY_COMPILE_SYMBOL([
   			#include <drm/drm_connector.h>
        ], [
            drm_mode_create_hdmi_colorspace_property(NULL, 0);
   		], [drm_mode_create_hdmi_colorspace_property], [drivers/gpu/drm/drm_connector.c], [
   				AC_DEFINE(HAVE_DRM_MODE_CREATE_HDMI_COLORSPACE_PROPERTY_2ARGS, 1,
                    	[drm_mode_create_hdmi_colorspace_property() has 2 args])
   		])
]) 

dnl #
dnl # commit v6.1-5783-g08383039cd19
dnl # drm/connector: Allow drivers to pass list of supported colorspaces
dnl #
AC_DEFUN([AC_AMDGPU_DRM_MODE_CREATE_DP_COLORSPACE_PROPERTY], [
	AC_KERNEL_TRY_COMPILE_SYMBOL([
   			#include <drm/drm_connector.h>
        ], [
            drm_mode_create_dp_colorspace_property(NULL, 0);
   		], [drm_mode_create_dp_colorspace_property], [drivers/gpu/drm/drm_connector.c], [
   				AC_DEFINE(HAVE_DRM_MODE_CREATE_DP_COLORSPACE_PROPERTY_2ARGS, 1,
                    	[drm_mode_create_dp_colorspace_property() has 2 args])
   		])
])             


AC_DEFUN([AC_AMDGPU_DRM_MODE_CREATE_COLORSPACE_PROPERTY_FUNCS], [
	AC_AMDGPU_DRM_CONNECT_ATTACH_COLORSPACE_PROPERTY
	AC_AMDGPU_DRM_MODE_CREATE_HDMI_COLORSPACE_PROPERTY
	AC_AMDGPU_DRM_MODE_CREATE_DP_COLORSPACE_PROPERTY
])
