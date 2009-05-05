/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains /proc/driver/sfc_resource/ implementation.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Developed and maintained by Solarflare Communications:
 *                      <linux-xen-drivers@solarflare.com>
 *                      <onload-dev@solarflare.com>
 *
 * Certain parts of the driver were implemented by
 *          Alexandra Kossovsky <Alexandra.Kossovsky@oktetlabs.ru>
 *          OKTET Labs Ltd, Russia,
 *          http://oktetlabs.ru, <info@oktetlabs.ru>
 *          by request of Solarflare Communications
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#include <ci/efrm/debug.h>
#include <ci/efrm/driver_private.h>
#include <linux/proc_fs.h>

/** Top level directory for sfc specific stats **/
static struct proc_dir_entry *efrm_proc_root; /* = NULL */

static int
efrm_resource_read_proc(char *buf, char **start, off_t offset, int count,
			int *eof, void *data);

int efrm_install_proc_entries(void)
{
	/* create the top-level directory for etherfabric specific stuff */
	efrm_proc_root = proc_mkdir("driver/sfc_resource", NULL);
	if (!efrm_proc_root)
		return -ENOMEM;

	if (create_proc_read_entry("resources", 0, efrm_proc_root,
				   efrm_resource_read_proc, 0) == NULL) {
		EFRM_WARN("%s: Unable to create /proc/drivers/sfc_resource/"
			  "resources", __func__);
	}
	return 0;
}

void efrm_uninstall_proc_entries(void)
{
	EFRM_ASSERT(efrm_proc_root);
	remove_proc_entry("resources", efrm_proc_root);
	remove_proc_entry(efrm_proc_root->name, efrm_proc_root->parent);
	efrm_proc_root = NULL;
}

/****************************************************************************
 *
 * /proc/drivers/sfc/resources
 *
 ****************************************************************************/

#define EFRM_PROC_PRINTF(buf, len, fmt, ...)				\
	do {								\
		if (count - len > 0)					\
			len += snprintf(buf+len, count-len, (fmt),	\
					__VA_ARGS__);			\
	} while (0)

static int
efrm_resource_read_proc(char *buf, char **start, off_t offset, int count,
			int *eof, void *data)
{
	irq_flags_t lock_flags;
	int len = 0;
	int type;
	struct efrm_resource_manager *rm;

	for (type = 0; type < EFRM_RESOURCE_NUM; type++) {
		rm = efrm_rm_table[type];
		if (rm == NULL)
			continue;

		EFRM_PROC_PRINTF(buf, len, "*** %s ***\n", rm->rm_name);

		spin_lock_irqsave(&rm->rm_lock, lock_flags);
		EFRM_PROC_PRINTF(buf, len, "current = %u\n", rm->rm_resources);
		EFRM_PROC_PRINTF(buf, len, "    max = %u\n\n",
				 rm->rm_resources_hiwat);
		spin_unlock_irqrestore(&rm->rm_lock, lock_flags);
	}

	return count ? strlen(buf) : 0;
}
