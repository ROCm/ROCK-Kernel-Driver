/*
 * hvconsole.h
 * Copyright (C) 2004 Ryan S Arnold, IBM Corporation
 *
 * LPAR console support.
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

#ifndef _PPC64_HVCONSOLE_H
#define _PPC64_HVCONSOLE_H

#include <linux/list.h>

#define MAX_NR_HVC_CONSOLES	4

extern int hvc_arch_get_chars(int index, char *buf, int count);
extern int hvc_arch_put_chars(int index, const char *buf, int count);
extern int hvc_arch_tiocmset(int index, unsigned int set, unsigned int clear);
extern int hvc_arch_tiocmget(int index);
extern int hvc_arch_find_vterms(void);

extern int hvc_instantiate(void);

/* hvterm_get/put_chars() do not work with HVSI console protocol; present only
 * for HVCS console server driver */
extern int hvterm_get_chars(uint32_t vtermno, char *buf, int count);
extern int hvterm_put_chars(uint32_t vtermno, const char *buf, int count);

/* Converged Location Code length */
#define HVCS_CLC_LENGTH	79

struct hvcs_partner_info {
	/* list management */
	struct list_head node;
	/* partner unit address */
	unsigned int unit_address;
	/*partner partition ID */
	unsigned int partition_ID;
	/* CLC (79 chars) + 1 Null-term char */
	char location_code[HVCS_CLC_LENGTH + 1];
};

extern int hvcs_free_partner_info(struct list_head *head);
extern int hvcs_get_partner_info(unsigned int unit_address, struct list_head *head);
extern int hvcs_register_connection(unsigned int unit_address, unsigned int p_partition_ID, unsigned int p_unit_address);
extern int hvcs_free_connection(unsigned int unit_address);
extern int hvc_interrupt(int index);

#endif /* _PPC64_HVCONSOLE_H */

