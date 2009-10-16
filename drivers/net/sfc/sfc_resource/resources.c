/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains resource managers initialisation functions.
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

#include <ci/efrm/private.h>
#include <ci/efrm/buffer_table.h>

int
efrm_resources_init(const struct vi_resource_dimensions *vi_res_dim,
		    int buffer_table_min, int buffer_table_lim)
{
	int i, rc;

	rc = efrm_buffer_table_ctor(buffer_table_min, buffer_table_lim);
	if (rc != 0)
		return rc;

	/* Create resources in the correct order */
	for (i = 0; i < EFRM_RESOURCE_NUM; ++i) {
		struct efrm_resource_manager **rmp = &efrm_rm_table[i];

		EFRM_ASSERT(*rmp == NULL);
		switch (i) {
		case EFRM_RESOURCE_VI:
			rc = efrm_create_vi_resource_manager(rmp,
							     vi_res_dim);
			break;
		case EFRM_RESOURCE_FILTER:
			rc = efrm_create_filter_resource_manager(rmp);
			break;
		case EFRM_RESOURCE_IOBUFSET:
			rc = efrm_create_iobufset_resource_manager(rmp);
			break;
		default:
			rc = 0;
			break;
		}

		if (rc < 0) {
			EFRM_ERR("%s: failed type=%d (%d)",
				 __func__, i, rc);
			efrm_buffer_table_dtor();
			return rc;
		}
	}

	return 0;
}

void efrm_resources_fini(void)
{
	int i;

	for (i = EFRM_RESOURCE_NUM - 1; i >= 0; --i)
		if (efrm_rm_table[i]) {
			efrm_resource_manager_dtor(efrm_rm_table[i]);
			efrm_rm_table[i] = NULL;
		}

	efrm_buffer_table_dtor();
}
