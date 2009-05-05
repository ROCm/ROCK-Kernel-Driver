/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file provides EtherFabric NIC hardware interface common
 * definitions.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Developed and maintained by Solarflare Communications:
 *                      <linux-xen-drivers@solarflare.com>
 *                      <onload-dev@solarflare.com>
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

#ifndef __CI_DRIVER_EFAB_HARDWARE_COMMON_H__
#define __CI_DRIVER_EFAB_HARDWARE_COMMON_H__

/*----------------------------------------------------------------------------
 *
 * EtherFabric constants
 *
 *---------------------------------------------------------------------------*/

#define EFHW_1K		0x00000400u
#define EFHW_2K		0x00000800u
#define EFHW_4K		0x00001000u
#define EFHW_8K		0x00002000u
#define EFHW_16K	0x00004000u
#define EFHW_32K	0x00008000u
#define EFHW_64K	0x00010000u
#define EFHW_128K	0x00020000u
#define EFHW_256K	0x00040000u
#define EFHW_512K	0x00080000u
#define EFHW_1M		0x00100000u
#define EFHW_2M		0x00200000u
#define EFHW_4M		0x00400000u
#define EFHW_8M		0x00800000u
#define EFHW_16M	0x01000000u
#define EFHW_32M	0x02000000u
#define EFHW_48M	0x03000000u
#define EFHW_64M	0x04000000u
#define EFHW_128M	0x08000000u
#define EFHW_256M	0x10000000u
#define EFHW_512M	0x20000000u
#define EFHW_1G 	0x40000000u
#define EFHW_2G		0x80000000u
#define EFHW_4G		0x100000000ULL
#define EFHW_8G		0x200000000ULL

#endif /* __CI_DRIVER_EFAB_HARDWARE_COMMON_H__ */
