/*
 * AppArmor security module
 *
 * This file contains AppArmor policy loading interface function definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __POLICY_INTERFACE_H
#define __POLICY_INTERFACE_H

ssize_t aa_interface_add_profiles(void *data, size_t size);
ssize_t aa_interface_replace_profiles(void *udata, size_t size);
ssize_t aa_interface_remove_profiles(char *name, size_t size);

#endif	/* __POLICY_INTERFACE_H */
