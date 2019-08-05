dnl #
dnl #commit b67bfe0d42cac56c512dd5da4b1b347a23f4b70a
dnl #Author: Sasha Levin <sasha.levin@oracle.com>
dnl #Date:   Wed Feb 27 17:06:00 2013 -0800
dnl #hlist: drop the node parameter from iterators
dnl #

AC_DEFUN([AC_AMDGPU_4ARGS_HASH_FOR_EACH_RCU],
		[AC_MSG_CHECKING([whether hash_for_each_rcu() wants 4 arguments])
		AC_KERNEL_TRY_COMPILE([
				#include <linux/hashtable.h>
		],[
				struct dfs_cache_entry {
				struct hlist_node ce_hlist;
				};
				static struct hlist_head dfs_cache_htable[32];
				struct dfs_cache_entry *ce;
				int bucket = 0;
				hash_for_each_rcu(dfs_cache_htable, bucket, ce, ce_hlist){}
		],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_4ARGS_HASH_FOR_EACH_RCU, 1, [hash_for_each_rcu() wants 4 arguments])
		],[
				AC_MSG_RESULT(no)
		])
])

