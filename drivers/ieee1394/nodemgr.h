/*
 * Copyright (C) 2000	Andreas E. Bombe
 *               2001	Ben Collins <bcollins@debian.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _IEEE1394_NODEMGR_H
#define _IEEE1394_NODEMGR_H


/*
 * General information: Finding out which GUID belongs to which node is done by
 * sending packets and therefore waiting for the answers.  Wherever it is
 * mentioned that a node is inaccessible this could just as well mean that we
 * just don't know yet (usually, bus reset handlers can't rely on GUIDs being
 * associated with current nodes).
 */

struct node_entry;
typedef struct node_entry *hpsb_guid_t;


/*
 * Returns a guid handle (which has its reference count incremented) or NULL if
 * there is the GUID in question is not known of.  Getting a valid handle does
 * not mean that the node with this GUID is currently accessible (might not be
 * plugged in or powered down).
 */
hpsb_guid_t hpsb_guid_get_handle(u64 guid);

/*
 * If the handle refers to a local host, this function will return the pointer
 * to the hpsb_host structure.  It will return NULL otherwise.  Once you have
 * established it is a local host, you can use that knowledge from then on (the
 * GUID won't wander to an external node).
 *
 * Note that the local GUID currently isn't collected, so this will always
 * return NULL.
 */
struct hpsb_host *hpsb_get_host_by_ge(hpsb_guid_t handle);

/*
 * This will fill in the given, pre-initialised hpsb_packet with the current
 * information from the GUID handle (host, node ID, generation number).  It will
 * return false if the node owning the GUID is not accessible (and not modify the 
 * hpsb_packet) and return true otherwise.
 *
 * Note that packet sending may still fail in hpsb_send_packet if a bus reset
 * happens while you are trying to set up the packet (due to obsolete generation
 * number).  It will at least reliably fail so that you don't accidentally and
 * unknowingly send your packet to the wrong node.
 */
int hpsb_guid_fill_packet(hpsb_guid_t handle, struct hpsb_packet *pkt);


void init_ieee1394_nodemgr(void);
void cleanup_ieee1394_nodemgr(void);

#endif /* _IEEE1394_NODEMGR_H */
