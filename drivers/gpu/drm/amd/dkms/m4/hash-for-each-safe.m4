dnl #
dnl #commit b67bfe0d42cac56c512dd5da4b1b347a23f4b70a
dnl #Author: Sasha Levin <sasha.levin@oracle.com>
dnl #Date:   Wed Feb 27 17:06:00 2013 -0800
dnl #hlist: drop the node parameter from iterators
dnl #

AC_DEFUN([AC_AMDGPU_5ARGS_HASH_FOR_EACH_SAFE],
		[AC_MSG_CHECKING([whether hash_for_each_safe() wants 5 arguments])
		AC_KERNEL_TRY_COMPILE([
				#include <linux/hashtable.h>
		],[
				struct node_hash {
					struct hlist_node node;
				};

				static DEFINE_HASHTABLE(phandle_ht, 8);
				struct node_hash *nh;
				struct hlist_node *tmp;
				int i = 0;
				hash_for_each_safe(phandle_ht, i, tmp, nh, node){}
		],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_5ARGS_HASH_FOR_EACH_SAFE, 1, [hash_for_each_safe() wants 5 arguments])
		],[
				AC_MSG_RESULT(no)
		])
])

