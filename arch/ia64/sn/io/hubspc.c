/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

/*
 * hubspc.c - Hub Memory Space Management Driver
 * This driver implements the managers for the following
 * memory resources:
 * 1) reference counters
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <linux/devfs_fs.h>
#include <linux/devfs_fs_kernel.h>
#include <asm/io.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/mem_refcnt.h>
#include <asm/sn/agent.h>
#include <asm/sn/addrs.h>


#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
#include <asm/sn/sn1/ip27config.h>
#include <asm/sn/sn1/hubdev.h>
#include <asm/sn/ksys/elsc.h>
#endif

#include <asm/sn/hubspc.h>


/* Uncomment the following line for tracing */
/* #define HUBSPC_DEBUG 1 */

int hubspc_devflag = D_MP;

extern void *device_info_get(devfs_handle_t device);
extern void device_info_set(devfs_handle_t device, void *info);



/***********************************************************************/
/* CPU Prom Space 						       */
/***********************************************************************/

typedef struct cpuprom_info {
	devfs_handle_t	prom_dev;
	devfs_handle_t	nodevrtx;
	struct	cpuprom_info *next;
}cpuprom_info_t;

static cpuprom_info_t	*cpuprom_head;
lock_t	cpuprom_spinlock;
#define	PROM_LOCK()	mutex_spinlock(&cpuprom_spinlock)
#define	PROM_UNLOCK(s)	mutex_spinunlock(&cpuprom_spinlock, (s))

/*
 * Add prominfo to the linked list maintained.
 */
void
prominfo_add(devfs_handle_t hub, devfs_handle_t prom)
{
	cpuprom_info_t	*info;
	int	s;

	info = kmalloc(sizeof(cpuprom_info_t), GFP_KERNEL);
	ASSERT(info);
	info->prom_dev = prom;
	info->nodevrtx = hub;


	s = PROM_LOCK();
	info->next = cpuprom_head;
	cpuprom_head = info;
	PROM_UNLOCK(s);
}

void
prominfo_del(devfs_handle_t prom)
{
	int	s;
	cpuprom_info_t	*info;
	cpuprom_info_t	**prev;

	s = PROM_LOCK();
	prev = &cpuprom_head;
	while ( (info = *prev) ) {
		if (info->prom_dev == prom) {
			*prev = info->next;
			PROM_UNLOCK(s);
			return;
		}
		
		prev = &info->next;
	}
	PROM_UNLOCK(s);
	ASSERT(0);
}

devfs_handle_t
prominfo_nodeget(devfs_handle_t prom)
{
	int	s;
	cpuprom_info_t	*info;

	s = PROM_LOCK();
	info = cpuprom_head;
	while (info) {
		if(info->prom_dev == prom) {
			PROM_UNLOCK(s);
			return info->nodevrtx;
		}
		info = info->next;
	}
	PROM_UNLOCK(s);
	return 0;
}

#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
#define	SN_PROMVERSION		INV_IP35PROM
#endif

/* Add "detailed" labelled inventory information to the
 * prom vertex 
 */
void
cpuprom_detailed_inventory_info_add(devfs_handle_t prom_dev,devfs_handle_t node)
{
	invent_miscinfo_t 	*cpuprom_inventory_info;
	extern invent_generic_t *klhwg_invent_alloc(cnodeid_t cnode, 
						     int class, int size);
	cnodeid_t		cnode = hubdev_cnodeid_get(node);

	/* Allocate memory for the extra inventory information
	 * for the  prom
	 */
	cpuprom_inventory_info = (invent_miscinfo_t *) 
		klhwg_invent_alloc(cnode, INV_PROM, sizeof(invent_miscinfo_t));

	ASSERT(cpuprom_inventory_info);

	/* Set the enabled flag so that the hinv interprets this
	 * information
	 */
	cpuprom_inventory_info->im_gen.ig_flag = INVENT_ENABLED;
	cpuprom_inventory_info->im_type = SN_PROMVERSION;
	/* Store prom revision into inventory information */
	cpuprom_inventory_info->im_rev = IP27CONFIG.pvers_rev;
	cpuprom_inventory_info->im_version = IP27CONFIG.pvers_vers;


	/* Store this info as labelled information hanging off the
	 * prom device vertex
	 */
	hwgraph_info_add_LBL(prom_dev, INFO_LBL_DETAIL_INVENT, 
			     (arbitrary_info_t) cpuprom_inventory_info);
	/* Export this information so that user programs can get to
	 * this by using attr_get()
	 */
        hwgraph_info_export_LBL(prom_dev, INFO_LBL_DETAIL_INVENT,
				sizeof(invent_miscinfo_t));
}

int
cpuprom_attach(devfs_handle_t node)
{
        devfs_handle_t prom_dev;

        hwgraph_char_device_add(node, EDGE_LBL_PROM, "hubspc_", &prom_dev);
#ifdef	HUBSPC_DEBUG
	printf("hubspc: prom_attach hub: 0x%x prom: 0x%x\n", node, prom_dev);
#endif	/* HUBSPC_DEBUG */
	device_inventory_add(prom_dev, INV_PROM, SN_PROMVERSION,
				(major_t)0, (minor_t)0, 0);

	/* Add additional inventory info about the cpu prom like
	 * revision & version numbers etc.
	 */
	cpuprom_detailed_inventory_info_add(prom_dev,node);
        device_info_set(prom_dev, (void*)(ulong)HUBSPC_PROM);
	prominfo_add(node, prom_dev);

        return (0);
}

#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
#define FPROM_CONFIG_ADDR	MD_JUNK_BUS_TIMING
#define FPROM_ENABLE_MASK	MJT_FPROM_ENABLE_MASK
#define FPROM_ENABLE_SHFT	MJT_FPROM_ENABLE_SHFT
#define FPROM_SETUP_MASK	MJT_FPROM_SETUP_MASK
#define FPROM_SETUP_SHFT	MJT_FPROM_SETUP_SHFT
#endif

/*ARGSUSED*/
int
cpuprom_map(devfs_handle_t dev, vhandl_t *vt, off_t addr, size_t len)
{
        int 		errcode;
	caddr_t 	kvaddr;
	devfs_handle_t		node;
	cnodeid_t 	cnode;

	node = prominfo_nodeget(dev);

	if (!node)
		return EIO;
        

	kvaddr = hubdev_prombase_get(node);
	cnode  = hubdev_cnodeid_get(node);
#ifdef	HUBSPC_DEBUG
	printf("cpuprom_map: hubnode %d kvaddr 0x%x\n", node, kvaddr);
#endif

	if (len > RBOOT_SIZE)
		len = RBOOT_SIZE;
        /*
         * Map in the prom space
         */
	errcode = v_mapphys(vt, kvaddr, len);

	if (errcode == 0 ){
		/*
		 * Set the MD configuration registers suitably.
		 */
		nasid_t		nasid;
		uint64_t	value;
		volatile hubreg_t	*regaddr;

		nasid = COMPACT_TO_NASID_NODEID(cnode);
		regaddr = REMOTE_HUB_ADDR(nasid, FPROM_CONFIG_ADDR);
		value = HUB_L(regaddr);
		value &= ~(FPROM_SETUP_MASK | FPROM_ENABLE_MASK);
		{
			value |= (((long)CONFIG_FPROM_SETUP << FPROM_SETUP_SHFT) | 
				  ((long)CONFIG_FPROM_ENABLE << FPROM_ENABLE_SHFT));
		}
		HUB_S(regaddr, value);

	}
        return (errcode);
}

/*ARGSUSED*/
int
cpuprom_unmap(devfs_handle_t dev, vhandl_t *vt)
{
        return 0;
}

/***********************************************************************/
/* Base Hub Space Driver                                               */
/***********************************************************************/

// extern int l1_attach( devfs_handle_t );

/*
 * hubspc_init
 * Registration of the hubspc devices with the hub manager
 */
void
hubspc_init(void)
{
        /*
         * Register with the hub manager
         */

        /* The reference counters */
        hubdev_register(mem_refcnt_attach);

	/* Prom space */
	hubdev_register(cpuprom_attach);

#if defined(CONFIG_SERIAL_SGI_L1_PROTOCOL)
	/* L1 system controller link */
	if ( !IS_RUNNING_ON_SIMULATOR() ) {
		/* initialize the L1 link */
		void l1_cons_init( l1sc_t *sc );
		elsc_t *get_elsc(void);

		l1_cons_init((l1sc_t *)get_elsc());
	}
#endif

#ifdef	HUBSPC_DEBUG
	printf("hubspc_init: Completed\n");
#endif	/* HUBSPC_DEBUG */
	/* Initialize spinlocks */
	spinlock_init(&cpuprom_spinlock, "promlist");
}

/* ARGSUSED */
int
hubspc_open(devfs_handle_t *devp, mode_t oflag, int otyp, cred_t *crp)
{
        int errcode = 0;
        
        switch ((hubspc_subdevice_t)(ulong)device_info_get(*devp)) {
        case HUBSPC_REFCOUNTERS:
                errcode = mem_refcnt_open(devp, oflag, otyp, crp);
                break;

        case HUBSPC_PROM:
		/* Check if the user has proper access rights to 
		 * read/write the prom space.
		 */
                if (!cap_able(CAP_DEVICE_MGT)) {
                        errcode = EPERM;
                }                
                break;

        default:
                errcode = ENODEV;
        }

#ifdef	HUBSPC_DEBUG
	printf("hubspc_open: Completed open for type %d\n",
               (hubspc_subdevice_t)(ulong)device_info_get(*devp));
#endif	/* HUBSPC_DEBUG */

        return (errcode);
}


/* ARGSUSED */
int
hubspc_close(devfs_handle_t dev, int oflag, int otyp, cred_t *crp)
{
        int errcode = 0;
        
        switch ((hubspc_subdevice_t)(ulong)device_info_get(dev)) {
        case HUBSPC_REFCOUNTERS:
                errcode = mem_refcnt_close(dev, oflag, otyp, crp);
                break;

        case HUBSPC_PROM:
                break;
        default:
                errcode = ENODEV;
        }

#ifdef	HUBSPC_DEBUG
	printf("hubspc_close: Completed close for type %d\n",
               (hubspc_subdevice_t)(ulong)device_info_get(dev));
#endif	/* HUBSPC_DEBUG */

        return (errcode);
}

/* ARGSUSED */
int
hubspc_map(devfs_handle_t dev, vhandl_t *vt, off_t off, size_t len, uint prot)
{
	/*REFERENCED*/
        hubspc_subdevice_t subdevice;
        int errcode = 0;

	/* check validity of request */
	if( len == 0 ) {
		return ENXIO;
        }

        subdevice = (hubspc_subdevice_t)(ulong)device_info_get(dev);

#ifdef	HUBSPC_DEBUG
	printf("hubspc_map: subdevice: %d vaddr: 0x%x phyaddr: 0x%x len: 0x%x\n",
	       subdevice, v_getaddr(vt), off, len);
#endif /* HUBSPC_DEBUG */

        switch ((hubspc_subdevice_t)(ulong)device_info_get(dev)) {
        case HUBSPC_REFCOUNTERS:
                errcode = mem_refcnt_mmap(dev, vt, off, len, prot);
                break;

        case HUBSPC_PROM:
		errcode = cpuprom_map(dev, vt, off, len);
                break;
        default:
                errcode = ENODEV;
        }

#ifdef	HUBSPC_DEBUG
	printf("hubspc_map finished: spctype: %d vaddr: 0x%x len: 0x%x\n",
	       (hubspc_subdevice_t)(ulong)device_info_get(dev), v_getaddr(vt), len);
#endif /* HUBSPC_DEBUG */

	return errcode;
}

/* ARGSUSED */
int
hubspc_unmap(devfs_handle_t dev, vhandl_t *vt)
{
        int errcode = 0;
        
        switch ((hubspc_subdevice_t)(ulong)device_info_get(dev)) {
        case HUBSPC_REFCOUNTERS:
                errcode = mem_refcnt_unmap(dev, vt);
                break;

        case HUBSPC_PROM:
                errcode = cpuprom_unmap(dev, vt);
                break;

        default:
                errcode = ENODEV;
        }
	return errcode;

}

/* ARGSUSED */
int
hubspc_ioctl(devfs_handle_t dev,
             int cmd,
             void *arg,
             int mode,
             cred_t *cred_p,
             int *rvalp)
{
        int errcode = 0;
        
        switch ((hubspc_subdevice_t)(ulong)device_info_get(dev)) {
        case HUBSPC_REFCOUNTERS:
                errcode = mem_refcnt_ioctl(dev, cmd, arg, mode, cred_p, rvalp);
                break;

        case HUBSPC_PROM:
                break;

        default:
                errcode = ENODEV;
        }
	return errcode;

}
