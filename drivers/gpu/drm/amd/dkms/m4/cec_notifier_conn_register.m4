dnl #
dnl # commit v5.2-rc5-393-gb48cb35c6a7b
dnl # media: cec-notifier: add new notifier functions
dnl #
AC_DEFUN([AC_AMDGPU_CEC_NOTIFIER_CONN_REGISTER], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <media/cec-notifier.h>
		], [
			cec_notifier_conn_register(NULL, NULL, NULL);
		], [cec_notifier_conn_register], [drivers/media/cec/core/cec-notifier.c], [
			AC_DEFINE(HAVE_CEC_NOTIFIER_CONN_REGISTER, 1,
				[cec_notifier_conn_register() is available])
		])
	])
])
