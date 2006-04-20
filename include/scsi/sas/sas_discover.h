/*
 * Serial Attached SCSI (SAS) Discover process header file
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
 * $Id: //depot/sas-class/sas_discover.h#41 $
 */

#ifndef _SAS_DISCOVER_H_
#define _SAS_DISCOVER_H_

#include <scsi/sas/sas_class.h>
#include <scsi/sas/sas_frames.h>

/* ---------- SMP ---------- */

#define SMP_REPORT_GENERAL       0x00
#define SMP_REPORT_MANUF_INFO    0x01
#define SMP_READ_GPIO_REG        0x02
#define SMP_DISCOVER             0x10
#define SMP_REPORT_PHY_ERR_LOG   0x11
#define SMP_REPORT_PHY_SATA      0x12
#define SMP_REPORT_ROUTE_INFO    0x13
#define SMP_WRITE_GPIO_REG       0x82
#define SMP_CONF_ROUTE_INFO      0x90
#define SMP_PHY_CONTROL          0x91
#define SMP_PHY_TEST_FUNCTION    0x92

#define SMP_RESP_FUNC_ACC        0x00
#define SMP_RESP_FUNC_UNK        0x01
#define SMP_RESP_FUNC_FAILED     0x02
#define SMP_RESP_INV_FRM_LEN     0x03
#define SMP_RESP_NO_PHY          0x10
#define SMP_RESP_NO_INDEX        0x11
#define SMP_RESP_PHY_NO_SATA     0x12
#define SMP_RESP_PHY_UNK_OP      0x13
#define SMP_RESP_PHY_UNK_TESTF   0x14
#define SMP_RESP_PHY_TEST_INPROG 0x15
#define SMP_RESP_PHY_VACANT      0x16

/* ---------- Domain Devices ---------- */

/* See sas_discover.c before changing these.
 */

/* ---------- SATA device ---------- */

enum ata_command_set {
	ATA_COMMAND_SET   = 0,
	ATAPI_COMMAND_SET = 1,
};

struct domain_device;

struct sata_device {
	enum   ata_command_set command_set;
	struct smp_resp        rps_resp; /* report_phy_sata_resp */
	__le16 *identify_device;
	__le16 *identify_packet_device;

	u8     port_no;	       /* port number, if this is a PM (Port) */
	struct list_head children; /* PM Ports if this is a PM */
};

#include <scsi/sas/sas_expander.h>

/* ---------- Domain device ---------- */

struct domain_device {
	enum sas_dev_type dev_type;

	enum sas_phy_linkrate linkrate;
	enum sas_phy_linkrate min_linkrate;
	enum sas_phy_linkrate max_linkrate;

	int  pathways;

	struct domain_device *parent;
	struct list_head siblings; /* devices on the same level */
	struct sas_port *port;	  /* shortcut to root of the tree */

	struct list_head dev_list_node;

	enum sas_proto    iproto;
	enum sas_proto    tproto;

	struct sas_rphy *rphy;

	u8  sas_addr[SAS_ADDR_SIZE];
	u8  hashed_sas_addr[HASHED_SAS_ADDR_SIZE];

	u8  frame_rcvd[32];

	union {
		struct expander_device ex_dev;
		struct sata_device     sata_dev; /* STP & directly attached */
	};

	void *lldd_dev;
};

#define list_for_each_entry_reverse_safe(pos, n, head, member)		\
	for (pos = list_entry((head)->prev, typeof(*pos), member),	\
		n = list_entry(pos->member.prev, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = n, n = list_entry(n->member.prev, typeof(*n), member))


static inline void sas_init_dev(struct domain_device *dev)
{
	INIT_LIST_HEAD(&dev->siblings);
	INIT_LIST_HEAD(&dev->dev_list_node);
	switch (dev->dev_type) {
	case SAS_END_DEV:
		break;
	case EDGE_DEV:
	case FANOUT_DEV:
		INIT_LIST_HEAD(&dev->ex_dev.children);
		break;
	case SATA_DEV:
	case SATA_PM:
	case SATA_PM_PORT:
		INIT_LIST_HEAD(&dev->sata_dev.children);
		break;
	default:
		break;
	}
}

void sas_init_disc(struct sas_discovery *disc, struct sas_port *port);
int  sas_discover_event(struct sas_port *sas_port, enum discover_event ev);

int  sas_discover_sata(struct domain_device *dev);
int  sas_discover_end_dev(struct domain_device *dev);

void sas_unregister_dev(struct domain_device *dev);

void sas_unregister_devices(struct sas_ha_struct *sas_ha);

#endif /* _SAS_DISCOVER_H_ */
