/*======================================================================

    PCMCIA Bulk Memory Services

    bulkmem.c 1.38 2000/09/25 19:29:51

    The contents of this file are subject to the Mozilla Public
    License Version 1.1 (the "License"); you may not use this file
    except in compliance with the License. You may obtain a copy of
    the License at http://www.mozilla.org/MPL/

    Software distributed under the License is distributed on an "AS
    IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
    implied. See the License for the specific language governing
    rights and limitations under the License.

    The initial developer of the original code is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

    Alternatively, the contents of this file may be used under the
    terms of the GNU General Public License version 2 (the "GPL"), in which
    case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.
    
======================================================================*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/timer.h>

#define IN_CARD_SERVICES
#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>
#include "cs_internal.h"

#ifdef DEBUG
extern int ds_pc_debug;
#define cs_socket_name(skt)	((skt)->dev.class_id)

#define ds_dbg(skt, lvl, fmt, arg...) do {		\
	if (ds_pc_debug >= lvl)				\
		printk(KERN_DEBUG "ds: %s: " fmt, 	\
		       cs_socket_name(skt) , ## arg);	\
} while (0)
#else
#define ds_dbg(lvl, fmt, arg...) do { } while (0)
#endif

/*======================================================================

    This stuff is used by Card Services to initialize the table of
    region info used for subsequent calls to GetFirstRegion and
    GetNextRegion.
    
======================================================================*/

static void setup_regions(struct pcmcia_socket *s, unsigned int function,
			  int attr,  memory_handle_t *list)
{
    int i, code, has_jedec, has_geo;
    u_int offset;
    cistpl_device_t device;
    cistpl_jedec_t jedec;
    cistpl_device_geo_t geo;
    memory_handle_t r;

    ds_dbg(s, 1, "setup_regions(0x%d, %d, 0x%p)\n",
	   function, attr, list);

    code = (attr) ? CISTPL_DEVICE_A : CISTPL_DEVICE;
    if (pccard_read_tuple(s, function, code, &device) != CS_SUCCESS)
	return;
    code = (attr) ? CISTPL_JEDEC_A : CISTPL_JEDEC_C;
    has_jedec = (pccard_read_tuple(s, function, code, &jedec) == CS_SUCCESS);
    if (has_jedec && (device.ndev != jedec.nid)) {
	ds_dbg(s, 0, "Device info does not match JEDEC info.\n");
	has_jedec = 0;
    }
    code = (attr) ? CISTPL_DEVICE_GEO_A : CISTPL_DEVICE_GEO;
    has_geo = (pccard_read_tuple(s, function, code, &geo) == CS_SUCCESS);
    if (has_geo && (device.ndev != geo.ngeo)) {
	ds_dbg(s, 0, "Device info does not match geometry tuple.\n");
	has_geo = 0;
    }
    
    offset = 0;
    for (i = 0; i < device.ndev; i++) {
	if ((device.dev[i].type != CISTPL_DTYPE_NULL) &&
	    (device.dev[i].size != 0)) {
	    r = kmalloc(sizeof(*r), GFP_KERNEL);
	    if (!r) {
		printk(KERN_NOTICE "cs: setup_regions: kmalloc failed!\n");
		return;
	    }
	    r->region_magic = REGION_MAGIC;
	    r->state = 0;
	    r->dev_info[0] = '\0';
	    r->mtd = NULL;
	    r->info.Attributes = (attr) ? REGION_TYPE_AM : 0;
	    r->info.CardOffset = offset;
	    r->info.RegionSize = device.dev[i].size;
	    r->info.AccessSpeed = device.dev[i].speed;
	    if (has_jedec) {
		r->info.JedecMfr = jedec.id[i].mfr;
		r->info.JedecInfo = jedec.id[i].info;
	    } else
		r->info.JedecMfr = r->info.JedecInfo = 0;
	    if (has_geo) {
		r->info.BlockSize = geo.geo[i].buswidth *
		    geo.geo[i].erase_block * geo.geo[i].interleave;
		r->info.PartMultiple =
		    r->info.BlockSize * geo.geo[i].partition;
	    } else
		r->info.BlockSize = r->info.PartMultiple = 1;
	    r->info.next = *list; *list = r;
	}
	offset += device.dev[i].size;
    }
} /* setup_regions */

/*======================================================================

    This is tricky.  When get_first_region() is called by Driver
    Services, we initialize the region info table in the socket
    structure.  When it is called by an MTD, we can just scan the
    table for matching entries.
    
======================================================================*/

static int pccard_match_region(memory_handle_t list, region_info_t *match)
{
	if (list) {
		*match = list->info;
		return CS_SUCCESS;
	}
	return CS_NO_MORE_ITEMS;
} /* match_region */

int pccard_get_first_region(struct pcmcia_socket *s, region_info_t *rgn)
{
	if (!(s->state & SOCKET_REGION_INFO)) {
		setup_regions(s, BIND_FN_ALL, 0, &s->c_region);
		setup_regions(s, BIND_FN_ALL, 1, &s->a_region);
		s->state |= SOCKET_REGION_INFO;
	}

	if (rgn->Attributes & REGION_TYPE_AM)
		return pccard_match_region(s->a_region, rgn);
	else
		return pccard_match_region(s->c_region, rgn);
} /* get_first_region */

int pccard_get_next_region(struct pcmcia_socket *s, region_info_t *rgn)
{
    return pccard_match_region(rgn->next, rgn);
} /* get_next_region */


#ifdef CONFIG_PCMCIA_OBSOLETE

static int match_region(client_handle_t handle, memory_handle_t list,
			region_info_t *match)
{
    while (list != NULL) {
	if (!(handle->Attributes & INFO_MTD_CLIENT) ||
	    (strcmp(handle->dev_info, list->dev_info) == 0)) {
	    *match = list->info;
	    return CS_SUCCESS;
	}
	list = list->info.next;
    }
    return CS_NO_MORE_ITEMS;
} /* match_region */

int pcmcia_get_first_region(client_handle_t handle, region_info_t *rgn)
{
    struct pcmcia_socket *s = SOCKET(handle);
    if (CHECK_HANDLE(handle))
	return CS_BAD_HANDLE;
    
    if ((handle->Attributes & INFO_MASTER_CLIENT) &&
	(!(s->state & SOCKET_REGION_INFO))) {
	setup_regions(s, handle->Function, 0, &s->c_region);
	setup_regions(s, handle->Function, 1, &s->a_region);
	s->state |= SOCKET_REGION_INFO;
    }

    if (rgn->Attributes & REGION_TYPE_AM)
	return match_region(handle, s->a_region, rgn);
    else
	return match_region(handle, s->c_region, rgn);
} /* get_first_region */
EXPORT_SYMBOL(pcmcia_get_first_region);

int pcmcia_get_next_region(client_handle_t handle, region_info_t *rgn)
{
    if (CHECK_HANDLE(handle))
	return CS_BAD_HANDLE;
    return match_region(handle, rgn->next, rgn);
} /* get_next_region */
EXPORT_SYMBOL(pcmcia_get_next_region);

#endif
