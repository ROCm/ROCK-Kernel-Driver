/*
 * Definitions for bulk memory services
 *
 * bulkmem.h 1.12 2000/06/12 21:55:41
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License. 
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in which
 * case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use
 * your version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 * bulkmem.h 1.3 1995/05/27 04:49:49
 */

#ifndef _LINUX_BULKMEM_H
#define _LINUX_BULKMEM_H

/* For GetFirstRegion and GetNextRegion */
typedef struct region_info_t {
    u_int		Attributes;
    u_int		CardOffset;
    u_int		RegionSize;
    u_int		AccessSpeed;
    u_int		BlockSize;
    u_int		PartMultiple;
    u_char		JedecMfr, JedecInfo;
    memory_handle_t	next;
} region_info_t;

#define REGION_TYPE		0x0001
#define REGION_TYPE_CM		0x0000
#define REGION_TYPE_AM		0x0001
#define REGION_PREFETCH		0x0008
#define REGION_CACHEABLE	0x0010
#define REGION_BAR_MASK		0xe000
#define REGION_BAR_SHIFT	13

int pcmcia_get_first_region(client_handle_t handle, region_info_t *rgn);
int pcmcia_get_next_region(client_handle_t handle, region_info_t *rgn);

#endif /* _LINUX_BULKMEM_H */
