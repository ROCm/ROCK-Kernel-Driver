/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992-1997,2000-2003 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_IOGRAPH_H
#define _ASM_IA64_SN_IOGRAPH_H

#include <asm/sn/xtalk/xbow.h>	/* For get MAX_PORT_NUM */

/*
 * During initialization, platform-dependent kernel code establishes some
 * basic elements of the hardware graph.  This file contains edge and
 * info labels that are used across various platforms -- it serves as an
 * ad-hoc registry.
 */

/* edges names */
#define EDGE_LBL_BUS			"bus"
#define EDGE_LBL_CONN			".connection"
#define EDGE_LBL_GUEST			".guest"	/* For IOC3 */
#define EDGE_LBL_HOST			".host"		/* For IOC3 */
#define EDGE_LBL_PERFMON		"mon"
#define EDGE_LBL_USRPCI			"usrpci"
#define EDGE_LBL_BLOCK			"block"
#define EDGE_LBL_BOARD			"board"
#define EDGE_LBL_CHAR			"char"
#define EDGE_LBL_CONTROLLER		"controller"
#define EDGE_LBL_CPU			"cpu"
#define EDGE_LBL_CPUNUM			"cpunum"
#define EDGE_LBL_DIRECT			"direct"
#define EDGE_LBL_DISABLED		"disabled"
#define EDGE_LBL_DISK			"disk"
#define EDGE_LBL_HUB			"hub"		/* For SN0 */
#define EDGE_LBL_HW			"hw"
#define EDGE_LBL_INTERCONNECT		"link"
#define EDGE_LBL_IO			"io"
#define EDGE_LBL_LUN                    "lun"
#define EDGE_LBL_LINUX                  "linux"
#define EDGE_LBL_LINUX_BUS              EDGE_LBL_LINUX "/bus/pci-x"
#define EDGE_LBL_MACHDEP                "machdep"       /* Platform depedent devices */
#define EDGE_LBL_MASTER			".master"
#define EDGE_LBL_MEMORY			"memory"
#define EDGE_LBL_META_ROUTER		"metarouter"
#define EDGE_LBL_MIDPLANE		"midplane"
#define EDGE_LBL_MODULE			"module"
#define EDGE_LBL_NODE			"node"
#define EDGE_LBL_NODENUM		"nodenum"
#define EDGE_LBL_NVRAM			"nvram"
#define EDGE_LBL_PARTITION		"partition"
#define EDGE_LBL_PCI			"pci"
#define EDGE_LBL_PCIX			"pci-x"
#define EDGE_LBL_PCIX_0			EDGE_LBL_PCIX "/0"
#define EDGE_LBL_PCIX_1			EDGE_LBL_PCIX "/1"
#define EDGE_LBL_AGP			"agp"
#define EDGE_LBL_AGP_0			EDGE_LBL_AGP "/0"
#define EDGE_LBL_AGP_1			EDGE_LBL_AGP "/1"
#define EDGE_LBL_PORT			"port"
#define EDGE_LBL_PROM			"prom"
#define EDGE_LBL_RACK			"rack"
#define EDGE_LBL_RDISK			"rdisk"
#define EDGE_LBL_REPEATER_ROUTER	"repeaterrouter"
#define EDGE_LBL_ROUTER			"router"
#define EDGE_LBL_RPOS			"bay"		/* Position in rack */
#define EDGE_LBL_SCSI			"scsi"
#define EDGE_LBL_SCSI_CTLR		"scsi_ctlr"
#define EDGE_LBL_SLOT			"slot"
#define EDGE_LBL_TARGET                 "target"
#define EDGE_LBL_UNKNOWN		"unknown"
#define EDGE_LBL_XBOW			"xbow"
#define	EDGE_LBL_XIO			"xio"
#define EDGE_LBL_XSWITCH		".xswitch"
#define EDGE_LBL_XTALK			"xtalk"
#define EDGE_LBL_XWIDGET		"xwidget"
#define EDGE_LBL_ELSC			"elsc"
#define EDGE_LBL_L1			"L1"
#define EDGE_LBL_XPLINK			"xplink" 	/* Cross partition */
#define	EDGE_LBL_XPLINK_NET		"net" 		/* XP network devs */
#define	EDGE_LBL_XPLINK_RAW		"raw"		/* XP Raw devs */
#define EDGE_LBL_SLAB			"slab"		/* Slab of a module */
#define	EDGE_LBL_XPLINK_KERNEL		"kernel"	/* XP kernel devs */
#define	EDGE_LBL_XPLINK_ADMIN		"admin"	   	/* Partition admin */
#define EDGE_LBL_IOBRICK		"iobrick"
#define EDGE_LBL_PXBRICK		"PXbrick"
#define EDGE_LBL_OPUSBRICK		"onboardio"
#define EDGE_LBL_IXBRICK		"IXbrick"
#define EDGE_LBL_CGBRICK		"CGbrick"
#define EDGE_LBL_CPUBUS			"cpubus"	/* CPU Interfaces (SysAd) */

/* vertex info labels in hwgraph */
#define INFO_LBL_CNODEID		"_cnodeid"
#define INFO_LBL_CONTROLLER_NAME	"_controller_name"
#define INFO_LBL_CPUBUS			"_cpubus"
#define INFO_LBL_CPUID			"_cpuid"
#define INFO_LBL_CPU_INFO		"_cpu"
#define INFO_LBL_DETAIL_INVENT		"_detail_invent" /* inventory data*/
#define INFO_LBL_DIAGVAL		"_diag_reason"   /* Reason disabled */
#define INFO_LBL_DRIVER			"_driver"	/* points to attached device_driver_t */
#define INFO_LBL_ELSC			"_elsc"
#define	INFO_LBL_SUBCH			"_subch"	/* system controller subchannel */
#define INFO_LBL_HUB_INFO		"_hubinfo"
#define INFO_LBL_HWGFSLIST		"_hwgfs_list"
#define INFO_LBL_TRAVERSE		"_hwg_traverse" /* hwgraph traverse function */
#define INFO_LBL_MODULE_INFO		"_module"	/* module data ptr */
#define INFO_LBL_MDPERF_DATA		"_mdperf"	/* mdperf monitoring*/
#define INFO_LBL_NODE_INFO		"_node"
#define	INFO_LBL_PCIBR_HINTS		"_pcibr_hints"
#define INFO_LBL_PCIIO			"_pciio"
#define INFO_LBL_PFUNCS			"_pciio_ops"	/* ops vector for gio providers */
#define INFO_LBL_PERMISSIONS		"_permissions"	/* owner, uid, gid */
#define INFO_LBL_ROUTER_INFO		"_router"
#define INFO_LBL_SUBDEVS		"_subdevs"	/* subdevice enable bits */
#define INFO_LBL_XSWITCH		"_xswitch"
#define INFO_LBL_XSWITCH_ID		"_xswitch_id"
#define INFO_LBL_XSWITCH_VOL		"_xswitch_volunteer"
#define INFO_LBL_XFUNCS			"_xtalk_ops"	/* ops vector for gio providers */
#define INFO_LBL_XWIDGET		"_xwidget"


#ifdef __KERNEL__
void init_all_devices(void);
#endif /* __KERNEL__ */

int io_brick_map_widget(int, int);

/*
 * Map a brick's widget number to a meaningful int
 */

struct io_brick_map_s {
    int                 ibm_type;                  /* brick type */
    int                 ibm_map_wid[MAX_PORT_NUM]; /* wid to int map */
};

#endif /* _ASM_IA64_SN_IOGRAPH_H */
