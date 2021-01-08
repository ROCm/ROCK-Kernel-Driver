dnl #
dnl # v4.20-rc2-10-ge07db28eea38
dnl # kbuild: fix single target build for external module
dnl #
AC_DEFUN([AC_KERNEL_SINGLE_TARGET], [
	AC_KERNEL_TMP_BUILD_DIR([
		AC_KERNEL_TRY_COMPILE([], [], [], [
			SINGLE_TARGET_BUILD_MODVERDIR=.tmp_versions
			AS_IF([test ! -d $SINGLE_TARGET_BUILD_MODVERDIR], [
				SINGLE_TARGET_BUILD_NO_TMP_VERSIONS=1
			], [
				AC_MSG_WARN([compile single target fail expectedly])
			])
		])
	])
])
