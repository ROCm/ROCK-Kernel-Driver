/*
 * Serial Attached SCSI (SAS) class internal header file
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 * $Id: //depot/sas-class/sas_internal.h#35 $
 */

#ifndef _SAS_INTERNAL_H_
#define _SAS_INTERNAL_H_

#include <scsi/sas/sas_class.h>
#include <scsi/scsi_host.h>

#define sas_printk(fmt, ...) printk(KERN_NOTICE "sas: " fmt, ## __VA_ARGS__)

#ifdef SAS_DEBUG
#define SAS_DPRINTK(fmt, ...) printk(KERN_NOTICE "sas: " fmt, ## __VA_ARGS__)
#else
#define SAS_DPRINTK(fmt, ...)
#endif

int sas_show_class(enum sas_class class, char *buf);
int sas_show_proto(enum sas_proto proto, char *buf);
int sas_show_linkrate(enum sas_phy_linkrate linkrate, char *buf);
int sas_show_oob_mode(enum sas_oob_mode oob_mode, char *buf);

int  sas_register_phys(struct sas_ha_struct *sas_ha);
void sas_unregister_phys(struct sas_ha_struct *sas_ha);

int  sas_register_ports(struct sas_ha_struct *sas_ha);
void sas_unregister_ports(struct sas_ha_struct *sas_ha);

enum scsi_eh_timer_return sas_scsi_timed_out(struct scsi_cmnd *);

int  sas_init_queue(struct sas_ha_struct *sas_ha);
int  sas_init_events(struct sas_ha_struct *sas_ha);
void sas_shutdown_queue(struct sas_ha_struct *sas_ha);

void sas_deform_port(struct asd_sas_phy *phy);

void sas_porte_bytes_dmaed(void *);
void sas_porte_broadcast_rcvd(void *);
void sas_porte_link_reset_err(void *);
void sas_porte_timer_event(void *);
void sas_porte_hard_reset(void *);

int sas_notify_lldd_dev_found(struct domain_device *);
void sas_notify_lldd_dev_gone(struct domain_device *);


void sas_hae_reset(void *);

static inline void sas_queue_event(int event, spinlock_t *lock, u32 *pending,
				   struct work_struct *work,
				   struct Scsi_Host *shost)
{
	unsigned long flags;

	spin_lock_irqsave(lock, flags);
	if (*pending & (1 << event)) {
		spin_unlock_irqrestore(lock, flags);
		return;
	}
	*pending |= (1 << event);
	spin_unlock_irqrestore(lock, flags);
	scsi_queue_work(shost, work);
}

static inline void sas_begin_event(int event, spinlock_t *lock, u32 *pending)
{
	unsigned long flags;

	spin_lock_irqsave(lock, flags);
	*pending &= ~(1 << event);
	spin_unlock_irqrestore(lock, flags);
}

#endif /* _SAS_INTERNAL_H_ */
