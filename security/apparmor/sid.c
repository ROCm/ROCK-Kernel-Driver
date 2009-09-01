/*
 * AppArmor security module
 *
 * This file contains AppArmor security identifier (sid) manipulation fns
 *
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 *
 * AppArmor allocates a unique sid for every profile loaded.  If a profile
 * is replaced it receive the sid of the profile it is replacing.  Each sid
 * is a u32 with the lower u16 being sids of system profiles and the
 * upper u16 being user profile sids.
 *
 * The sid value of 0 is invalid for system sids and is used to indicate
 * unconfined for user sids.
 *
 * A compound sid is a pair of user and system sids that is used to identify
 * both profiles confining a task.
 *
 * Both system and user sids are globally unique with all users pulling
 * from the same sid pool.  User sid allocation is limited by the
 * user controls, that can limit how many profiles are loaded by a user.
 */

#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/err.h>

#include "include/sid.h"

/* global counter from which sids are allocated */
static u16 global_sys_sid;
static u16 global_usr_sid;
static DEFINE_SPINLOCK(sid_lock);


/* TODO FIXME: add sid to profile mapping, and sid recycling */


/**
 * aa_alloc_sid - allocate a new sid for a profile
 * @is_usr: true if the new sid is a user based sid
 */
u32 aa_alloc_sid(int is_usr)
{
	u32 sid;

	/* 
	 * TODO FIXME: sid recycling - part of profile mapping table
	 */
	spin_lock(&sid_lock);
	if (is_usr) {
		sid = (++global_usr_sid) << 16;
	        
	} else {
		sid = ++global_sys_sid;
	}
	spin_unlock(&sid_lock);
	return sid;
}

/**
 * aa_free_sid - free a sid
 * @sid: sid to free
 */
void aa_free_sid(u32 sid)
{
	;	/* NOP ATM */
}

/**
 * aa_add_sid_profile - associate a profile to a sid for sid -> profile lookup
 * @sid: sid of te profile
 * @profile: profile to associate
 *
 * return 0 or error
 */
int aa_add_sid_profile(u32 sid, struct aa_profile *profile)
{
	/* NOP ATM */
	return 0;
}

/**
 * aa_replace_sid_profile - replace the profile associated with a sid
 * @sid: sid to associate a new profile with
 * @profile: profile to associate with side
 *
 * return 0 or error
 */
int aa_replace_sid_profile(u32 sid, struct aa_profile *profile)
{
	/* NOP ATM */
	return 0;
}

/**
 * aa_get_sid_profile - get the profile associated with the sid
 * @sid: sid to lookup
 *
 * returns - the profile, or NULL for unconfined user.
 *         - if there is an error -ENOENT, -EINVAL
 */
struct aa_profile *aa_get_sid_profile(u32 sid)
{
	return ERR_PTR(-EINVAL);
}

