/*
 * Serial Attached SCSI (SAS) Event processing
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * $Id: //depot/sas-class/sas_event.c#26 $
 */

/**
 * Implementation Of Priority Queue Without Duplication
 * Luben Tuikov 2005/07/11
 *
 * The SAS class implements priority queue without duplication for
 * handling ha/port/phy/discover events.  That is, we want to process
 * the last N unique/non-duplicating events, in the order they arrived.
 *
 * The requirement is that insertion is O(1), and ordered removal is O(1).
 *
 * Suppose events are identified by integers.  Then what is required
 * is that for a given sequence of any random integers R, to find a
 * sorted sequence E, where
 *     a) |E| <= |R|.  If the number of types of events is bounded,
 *        then E is also bounded by that number, from b).
 *     b) For all i and k, E[i] != E[k], except when i == k,
 *        this gives us uniqueness/non duplication.
 *     c) For all i < k, Order(E[i]) < Order(E[k]), this gives us
 *        ordering.
 *     d) If T(R) = E, then O(T) <= |R|, this ensures that insertion
 *        is O(1), and ordered removal is O(1) trivially, since we
 *        remove at the head of E.
 *
 * Example:
 * If R = {4, 5, 1, 2, 5, 3, 3, 4, 4, 3, 1}, then
 *    E = {2, 5, 4, 3, 1}.
 *
 * The algorithm, T, makes use of an array of list elements, indexed
 * by event type, and an event list head which is a linked list of the
 * elements of the array.  When the next event arrives, we index the
 * array by the event, and add that event to the tail of the event
 * list head, deleting it from its previous list position (if it had
 * one).
 *
 * Clearly insertion is O(1).
 *
 * E is given by the elements of the event list, traversed from head
 * to tail.
 */

#include <scsi/scsi_host.h>
#include "sas_internal.h"
#include "sas_dump.h"
#include <scsi/sas/sas_discover.h>

static void notify_ha_event(struct sas_ha_struct *sas_ha, enum ha_event event)
{
	BUG_ON(event >= HA_NUM_EVENTS);

	sas_queue_event(event, &sas_ha->event_lock, &sas_ha->pending,
			&sas_ha->ha_events[event], sas_ha->core.shost);
}

static void notify_port_event(struct asd_sas_phy *phy, enum port_event event)
{
	struct sas_ha_struct *ha = phy->ha;

	BUG_ON(event >= PORT_NUM_EVENTS);

	sas_queue_event(event, &ha->event_lock, &phy->port_events_pending,
			&phy->port_events[event], ha->core.shost);
}

static void notify_phy_event(struct asd_sas_phy *phy, enum phy_event event)
{
	struct sas_ha_struct *ha = phy->ha;

	BUG_ON(event >= PHY_NUM_EVENTS);

	sas_queue_event(event, &ha->event_lock, &phy->phy_events_pending,
			&phy->phy_events[event], ha->core.shost);
}

int sas_init_events(struct sas_ha_struct *sas_ha)
{
	static void (*sas_ha_event_fns[HA_NUM_EVENTS])(void *) = {
		[HAE_RESET] = sas_hae_reset,
	};

	int i;

	spin_lock_init(&sas_ha->event_lock);

	for (i = 0; i < HA_NUM_EVENTS; i++)
		INIT_WORK(&sas_ha->ha_events[i], sas_ha_event_fns[i], sas_ha);

	sas_ha->notify_ha_event = notify_ha_event;
	sas_ha->notify_port_event = notify_port_event;
	sas_ha->notify_phy_event = notify_phy_event;

	return 0;
}
