dnl #
dnl # v6.9-11925-g2c92ca849fcc
dnl # tracing/treewide: Remove second parameter of __assign_str()
dnl # Due to trace system bases on runtime, so use script to handle specially
dnl #
AC_DEFUN([AC_AMDGPU_ASSIGN_STR], [
        AC_KERNEL_DO_BACKGROUND([
                header_file=stage6_event_callback.h
                header_file_src=$LINUX/include/trace/stages/$header_file
                AS_IF([test -f "$header_file_src"], [
                        AS_IF([grep -q '^#define __assign_str(dst)' $header_file_src], [
                        AC_DEFINE(HAVE_ASSIGN_STR_ONE_ARGUMENT, 1,
				[__assign_str() wants 1 arguments])
                        ])
                ])
        ])
])