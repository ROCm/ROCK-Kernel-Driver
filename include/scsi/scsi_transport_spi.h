/* 
 *  Parallel SCSI (SPI) transport specific attributes exported to sysfs.
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
#ifndef SCSI_TRANSPORT_SPI_H
#define SCSI_TRANSPORT_SPI_H

#include <linux/config.h>

struct scsi_transport_template;

struct spi_transport_attrs {
	int period;
	int offset;
};

/* accessor functions */
#define spi_period(x)	(((struct spi_transport_attrs *)&(x)->transport_data)->period)
#define spi_offset(x)	(((struct spi_transport_attrs *)&(x)->transport_data)->offset)

extern struct scsi_transport_template spi_transport_template;

#endif /* SCSI_TRANSPORT_SPI_H */
