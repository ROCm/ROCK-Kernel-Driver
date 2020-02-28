/*
 * livepatch.h - Kernel Live Patching Core
 *
 * Copyright (C) 2016 Josh Poimboeuf <jpoimboe@redhat.com>
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

#ifndef _UAPI_LIVEPATCH_H
#define _UAPI_LIVEPATCH_H

#include <linux/types.h>

#define KLP_RELA_PREFIX		".klp.rela."
#define KLP_SYM_PREFIX		".klp.sym."

#endif /* _UAPI_LIVEPATCH_H */
