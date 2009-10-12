/*
 * AppArmor security module
 *
 * This file contains AppArmor basic path manipulation function definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_PATH_H
#define __AA_PATH_H

int aa_get_name_to_buffer(struct path *path, int is_dir, char *buffer, int size,
			  char **name);
int aa_get_name(struct path *path, int is_dir, char **buffer, char **name);
int d_namespace_path(struct path *path, char *buf, int buflen, char **name);
char *sysctl_pathname(struct ctl_table *table, char *buffer, int buflen);

#endif	/* __AA_PATH_H */
