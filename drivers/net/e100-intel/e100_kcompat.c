/*******************************************************************************

  
  Copyright(c) 1999 - 2004 Intel Corporation. All rights reserved.
  
  This program is free software; you can redistribute it and/or modify it 
  under the terms of the GNU General Public License as published by the Free 
  Software Foundation; either version 2 of the License, or (at your option) 
  any later version.
  
  This program is distributed in the hope that it will be useful, but WITHOUT 
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for 
  more details.
  
  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59 
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
  
  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
  
  Contact Information:
  Linux NICS <linux.nics@intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
*******************************************************************************/

#include "e100_kcompat.h"

/******************************************************************
 *#################################################################
 *#
 *# Kernels before 2.4.3
 *#
 *#################################################################
 ******************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,3)

void
e100_pci_release_regions(struct pci_dev *pdev)
{
	release_region(pci_resource_start(pdev, 1), pci_resource_len(pdev, 1));

	release_mem_region(pci_resource_start(pdev, 0),
			   pci_resource_len(pdev, 0));
}

int __devinit
e100_pci_request_regions(struct pci_dev *pdev, char *res_name)
{
	unsigned long io_len = pci_resource_len(pdev, 1);
	unsigned long base_addr;

	base_addr = pci_resource_start(pdev, 1);

	if (!request_region(base_addr, io_len, res_name)) {
		printk(KERN_ERR "%s: Failed to reserve I/O region\n", res_name);
		goto err;
	}

	if (!request_mem_region(pci_resource_start(pdev, 0),
				pci_resource_len(pdev, 0), res_name)) {
		printk(KERN_ERR
		       "%s: Failed to reserve memory region\n", res_name);
		goto err_io;
	}

	return 0;

err_io:
	release_region(base_addr, io_len);

err:
	return -EBUSY;
}

int _kc_is_valid_ether_addr(u8 *addr)
{
	const char zaddr[6] = {0,};

	return !(addr[0]&1) && memcmp( addr, zaddr, 6);
}

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,4,3) */

/*****************************************************************************/
/* 2.4.6 => 2.4.3 */
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,4,6) )
int _kc_pci_set_power_state(struct pci_dev *dev, int state)
{ return 0; }
int _kc_pci_save_state(struct pci_dev *dev, u32 *buffer)
{ return 0; }
int _kc_pci_restore_state(struct pci_dev *pdev, u32 *buffer)
{ return 0; }
int _kc_pci_enable_wake(struct pci_dev *pdev, u32 state, int enable)
{ return 0; }
#endif
