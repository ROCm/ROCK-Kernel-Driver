dnl #
dnl # commit v3.8-9570-gb67bfe0d42ca
dnl # hlist: drop the node parameter from iterators
dnl #
AC_DEFUN([AC_AMDGPU_HASH_FOR_EACH_XXX], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/hashtable.h>
		], [
			struct mm_slot {
				struct hlist_node link;
			};

			struct mm_slot *slot;
			static DEFINE_HASHTABLE(mm_slots_hash, 10);
			hash_for_each_possible(mm_slots_hash, slot, link, 0){}
		], [
			AC_DEFINE(HAVE_HASH_FOR_EACH_XXX_DROP_NODE, 1,
				[hash_for_each_xxx() drop the node parameter])
		])
	])
])
