/*
 * hvcserver.c
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

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <asm/hvcall.h>
#include <asm/hvcserver.h>
#include <asm/io.h>

#define HVCS_ARCH_VERSION "1.0.0"

MODULE_AUTHOR("Ryan S. Arnold <rsa@us.ibm.com>");
MODULE_DESCRIPTION("IBM hvcs ppc64 API");
MODULE_LICENSE("GPL");
MODULE_VERSION(HVCS_ARCH_VERSION);

/*
 * Convert arch specific return codes into relevant errnos.  The hvcs
 * functions aren't performance sensitive, so this conversion isn't an
 * issue.
 */
int hvcs_convert(long to_convert)
{
	switch (to_convert) {
		case H_Success:
			return 0;
		case H_Parameter:
			return -EINVAL;
		case H_Hardware:
			return -EIO;
		case H_Busy:
		case H_LongBusyOrder1msec:
		case H_LongBusyOrder10msec:
		case H_LongBusyOrder100msec:
		case H_LongBusyOrder1sec:
		case H_LongBusyOrder10sec:
		case H_LongBusyOrder100sec:
			return -EBUSY;
		case H_Function: /* fall through */
		default:
			return -EPERM;
	}
}

int hvcs_free_partner_info(struct list_head *head)
{
	struct hvcs_partner_info *pi;
	struct list_head *element;

	if (!head) {
		return -EINVAL;
	}

	while (!list_empty(head)) {
		element = head->next;
		pi = list_entry(element, struct hvcs_partner_info, node);
		list_del(element);
		kfree(pi);
	}

	return 0;
}
EXPORT_SYMBOL(hvcs_free_partner_info);

/* Helper function for hvcs_get_partner_info */
int hvcs_next_partner(unsigned int unit_address,
		unsigned long last_p_partition_ID,
		unsigned long last_p_unit_address, unsigned long *pi_buff)

{
	long retval;
	retval = plpar_hcall_norets(H_VTERM_PARTNER_INFO, unit_address,
			last_p_partition_ID,
				last_p_unit_address, virt_to_phys(pi_buff));
	return hvcs_convert(retval);
}

/*
 * The unit_address parameter is the unit address of the vty-server vdevice
 * in whose partner information the caller is interested.  This function
 * uses a pointer to a list_head instance in which to store the partner info.
 * This function returns non-zero on success, or if there is no partner info.
 *
 * Invocation of this function should always be followed by an invocation of
 * hvcs_free_partner_info() using a pointer to the SAME list head instance
 * that was used to store the partner_info list.
 */
int hvcs_get_partner_info(unsigned int unit_address, struct list_head *head,
		unsigned long *pi_buff)
{
	/*
	 * This is a page sized buffer to be passed to hvcall per invocation.
	 * NOTE: the first long returned is unit_address.  The second long
	 * returned is the partition ID and starting with pi_buff[2] are
	 * HVCS_CLC_LENGTH characters, which are diff size than the unsigned
	 * long, hence the casting mumbojumbo you see later.
	 */
	unsigned long	last_p_partition_ID;
	unsigned long	last_p_unit_address;
	struct hvcs_partner_info *next_partner_info = NULL;
	int more = 1;
	int retval;

	memset(pi_buff, 0x00, PAGE_SIZE);
	/* invalid parameters */
	if (!head)
		return -EINVAL;

	last_p_partition_ID = last_p_unit_address = ~0UL;
	INIT_LIST_HEAD(head);

	if (!pi_buff)
		return -ENOMEM;

	do {
		retval = hvcs_next_partner(unit_address, last_p_partition_ID,
				last_p_unit_address, pi_buff);
		if (retval) {
			/*
			 * Don't indicate that we've failed if we have
			 * any list elements.
			 */
			if (!list_empty(head))
				return 0;
			return retval;
		}

		last_p_partition_ID = pi_buff[0];
		last_p_unit_address = pi_buff[1];

		/* This indicates that there are no further partners */
		if (last_p_partition_ID == ~0UL
				&& last_p_unit_address == ~0UL)
			break;

		/* This is a very small struct and will be freed soon in
		 * hvcs_free_partner_info(). */
		next_partner_info = kmalloc(sizeof(struct hvcs_partner_info),
				GFP_ATOMIC);

		if (!next_partner_info) {
			printk(KERN_WARNING "HVCONSOLE: kmalloc() failed to"
				" allocate partner info struct.\n");
			hvcs_free_partner_info(head);
			return -ENOMEM;
		}

		next_partner_info->unit_address
			= (unsigned int)last_p_unit_address;
		next_partner_info->partition_ID
			= (unsigned int)last_p_partition_ID;

		/* copy the Null-term char too */
		strncpy(&next_partner_info->location_code[0],
			(char *)&pi_buff[2],
			strlen((char *)&pi_buff[2]) + 1);

		list_add_tail(&(next_partner_info->node), head);
		next_partner_info = NULL;

	} while (more);

	return 0;
}
EXPORT_SYMBOL(hvcs_get_partner_info);

/*
 * If this function is called once and -EINVAL is returned it may
 * indicate that the partner info needs to be refreshed for the
 * target unit address at which point the caller must invoke
 * hvcs_get_partner_info() and then call this function again.  If,
 * for a second time, -EINVAL is returned then it indicates that
 * there is probably already a partner connection registered to a
 * different vty-server@ vdevice.  It is also possible that a second
 * -EINVAL may indicate that one of the parms is not valid, for
 * instance if the link was removed between the vty-server@ vdevice
 * and the vty@ vdevice that you are trying to open.  Don't shoot the
 * messenger.  Firmware implemented it this way.
 */
int hvcs_register_connection( unsigned int unit_address,
		unsigned int p_partition_ID, unsigned int p_unit_address)
{
	long retval;
	retval = plpar_hcall_norets(H_REGISTER_VTERM, unit_address,
				p_partition_ID, p_unit_address);
	return hvcs_convert(retval);
}
EXPORT_SYMBOL(hvcs_register_connection);

/*
 * If -EBUSY is returned continue to call this function
 * until 0 is returned.
 */
int hvcs_free_connection(unsigned int unit_address)
{
	long retval;
	retval = plpar_hcall_norets(H_FREE_VTERM, unit_address);
	return hvcs_convert(retval);
}
EXPORT_SYMBOL(hvcs_free_connection);
