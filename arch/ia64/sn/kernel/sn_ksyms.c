/* 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2002 Silicon Graphics, Inc. All rights reserved.
 */


/*
 * Architecture-specific kernel symbols
 */

#include <linux/config.h>
#include <linux/module.h>

#include <asm/machvec.h>
#include <asm/sn/intr.h>
#include <asm/sn/arch.h>

#include <linux/mm.h>
#include <linux/devfs_fs_kernel.h>
extern devfs_handle_t          base_io_scsi_ctlr_vhdl[];
#include <asm/sn/types.h>
extern cnodeid_t master_node_get(devfs_handle_t vhdl);
#include <asm/sn/arch.h>
EXPORT_SYMBOL(base_io_scsi_ctlr_vhdl);
EXPORT_SYMBOL(master_node_get);


/*
 * symbols referenced by the PCIBA module
 */
#include <asm/sn/invent.h>
#include <asm/sn/hack.h>
#include <asm/sn/hcl.h>
#include <asm/sn/pci/pciio.h>

devfs_handle_t
devfn_to_vertex(unsigned char busnum, unsigned int devfn);
EXPORT_SYMBOL(devfn_to_vertex);
EXPORT_SYMBOL(hwgraph_vertex_unref);
EXPORT_SYMBOL(pciio_config_get);
EXPORT_SYMBOL(pciio_info_slot_get);
EXPORT_SYMBOL(hwgraph_edge_add);
EXPORT_SYMBOL(pciio_info_master_get);
EXPORT_SYMBOL(pciio_info_get);

#ifdef CONFIG_IA64_SGI_SN_DEBUG
EXPORT_SYMBOL(__pa_debug);
EXPORT_SYMBOL(__va_debug);
#endif

/* Support IPIs for loaded modules. */
EXPORT_SYMBOL(sn_send_IPI_phys);

/* symbols referenced by partitioning modules */
#include <asm/sn/bte_copy.h>
EXPORT_SYMBOL(bte_unaligned_copy);
#include <asm/sal.h>
EXPORT_SYMBOL(ia64_sal);

#ifdef CONFIG_IA64_SGI_SN2
#include <asm/sn/sn_sal.h>
EXPORT_SYMBOL(sal_lock);
EXPORT_SYMBOL(sn_partid);
EXPORT_SYMBOL(sn_local_partid);
EXPORT_SYMBOL(sn_system_serial_number_string);
EXPORT_SYMBOL(sn_partition_serial_number);
#endif

/* added by tduffy 04.08.01 to fix depmod issues */
#include <linux/mmzone.h>

#ifdef BUS_INT_WAR
extern void sn_add_polled_interrupt(int, int);
extern void sn_delete_polled_interrupt(int);
EXPORT_SYMBOL(sn_add_polled_interrupt);
EXPORT_SYMBOL(sn_delete_polled_interrupt);
#endif

extern nasid_t master_nasid;
EXPORT_SYMBOL(master_nasid);
