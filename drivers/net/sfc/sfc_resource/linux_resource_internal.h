/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file contains Linux-specific API internal for the resource driver.
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

#ifndef __LINUX_RESOURCE_INTERNAL__
#define __LINUX_RESOURCE_INTERNAL__

#include <ci/driver/resource/linux_efhw_nic.h>
#include <ci/efrm/debug.h>
#include <ci/efrm/driver_private.h>
#include <ci/driver/efab/hardware.h>


/*! Linux specific EtherFabric initialisation */
extern int
linux_efrm_nic_ctor(struct linux_efhw_nic *, struct pci_dev *,
		    spinlock_t *reg_lock,
		    unsigned nic_flags, unsigned nic_options);

/*! Linux specific EtherFabric initialisation */
extern void linux_efrm_nic_dtor(struct linux_efhw_nic *);

/*! Linux specific EtherFabric initialisation -- interrupt registration */
extern int linux_efrm_irq_ctor(struct linux_efhw_nic *);

/*! Linux specific  EtherFabric initialisation -- interrupt deregistration */
extern void linux_efrm_irq_dtor(struct linux_efhw_nic *);

extern int  efrm_driverlink_register(void);
extern void efrm_driverlink_unregister(void);

extern int
efrm_nic_add(struct pci_dev *dev, unsigned int opts, const uint8_t *mac_addr,
	     struct linux_efhw_nic **lnic_out, spinlock_t *reg_lock,
	     int bt_min, int bt_max, int non_irq_evq,
	     const struct vi_resource_dimensions *);
extern void efrm_nic_del(struct linux_efhw_nic *);


extern int efrm_install_proc_entries(void);
extern void efrm_uninstall_proc_entries(void);

#endif  /* __LINUX_RESOURCE_INTERNAL__ */
