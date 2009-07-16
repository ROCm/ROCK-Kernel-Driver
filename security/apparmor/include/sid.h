/*
 * AppArmor security module
 *
 * This file contains AppArmor security identifier (sid) definitions
 *
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_SID_H
#define __AA_SID_H

#include <linux/types.h>

struct aa_profile;

#define AA_ALLOC_USR_SID 1
#define AA_ALLOC_SYS_SID 0

u32 aa_alloc_sid(int is_usr);
void aa_free_sid(u32 sid);
int aa_add_sid_profile(u32 sid, struct aa_profile *profile);
int aa_replace_sid_profile(u32 sid, struct aa_profile *profile);
struct aa_profile *aa_get_sid_profile(u32 sid);


static inline u32 aa_compound_sid(u32 sys, u32 usr)
{
	return sys | usr;
}

static inline u32 aa_usr_sid(u32 sid)
{
	return sid & 0xffff0000;
}

static inline u32 aa_sys_sid(u32 sid)
{
	return sid & 0xffff;
}

#endif	/* __AA_SID_H */
