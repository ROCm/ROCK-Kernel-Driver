/* 
 *  FiberChannel transport specific attributes exported to sysfs.
 *
 *  Copyright (c) 2003 Silicon Graphics, Inc.  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef SCSI_TRANSPORT_FC_H
#define SCSI_TRANSPORT_FC_H

struct scsi_transport_template;

struct fc_transport_attrs {
	int port_id;
	uint64_t node_name;
	uint64_t port_name;
};

/* accessor functions */
#define fc_port_id(x)	(((struct fc_transport_attrs *)&(x)->transport_data)->port_id)
#define fc_node_name(x)	(((struct fc_transport_attrs *)&(x)->transport_data)->node_name)
#define fc_port_name(x)	(((struct fc_transport_attrs *)&(x)->transport_data)->port_name)

extern struct scsi_transport_template fc_transport_template;

#endif /* SCSI_TRANSPORT_FC_H */
