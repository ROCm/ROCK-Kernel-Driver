/*
 * Copyright (C) 2017 Josh Poimboeuf <jpoimboe@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _UNDWARF_H
#define _UNDWARF_H

#include "undwarf-types.h"

struct objtool_file;

int create_undwarf(struct objtool_file *file);
int create_undwarf_section(struct objtool_file *file);
int update_file(struct objtool_file *file);

int undwarf_dump(const char *objname);

#endif /* _UNDWARF_H */
