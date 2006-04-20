/*
 * Serial Attached SCSI (SAS) Expander discovery and configuration
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
 * $Id: //depot/sas-class/sas_expander.h#19 $
 */

#ifndef _SAS_EXPANDER_H_
#define _SAS_EXPANDER_H_

#define ETASK 0xFA

#define to_dom_device(_obj) container_of(_obj, struct domain_device, dev_obj)
#define to_dev_attr(_attr)  container_of(_attr, struct domain_dev_attribute,\
                                         attr)

/* ---------- Expander device ---------- */

enum routing_attribute {
	DIRECT_ROUTING,
	SUBTRACTIVE_ROUTING,
	TABLE_ROUTING,
};

enum ex_phy_state {
	PHY_EMPTY,
	PHY_VACANT,
	PHY_NOT_PRESENT,
	PHY_DEVICE_DISCOVERED
};

struct ex_phy {
	int    phy_id;

	enum ex_phy_state phy_state;

	enum sas_dev_type attached_dev_type;
	enum sas_phy_linkrate linkrate;

	u8   attached_sata_host:1;
	u8   attached_sata_dev:1;
	u8   attached_sata_ps:1;

	enum sas_proto attached_tproto;
	enum sas_proto attached_iproto;

	u8   attached_sas_addr[SAS_ADDR_SIZE];
	u8   attached_phy_id;

	u8   phy_change_count;
	enum routing_attribute routing_attr;
	u8   virtual:1;

	int  last_da_index;

	struct sas_phy *phy;
};

struct expander_device {
	struct list_head children;

	u16    ex_change_count;
	u16    max_route_indexes;
	u8     num_phys;
	u8     configuring:1;
	u8     conf_route_table:1;
	u8     enclosure_logical_id[8];

	struct ex_phy *ex_phy;
};

int  sas_discover_root_expander(struct domain_device *dev);

void sas_init_ex_attr(void);

int  sas_ex_revalidate_domain(struct domain_device *port_dev);

#endif /* _SAS_EXPANDER_H_ */
