/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains API provided by efhw/falcon_hash.c file.
 * Function declared in this file are not exported from the Linux
 * sfc_resource driver.
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

#ifndef __CI_EFHW_FALCON_HASH_H__
#define __CI_EFHW_FALCON_HASH_H__

extern unsigned int
falcon_hash_get_ip_key(unsigned int src_ip, unsigned int src_port,
		       unsigned int dest_ip, unsigned int dest_port,
		       int tcp, int full);

extern unsigned int
falcon_hash_function1(unsigned int key, unsigned int nfilters);

extern unsigned int
falcon_hash_function2(unsigned int key, unsigned int nfilters);

extern unsigned int
falcon_hash_iterator(unsigned int hash1, unsigned int hash2,
		     unsigned int n_search, unsigned int nfilters);

#endif /* __CI_EFHW_FALCON_HASH_H__ */
