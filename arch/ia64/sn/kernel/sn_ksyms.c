/* 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */


/*
 * Architecture-specific kernel symbols
 */

#include <linux/config.h>
#include <linux/module.h>

#include <asm/machvec.h>
#include <asm/sn/intr.h>

#include <linux/mm.h>
#include <asm/sn/sgi.h>
extern vertex_hdl_t          base_io_scsi_ctlr_vhdl[];
#include <asm/sn/types.h>
extern cnodeid_t master_node_get(devfs_handle_t vhdl);
#include <asm/sn/arch.h>
EXPORT_SYMBOL(base_io_scsi_ctlr_vhdl);
EXPORT_SYMBOL(master_node_get);

#ifdef CONFIG_IA64_SGI_SN_DEBUG
EXPORT_SYMBOL(__pa_debug);
EXPORT_SYMBOL(__va_debug);
#endif

/* Support IPIs for loaded modules. */
EXPORT_SYMBOL(sn_send_IPI_phys);

/* symbols referenced by partitioning modules */
#include <asm/sn/bte.h>
EXPORT_SYMBOL(bte_copy);
EXPORT_SYMBOL(bte_unaligned_copy);
#include <asm/sal.h>
EXPORT_SYMBOL(ia64_sal);
EXPORT_SYMBOL(physical_node_map);

#include <asm/sn/sn_sal.h>
EXPORT_SYMBOL(sal_lock);
EXPORT_SYMBOL(sn_partid);
EXPORT_SYMBOL(sn_local_partid);
EXPORT_SYMBOL(sn_system_serial_number_string);
EXPORT_SYMBOL(sn_partition_serial_number);

EXPORT_SYMBOL(sn_mmiob);

/* added by tduffy 04.08.01 to fix depmod issues */
#include <linux/mmzone.h>

extern nasid_t master_nasid;
EXPORT_SYMBOL(master_nasid);

EXPORT_SYMBOL(sn_flush_all_caches);
