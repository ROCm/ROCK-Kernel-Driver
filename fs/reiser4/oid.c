/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

#include "debug.h"
#include "super.h"
#include "txnmgr.h"

/* we used to have oid allocation plugin. It was removed because it
   was recognized as providing unneeded level of abstraction. If one
   ever will find it useful - look at yet_unneeded_abstractions/oid
*/

/*
 * initialize in-memory data for oid allocator at @super. @nr_files and @next
 * are provided by disk format plugin that reads them from the disk during
 * mount.
 */
reiser4_internal int
oid_init_allocator(struct super_block *super, oid_t nr_files, oid_t next)
{
	reiser4_super_info_data *sbinfo;

	sbinfo = get_super_private(super);

	sbinfo->next_to_use = next;
	sbinfo->oids_in_use = nr_files;
	return 0;
}

/*
 * allocate oid and return it. ABSOLUTE_MAX_OID is returned when allocator
 * runs out of oids.
 */
reiser4_internal oid_t
oid_allocate(struct super_block *super)
{
	reiser4_super_info_data *sbinfo;
	oid_t oid;

	sbinfo = get_super_private(super);

	reiser4_spin_lock_sb(sbinfo);
	if (sbinfo->next_to_use != ABSOLUTE_MAX_OID) {
		oid = sbinfo->next_to_use ++;
		sbinfo->oids_in_use ++;
	} else
		oid = ABSOLUTE_MAX_OID;
	reiser4_spin_unlock_sb(sbinfo);
	return oid;
}

/*
 * Tell oid allocator that @oid is now free.
 */
reiser4_internal int
oid_release(struct super_block *super, oid_t oid UNUSED_ARG)
{
	reiser4_super_info_data *sbinfo;

	sbinfo = get_super_private(super);

	reiser4_spin_lock_sb(sbinfo);
	sbinfo->oids_in_use --;
	reiser4_spin_unlock_sb(sbinfo);
	return 0;
}

/*
 * return next @oid that would be allocated (i.e., returned by oid_allocate())
 * without actually allocating it. This is used by disk format plugin to save
 * oid allocator state on the disk.
 */
reiser4_internal oid_t oid_next(const struct super_block *super)
{
	reiser4_super_info_data *sbinfo;
	oid_t oid;

	sbinfo = get_super_private(super);

	reiser4_spin_lock_sb(sbinfo);
	oid = sbinfo->next_to_use;
	reiser4_spin_unlock_sb(sbinfo);
	return oid;
}

/*
 * returns number of currently used oids. This is used by statfs(2) to report
 * number of "inodes" and by disk format plugin to save oid allocator state on
 * the disk.
 */
reiser4_internal long oids_used(const struct super_block *super)
{
	reiser4_super_info_data *sbinfo;
	oid_t used;

	sbinfo = get_super_private(super);

	reiser4_spin_lock_sb(sbinfo);
	used = sbinfo->oids_in_use;
	reiser4_spin_unlock_sb(sbinfo);
	if (used < (__u64) ((long) ~0) >> 1)
		return (long) used;
	else
		return (long) -1;
}

/*
 * return number of "free" oids. This is used by statfs(2) to report "free"
 * inodes.
 */
reiser4_internal long oids_free(const struct super_block *super)
{
	reiser4_super_info_data *sbinfo;
	oid_t oids;

	sbinfo = get_super_private(super);

	reiser4_spin_lock_sb(sbinfo);
	oids = ABSOLUTE_MAX_OID - OIDS_RESERVED - sbinfo->next_to_use;
	reiser4_spin_unlock_sb(sbinfo);
	if (oids < (__u64) ((long) ~0) >> 1)
		return (long) oids;
	else
		return (long) -1;
}

/*
 * Count oid as allocated in atom. This is done after call to oid_allocate()
 * at the point when we are irrevocably committed to creation of the new file
 * (i.e., when oid allocation cannot be any longer rolled back due to some
 * error).
 */
reiser4_internal void
oid_count_allocated(void)
{
	txn_atom *atom;

	atom = get_current_atom_locked();
	atom->nr_objects_created++;
	UNLOCK_ATOM(atom);
}

/*
 * Count oid as free in atom. This is done after call to oid_release() at the
 * point when we are irrevocably committed to the deletion of the file (i.e.,
 * when oid release cannot be any longer rolled back due to some error).
 */
reiser4_internal void
oid_count_released(void)
{
	txn_atom *atom;

	atom = get_current_atom_locked();
	atom->nr_objects_deleted++;
	UNLOCK_ATOM(atom);
}

/*
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
