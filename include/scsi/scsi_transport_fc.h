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

#include <linux/config.h>

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

/* The functions by which the transport class and the driver communicate */
struct fc_function_template {
	void 	(*get_port_id)(struct scsi_device *);
	void	(*get_node_name)(struct scsi_device *);
	void	(*get_port_name)(struct scsi_device *);
	/* The driver sets these to tell the transport class it
	 * wants the attributes displayed in sysfs.  If the show_ flag
	 * is not set, the attribute will be private to the transport
	 * class */
	unsigned long	show_port_id:1;
	unsigned long	show_node_name:1;
	unsigned long	show_port_name:1;
	/* Private Attributes */
};

struct scsi_transport_template *fc_attach_transport(struct fc_function_template *);
void fc_release_transport(struct scsi_transport_template *);

#endif /* SCSI_TRANSPORT_FC_H */
