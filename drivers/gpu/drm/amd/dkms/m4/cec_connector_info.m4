dnl #
dnl #
dnl # v5.2-rc5-392-g32a847f9fa40
dnl # media: cec: add struct cec_connector_info support
dnl #
dnl # v5.4-rc1-53-g9098c1c251ff
dnl # media: cec: expose the new connector info API
dnl # The structure cec_connector_info has been moved from the media/cec.h to the uapi/linux/cec.h
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_CEC_CONNECTOR_INFO], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <uapi/linux/cec.h>
			#include <media/cec.h>
		], [
			struct cec_connector_info conn_info;
			memset(&conn_info, 0, sizeof(conn_info));
		], [
			AC_DEFINE(HAVE_CEC_CONNECTOR_INFO, 1,
				[struct cec_connector_info is available])
		])
	])
])
