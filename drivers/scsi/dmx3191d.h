/*
    dmx3191d.h - defines for the Domex DMX3191D SCSI card.
    Copyright (C) 2000 by Massimo Piccioni <dafastidio@libero.it>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef __DMX3191D_H
#define __DMX3191D_H

#define DMX3191D_DRIVER_NAME	"dmx3191d"
#define DMX3191D_REGION		8

#ifndef PCI_VENDOR_ID_DOMEX
#define PCI_VENDOR_ID_DOMEX		0x134a
#define PCI_DEVICE_ID_DOMEX_DMX3191D	0x0001
#endif

static int dmx3191d_abort(Scsi_Cmnd *);
static int dmx3191d_detect(Scsi_Host_Template *);
static const char* dmx3191d_info(struct Scsi_Host *);
static int dmx3191d_queue_command(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
static int dmx3191d_release_resources(struct Scsi_Host *);
static int dmx3191d_bus_reset(Scsi_Cmnd *);
static int dmx3191d_host_reset(Scsi_Cmnd *);
static int dmx3191d_device_reset(Scsi_Cmnd *);

#define NCR5380_read(reg)			inb(port + reg)
#define NCR5380_write(reg, value)		outb(value, port + reg)

#define NCR5380_implementation_fields		unsigned int port
#define NCR5380_local_declare()			NCR5380_implementation_fields
#define NCR5380_setup(instance)			port = instance->io_port

#define NCR5380_abort				dmx3191d_abort
#define do_NCR5380_intr				dmx3191d_do_intr
#define NCR5380_intr				dmx3191d_intr
#define NCR5380_proc_info			dmx3191d_proc_info
#define NCR5380_queue_command			dmx3191d_queue_command
#define NCR5380_host_reset			dmx3191d_host_reset
#define NCR5380_bus_reset			dmx3191d_bus_reset
#define NCR5380_device_reset			dmx3191d_device_reset

#endif	/* __DMX3191D_H */

