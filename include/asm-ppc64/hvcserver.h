/*
 * hvcserver.h
 * Copyright (C) 2004 Ryan S Arnold, IBM Corporation
 *
 * PPC64 virtual I/O console server support.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef _PPC64_HVCSERVER_H
#define _PPC64_HVCSERVER_H

#include <linux/list.h>

/* Converged Location Code length */
#define HVCS_CLC_LENGTH	79

struct hvcs_partner_info {
	struct list_head node;
	unsigned int unit_address;
	unsigned int partition_ID;
	char location_code[HVCS_CLC_LENGTH + 1]; /* CLC + 1 null-term char */
};

extern int hvcs_free_partner_info(struct list_head *head);
extern int hvcs_get_partner_info(unsigned int unit_address,
		struct list_head *head, unsigned long *pi_buff);
extern int hvcs_register_connection(unsigned int unit_address,
		unsigned int p_partition_ID, unsigned int p_unit_address);
extern int hvcs_free_connection(unsigned int unit_address);

#endif /* _PPC64_HVCSERVER_H */
