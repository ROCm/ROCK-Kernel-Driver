/******************************************************************************
**  High Performance device driver for the Symbios 53C896 controller.
**
**  Copyright (C) 1998-2001  Gerard Roudier <groudier@free.fr>
**
**  This driver also supports all the Symbios 53C8XX controller family, 
**  except 53C810 revisions < 16, 53C825 revisions < 16 and all 
**  revisions of 53C815 controllers.
**
**  This driver is based on the Linux port of the FreeBSD ncr driver.
** 
**  Copyright (C) 1994  Wolfgang Stanglmeier
**  
**-----------------------------------------------------------------------------
**  
**  This program is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 2 of the License, or
**  (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**-----------------------------------------------------------------------------
**
**  The Linux port of the FreeBSD ncr driver has been achieved in 
**  november 1995 by:
**
**          Gerard Roudier              <groudier@free.fr>
**
**  Being given that this driver originates from the FreeBSD version, and
**  in order to keep synergy on both, any suggested enhancements and corrections
**  received on Linux are automatically a potential candidate for the FreeBSD 
**  version.
**
**  The original driver has been written for 386bsd and FreeBSD by
**          Wolfgang Stanglmeier        <wolf@cologne.de>
**          Stefan Esser                <se@mi.Uni-Koeln.de>
**
**-----------------------------------------------------------------------------
**
**  Major contributions:
**  --------------------
**
**  NVRAM detection and reading.
**    Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
**
*******************************************************************************
*/

#ifndef SYM53C8XX_H
#define SYM53C8XX_H

#include "sym53c8xx_defs.h"

/*
**	Define Scsi_Host_Template parameters
**
**	Used by hosts.c and sym53c8xx.c with module configuration.
*/

#if (LINUX_VERSION_CODE >= 0x020400) || defined(HOSTS_C) || defined(MODULE)

#include <scsi/scsicam.h>

int sym53c8xx_abort(Scsi_Cmnd *);
int sym53c8xx_detect(Scsi_Host_Template *tpnt);
const char *sym53c8xx_info(struct Scsi_Host *host);
int sym53c8xx_queue_command(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
int sym53c8xx_reset(Scsi_Cmnd *, unsigned int);
int sym53c8xx_slave_configure(Scsi_Device *);
int sym53c8xx_release(struct Scsi_Host *);

#endif /* defined(HOSTS_C) || defined(MODULE) */ 

#endif /* SYM53C8XX_H */
