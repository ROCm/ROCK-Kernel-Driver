/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Derived from IRIX <sys/SN/klconfig.h>.
 *
 * Copyright (C) 1992-1997,1999,2001-2003 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (C) 1999 by Ralf Baechle
 */
#ifndef _ASM_IA64_SN_KLCONFIG_H
#define _ASM_IA64_SN_KLCONFIG_H

/*
 * The KLCONFIG structures store info about the various BOARDs found
 * during Hardware Discovery. In addition, it stores info about the
 * components found on the BOARDs.
 */

/*
 * WARNING:
 *	Certain assembly language routines (notably xxxxx.s) in the IP27PROM 
 *	will depend on the format of the data structures in this file.  In 
 *      most cases, rearranging the fields can seriously break things.   
 *      Adding fields in the beginning or middle can also break things.
 *      Add fields if necessary, to the end of a struct in such a way
 *      that offsets of existing fields do not change.
 */

#include <linux/types.h>
#include <asm/sn/types.h>
#include <asm/sn/slotnum.h>
#include <asm/sn/router.h>
#include <asm/sn/sgi.h>
#include <asm/sn/addrs.h>
#include <asm/sn/vector.h>
#include <asm/sn/xtalk/xbow.h>
#include <asm/sn/xtalk/xtalk.h>
#include <asm/sn/kldir.h>
#include <asm/sn/sn_fru.h>
#include <asm/sn/sn2/shub_md.h>
#include <asm/sn/geo.h>

#define KLCFGINFO_MAGIC	0xbeedbabe

typedef s32 klconf_off_t;

#define	MAX_MODULE_ID		255
#define SIZE_PAD		4096 /* 4k padding for structures */
/* 
 * 1 NODE brick, 3 Router bricks (1 local, 1 meta, 1 repeater),
 * 6 XIO Widgets, 1 Xbow, 1 gfx
 */
#define MAX_SLOTS_PER_NODE	(1 + 3 + 6 + 1 + 1) 

/* XXX if each node is guranteed to have some memory */

#define MAX_PCI_DEVS		8

/* lboard_t->brd_flags fields */
/* All bits in this field are currently used. Try the pad fields if
   you need more flag bits */

#define ENABLE_BOARD 		0x01
#define FAILED_BOARD  		0x02
#define DUPLICATE_BOARD 	0x04    /* Boards like midplanes/routers which
                                   	   are discovered twice. Use one of them */
#define VISITED_BOARD		0x08	/* Used for compact hub numbering. */
#define LOCAL_MASTER_IO6	0x10    /* master io6 for that node */
#define KLTYPE_IOBRICK_XBOW	(KLCLASS_MIDPLANE | 0x2)

/* klinfo->flags fields */

#define KLINFO_ENABLE 		0x01    /* This component is enabled */
#define KLINFO_FAILED   	0x02 	/* This component failed */
#define KLINFO_DEVICE   	0x04 	/* This component is a device */
#define KLINFO_VISITED  	0x08 	/* This component has been visited */
#define KLINFO_CONTROLLER   	0x10 	/* This component is a device controller */
#define KLINFO_INSTALL   	0x20  	/* Install a driver */
#define	KLINFO_HEADLESS		0x40	/* Headless (or hubless) component */

/* Structures to manage various data storage areas */
/* The numbers must be contiguous since the array index i
   is used in the code to allocate various areas. 
*/

#define BOARD_STRUCT 		0
#define COMPONENT_STRUCT 	1
#define ERRINFO_STRUCT 		2
#define KLMALLOC_TYPE_MAX 	(ERRINFO_STRUCT + 1)
#define DEVICE_STRUCT 		3


typedef struct console_s {
	unsigned long 	uart_base;
	unsigned long 	config_base;
	unsigned long 	memory_base;
	short		baud;
	short		flag;
	int		type;
	nasid_t		nasid;
	char		wid;
	char 		npci;
	nic_t		baseio_nic;
} console_t;

typedef struct klc_malloc_hdr {
        klconf_off_t km_base;
        klconf_off_t km_limit;
        klconf_off_t km_current;
} klc_malloc_hdr_t;

/* Functions/macros needed to use this structure */

typedef struct kl_config_hdr {
	u64		ch_magic;	/* set this to KLCFGINFO_MAGIC */
	u32		ch_version;    /* structure version number */
	klconf_off_t	ch_malloc_hdr_off; /* offset of ch_malloc_hdr */
	klconf_off_t	ch_cons_off;       /* offset of ch_cons */
	klconf_off_t	ch_board_info;	/* the link list of boards */
	console_t	ch_cons_info;	/* address info of the console */
	klc_malloc_hdr_t ch_malloc_hdr[KLMALLOC_TYPE_MAX];
	confidence_t	ch_sw_belief;	/* confidence that software is bad*/
	confidence_t	ch_sn0net_belief; /* confidence that sn0net is bad */
} kl_config_hdr_t;


#define KL_CONFIG_HDR(_nasid) 	((kl_config_hdr_t *)(KLCONFIG_ADDR(_nasid)))

#define NODE_OFFSET_TO_LBOARD(nasid,off)        (lboard_t*)(NODE_CAC_BASE(nasid) + (off))

#define KL_CONFIG_INFO(_nasid) root_lboard[nasid_to_cnodeid(_nasid)]

/* --- New Macros for the changed kl_config_hdr_t structure --- */

#define PTR_CH_CONS_INFO(_k)	((console_t *)\
			((unsigned long)_k + (_k->ch_cons_off)))

#define KL_CONFIG_CH_CONS_INFO(_n)   PTR_CH_CONS_INFO(KL_CONFIG_HDR(_n))

/* ------------------------------------------------------------- */

#define KL_CONFIG_DUPLICATE_BOARD(_brd)	((_brd)->brd_flags & DUPLICATE_BOARD)

#define XBOW_PORT_TYPE_HUB(_xbowp, _link) 	\
               ((_xbowp)->xbow_port_info[(_link) - BASE_XBOW_PORT].port_flag & XBOW_PORT_HUB)
#define XBOW_PORT_TYPE_IO(_xbowp, _link) 	\
               ((_xbowp)->xbow_port_info[(_link) - BASE_XBOW_PORT].port_flag & XBOW_PORT_IO)

#define XBOW_PORT_IS_ENABLED(_xbowp, _link) 	\
               ((_xbowp)->xbow_port_info[(_link) - BASE_XBOW_PORT].port_flag & XBOW_PORT_ENABLE)
#define XBOW_PORT_NASID(_xbowp, _link) 	\
               ((_xbowp)->xbow_port_info[(_link) - BASE_XBOW_PORT].port_nasid)

#define XBOW_PORT_IO     0x1
#define XBOW_PORT_HUB    0x2
#define XBOW_PORT_ENABLE 0x4

/*
 * The KLCONFIG area is organized as a LINKED LIST of BOARDs. A BOARD
 * can be either 'LOCAL' or 'REMOTE'. LOCAL means it is attached to 
 * the LOCAL/current NODE. REMOTE means it is attached to a different
 * node.(TBD - Need a way to treat ROUTER boards.)
 *
 * There are 2 different structures to represent these boards -
 * lboard - Local board, rboard - remote board. These 2 structures
 * can be arbitrarily mixed in the LINKED LIST of BOARDs. (Refer
 * Figure below). The first byte of the rboard or lboard structure
 * is used to find out its type - no unions are used.
 * If it is a lboard, then the config info of this board will be found
 * on the local node. (LOCAL NODE BASE + offset value gives pointer to 
 * the structure.
 * If it is a rboard, the local structure contains the node number
 * and the offset of the beginning of the LINKED LIST on the remote node.
 * The details of the hardware on a remote node can be built locally,
 * if required, by reading the LINKED LIST on the remote node and 
 * ignoring all the rboards on that node.
 *
 * The local node uses the REMOTE NODE NUMBER + OFFSET to point to the 
 * First board info on the remote node. The remote node list is 
 * traversed as the local list, using the REMOTE BASE ADDRESS and not
 * the local base address and ignoring all rboard values.
 *
 * 
 KLCONFIG

 +------------+      +------------+      +------------+      +------------+
 |  lboard    |  +-->|   lboard   |  +-->|   rboard   |  +-->|   lboard   |
 +------------+  |   +------------+  |   +------------+  |   +------------+
 | board info |  |   | board info |  |   |errinfo,bptr|  |   | board info |
 +------------+  |   +------------+  |   +------------+  |   +------------+
 | offset     |--+   |  offset    |--+   |  offset    |--+   |offset=NULL |
 +------------+      +------------+      +------------+      +------------+


 +------------+
 | board info |
 +------------+       +--------------------------------+
 | compt 1    |------>| type, rev, diaginfo, size ...  |  (CPU)
 +------------+       +--------------------------------+
 | compt 2    |--+
 +------------+  |    +--------------------------------+
 |  ...       |  +--->| type, rev, diaginfo, size ...  |  (MEM_BANK)
 +------------+       +--------------------------------+
 | errinfo    |--+
 +------------+  |    +--------------------------------+
                 +--->|r/l brd errinfo,compt err flags |
                      +--------------------------------+

 *
 * Each BOARD consists of COMPONENTs and the BOARD structure has 
 * pointers (offsets) to its COMPONENT structure.
 * The COMPONENT structure has version info, size and speed info, revision,
 * error info and the NIC info. This structure can accommodate any
 * BOARD with arbitrary COMPONENT composition.
 *
 * The ERRORINFO part of each BOARD has error information
 * that describes errors about the BOARD itself. It also has flags to
 * indicate the COMPONENT(s) on the board that have errors. The error 
 * information specific to the COMPONENT is present in the respective 
 * COMPONENT structure.
 *
 * The ERRORINFO structure is also treated like a COMPONENT, ie. the 
 * BOARD has pointers(offset) to the ERRORINFO structure. The rboard
 * structure also has a pointer to the ERRORINFO structure. This is 
 * the place to store ERRORINFO about a REMOTE NODE, if the HUB on
 * that NODE is not working or if the REMOTE MEMORY is BAD. In cases where 
 * only the CPU of the REMOTE NODE is disabled, the ERRORINFO pointer can
 * be a NODE NUMBER, REMOTE OFFSET combination, pointing to error info 
 * which is present on the REMOTE NODE.(TBD)
 * REMOTE ERRINFO can be stored on any of the nearest nodes 
 * or on all the nearest nodes.(TBD)
 * Like BOARD structures, REMOTE ERRINFO structures can be built locally
 * using the rboard errinfo pointer.
 *
 * In order to get useful information from this Data organization, a set of
 * interface routines are provided (TBD). The important thing to remember while
 * manipulating the structures, is that, the NODE number information should
 * be used. If the NODE is non-zero (remote) then each offset should
 * be added to the REMOTE BASE ADDR else it should be added to the LOCAL BASE ADDR. 
 * This includes offsets for BOARDS, COMPONENTS and ERRORINFO.
 * 
 * Note that these structures do not provide much info about connectivity.
 * That info will be part of HWGRAPH, which is an extension of the cfg_t
 * data structure. (ref IP27prom/cfg.h) It has to be extended to include
 * the IO part of the Network(TBD).
 *
 * The data structures below define the above concepts.
 */

/*
 * BOARD classes
 */

#define KLCLASS_MASK	0xf0   
#define KLCLASS_NONE	0x00
#define KLCLASS_NODE	0x10             /* CPU, Memory and HUB board */
#define KLCLASS_CPU	KLCLASS_NODE	
#define KLCLASS_IO	0x20             /* BaseIO, 4 ch SCSI, ethernet, FDDI 
					    and the non-graphics widget boards */
#define KLCLASS_ROUTER	0x30             /* Router board */
#define KLCLASS_MIDPLANE 0x40            /* We need to treat this as a board
                                            so that we can record error info */
#define KLCLASS_GFX	0x50		/* graphics boards */

#define KLCLASS_PSEUDO_GFX	0x60	/* HDTV type cards that use a gfx
					 * hw ifc to xtalk and are not gfx
					 * class for sw purposes */

#define KLCLASS_IOBRICK	0x70		/* IP35 iobrick */

#define KLCLASS_MAX	8		/* Bump this if a new CLASS is added */
#define KLTYPE_MAX	11		/* Bump this if a new CLASS is added */

#define KLCLASS_UNKNOWN	0xf0

#define KLCLASS(_x) ((_x) & KLCLASS_MASK)

/*
 * board types
 */

#define KLTYPE_MASK	0x0f
#define KLTYPE_NONE	0x00
#define KLTYPE_EMPTY	0x00

#define KLTYPE_WEIRDCPU (KLCLASS_CPU | 0x0)
#define KLTYPE_SNIA	(KLCLASS_CPU | 0x1)

#define KLTYPE_ROUTER     (KLCLASS_ROUTER | 0x1)
#define KLTYPE_META_ROUTER (KLCLASS_ROUTER | 0x3)
#define KLTYPE_REPEATER_ROUTER (KLCLASS_ROUTER | 0x4)

#define KLTYPE_IOBRICK		(KLCLASS_IOBRICK | 0x0)
#define KLTYPE_NBRICK		(KLCLASS_IOBRICK | 0x4)
#define KLTYPE_PXBRICK		(KLCLASS_IOBRICK | 0x6)
#define KLTYPE_IXBRICK		(KLCLASS_IOBRICK | 0x7)
#define KLTYPE_CGBRICK		(KLCLASS_IOBRICK | 0x8)
#define KLTYPE_OPUSBRICK	(KLCLASS_IOBRICK | 0x9)


/* The value of type should be more than 8 so that hinv prints
 * out the board name from the NIC string. For values less than
 * 8 the name of the board needs to be hard coded in a few places.
 * When bringup started nic names had not standardized and so we
 * had to hard code. (For people interested in history.) 
 */
#define KLTYPE_UNKNOWN	(KLCLASS_UNKNOWN | 0xf)

#define KLTYPE(_x) 	((_x) & KLTYPE_MASK)

/* 
 * board structures
 */

#define MAX_COMPTS_PER_BRD 24

typedef struct lboard_s {
	klconf_off_t 	brd_next_any;     /* Next BOARD */
	unsigned char 	struct_type;      /* type of structure, local or remote */
	unsigned char 	brd_type;         /* type+class */
	unsigned char 	brd_sversion;     /* version of this structure */
        unsigned char 	brd_brevision;    /* board revision */
        unsigned char 	brd_promver;      /* board prom version, if any */
 	unsigned char 	brd_flags;        /* Enabled, Disabled etc */
	unsigned char 	brd_slot;         /* slot number */
	unsigned short	brd_debugsw;      /* Debug switches */
	geoid_t		brd_geoid;	  /* geo id */
	partid_t 	brd_partition;    /* Partition number */
        unsigned short 	brd_diagval;      /* diagnostic value */
        unsigned short 	brd_diagparm;     /* diagnostic parameter */
        unsigned char 	brd_inventory;    /* inventory history */
        unsigned char 	brd_numcompts;    /* Number of components */
        nic_t         	brd_nic;          /* Number in CAN */
	nasid_t		brd_nasid;        /* passed parameter */
	klconf_off_t 	brd_compts[MAX_COMPTS_PER_BRD]; /* pointers to COMPONENTS */
	klconf_off_t 	brd_errinfo;      /* Board's error information */
	struct lboard_s *brd_parent;	  /* Logical parent for this brd */
	char            pad0[4];
	confidence_t	brd_confidence;	  /* confidence that the board is bad */
	nasid_t		brd_owner;        /* who owns this board */
	unsigned char 	brd_nic_flags;    /* To handle 8 more NICs */
	char		pad1[24];	  /* future expansion */
	char		brd_name[32];
	nasid_t		brd_next_same_host; /* host of next brd w/same nasid */
	klconf_off_t	brd_next_same;    /* Next BOARD with same nasid */
} lboard_t;

/*
 *	Make sure we pass back the calias space address for local boards.
 *	klconfig board traversal and error structure extraction defines.
 */

#define BOARD_SLOT(_brd)	((_brd)->brd_slot)

#define KLCF_CLASS(_brd)	KLCLASS((_brd)->brd_type)
#define KLCF_TYPE(_brd)		KLTYPE((_brd)->brd_type)
#define KLCF_NUM_COMPS(_brd)	((_brd)->brd_numcompts)
#define KLCF_MODULE_ID(_brd)	((_brd)->brd_module)

#define NODE_OFFSET_TO_KLINFO(n,off)    ((klinfo_t*) TO_NODE_CAC(n,off))
#define KLCF_NEXT(_brd)         \
        ((_brd)->brd_next_same ?     \
         (NODE_OFFSET_TO_LBOARD((_brd)->brd_next_same_host, (_brd)->brd_next_same)): NULL)
#define KLCF_NEXT_ANY(_brd)         \
        ((_brd)->brd_next_any ?     \
         (NODE_OFFSET_TO_LBOARD(NASID_GET(_brd), (_brd)->brd_next_any)): NULL)
#define KLCF_COMP(_brd, _ndx)   \
                ((((_brd)->brd_compts[(_ndx)]) == 0) ? 0 : \
			(NODE_OFFSET_TO_KLINFO(NASID_GET(_brd), (_brd)->brd_compts[(_ndx)])))

#define KLCF_COMP_TYPE(_comp)	((_comp)->struct_type)
#define KLCF_BRIDGE_W_ID(_comp)	((_comp)->physid)	/* Widget ID */



/*
 * Generic info structure. This stores common info about a 
 * component.
 */
 
typedef struct klinfo_s {                  /* Generic info */
        unsigned char   struct_type;       /* type of this structure */
        unsigned char   struct_version;    /* version of this structure */
        unsigned char   flags;            /* Enabled, disabled etc */
        unsigned char   revision;         /* component revision */
        unsigned short  diagval;          /* result of diagnostics */
        unsigned short  diagparm;         /* diagnostic parameter */
        unsigned char   inventory;        /* previous inventory status */
        unsigned short  partid;		   /* widget part number */
	nic_t 		nic;              /* MUst be aligned properly */
        unsigned char   physid;           /* physical id of component */
        unsigned int    virtid;           /* virtual id as seen by system */
	unsigned char	widid;	          /* Widget id - if applicable */
	nasid_t		nasid;            /* node number - from parent */
	char		pad1;		  /* pad out structure. */
	char		pad2;		  /* pad out structure. */
	void		*data;
        klconf_off_t	errinfo;          /* component specific errors */
        unsigned short  pad3;             /* pci fields have moved over to */
        unsigned short  pad4;             /* klbri_t */
} klinfo_t ;

#define KLCONFIG_INFO_ENABLED(_i)	((_i)->flags & KLINFO_ENABLE)
/*
 * Component structures.
 * Following are the currently identified components:
 * 	CPU, HUB, MEM_BANK, 
 * 	XBOW(consists of 16 WIDGETs, each of which can be HUB or GRAPHICS or BRIDGE)
 * 	BRIDGE, IOC3, SuperIO, SCSI, FDDI 
 * 	ROUTER
 * 	GRAPHICS
 */
#define KLSTRUCT_UNKNOWN	0
#define KLSTRUCT_CPU  		1
#define KLSTRUCT_HUB  		2
#define KLSTRUCT_MEMBNK 	3
#define KLSTRUCT_XBOW 		4
#define KLSTRUCT_BRI 		5
#define KLSTRUCT_ROU		9
#define KLSTRUCT_GFX 		10
#define KLSTRUCT_SCSI 		11
#define KLSTRUCT_DISK 		14
#define KLSTRUCT_CDROM 		16

#define KLSTRUCT_FIBERCHANNEL 	25
#define KLSTRUCT_MOD_SERIAL_NUM 26
#define KLSTRUCT_QLFIBRE        32
#define KLSTRUCT_1394           33
#define KLSTRUCT_USB		34
#define KLSTRUCT_USBKBD		35
#define KLSTRUCT_USBMS		36
#define KLSTRUCT_SCSI_CTLR	37
#define KLSTRUCT_PEBRICK	38
#define KLSTRUCT_GIGE           39
#define KLSTRUCT_IDE		40
#define KLSTRUCT_IOC4		41
#define KLSTRUCT_IOC4UART	42
#define KLSTRUCT_IOC4_TTY	43
#define KLSTRUCT_IOC4PCKM	44
#define KLSTRUCT_IOC4MS		45
#define KLSTRUCT_IOC4_ATA	46
#define KLSTRUCT_PCIGFX		47


/*
 * The port info in ip27_cfg area translates to a lboart_t in the 
 * KLCONFIG area. But since KLCONFIG does not use pointers, lboart_t
 * is stored in terms of a nasid and a offset from start of KLCONFIG 
 * area  on that nasid.
 */
typedef struct klport_s {
	nasid_t		port_nasid;
	unsigned char	port_flag;
	klconf_off_t	port_offset;
	short		port_num;
} klport_t;

typedef struct klcpu_s {                          /* CPU */
	klinfo_t 	cpu_info;
	unsigned short 	cpu_prid;	/* Processor PRID value */
	unsigned short 	cpu_fpirr;	/* FPU IRR value */
    	unsigned short 	cpu_speed;	/* Speed in MHZ */
    	unsigned short 	cpu_scachesz;	/* secondary cache size in MB */
    	unsigned short 	cpu_scachespeed;/* secondary cache speed in MHz */
	unsigned long	pad;
} klcpu_t ;

#define CPU_STRUCT_VERSION   2

typedef struct klhub_s {			/* HUB */
	klinfo_t 	hub_info;
	unsigned int 	hub_flags;		/* PCFG_HUB_xxx flags */
#define MAX_NI_PORTS                    2
	klport_t	hub_port[MAX_NI_PORTS + 1];/* hub is connected to this */
	nic_t		hub_box_nic;		/* nic of containing box */
	klconf_off_t	hub_mfg_nic;		/* MFG NIC string */
	u64		hub_speed;		/* Speed of hub in HZ */
	moduleid_t	hub_io_module;		/* attached io module */
	unsigned long	pad;
} klhub_t ;

typedef struct klhub_uart_s {			/* HUB */
	klinfo_t 	hubuart_info;
	unsigned int 	hubuart_flags;		/* PCFG_HUB_xxx flags */
	nic_t		hubuart_box_nic;	/* nic of containing box */
	unsigned long	pad;
} klhub_uart_t ;

#define MEMORY_STRUCT_VERSION   2

typedef struct klmembnk_s {			/* MEMORY BANK */
	klinfo_t 	membnk_info;
    	short 		membnk_memsz;		/* Total memory in megabytes */
	short		membnk_dimm_select; /* bank to physical addr mapping*/
	short		membnk_bnksz[MD_MEM_BANKS]; /* Memory bank sizes */
	short		membnk_attr;
	unsigned long	pad;
} klmembnk_t ;

#define MAX_SERIAL_NUM_SIZE 10

typedef struct klmod_serial_num_s {
      klinfo_t        snum_info;
      union {
              char snum_str[MAX_SERIAL_NUM_SIZE];
              unsigned long long       snum_int;
      } snum;
      unsigned long   pad;
} klmod_serial_num_t;

/* Macros needed to access serial number structure in lboard_t.
   Hard coded values are necessary since we cannot treat 
   serial number struct as a component without losing compatibility
   between prom versions. */

#define GET_SNUM_COMP(_l) 	((klmod_serial_num_t *)\
				KLCF_COMP(_l, _l->brd_numcompts))

#define MAX_XBOW_LINKS 16

typedef struct klxbow_s {                          /* XBOW */
	klinfo_t 	xbow_info ;
    	klport_t	xbow_port_info[MAX_XBOW_LINKS] ; /* Module number */
        int		xbow_master_hub_link;
        /* type of brd connected+component struct ptr+flags */
	unsigned long	pad;
} klxbow_t ;

#define MAX_PCI_SLOTS 8

typedef struct klpci_device_s {
	s32	pci_device_id;	/* 32 bits of vendor/device ID. */
	s32	pci_device_pad;	/* 32 bits of padding. */
} klpci_device_t;

#define BRIDGE_STRUCT_VERSION	2

typedef struct klbri_s {                          /* BRIDGE */
	klinfo_t 	bri_info ;
    	unsigned char	bri_eprominfo ;    /* IO6prom connected to bridge */
    	unsigned char	bri_bustype ;      /* PCI/VME BUS bridge/GIO */
    	u64	    	*pci_specific  ;    /* PCI Board config info */
	klpci_device_t	bri_devices[MAX_PCI_DEVS] ;	/* PCI IDs */
	klconf_off_t	bri_mfg_nic ;
	unsigned long	pad;
} klbri_t ;

#define ROUTER_VECTOR_VERS	2

/* XXX - Don't we need the number of ports here?!? */
typedef struct klrou_s {                          /* ROUTER */
	klinfo_t 	rou_info ;
	unsigned int	rou_flags ;           /* PCFG_ROUTER_xxx flags */
	nic_t		rou_box_nic ;         /* nic of the containing module */
    	klport_t 	rou_port[MAX_ROUTER_PORTS + 1] ; /* array index 1 to 6 */
	klconf_off_t	rou_mfg_nic ;     /* MFG NIC string */
	u64	rou_vector;	  /* vector from master node */
	unsigned long   pad;
} klrou_t ;

/*
 *  Graphics Controller/Device
 *
 *  (IP27/IO6) Prom versions 6.13 (and 6.5.1 kernels) and earlier
 *  used a couple different structures to store graphics information.
 *  For compatibility reasons, the newer data structure preserves some
 *  of the layout so that fields that are used in the old versions remain
 *  in the same place (with the same info).  Determination of what version
 *  of this structure we have is done by checking the cookie field.
 */
#define KLGFX_COOKIE	0x0c0de000

typedef struct klgfx_s {		/* GRAPHICS Device */
	klinfo_t 	gfx_info;
	klconf_off_t    old_gndevs;	/* for compatibility with older proms */
	klconf_off_t    old_gdoff0;	/* for compatibility with older proms */
	unsigned int	cookie;		/* for compatibility with older proms */
	unsigned int	moduleslot;
	struct klgfx_s	*gfx_next_pipe;
	u64		*gfx_specific;
	klconf_off_t    pad0;		/* for compatibility with older proms */
	klconf_off_t    gfx_mfg_nic;
	unsigned long	pad;
} klgfx_t;

#define MAX_SCSI_DEVS 16

/*
 * NOTE: THis is the max sized kl* structure and is used in klmalloc.c
 * to allocate space of type COMPONENT. Make sure that if the size of
 * any other component struct becomes more than this, then redefine
 * that as the size to be klmalloced.
 */

typedef struct klscsi_s {                          /* SCSI Bus */
	klinfo_t 	scsi_info ;
    	u64       	*scsi_specific   ; 
	unsigned char 	scsi_numdevs ;
	klconf_off_t	scsi_devinfo[MAX_SCSI_DEVS] ; 
	unsigned long	pad;
} klscsi_t ;

typedef struct klscctl_s {                          /* SCSI Controller */
	klinfo_t 	scsi_info ;
	unsigned int	type;
	unsigned int	scsi_buscnt;                        /* # busses this cntlr */
	void		*scsi_bus[2];                       /* Pointer to 2 klscsi_t's */
	unsigned long	pad;
} klscctl_t ;

typedef struct klscdev_s {                          /* SCSI device */
	klinfo_t 	scdev_info ;
	struct scsidisk_data *scdev_cfg ; /* driver fills up this */
	unsigned long	pad;
} klscdev_t ;

typedef struct klttydev_s {                          /* TTY device */
	klinfo_t 	ttydev_info ;
	struct terminal_data *ttydev_cfg ; /* driver fills up this */
	unsigned long	pad;
} klttydev_t ;

typedef struct klpcigfx_s {                          /* PCI GFX */
        klinfo_t        gfx_info ;
} klpcigfx_t ;

typedef struct klkbddev_s {                          /* KBD device */
	klinfo_t 	kbddev_info ;
	struct keyboard_data *kbddev_cfg ; /* driver fills up this */
	unsigned long	pad;
} klkbddev_t ;

typedef struct klmsdev_s {                          /* mouse device */
        klinfo_t        msdev_info ;
        void 		*msdev_cfg ; 
	unsigned long	pad;
} klmsdev_t ;

/*
 * USB info
 */

typedef struct klusb_s {
	klinfo_t	usb_info;	/* controller info */
	void		*usb_bus;	/* handle to usb_bus_t */
	uint64_t	usb_controller;	/* ptr to controller info */
	unsigned long	pad;
} klusb_t ; 

typedef union klcomp_s {
	klcpu_t		kc_cpu;
	klhub_t		kc_hub;
	klmembnk_t 	kc_mem;
	klxbow_t  	kc_xbow;
	klbri_t		kc_bri;
	klrou_t		kc_rou;
	klgfx_t		kc_gfx;
	klscsi_t	kc_scsi;
	klscctl_t	kc_scsi_ctl;
	klscdev_t	kc_scsi_dev;
	klmod_serial_num_t kc_snum ;
	klusb_t		kc_usb;
} klcomp_t;

typedef union kldev_s {      /* for device structure allocation */
	klscdev_t	kc_scsi_dev ;
	klttydev_t	kc_tty_dev ;
	klkbddev_t 	kc_kbd_dev ;
} kldev_t ;

/* external declarations of Linux kernel functions. */

extern lboard_t *root_lboard[];
extern lboard_t *find_lboard_any(lboard_t *start, unsigned char type);
extern lboard_t *find_lboard_nasid(lboard_t *start, nasid_t, unsigned char type);
extern klinfo_t *find_component(lboard_t *brd, klinfo_t *kli, unsigned char type);
extern klinfo_t *find_first_component(lboard_t *brd, unsigned char type);
extern klcpu_t *nasid_slice_to_cpuinfo(nasid_t, int);


extern lboard_t *find_gfxpipe(int pipenum);
extern lboard_t *find_lboard_class_any(lboard_t *start, unsigned char brd_class);
extern lboard_t *find_lboard_class_nasid(lboard_t *start, nasid_t, unsigned char brd_class);
extern lboard_t *find_nic_lboard(lboard_t *, nic_t);
extern lboard_t *find_nic_type_lboard(nasid_t, unsigned char, nic_t);
extern lboard_t *find_lboard_modslot(lboard_t *start, geoid_t geoid);
extern int	config_find_nic_router(nasid_t, nic_t, lboard_t **, klrou_t**);
extern int	config_find_nic_hub(nasid_t, nic_t, lboard_t **, klhub_t**);
extern int	config_find_xbow(nasid_t, lboard_t **, klxbow_t**);
extern int 	update_klcfg_cpuinfo(nasid_t, int);
extern void 	board_to_path(lboard_t *brd, char *path);
extern void 	nic_name_convert(char *old_name, char *new_name);
extern int 	module_brds(nasid_t nasid, lboard_t **module_brds, int n);
extern lboard_t *brd_from_key(uint64_t key);
extern void 	device_component_canonical_name_get(lboard_t *,klinfo_t *,
						    char *);
extern int	board_serial_number_get(lboard_t *,char *);
extern nasid_t	get_actual_nasid(lboard_t *brd) ;
extern net_vec_t klcfg_discover_route(lboard_t *, lboard_t *, int);

#endif /* _ASM_IA64_SN_KLCONFIG_H */
