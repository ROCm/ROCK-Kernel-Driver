/* 
 *  Transport specific attributes.
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
#ifndef SCSI_TRANSPORT_H
#define SCSI_TRANSPORT_H

struct scsi_transport_template {
	/* The NULL terminated list of transport attributes
	 * that should be exported.
	 */
	struct class_device_attribute **device_attrs;
	struct class_device_attribute **target_attrs;
	struct class_device_attribute **host_attrs;


	/* The transport class that the device is in */
	struct class *device_class;
	struct class *target_class;
	struct class *host_class;

	/* Constructor functions */
	int (*device_setup)(struct scsi_device *);
	int (*device_configure)(struct scsi_device *);
	int (*target_setup)(struct scsi_target *);
	int (*host_setup)(struct Scsi_Host *);

	/* The size of the specific transport attribute structure (a
	 * space of this size will be left at the end of the
	 * scsi_* structure */
	int	device_size;
	int	target_size;
	int	host_size;
};

#endif /* SCSI_TRANSPORT_H */
