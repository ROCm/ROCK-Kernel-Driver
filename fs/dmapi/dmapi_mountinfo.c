/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#include "dmapi.h"
#include "dmapi_kern.h"
#include "dmapi_private.h"

static LIST_HEAD(dm_fsys_map);
static spinlock_t dm_fsys_lock = SPIN_LOCK_UNLOCKED;

int
dm_code_level(void)
{
	return DM_CLVL_XOPEN;	/* initial X/Open compliant release */
}


/* Dummy routine which is stored in each function vector slot for which the
   filesystem provides no function of its own.	If an application calls the
   function, he will just get ENOSYS.
*/

static int
dm_enosys(void)
{
	return -ENOSYS;		/* function not supported by filesystem */
}


/* dm_query_fsys_for_vector() asks a filesystem for its list of supported
   DMAPI functions, and builds a dm_vector_map_t structure based upon the
   reply.  We ignore functions supported by the filesystem which we do not
   know about, and we substitute the subroutine 'dm_enosys' for each function
   we know about but the filesystem does not support.
*/

static void
dm_query_fsys_for_vector(
	dm_vector_map_t		*map)
{
	struct super_block	*sb = map->sb;
	fsys_function_vector_t	*vecp;
	dm_fcntl_vector_t	vecrq;
	dm_fsys_vector_t	*vptr;
	struct filesystem_dmapi_operations *dmapiops = map->dmapiops;
	int			error;
	int			i;


	/* Allocate a function vector and initialize all fields with a
	   dummy function that returns ENOSYS.
	*/

	vptr = map->vptr = kmem_cache_alloc(dm_fsys_vptr_cachep, GFP_KERNEL);
	if (vptr == NULL) {
		printk("%s/%d: kmem_cache_alloc(dm_fsys_vptr_cachep) returned NULL\n", __FUNCTION__, __LINE__);
		return;
	}

	vptr->code_level = 0;
	vptr->clear_inherit = (dm_fsys_clear_inherit_t)dm_enosys;
	vptr->create_by_handle = (dm_fsys_create_by_handle_t)dm_enosys;
	vptr->downgrade_right = (dm_fsys_downgrade_right_t)dm_enosys;
	vptr->get_allocinfo_rvp = (dm_fsys_get_allocinfo_rvp_t)dm_enosys;
	vptr->get_bulkall_rvp = (dm_fsys_get_bulkall_rvp_t)dm_enosys;
	vptr->get_bulkattr_rvp = (dm_fsys_get_bulkattr_rvp_t)dm_enosys;
	vptr->get_config = (dm_fsys_get_config_t)dm_enosys;
	vptr->get_config_events = (dm_fsys_get_config_events_t)dm_enosys;
	vptr->get_destroy_dmattr = (dm_fsys_get_destroy_dmattr_t)dm_enosys;
	vptr->get_dioinfo = (dm_fsys_get_dioinfo_t)dm_enosys;
	vptr->get_dirattrs_rvp = (dm_fsys_get_dirattrs_rvp_t)dm_enosys;
	vptr->get_dmattr = (dm_fsys_get_dmattr_t)dm_enosys;
	vptr->get_eventlist = (dm_fsys_get_eventlist_t)dm_enosys;
	vptr->get_fileattr = (dm_fsys_get_fileattr_t)dm_enosys;
	vptr->get_region = (dm_fsys_get_region_t)dm_enosys;
	vptr->getall_dmattr = (dm_fsys_getall_dmattr_t)dm_enosys;
	vptr->getall_inherit = (dm_fsys_getall_inherit_t)dm_enosys;
	vptr->init_attrloc = (dm_fsys_init_attrloc_t)dm_enosys;
	vptr->mkdir_by_handle = (dm_fsys_mkdir_by_handle_t)dm_enosys;
	vptr->probe_hole = (dm_fsys_probe_hole_t)dm_enosys;
	vptr->punch_hole = (dm_fsys_punch_hole_t)dm_enosys;
	vptr->read_invis_rvp = (dm_fsys_read_invis_rvp_t)dm_enosys;
	vptr->release_right = (dm_fsys_release_right_t)dm_enosys;
	vptr->request_right = (dm_fsys_request_right_t)dm_enosys;
	vptr->remove_dmattr = (dm_fsys_remove_dmattr_t)dm_enosys;
	vptr->set_dmattr = (dm_fsys_set_dmattr_t)dm_enosys;
	vptr->set_eventlist = (dm_fsys_set_eventlist_t)dm_enosys;
	vptr->set_fileattr = (dm_fsys_set_fileattr_t)dm_enosys;
	vptr->set_inherit = (dm_fsys_set_inherit_t)dm_enosys;
	vptr->set_region = (dm_fsys_set_region_t)dm_enosys;
	vptr->symlink_by_handle = (dm_fsys_symlink_by_handle_t)dm_enosys;
	vptr->sync_by_handle = (dm_fsys_sync_by_handle_t)dm_enosys;
	vptr->upgrade_right = (dm_fsys_upgrade_right_t)dm_enosys;
	vptr->write_invis_rvp = (dm_fsys_write_invis_rvp_t)dm_enosys;
	vptr->obj_ref_hold = (dm_fsys_obj_ref_hold_t)dm_enosys;

	/* Issue a call to the filesystem in order to obtain
	   its vector of filesystem-specific DMAPI routines.
	*/

	vecrq.count = 0;
	vecrq.vecp = NULL;

  	error = -ENOSYS;
	ASSERT(dmapiops);
	if (dmapiops->get_fsys_vector)
		error = dmapiops->get_fsys_vector(sb, (caddr_t)&vecrq);

	/* If we still have an error at this point, then the filesystem simply
	   does not support DMAPI, so we give up with all functions set to
	   ENOSYS.
	*/

	if (error || vecrq.count == 0) {
		kmem_cache_free(dm_fsys_vptr_cachep, vptr);
		map->vptr = NULL;
		return;
	}

	/* The request succeeded and we were given a vector which we need to
	   map to our current level.  Overlay the dummy function with every
	   filesystem function we understand.
	*/

	vptr->code_level = vecrq.code_level;
	vecp = vecrq.vecp;
	for (i = 0; i < vecrq.count; i++) {
		switch (vecp[i].func_no) {
		case DM_FSYS_CLEAR_INHERIT:
			vptr->clear_inherit = vecp[i].u_fc.clear_inherit;
			break;
		case DM_FSYS_CREATE_BY_HANDLE:
			vptr->create_by_handle = vecp[i].u_fc.create_by_handle;
			break;
		case DM_FSYS_DOWNGRADE_RIGHT:
			vptr->downgrade_right = vecp[i].u_fc.downgrade_right;
			break;
		case DM_FSYS_GET_ALLOCINFO_RVP:
			vptr->get_allocinfo_rvp = vecp[i].u_fc.get_allocinfo_rvp;
			break;
		case DM_FSYS_GET_BULKALL_RVP:
			vptr->get_bulkall_rvp = vecp[i].u_fc.get_bulkall_rvp;
			break;
		case DM_FSYS_GET_BULKATTR_RVP:
			vptr->get_bulkattr_rvp = vecp[i].u_fc.get_bulkattr_rvp;
			break;
		case DM_FSYS_GET_CONFIG:
			vptr->get_config = vecp[i].u_fc.get_config;
			break;
		case DM_FSYS_GET_CONFIG_EVENTS:
			vptr->get_config_events = vecp[i].u_fc.get_config_events;
			break;
		case DM_FSYS_GET_DESTROY_DMATTR:
			vptr->get_destroy_dmattr = vecp[i].u_fc.get_destroy_dmattr;
			break;
		case DM_FSYS_GET_DIOINFO:
			vptr->get_dioinfo = vecp[i].u_fc.get_dioinfo;
			break;
		case DM_FSYS_GET_DIRATTRS_RVP:
			vptr->get_dirattrs_rvp = vecp[i].u_fc.get_dirattrs_rvp;
			break;
		case DM_FSYS_GET_DMATTR:
			vptr->get_dmattr = vecp[i].u_fc.get_dmattr;
			break;
		case DM_FSYS_GET_EVENTLIST:
			vptr->get_eventlist = vecp[i].u_fc.get_eventlist;
			break;
		case DM_FSYS_GET_FILEATTR:
			vptr->get_fileattr = vecp[i].u_fc.get_fileattr;
			break;
		case DM_FSYS_GET_REGION:
			vptr->get_region = vecp[i].u_fc.get_region;
			break;
		case DM_FSYS_GETALL_DMATTR:
			vptr->getall_dmattr = vecp[i].u_fc.getall_dmattr;
			break;
		case DM_FSYS_GETALL_INHERIT:
			vptr->getall_inherit = vecp[i].u_fc.getall_inherit;
			break;
		case DM_FSYS_INIT_ATTRLOC:
			vptr->init_attrloc = vecp[i].u_fc.init_attrloc;
			break;
		case DM_FSYS_MKDIR_BY_HANDLE:
			vptr->mkdir_by_handle = vecp[i].u_fc.mkdir_by_handle;
			break;
		case DM_FSYS_PROBE_HOLE:
			vptr->probe_hole = vecp[i].u_fc.probe_hole;
			break;
		case DM_FSYS_PUNCH_HOLE:
			vptr->punch_hole = vecp[i].u_fc.punch_hole;
			break;
		case DM_FSYS_READ_INVIS_RVP:
			vptr->read_invis_rvp = vecp[i].u_fc.read_invis_rvp;
			break;
		case DM_FSYS_RELEASE_RIGHT:
			vptr->release_right = vecp[i].u_fc.release_right;
			break;
		case DM_FSYS_REMOVE_DMATTR:
			vptr->remove_dmattr = vecp[i].u_fc.remove_dmattr;
			break;
		case DM_FSYS_REQUEST_RIGHT:
			vptr->request_right = vecp[i].u_fc.request_right;
			break;
		case DM_FSYS_SET_DMATTR:
			vptr->set_dmattr = vecp[i].u_fc.set_dmattr;
			break;
		case DM_FSYS_SET_EVENTLIST:
			vptr->set_eventlist = vecp[i].u_fc.set_eventlist;
			break;
		case DM_FSYS_SET_FILEATTR:
			vptr->set_fileattr = vecp[i].u_fc.set_fileattr;
			break;
		case DM_FSYS_SET_INHERIT:
			vptr->set_inherit = vecp[i].u_fc.set_inherit;
			break;
		case DM_FSYS_SET_REGION:
			vptr->set_region = vecp[i].u_fc.set_region;
			break;
		case DM_FSYS_SYMLINK_BY_HANDLE:
			vptr->symlink_by_handle = vecp[i].u_fc.symlink_by_handle;
			break;
		case DM_FSYS_SYNC_BY_HANDLE:
			vptr->sync_by_handle = vecp[i].u_fc.sync_by_handle;
			break;
		case DM_FSYS_UPGRADE_RIGHT:
			vptr->upgrade_right = vecp[i].u_fc.upgrade_right;
			break;
		case DM_FSYS_WRITE_INVIS_RVP:
			vptr->write_invis_rvp = vecp[i].u_fc.write_invis_rvp;
			break;
		case DM_FSYS_OBJ_REF_HOLD:
			vptr->obj_ref_hold = vecp[i].u_fc.obj_ref_hold;
			break;
		default:		/* ignore ones we don't understand */
			break;
		}
	}
}


/* Must hold dm_fsys_lock.
 * This returns the prototype for all instances of the fstype.
 */
static dm_vector_map_t *
dm_fsys_map_by_fstype(
	struct file_system_type *fstype)
{
	struct list_head *p;
	dm_vector_map_t *proto = NULL;
	dm_vector_map_t *m;

	ASSERT_ALWAYS(fstype);
	list_for_each(p, &dm_fsys_map) {
		m = list_entry(p, dm_vector_map_t, ftype_list);
		if (m->f_type == fstype) {
			proto = m;
			break;
		}
	}
	return proto;
}


/* Must hold dm_fsys_lock */
static dm_vector_map_t *
dm_fsys_map_by_sb(
	struct super_block *sb)
{
	struct list_head *p;
	dm_vector_map_t *proto;
	dm_vector_map_t *m;
	dm_vector_map_t *foundmap = NULL;

	proto = dm_fsys_map_by_fstype(sb->s_type);
	if(proto == NULL) {
		return NULL;
	}

	list_for_each(p, &proto->sb_list) {
		m = list_entry(p, dm_vector_map_t, sb_list);
		if (m->sb == sb) {
			foundmap = m;
			break;
		}
	}
	return foundmap;
}


#ifdef CONFIG_DMAPI_DEBUG
static void
sb_list(
	struct super_block *sb)
{
	struct list_head *p;
	dm_vector_map_t *proto;
	dm_vector_map_t *m;

	proto = dm_fsys_map_by_fstype(sb->s_type);
	ASSERT(proto);

printk("%s/%d: Current sb_list\n", __FUNCTION__, __LINE__);
	list_for_each(p, &proto->sb_list) {
		m = list_entry(p, dm_vector_map_t, sb_list);
printk("%s/%d: map 0x%p, sb 0x%p, vptr 0x%p, dmapiops 0x%p\n", __FUNCTION__, __LINE__, m, m->sb, m->vptr, m->dmapiops);
	}
printk("%s/%d: Done sb_list\n", __FUNCTION__, __LINE__);
}
#else
#define sb_list(x)
#endif

#ifdef CONFIG_DMAPI_DEBUG
static void
ftype_list(void)
{
	struct list_head *p;
	dm_vector_map_t *m;

printk("%s/%d: Current ftype_list\n", __FUNCTION__, __LINE__);
	list_for_each(p, &dm_fsys_map) {
		m = list_entry(p, dm_vector_map_t, ftype_list);
		printk("%s/%d: FS 0x%p, ftype 0x%p %s\n", __FUNCTION__, __LINE__, m, m->f_type, m->f_type->name);
	}
printk("%s/%d: Done ftype_list\n", __FUNCTION__, __LINE__);
}
#else
#define ftype_list()
#endif

/* Ask for vptr for this filesystem instance.
 * The caller knows this inode is on a dmapi-managed filesystem.
 */
dm_fsys_vector_t *
dm_fsys_vector(
	struct inode	*ip)
{
	dm_vector_map_t *map;

	spin_lock(&dm_fsys_lock);
	ftype_list();
	map = dm_fsys_map_by_sb(ip->i_sb);
	spin_unlock(&dm_fsys_lock);
	ASSERT(map);
	ASSERT(map->vptr);
	return map->vptr;
}


/* Ask for the dmapiops for this filesystem instance.  The caller is
 * also asking if this is a dmapi-managed filesystem.
 */
struct filesystem_dmapi_operations *
dm_fsys_ops(
	struct super_block	*sb)
{
	dm_vector_map_t *proto = NULL;
	dm_vector_map_t *map;

	spin_lock(&dm_fsys_lock);
	ftype_list();
	sb_list(sb);
	map = dm_fsys_map_by_sb(sb);
	if (map == NULL)
		proto = dm_fsys_map_by_fstype(sb->s_type);
	spin_unlock(&dm_fsys_lock);

	if ((map == NULL) && (proto == NULL))
		return NULL;

	if (map == NULL) {
		/* Find out if it's dmapi-managed */
		dm_vector_map_t *m;

		ASSERT(proto);
		m = kmem_cache_alloc(dm_fsys_map_cachep, GFP_KERNEL);
		if (m == NULL) {
			printk("%s/%d: kmem_cache_alloc(dm_fsys_map_cachep) returned NULL\n", __FUNCTION__, __LINE__);
			return NULL;
		}
		memset(m, 0, sizeof(*m));
		m->dmapiops = proto->dmapiops;
		m->f_type = sb->s_type;
		m->sb = sb;
		INIT_LIST_HEAD(&m->sb_list);
		INIT_LIST_HEAD(&m->ftype_list);

		dm_query_fsys_for_vector(m);
		if (m->vptr == NULL) {
			/* This isn't dmapi-managed */
			kmem_cache_free(dm_fsys_map_cachep, m);
			return NULL;
		}

		spin_lock(&dm_fsys_lock);
		if ((map = dm_fsys_map_by_sb(sb)) == NULL)
			list_add(&m->sb_list, &proto->sb_list);
		spin_unlock(&dm_fsys_lock);

		if (map) {
			kmem_cache_free(dm_fsys_vptr_cachep, m->vptr);
			kmem_cache_free(dm_fsys_map_cachep, m);
		}
		else {
			map = m;
		}
	}

	return map->dmapiops;
}



/* Called when a filesystem instance is unregistered from dmapi */
void
dm_fsys_ops_release(
	struct super_block	*sb)
{
	dm_vector_map_t *map;

	spin_lock(&dm_fsys_lock);
	ASSERT(!list_empty(&dm_fsys_map));
	map = dm_fsys_map_by_sb(sb);
	ASSERT(map);
	list_del(&map->sb_list);
	spin_unlock(&dm_fsys_lock);

	ASSERT(map->vptr);
	kmem_cache_free(dm_fsys_vptr_cachep, map->vptr);
	kmem_cache_free(dm_fsys_map_cachep, map);
}


/* Called by a filesystem module that is loading into the kernel.
 * This creates a new dm_vector_map_t which serves as the prototype
 * for instances of this fstype and also provides the list_head
 * for instances of this fstype.  The prototypes are the only ones
 * on the fstype_list, and will never be on the sb_list.
 */
void
dmapi_register(
	struct file_system_type *fstype,
	struct filesystem_dmapi_operations *dmapiops)
{
	dm_vector_map_t *proto;

	proto = kmem_cache_alloc(dm_fsys_map_cachep, GFP_KERNEL);
	if (proto == NULL) {
		printk("%s/%d: kmem_cache_alloc(dm_fsys_map_cachep) returned NULL\n", __FUNCTION__, __LINE__);
		return;
	}
	memset(proto, 0, sizeof(*proto));
	proto->dmapiops = dmapiops;
	proto->f_type = fstype;
	INIT_LIST_HEAD(&proto->sb_list);
	INIT_LIST_HEAD(&proto->ftype_list);

	spin_lock(&dm_fsys_lock);
	ASSERT(dm_fsys_map_by_fstype(fstype) == NULL);
	list_add(&proto->ftype_list, &dm_fsys_map);
	ftype_list();
	spin_unlock(&dm_fsys_lock);
}

/* Called by a filesystem module that is unloading from the kernel */
void
dmapi_unregister(
	struct file_system_type *fstype)
{
	struct list_head *p;
	dm_vector_map_t *proto;
	dm_vector_map_t *m;

	spin_lock(&dm_fsys_lock);
	ASSERT(!list_empty(&dm_fsys_map));
	proto = dm_fsys_map_by_fstype(fstype);
	ASSERT(proto);
	list_del(&proto->ftype_list);
	spin_unlock(&dm_fsys_lock);

	p = &proto->sb_list;
	while (!list_empty(p)) {
		m = list_entry(p->next, dm_vector_map_t, sb_list);
		list_del(&m->sb_list);
		ASSERT(m->vptr);
		kmem_cache_free(dm_fsys_vptr_cachep, m->vptr);
		kmem_cache_free(dm_fsys_map_cachep, m);
	}
	kmem_cache_free(dm_fsys_map_cachep, proto);
}


int
dmapi_registered(
	struct file_system_type *fstype,
	struct filesystem_dmapi_operations **dmapiops)
{
	return 0;
}
