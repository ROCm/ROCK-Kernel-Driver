/* fd_mcs.h -- Header for Future Domain MCS 600/700 (or IBM OEM) driver
 * 
 * fd_mcs.h v0.2 03/11/1998 ZP Gu (zpg@castle.net)
 *

 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.

 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.

 */

#ifndef _FD_MCS_H
#define _FD_MCS_H

static int fd_mcs_detect(Scsi_Host_Template *);
static int fd_mcs_release(struct Scsi_Host *);
static int fd_mcs_command(Scsi_Cmnd *);
static int fd_mcs_abort(Scsi_Cmnd *);
static int fd_mcs_bus_reset(Scsi_Cmnd *);
static int fd_mcs_device_reset(Scsi_Cmnd *);
static int fd_mcs_host_reset(Scsi_Cmnd *);
static int fd_mcs_queue(Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
static int fd_mcs_biosparam(struct scsi_device *, struct block_device *,
			    sector_t, int *);
static const char *fd_mcs_info(struct Scsi_Host *);

#endif				/* _FD_MCS_H */
