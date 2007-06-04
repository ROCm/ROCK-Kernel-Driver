/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * disk_heartbeat.h
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 */

#ifndef O2CLUSTER_DISK_HEARTBEAT_H
#define O2CLUSTER_DISK_HEARTBEAT_H

#define O2HB_REGION_TIMEOUT_MS		2000

extern unsigned int o2hb_dead_threshold;
/* number of changes to be seen as live */
#define O2HB_LIVE_THRESHOLD	   2
/* number of equal samples to be seen as dead */
#define O2HB_DEFAULT_DEAD_THRESHOLD	   7
/* Otherwise MAX_WRITE_TIMEOUT will be zero... */
#define O2HB_MIN_DEAD_THRESHOLD	  2
#define O2HB_MAX_WRITE_TIMEOUT_MS (O2HB_REGION_TIMEOUT_MS * (o2hb_dead_threshold - 1))
void o2hb_stop_all_regions(void);
int o2hb_disk_heartbeat_init(void);
void o2hb_disk_heartbeat_exit(void);

#endif /* O2CLUSTER_DISK_HEARTBEAT_H */
