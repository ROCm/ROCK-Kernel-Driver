/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
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
 *
 */

/*
 * Source file used to associate/disassociate behaviors with virtualized
 * objects.  See behavior.h for more information about behaviors, etc.
 *
 * The implementation is split between functions in this file and macros
 * in behavior.h.
 */
#include <xfs.h>

kmem_zone_t	*bhv_global_zone;

/*
 * Global initialization function called out of main.
 */
void
bhv_global_init(void)
{
	/*
	 * Initialize a behavior zone used by subsystems using behaviors
	 * but without any private data.  In the UNIKERNEL case, this zone
	 * is used only for behaviors that are not yet isolated to a single
	 * cell.  The only such user is in pshm.c in which a dummy vnode is
	 * obtained in support of vce avoidance logic.
	 */
	bhv_global_zone = kmem_zone_init(sizeof(bhv_desc_t), "bhv_global_zone");
}

/*
 * Remove a behavior descriptor from a position in a behavior chain;
 * the postition is guaranteed not to be the first position.
 * Should only be called by the bhv_remove() macro.
 *
 * The act of modifying the chain is done atomically w.r.t. ops-in-progress
 * (see comment at top of behavior.h for more info on synchronization).
 */
void
bhv_remove_not_first(bhv_head_t *bhp, bhv_desc_t *bdp)
{
	bhv_desc_t	*curdesc, *prev;

	ASSERT(bhp->bh_first != NULL);
	ASSERT(bhp->bh_first->bd_next != NULL);

	prev = bhp->bh_first;
	for (curdesc = bhp->bh_first->bd_next;
	     curdesc != NULL;
	     curdesc = curdesc->bd_next) {

		if (curdesc == bdp)
			break;		/* found it */
		prev = curdesc;
	}

	ASSERT(curdesc == bdp);
	prev->bd_next = bdp->bd_next;	/* remove from after prev */
					/* atomic wrt oip's */
}

/*
 * Look for a specific ops vector on the specified behavior chain.
 * Return the associated behavior descriptor.  Or NULL, if not found.
 */
bhv_desc_t *
bhv_lookup(bhv_head_t *bhp, void *ops)
{
	bhv_desc_t	*curdesc;

	for (curdesc = bhp->bh_first;
	     curdesc != NULL;
	     curdesc = curdesc->bd_next) {

		if (curdesc->bd_ops == ops)
			return curdesc;
	}

	return NULL;
}

/*
 * Look for a specific ops vector on the specified behavior chain.
 * Return the associated behavior descriptor.  Or NULL, if not found.
 *
 * The caller has not read locked the behavior chain, so acquire the
 * lock before traversing the chain.
 */
bhv_desc_t *
bhv_lookup_unlocked(bhv_head_t *bhp, void *ops)
{
	bhv_desc_t	*bdp;

	bdp = bhv_lookup(bhp, ops);

	return bdp;
}

/*
 * Return the base behavior in the chain, or NULL if the chain
 * is empty.
 *
 * The caller has not read locked the behavior chain, so acquire the
 * lock before traversing the chain.
 */
bhv_desc_t *
bhv_base_unlocked(bhv_head_t *bhp)
{
	bhv_desc_t	*curdesc;

	for (curdesc = bhp->bh_first;
	     curdesc != NULL;
	     curdesc = curdesc->bd_next) {
		if (curdesc->bd_next == NULL)
			return curdesc;
	}
	return NULL;
}

#define BHVMAGIC (void *)0xf00d

/* ARGSUSED */
void
bhv_head_init(
	bhv_head_t *bhp,
	char *name)
{
	bhp->bh_first = NULL;
	bhp->bh_lockp = BHVMAGIC;
}


/* ARGSUSED */
void
bhv_head_reinit(
	bhv_head_t *bhp)
{
	ASSERT(bhp->bh_first == NULL);
	ASSERT(bhp->bh_lockp == BHVMAGIC);
}


void
bhv_insert_initial(
	bhv_head_t *bhp,
	bhv_desc_t *bdp)
{
	ASSERT(bhp->bh_first == NULL);
	ASSERT(bhp->bh_lockp == BHVMAGIC);
	(bhp)->bh_first = bdp;
}

void
bhv_head_destroy(
	bhv_head_t *bhp)
{
	ASSERT(bhp->bh_first == NULL);
	ASSERT(bhp->bh_lockp == BHVMAGIC);
	bhp->bh_lockp = NULL;
}

