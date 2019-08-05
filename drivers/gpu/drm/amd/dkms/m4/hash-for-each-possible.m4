dnl #
dnl #commit b67bfe0d42cac56c512dd5da4b1b347a23f4b70a
dnl #Author: Sasha Levin <sasha.levin@oracle.com>
dnl #Date:   Wed Feb 27 17:06:00 2013 -0800
dnl #hlist: drop the node parameter from iterators
dnl #

AC_DEFUN([AC_AMDGPU_4ARGS_HASH_FOR_EACH_POSSIBLE],
		[AC_MSG_CHECKING([whether hash_for_each_possible() wants 4 arguments])
		AC_KERNEL_TRY_COMPILE([
				#include <linux/hashtable.h>
		],[
				struct mm_slot {
					struct hlist_node link;
				};

				struct mm_slot *slot;
				static DEFINE_HASHTABLE(mm_slots_hash, 10);
				hash_for_each_possible(mm_slots_hash, slot, link, 0){}
		],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_4ARGS_HASH_FOR_EACH_POSSIBLE, 1, [hash_for_each_possible() wants 4 arguments])
		],[
				AC_MSG_RESULT(no)
		])
])

