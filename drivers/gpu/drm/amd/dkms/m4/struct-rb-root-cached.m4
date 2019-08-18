dnl #
dnl #commit cd9e61ed1eebbcd5dfad59475d41ec58d9b64b6a
dnl #Author: Davidlohr Bueso <dave@stgolabs.net>
dnl #Date:   Fri Sep 8 16:14:36 2017 -0700
dnl #rbtree: cache leftmost node internally
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_RB_ROOT_CACHED],
		[AC_MSG_CHECKING([whether struct rb_root_cached is defined])
		AC_KERNEL_TRY_COMPILE([
				#include <linux/rbtree.h>
		],[
				struct rb_root_cached objects;
		],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_STRUCT_RB_ROOT_CACHED, 1, [struct rb_root_cached is defined])
		],[
				AC_MSG_RESULT(no)
		])
])

