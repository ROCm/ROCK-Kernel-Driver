/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <asm/sn/types.h>
#include <asm/sn/sgi.h>
#include <asm/sn/cmn_err.h>
#include <asm/sn/iobus.h>
#include <asm/sn/iograph.h>
#include <asm/param.h>
#include <asm/sn/pio.h>
#include <asm/sn/xtalk/xwidget.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/addrs.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/hcl_util.h>
#include <asm/sn/agent.h>
#include <asm/sn/intr.h>
#include <asm/sn/xtalk/xtalkaddrs.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/io.h>
#include <asm/sn/sn_cpuid.h>

extern xtalk_provider_t hub_provider;

#ifndef CONFIG_IA64_SGI_IO
/* Global variables */
extern pdaindr_t       pdaindr[MAXCPUS];
#endif

/*
 * Perform any initializations needed to support hub-based I/O.
 * Called once during startup.
 */
void
hubio_init(void)
{
#if 0
	/* This isn't needed unless we port the entire sio driver ... */
        extern void early_brl1_port_init( void );
	early_brl1_port_init();
#endif
}

/* 
 * Implementation of hub iobus operations.
 *
 * Hub provides a crosstalk "iobus" on IP27 systems.  These routines
 * provide a platform-specific implementation of xtalk used by all xtalk 
 * cards on IP27 systems.
 *
 * Called from corresponding xtalk_* routines.
 */


/* PIO MANAGEMENT */
/* For mapping system virtual address space to xtalk space on a specified widget */

/*
 * Setup pio structures needed for a particular hub.
 */
static void
hub_pio_init(devfs_handle_t hubv)
{
	xwidgetnum_t widget;
	hubinfo_t hubinfo;
	nasid_t nasid;
	int bigwin;
	hub_piomap_t hub_piomap;

	hubinfo_get(hubv, &hubinfo);
	nasid = hubinfo->h_nasid;

	/* Initialize small window piomaps for this hub */
	for (widget=0; widget <= HUB_WIDGET_ID_MAX; widget++) {
		hub_piomap = hubinfo_swin_piomap_get(hubinfo, (int)widget);
		hub_piomap->hpio_xtalk_info.xp_target = widget;
		hub_piomap->hpio_xtalk_info.xp_xtalk_addr = 0;
		hub_piomap->hpio_xtalk_info.xp_mapsz = SWIN_SIZE;
		hub_piomap->hpio_xtalk_info.xp_kvaddr = (caddr_t)NODE_SWIN_BASE(nasid, widget);
		hub_piomap->hpio_hub = hubv;
		hub_piomap->hpio_flags = HUB_PIOMAP_IS_VALID;
	}

	/* Initialize big window piomaps for this hub */
	for (bigwin=0; bigwin < HUB_NUM_BIG_WINDOW; bigwin++) {
		hub_piomap = hubinfo_bwin_piomap_get(hubinfo, bigwin);
		hub_piomap->hpio_xtalk_info.xp_mapsz = BWIN_SIZE;
		hub_piomap->hpio_hub = hubv;
		hub_piomap->hpio_holdcnt = 0;
		hub_piomap->hpio_flags = HUB_PIOMAP_IS_BIGWINDOW;
		IIO_ITTE_DISABLE(nasid, bigwin);
	}
#ifdef	BRINGUP
	hub_set_piomode(nasid, HUB_PIO_CONVEYOR);
#else
	/* Set all the xwidgets in fire-and-forget mode
	 * by default
	 */
	hub_set_piomode(nasid, HUB_PIO_FIRE_N_FORGET);
#endif	/* BRINGUP */

	sv_init(&hubinfo->h_bwwait, SV_FIFO, "bigwin");
	spinlock_init(&hubinfo->h_bwlock, "bigwin");
}

/* 
 * Create a caddr_t-to-xtalk_addr mapping.
 *
 * Use a small window if possible (that's the usual case), but
 * manage big windows if needed.  Big window mappings can be
 * either FIXED or UNFIXED -- we keep at least 1 big window available
 * for UNFIXED mappings.
 *
 * Returns an opaque pointer-sized type which can be passed to
 * other hub_pio_* routines on success, or NULL if the request
 * cannot be satisfied.
 */
/* ARGSUSED */
hub_piomap_t
hub_piomap_alloc(devfs_handle_t dev,	/* set up mapping for this device */
		device_desc_t dev_desc,	/* device descriptor */
		iopaddr_t xtalk_addr,	/* map for this xtalk_addr range */
		size_t byte_count,
		size_t byte_count_max, 	/* maximum size of a mapping */
		unsigned flags)		/* defined in sys/pio.h */
{
	xwidget_info_t widget_info = xwidget_info_get(dev);
	xwidgetnum_t widget = xwidget_info_id_get(widget_info);
	devfs_handle_t hubv = xwidget_info_master_get(widget_info);
	hubinfo_t hubinfo;
	hub_piomap_t bw_piomap;
	int bigwin, free_bw_index;
	nasid_t nasid;
	volatile hubreg_t junk;
	int s;

	/* sanity check */
	if (byte_count_max > byte_count)
		return(NULL);

	hubinfo_get(hubv, &hubinfo);

	/* If xtalk_addr range is mapped by a small window, we don't have 
	 * to do much 
	 */
	if (xtalk_addr + byte_count <= SWIN_SIZE)
		return(hubinfo_swin_piomap_get(hubinfo, (int)widget));

	/* We need to use a big window mapping.  */

	/*
	 * TBD: Allow requests that would consume multiple big windows --
	 * split the request up and use multiple mapping entries.
	 * For now, reject requests that span big windows.
	 */
	if ((xtalk_addr % BWIN_SIZE) + byte_count > BWIN_SIZE)
		return(NULL);


	/* Round xtalk address down for big window alignement */
	xtalk_addr = xtalk_addr & ~(BWIN_SIZE-1);

	/*
	 * Check to see if an existing big window mapping will suffice.
	 */
tryagain:
	free_bw_index = -1;
	s = mutex_spinlock(&hubinfo->h_bwlock);
	for (bigwin=0; bigwin < HUB_NUM_BIG_WINDOW; bigwin++) {
		bw_piomap = hubinfo_bwin_piomap_get(hubinfo, bigwin);

		/* If mapping is not valid, skip it */
		if (!(bw_piomap->hpio_flags & HUB_PIOMAP_IS_VALID)) {
			free_bw_index = bigwin;
			continue;
		}

		/* 
		 * If mapping is UNFIXED, skip it.  We don't allow sharing
		 * of UNFIXED mappings, because this would allow starvation.
		 */
		if (!(bw_piomap->hpio_flags & HUB_PIOMAP_IS_FIXED))
			continue;

		if ( xtalk_addr == bw_piomap->hpio_xtalk_info.xp_xtalk_addr &&
		     widget == bw_piomap->hpio_xtalk_info.xp_target) {
			bw_piomap->hpio_holdcnt++;
			mutex_spinunlock(&hubinfo->h_bwlock, s);
			return(bw_piomap);
		}
	}

	/*
	 * None of the existing big window mappings will work for us --
	 * we need to establish a new mapping.
	 */

	/* Insure that we don't consume all big windows with FIXED mappings */
	if (flags & PIOMAP_FIXED) {
		if (hubinfo->h_num_big_window_fixed < HUB_NUM_BIG_WINDOW-1) {
			ASSERT(free_bw_index >= 0);
			hubinfo->h_num_big_window_fixed++;
		} else {
			bw_piomap = NULL;
			goto done;
		}
	} else /* PIOMAP_UNFIXED */ {
		if (free_bw_index < 0) {
			if (flags & PIOMAP_NOSLEEP) {
				bw_piomap = NULL;
				goto done;
			}

			sv_wait(&hubinfo->h_bwwait, PZERO, &hubinfo->h_bwlock, s);
			goto tryagain;
		}
	}


	/* OK!  Allocate big window free_bw_index for this mapping. */
 	/* 
	 * The code below does a PIO write to setup an ITTE entry.
	 * We need to prevent other CPUs from seeing our updated memory 
	 * shadow of the ITTE (in the piomap) until the ITTE entry is 
	 * actually set up; otherwise, another CPU might attempt a PIO 
	 * prematurely.  
	 *
	 * Also, the only way we can know that an entry has been received 
	 * by the hub and can be used by future PIO reads/writes is by 
	 * reading back the ITTE entry after writing it.
	 *
	 * For these two reasons, we PIO read back the ITTE entry after
	 * we write it.
	 */

	nasid = hubinfo->h_nasid;
	IIO_ITTE_PUT(nasid, free_bw_index, HUB_PIO_MAP_TO_MEM, widget, xtalk_addr);	
	junk = HUB_L(IIO_ITTE_GET(nasid, free_bw_index));

	bw_piomap = hubinfo_bwin_piomap_get(hubinfo, free_bw_index);
	bw_piomap->hpio_xtalk_info.xp_dev = dev;
	bw_piomap->hpio_xtalk_info.xp_target = widget;
	bw_piomap->hpio_xtalk_info.xp_xtalk_addr = xtalk_addr;
	bw_piomap->hpio_xtalk_info.xp_kvaddr = (caddr_t)NODE_BWIN_BASE(nasid, free_bw_index);
	bw_piomap->hpio_holdcnt++;
	bw_piomap->hpio_bigwin_num = free_bw_index;

	if (flags & PIOMAP_FIXED)
		bw_piomap->hpio_flags |= HUB_PIOMAP_IS_VALID | HUB_PIOMAP_IS_FIXED;
	else
		bw_piomap->hpio_flags |= HUB_PIOMAP_IS_VALID;

done:
	mutex_spinunlock(&hubinfo->h_bwlock, s);
	return(bw_piomap);
}

/*
 * hub_piomap_free destroys a caddr_t-to-xtalk pio mapping and frees
 * any associated mapping resources.  
 *
 * If this * piomap was handled with a small window, or if it was handled
 * in a big window that's still in use by someone else, then there's 
 * nothing to do.  On the other hand, if this mapping was handled 
 * with a big window, AND if we were the final user of that mapping, 
 * then destroy the mapping.
 */
void
hub_piomap_free(hub_piomap_t hub_piomap)
{
	devfs_handle_t hubv;
	hubinfo_t hubinfo;
	nasid_t nasid;
	int s;

	/* 
	 * Small windows are permanently mapped to corresponding widgets,
	 * so there're no resources to free.
	 */
	if (!(hub_piomap->hpio_flags & HUB_PIOMAP_IS_BIGWINDOW))
		return;

	ASSERT(hub_piomap->hpio_flags & HUB_PIOMAP_IS_VALID);
	ASSERT(hub_piomap->hpio_holdcnt > 0);

	hubv = hub_piomap->hpio_hub;
	hubinfo_get(hubv, &hubinfo);
	nasid = hubinfo->h_nasid;

	s = mutex_spinlock(&hubinfo->h_bwlock);

	/*
	 * If this is the last hold on this mapping, free it.
	 */
	if (--hub_piomap->hpio_holdcnt == 0) {
		IIO_ITTE_DISABLE(nasid, hub_piomap->hpio_bigwin_num );

		if (hub_piomap->hpio_flags & HUB_PIOMAP_IS_FIXED) {
			hub_piomap->hpio_flags &= ~(HUB_PIOMAP_IS_VALID | HUB_PIOMAP_IS_FIXED);
			hubinfo->h_num_big_window_fixed--;
			ASSERT(hubinfo->h_num_big_window_fixed >= 0);
		} else
			hub_piomap->hpio_flags &= ~HUB_PIOMAP_IS_VALID;

		(void)sv_signal(&hubinfo->h_bwwait);
	}

	mutex_spinunlock(&hubinfo->h_bwlock, s);
}

/*
 * Establish a mapping to a given xtalk address range using the resources
 * allocated earlier.
 */
caddr_t
hub_piomap_addr(hub_piomap_t hub_piomap,	/* mapping resources */
		iopaddr_t xtalk_addr,		/* map for this xtalk address */
		size_t byte_count)		/* map this many bytes */
{
	/* Verify that range can be mapped using the specified piomap */
	if (xtalk_addr < hub_piomap->hpio_xtalk_info.xp_xtalk_addr)
		return(0);

	if (xtalk_addr + byte_count > 
		( hub_piomap->hpio_xtalk_info.xp_xtalk_addr + 
			hub_piomap->hpio_xtalk_info.xp_mapsz))
		return(0);

	if (hub_piomap->hpio_flags & HUB_PIOMAP_IS_VALID)
		return(hub_piomap->hpio_xtalk_info.xp_kvaddr + 
			(xtalk_addr % hub_piomap->hpio_xtalk_info.xp_mapsz));
	else
		return(0);
}


/*
 * Driver indicates that it's done with PIO's from an earlier piomap_addr.
 */
/* ARGSUSED */
void
hub_piomap_done(hub_piomap_t hub_piomap)	/* done with these mapping resources */
{
	/* Nothing to do */
}


/*
 * For translations that require no mapping resources, supply a kernel virtual
 * address that maps to the specified xtalk address range.
 */
/* ARGSUSED */
caddr_t
hub_piotrans_addr(	devfs_handle_t dev,	/* translate to this device */
			device_desc_t dev_desc,	/* device descriptor */
			iopaddr_t xtalk_addr,	/* Crosstalk address */
			size_t byte_count,	/* map this many bytes */
			unsigned flags)		/* (currently unused) */
{
	xwidget_info_t widget_info = xwidget_info_get(dev);
	xwidgetnum_t widget = xwidget_info_id_get(widget_info);
	devfs_handle_t hubv = xwidget_info_master_get(widget_info);
	hub_piomap_t hub_piomap;
	hubinfo_t hubinfo;

	hubinfo_get(hubv, &hubinfo);

	if (xtalk_addr + byte_count <= SWIN_SIZE) {
		hub_piomap = hubinfo_swin_piomap_get(hubinfo, (int)widget);
		return(hub_piomap_addr(hub_piomap, xtalk_addr, byte_count));
	} else
		return(0);
}


/* DMA MANAGEMENT */
/* Mapping from crosstalk space to system physical space */

/* 
 * There's not really very much to do here, since crosstalk maps
 * directly to system physical space.  It's quite possible that this
 * DMA layer will be bypassed in performance kernels.
 */


/* ARGSUSED */
static void
hub_dma_init(devfs_handle_t hubv)
{
}


/*
 * Allocate resources needed to set up DMA mappings up to a specified size
 * on a specified adapter.
 * 
 * We don't actually use the adapter ID for anything.  It's just the adapter
 * that the lower level driver plans to use for DMA.
 */
/* ARGSUSED */
hub_dmamap_t
hub_dmamap_alloc(	devfs_handle_t dev,	/* set up mappings for this device */
			device_desc_t dev_desc,	/* device descriptor */
			size_t byte_count_max, 	/* max size of a mapping */
			unsigned flags)		/* defined in dma.h */
{
	hub_dmamap_t dmamap;
	xwidget_info_t widget_info = xwidget_info_get(dev);
	xwidgetnum_t widget = xwidget_info_id_get(widget_info);
	devfs_handle_t hubv = xwidget_info_master_get(widget_info);

	dmamap = kern_malloc(sizeof(struct hub_dmamap_s));
	dmamap->hdma_xtalk_info.xd_dev = dev;
	dmamap->hdma_xtalk_info.xd_target = widget;
	dmamap->hdma_hub = hubv;
	dmamap->hdma_flags = HUB_DMAMAP_IS_VALID;
 	if (flags & XTALK_FIXED)
		dmamap->hdma_flags |= HUB_DMAMAP_IS_FIXED;

	return(dmamap);
}

/*
 * Destroy a DMA mapping from crosstalk space to system address space.
 * There is no actual mapping hardware to destroy, but we at least mark
 * the dmamap INVALID and free the space that it took.
 */
void
hub_dmamap_free(hub_dmamap_t hub_dmamap)
{
	hub_dmamap->hdma_flags &= ~HUB_DMAMAP_IS_VALID;
	kern_free(hub_dmamap);
}

/*
 * Establish a DMA mapping using the resources allocated in a previous dmamap_alloc.
 * Return an appropriate crosstalk address range that maps to the specified physical 
 * address range.
 */
/* ARGSUSED */
extern iopaddr_t
hub_dmamap_addr(	hub_dmamap_t dmamap,	/* use these mapping resources */
			paddr_t paddr,		/* map for this address */
			size_t byte_count)	/* map this many bytes */
{
	devfs_handle_t vhdl;

	ASSERT(dmamap->hdma_flags & HUB_DMAMAP_IS_VALID);

	if (dmamap->hdma_flags & HUB_DMAMAP_USED) {
	    /* If the map is FIXED, re-use is OK. */
	    if (!(dmamap->hdma_flags & HUB_DMAMAP_IS_FIXED)) {
		vhdl = dmamap->hdma_xtalk_info.xd_dev;
#if defined(SUPPORT_PRINTING_V_FORMAT)
		cmn_err(CE_WARN, "%v: hub_dmamap_addr re-uses dmamap.\n",vhdl);
#else
		cmn_err(CE_WARN, "0x%p: hub_dmamap_addr re-uses dmamap.\n", &vhdl);
#endif
	    }
	} else {
		dmamap->hdma_flags |= HUB_DMAMAP_USED;
	}

	/* There isn't actually any DMA mapping hardware on the hub. */
	return(paddr);
}

/*
 * Establish a DMA mapping using the resources allocated in a previous dmamap_alloc.
 * Return an appropriate crosstalk address list that maps to the specified physical 
 * address list.
 */
/* ARGSUSED */
alenlist_t
hub_dmamap_list(hub_dmamap_t hub_dmamap,	/* use these mapping resources */
		alenlist_t palenlist,		/* map this area of memory */
		unsigned flags)
{
	devfs_handle_t vhdl;

	ASSERT(hub_dmamap->hdma_flags & HUB_DMAMAP_IS_VALID);

	if (hub_dmamap->hdma_flags & HUB_DMAMAP_USED) {
	    /* If the map is FIXED, re-use is OK. */
	    if (!(hub_dmamap->hdma_flags & HUB_DMAMAP_IS_FIXED)) {
		vhdl = hub_dmamap->hdma_xtalk_info.xd_dev;
#if defined(SUPPORT_PRINTING_V_FORMAT)
		cmn_err(CE_WARN,"%v: hub_dmamap_list re-uses dmamap\n",vhdl);
#else
		cmn_err(CE_WARN,"0x%p: hub_dmamap_list re-uses dmamap\n", &vhdl);
#endif
	    }
	} else {
		hub_dmamap->hdma_flags |= HUB_DMAMAP_USED;
	}

	/* There isn't actually any DMA mapping hardware on the hub.  */
	return(palenlist);
}

/*
 * Driver indicates that it has completed whatever DMA it may have started
 * after an earlier dmamap_addr or dmamap_list call.
 */
void
hub_dmamap_done(hub_dmamap_t hub_dmamap)	/* done with these mapping resources */
{
	devfs_handle_t vhdl;

	if (hub_dmamap->hdma_flags & HUB_DMAMAP_USED) {
		hub_dmamap->hdma_flags &= ~HUB_DMAMAP_USED;
	} else {
	    /* If the map is FIXED, re-done is OK. */
	    if (!(hub_dmamap->hdma_flags & HUB_DMAMAP_IS_FIXED)) {
		vhdl = hub_dmamap->hdma_xtalk_info.xd_dev;
#if defined(SUPPORT_PRINTING_V_FORMAT)
		cmn_err(CE_WARN, "%v: hub_dmamap_done already done with dmamap\n",vhdl);
#else
		cmn_err(CE_WARN, "0x%p: hub_dmamap_done already done with dmamap\n", &vhdl);
#endif
	    }
	}
}

/*
 * Translate a single system physical address into a crosstalk address.
 */
/* ARGSUSED */
iopaddr_t
hub_dmatrans_addr(	devfs_handle_t dev,	/* translate for this device */
			device_desc_t dev_desc,	/* device descriptor */
			paddr_t paddr,		/* system physical address */
			size_t byte_count,	/* length */
			unsigned flags)		/* defined in dma.h */
{
	/* no translation needed */
	return(paddr);
}

/*
 * Translate a list of IP27 addresses and lengths into a list of crosstalk 
 * addresses and lengths.  No actual hardware mapping takes place; the hub 
 * has no DMA mapping registers -- crosstalk addresses map directly.
 */
/* ARGSUSED */
alenlist_t
hub_dmatrans_list(	devfs_handle_t dev,	/* translate for this device */
			device_desc_t dev_desc,	/* device descriptor */
			alenlist_t palenlist,	/* system address/length list */
			unsigned flags)		/* defined in dma.h */
{
	/* no translation needed */
	return(palenlist);
}

/*ARGSUSED*/
void
hub_dmamap_drain(	hub_dmamap_t map)
{
    /* XXX- flush caches, if cache coherency WAR is needed */
}

/*ARGSUSED*/
void
hub_dmaaddr_drain(	devfs_handle_t vhdl,
			paddr_t addr,
			size_t bytes)
{
    /* XXX- flush caches, if cache coherency WAR is needed */
}

/*ARGSUSED*/
void
hub_dmalist_drain(	devfs_handle_t vhdl,
			alenlist_t list)
{
    /* XXX- flush caches, if cache coherency WAR is needed */
}



/* INTERRUPT MANAGEMENT */

/* ARGSUSED */
static void
hub_intr_init(devfs_handle_t hubv)
{
}

/*
 * hub_device_desc_update
 *	Update the passed in device descriptor with the actual the
 * 	target cpu number and interrupt priority level.
 *	NOTE : These might be the same as the ones passed in thru
 *	the descriptor.
 */
static void
hub_device_desc_update(device_desc_t 	dev_desc, 
		       ilvl_t 		intr_swlevel,
		       cpuid_t		cpu)
{
	char	cpuname[40];
	
	/* Store the interrupt priority level in the device descriptor */
	device_desc_intr_swlevel_set(dev_desc, intr_swlevel);

	/* Convert the cpuid to the vertex handle in the hwgraph and
	 * save it in the device descriptor.
	 */
	sprintf(cpuname,"/hw/cpunum/%ld",cpu);
	device_desc_intr_target_set(dev_desc, 
				    hwgraph_path_to_dev(cpuname));
}

int allocate_my_bit = INTRCONNECT_ANYBIT;

/*
 * Allocate resources required for an interrupt as specified in dev_desc.
 * Returns a hub interrupt handle on success, or 0 on failure.
 */
hub_intr_t
hub_intr_alloc(	devfs_handle_t dev,		/* which crosstalk device */
		device_desc_t dev_desc,		/* device descriptor */
		devfs_handle_t owner_dev)		/* owner of this interrupt, if known */
{
	cpuid_t cpu;			/* cpu to receive interrupt */
        int cpupicked = 0;
	int bit;			/* interrupt vector */
	/*REFERENCED*/
	int intr_resflags;
	hub_intr_t intr_hdl;
	cnodeid_t nodeid;		/* node to receive interrupt */
	/*REFERENCED*/
	nasid_t nasid;			/* nasid to receive interrupt */
	struct xtalk_intr_s *xtalk_info;
	iopaddr_t xtalk_addr;		/* xtalk addr on hub to set intr */
	xwidget_info_t xwidget_info;	/* standard crosstalk widget info handle */
	char *intr_name = NULL;
	ilvl_t intr_swlevel;
	extern int default_intr_pri;
#ifdef CONFIG_IA64_SGI_SN1 
	extern void synergy_intr_alloc(int, int);
#endif
	
	/*
	 * If caller didn't explicily specify a device descriptor, see if there's
	 * a default descriptor associated with the device.
	 */
	if (!dev_desc) 
		dev_desc = device_desc_default_get(dev);

	if (dev_desc) {
		intr_name = device_desc_intr_name_get(dev_desc);
		intr_swlevel = device_desc_intr_swlevel_get(dev_desc);
		if (dev_desc->flags & D_INTR_ISERR) {
			intr_resflags = II_ERRORINT;
		} else if (!(dev_desc->flags & D_INTR_NOTHREAD)) {
			intr_resflags = II_THREADED;
		} else {
			/* Neither an error nor a thread. */
			intr_resflags = 0;
		}
	} else {
		intr_swlevel = default_intr_pri;
		intr_resflags = II_THREADED;
	}

	/* XXX - Need to determine if the interrupt should be threaded. */

	/* If the cpu has not been picked already then choose a candidate 
	 * interrupt target and reserve the interrupt bit 
	 */
#if defined(NEW_INTERRUPTS)
	if (!cpupicked) {
		cpu = intr_heuristic(dev,dev_desc,allocate_my_bit,
				     intr_resflags,owner_dev,
				     intr_name,&bit);
	}
#endif

	/* At this point we SHOULD have a valid cpu */
	if (cpu == CPU_NONE) {
#if defined(SUPPORT_PRINTING_V_FORMAT)
		cmn_err(CE_WARN, 
			"%v hub_intr_alloc could not allocate interrupt\n",
			owner_dev);
#else
		cmn_err(CE_WARN, 
			"0x%p hub_intr_alloc could not allocate interrupt\n",
			&owner_dev);
#endif
		return(0);

	}

	/* If the cpu has been picked already (due to the bridge data 
	 * corruption bug) then try to reserve an interrupt bit .
	 */
#if defined(NEW_INTERRUPTS)
	if (cpupicked) {
		bit = intr_reserve_level(cpu, allocate_my_bit, 
					 intr_resflags, 
					 owner_dev, intr_name);
		if (bit < 0) {
#if defined(SUPPORT_PRINTING_V_FORMAT)
			cmn_err(CE_WARN,
				"Could not reserve an interrupt bit for cpu "
				" %d and dev %v\n",
				cpu,owner_dev);
#else
			cmn_err(CE_WARN,
				"Could not reserve an interrupt bit for cpu "
				" %d and dev 0x%x\n",
				cpu, &owner_dev);
#endif
				
			return(0);
		}
	}
#endif	/* NEW_INTERRUPTS */

	nodeid = cpuid_to_cnodeid(cpu);
	nasid = cpuid_to_nasid(cpu);
	xtalk_addr = HUBREG_AS_XTALKADDR(nasid, PIREG(PI_INT_PEND_MOD, cpuid_to_subnode(cpu)));

	/*
	 * Allocate an interrupt handle, and fill it in.  There are two
	 * pieces to an interrupt handle: the piece needed by generic
	 * xtalk code which is used by crosstalk device drivers, and
	 * the piece needed by low-level IP27 hardware code.
	 */
	intr_hdl = kmem_alloc_node(sizeof(struct hub_intr_s), KM_NOSLEEP, nodeid);
	ASSERT_ALWAYS(intr_hdl);

	/* 
	 * Fill in xtalk information for generic xtalk interfaces that
	 * operate on xtalk_intr_hdl's.
	 */
	xtalk_info = &intr_hdl->i_xtalk_info;
	xtalk_info->xi_dev = dev;
	xtalk_info->xi_vector = bit;
	xtalk_info->xi_addr = xtalk_addr;
	xtalk_info->xi_flags =  (intr_resflags == II_THREADED) ?
				0 : XTALK_INTR_NOTHREAD;

	/*
	 * Regardless of which CPU we ultimately interrupt, a given crosstalk
	 * widget always handles interrupts (and PIO and DMA) through its 
	 * designated "master" crosstalk provider.
	 */
	xwidget_info = xwidget_info_get(dev);
	if (xwidget_info)
		xtalk_info->xi_target = xwidget_info_masterid_get(xwidget_info);

	/* Fill in low level hub information for hub_* interrupt interface */
	intr_hdl->i_swlevel = intr_swlevel;
	intr_hdl->i_cpuid = cpu;
	intr_hdl->i_bit = bit;
	intr_hdl->i_flags = HUB_INTR_IS_ALLOCED;

	/* Store the actual interrupt priority level & interrupt target
	 * cpu back in the device descriptor.
	 */
	hub_device_desc_update(dev_desc, intr_swlevel, cpu);
#ifdef CONFIG_IA64_SGI_SN1
	synergy_intr_alloc((int)bit, (int)cpu);
#endif
	return(intr_hdl);
}


/*
 * Free resources consumed by intr_alloc.
 */
void
hub_intr_free(hub_intr_t intr_hdl)
{
	cpuid_t cpu = intr_hdl->i_cpuid;
	int bit = intr_hdl->i_bit;
	xtalk_intr_t xtalk_info;

	if (intr_hdl->i_flags & HUB_INTR_IS_CONNECTED) {
		/* Setting the following fields in the xtalk interrupt info
	 	 * clears the interrupt target register in the xtalk user
	 	 */
		xtalk_info = &intr_hdl->i_xtalk_info;
		xtalk_info->xi_dev = NODEV;
		xtalk_info->xi_vector = 0;
		xtalk_info->xi_addr = 0;
		hub_intr_disconnect(intr_hdl);
	}

	if (intr_hdl->i_flags & HUB_INTR_IS_ALLOCED)
		kfree(intr_hdl);

#if defined(NEW_INTERRUPTS)
	intr_unreserve_level(cpu, bit);
#endif
}


/*
 * Associate resources allocated with a previous hub_intr_alloc call with the
 * described handler, arg, name, etc.
 */
/*ARGSUSED*/
int
hub_intr_connect(	hub_intr_t intr_hdl,		/* xtalk intr resource handle */
			intr_func_t intr_func,		/* xtalk intr handler */
			void *intr_arg,			/* arg to intr handler */
			xtalk_intr_setfunc_t setfunc,	/* func to set intr hw */
			void *setfunc_arg,		/* arg to setfunc */
			void *thread)			/* intr thread to use */
{
	int rv;
	cpuid_t cpu = intr_hdl->i_cpuid;
	int bit = intr_hdl->i_bit;
#ifdef CONFIG_IA64_SGI_SN1
	extern int synergy_intr_connect(int, int);
#endif

	ASSERT(intr_hdl->i_flags & HUB_INTR_IS_ALLOCED);

#if defined(NEW_INTERRUPTS)
	rv = intr_connect_level(cpu, bit, intr_hdl->i_swlevel, 
					intr_func, intr_arg, NULL);
	if (rv < 0)
		return(rv);

#endif
	intr_hdl->i_xtalk_info.xi_setfunc = setfunc;
	intr_hdl->i_xtalk_info.xi_sfarg = setfunc_arg;

	if (setfunc) (*setfunc)((xtalk_intr_t)intr_hdl);

	intr_hdl->i_flags |= HUB_INTR_IS_CONNECTED;
#ifdef CONFIG_IA64_SGI_SN1
	return(synergy_intr_connect((int)bit, (int)cpu));
#endif
}


/*
 * Disassociate handler with the specified interrupt.
 */
void
hub_intr_disconnect(hub_intr_t intr_hdl)
{
	/*REFERENCED*/
	int rv;
	cpuid_t cpu = intr_hdl->i_cpuid;
	int bit = intr_hdl->i_bit;
	xtalk_intr_setfunc_t setfunc;

	setfunc = intr_hdl->i_xtalk_info.xi_setfunc;

	/* TBD: send disconnected interrupts somewhere harmless */
	if (setfunc) (*setfunc)((xtalk_intr_t)intr_hdl);

#if defined(NEW_INTERRUPTS)
	rv = intr_disconnect_level(cpu, bit);
	ASSERT(rv == 0);
#endif

	intr_hdl->i_flags &= ~HUB_INTR_IS_CONNECTED;
}


/*
 * Return a hwgraph vertex that represents the CPU currently
 * targeted by an interrupt.
 */
devfs_handle_t
hub_intr_cpu_get(hub_intr_t intr_hdl)
{
	cpuid_t cpuid = intr_hdl->i_cpuid;
	ASSERT(cpuid != CPU_NONE);

	return(cpuid_to_vertex(cpuid));
}



/* CONFIGURATION MANAGEMENT */

/*
 * Perform initializations that allow this hub to start crosstalk support.
 */
void
hub_provider_startup(devfs_handle_t hubv)
{
	hub_pio_init(hubv);
	hub_dma_init(hubv);
	hub_intr_init(hubv);
}

/*
 * Shutdown crosstalk support from a hub.
 */
void
hub_provider_shutdown(devfs_handle_t hub)
{
	/* TBD */
	xtalk_provider_unregister(hub);
}

/*
 * Check that an address is in teh real small window widget 0 space
 * or else in the big window we're using to emulate small window 0
 * in the kernel.
 */
int
hub_check_is_widget0(void *addr)
{
	nasid_t nasid = NASID_GET(addr);

	if (((__psunsigned_t)addr >= RAW_NODE_SWIN_BASE(nasid, 0)) &&
	    ((__psunsigned_t)addr < RAW_NODE_SWIN_BASE(nasid, 1)))
		return 1;
	return 0;
}


/*
 * Check that two addresses use the same widget
 */
int
hub_check_window_equiv(void *addra, void *addrb)
{
	if (hub_check_is_widget0(addra) && hub_check_is_widget0(addrb))
		return 1;

	/* XXX - Assume this is really a small window address */
	if (WIDGETID_GET((__psunsigned_t)addra) ==
	    WIDGETID_GET((__psunsigned_t)addrb))
		return 1;

	return 0;
}


/*
 * Determine whether two PCI addresses actually refer to the same device.
 * This only works if both addresses are in small windows.  It's used to
 * determine whether prom addresses refer to particular PCI devices.
 */
/*	
 * XXX - This won't work as written if we ever have more than two nodes
 * on a crossbow.  In that case, we'll need an array or partners.
 */
int
hub_check_pci_equiv(void *addra, void *addrb)
{
	nasid_t nasida, nasidb;

	/*
	 * This is for a permanent workaround that causes us to use a
	 * big window in place of small window 0.
	 */
	if (!hub_check_window_equiv(addra, addrb))
		return 0;

	/* If the offsets aren't the same, forget it. */
	if (SWIN_WIDGETADDR((__psunsigned_t)addra) !=
	    (SWIN_WIDGETADDR((__psunsigned_t)addrb)))
		return 0;

	/* Now, check the nasids */
	nasida = NASID_GET(addra);
	nasidb = NASID_GET(addrb);

	ASSERT(NASID_TO_COMPACT_NODEID(nasida) != INVALID_NASID);
	ASSERT(NASID_TO_COMPACT_NODEID(nasidb) != INVALID_NASID);

	/*
	 * Either the NASIDs must be the same or they must be crossbow
	 * partners (on the same crossbow).
	 */
	return (check_nasid_equiv(nasida, nasidb));
}

/*
 * hub_setup_prb(nasid, prbnum, credits, conveyor)
 *
 * 	Put a PRB into fire-and-forget mode if conveyor isn't set.  Otehrwise,
 * 	put it into conveyor belt mode with the specified number of credits.
 */
void
hub_setup_prb(nasid_t nasid, int prbnum, int credits, int conveyor)
{
	iprb_t prb;
	int prb_offset;
#ifdef IRIX
	extern int force_fire_and_forget;
	extern volatile int ignore_conveyor_override;

	if (force_fire_and_forget && !ignore_conveyor_override)
	    if (conveyor == HUB_PIO_CONVEYOR)
		conveyor = HUB_PIO_FIRE_N_FORGET;
#endif

	/*
	 * Get the current register value.
	 */
	prb_offset = IIO_IOPRB(prbnum);
	prb.iprb_regval = REMOTE_HUB_L(nasid, prb_offset);

	/*
	 * Clear out some fields.
	 */
	prb.iprb_ovflow = 1;
	prb.iprb_bnakctr = 0;
	prb.iprb_anakctr = 0;

	/*
	 * Enable or disable fire-and-forget mode.
	 */
	prb.iprb_ff = ((conveyor == HUB_PIO_CONVEYOR) ? 0 : 1);

	/*
	 * Set the appropriate number of PIO cresits for the widget.
	 */
	prb.iprb_xtalkctr = credits;

	/*
	 * Store the new value to the register.
	 */
	REMOTE_HUB_S(nasid, prb_offset, prb.iprb_regval);
}

/*
 * hub_set_piomode()
 *
 * 	Put the hub into either "PIO conveyor belt" mode or "fire-and-forget"
 * 	mode.  To do this, we have to make absolutely sure that no PIOs
 *	are in progress so we turn off access to all widgets for the duration
 *	of the function.
 * 
 * XXX - This code should really check what kind of widget we're talking
 * to.  Bridges can only handle three requests, but XG will do more.
 * How many can crossbow handle to widget 0?  We're assuming 1.
 *
 * XXX - There is a bug in the crossbow that link reset PIOs do not
 * return write responses.  The easiest solution to this problem is to
 * leave widget 0 (xbow) in fire-and-forget mode at all times.  This
 * only affects pio's to xbow registers, which should be rare.
 */
void
hub_set_piomode(nasid_t nasid, int conveyor)
{
	hubreg_t ii_iowa;
	int direct_connect;
	hubii_wcr_t ii_wcr;
	int prbnum;
	int s, cons_lock = 0;

	ASSERT(NASID_TO_COMPACT_NODEID(nasid) != INVALID_CNODEID);
	if (nasid == get_console_nasid()) {
		PUTBUF_LOCK(s);	
		cons_lock = 1;
	}

	ii_iowa = REMOTE_HUB_L(nasid, IIO_OUTWIDGET_ACCESS);
	REMOTE_HUB_S(nasid, IIO_OUTWIDGET_ACCESS, 0);

	ii_wcr.wcr_reg_value = REMOTE_HUB_L(nasid, IIO_WCR);
	direct_connect = ii_wcr.iwcr_dir_con;

	if (direct_connect) {
		/* 
		 * Assume a bridge here.
		 */
		hub_setup_prb(nasid, 0, 3, conveyor);
	} else {
		/* 
		 * Assume a crossbow here.
		 */
		hub_setup_prb(nasid, 0, 1, conveyor);
	}

	for (prbnum = HUB_WIDGET_ID_MIN; prbnum <= HUB_WIDGET_ID_MAX; prbnum++) {
		/*
		 * XXX - Here's where we should take the widget type into
		 * when account assigning credits.
		 */
		/* Always set the PRBs in fire-and-forget mode */
		hub_setup_prb(nasid, prbnum, 3, conveyor);
	}

#ifdef IRIX
	/*
	 * In direct connect mode, disable access to all widgets but 0.
	 * Later, the prom will do this for us.
	 */
	if (direct_connect)
		ii_iowa = 1;
#endif

	REMOTE_HUB_S(nasid, IIO_OUTWIDGET_ACCESS, ii_iowa);

	if (cons_lock)
	    PUTBUF_UNLOCK(s);
}
/* Interface to allow special drivers to set hub specific
 * device flags.
 * Return 0 on failure , 1 on success
 */
int
hub_widget_flags_set(nasid_t		nasid,
		     xwidgetnum_t	widget_num,
		     hub_widget_flags_t	flags)
{

	ASSERT((flags & HUB_WIDGET_FLAGS) == flags);

	if (flags & HUB_PIO_CONVEYOR) {
		hub_setup_prb(nasid,widget_num,
			      3,HUB_PIO_CONVEYOR); /* set the PRB in conveyor 
						    * belt mode with 3 credits
						    */
	} else if (flags & HUB_PIO_FIRE_N_FORGET) {
		hub_setup_prb(nasid,widget_num,
			      3,HUB_PIO_FIRE_N_FORGET); /* set the PRB in fire
							 *  and forget mode 
							 */
	}

	return 1;
}
/* Interface to allow special drivers to set hub specific
 * device flags.
 * Return 0 on failure , 1 on success
 */
int
hub_device_flags_set(devfs_handle_t	widget_vhdl,
		     hub_widget_flags_t	flags)
{
	xwidget_info_t		widget_info = xwidget_info_get(widget_vhdl);
	xwidgetnum_t		widget_num  = xwidget_info_id_get(widget_info);
	devfs_handle_t		hub_vhdl    = xwidget_info_master_get(widget_info);
	hubinfo_t		hub_info = 0;
	nasid_t			nasid;
	int			s,rv;

	/* Use the nasid from the hub info hanging off the hub vertex
	 * and widget number from the widget vertex
	 */
	hubinfo_get(hub_vhdl, &hub_info);
	/* Being over cautious by grabbing a lock */
	s 	= mutex_spinlock(&hub_info->h_bwlock);
	nasid 	= hub_info->h_nasid;
	rv 	= hub_widget_flags_set(nasid,widget_num,flags);
	mutex_spinunlock(&hub_info->h_bwlock, s);

	return rv;
}

#if ((defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)) && defined(BRINGUP))
/* BRINGUP:  This ought to be useful for IP27 too but, for now,
 * make it SN1 only because `ii_ixtt_u_t' is not in IP27/hubio.h
 * (or anywhere else :-).
 */
int
hubii_ixtt_set(devfs_handle_t widget_vhdl, ii_ixtt_u_t *ixtt)
{
	xwidget_info_t		widget_info = xwidget_info_get(widget_vhdl);
	devfs_handle_t		hub_vhdl    = xwidget_info_master_get(widget_info);
	hubinfo_t		hub_info = 0;
	nasid_t			nasid;
	int			s;

	/* Use the nasid from the hub info hanging off the hub vertex
	 * and widget number from the widget vertex
	 */
	hubinfo_get(hub_vhdl, &hub_info);
	/* Being over cautious by grabbing a lock */
	s 	= mutex_spinlock(&hub_info->h_bwlock);
	nasid 	= hub_info->h_nasid;

	REMOTE_HUB_S(nasid, IIO_IXTT, ixtt->ii_ixtt_regval);

	mutex_spinunlock(&hub_info->h_bwlock, s);
	return 0;
}

int
hubii_ixtt_get(devfs_handle_t widget_vhdl, ii_ixtt_u_t *ixtt)
{
	xwidget_info_t		widget_info = xwidget_info_get(widget_vhdl);
	devfs_handle_t		hub_vhdl    = xwidget_info_master_get(widget_info);
	hubinfo_t		hub_info = 0;
	nasid_t			nasid;
	int			s;

	/* Use the nasid from the hub info hanging off the hub vertex
	 * and widget number from the widget vertex
	 */
	hubinfo_get(hub_vhdl, &hub_info);
	/* Being over cautious by grabbing a lock */
	s 	= mutex_spinlock(&hub_info->h_bwlock);
	nasid 	= hub_info->h_nasid;

	ixtt->ii_ixtt_regval = REMOTE_HUB_L(nasid, IIO_IXTT);

	mutex_spinunlock(&hub_info->h_bwlock, s);
	return 0;
}
#endif /* CONFIG_IA64_SGI_SN1 */

/*
 * hub_device_inquiry
 *	Find out the xtalk widget related information stored in this 
 *	hub's II.
 */
void
hub_device_inquiry(devfs_handle_t	xbus_vhdl, xwidgetnum_t widget)
{
	devfs_handle_t	xconn, hub_vhdl;
	char		widget_name[8];
	hubreg_t	ii_iidem,ii_iiwa, ii_iowa;
	hubinfo_t	hubinfo;
	nasid_t		nasid;
	int		d;

	sprintf(widget_name, "%d", widget);
	if (hwgraph_traverse(xbus_vhdl, widget_name, &xconn)
	    != GRAPH_SUCCESS)
		return;

	hub_vhdl = device_master_get(xconn);
	if (hub_vhdl == GRAPH_VERTEX_NONE)
		return;

	hubinfo_get(hub_vhdl, &hubinfo);
	if (!hubinfo)
		return;
	
	nasid = hubinfo->h_nasid;

	ii_iidem	= REMOTE_HUB_L(nasid, IIO_IIDEM);
	ii_iiwa 	= REMOTE_HUB_L(nasid, IIO_IIWA);
	ii_iowa 	= REMOTE_HUB_L(nasid, IIO_IOWA);

#if defined(SUPPORT_PRINTING_V_FORMAT)
	cmn_err(CE_CONT, "Inquiry Info for %v\n", xconn);
#else
	cmn_err(CE_CONT, "Inquiry Info for 0x%p\n", &xconn);
#endif

	cmn_err(CE_CONT,"\tDevices shutdown [ ");

	for (d = 0 ; d <= 7 ; d++)
		if (!(ii_iidem & (IIO_IIDEM_WIDGETDEV_MASK(widget,d))))
			cmn_err(CE_CONT, " %d", d);

	cmn_err(CE_CONT,"]\n");

	cmn_err(CE_CONT,
		"\tInbound access ? %s\n",
		ii_iiwa & IIO_IIWA_WIDGET(widget) ? "yes" : "no");

	cmn_err(CE_CONT,
		"\tOutbound access ? %s\n",
		ii_iowa & IIO_IOWA_WIDGET(widget) ? "yes" : "no");

}

/*
 * A pointer to this structure hangs off of every hub hwgraph vertex.
 * The generic xtalk layer may indirect through it to get to this specific
 * crosstalk bus provider.
 */
xtalk_provider_t hub_provider = {
	(xtalk_piomap_alloc_f *)	hub_piomap_alloc,
	(xtalk_piomap_free_f *)		hub_piomap_free,
	(xtalk_piomap_addr_f *)		hub_piomap_addr,
	(xtalk_piomap_done_f *)		hub_piomap_done,
	(xtalk_piotrans_addr_f *)	hub_piotrans_addr,

	(xtalk_dmamap_alloc_f *)	hub_dmamap_alloc,
	(xtalk_dmamap_free_f *)		hub_dmamap_free,
	(xtalk_dmamap_addr_f *)		hub_dmamap_addr,
	(xtalk_dmamap_list_f *)		hub_dmamap_list,
	(xtalk_dmamap_done_f *)		hub_dmamap_done,
	(xtalk_dmatrans_addr_f *)	hub_dmatrans_addr,
	(xtalk_dmatrans_list_f *)	hub_dmatrans_list,
	(xtalk_dmamap_drain_f *)	hub_dmamap_drain,
	(xtalk_dmaaddr_drain_f *)	hub_dmaaddr_drain,
	(xtalk_dmalist_drain_f *)	hub_dmalist_drain,

	(xtalk_intr_alloc_f *)		hub_intr_alloc,
	(xtalk_intr_free_f *)		hub_intr_free,
	(xtalk_intr_connect_f *)	hub_intr_connect,
	(xtalk_intr_disconnect_f *)	hub_intr_disconnect,
	(xtalk_intr_cpu_get_f *)	hub_intr_cpu_get,

	(xtalk_provider_startup_f *)	hub_provider_startup,
	(xtalk_provider_shutdown_f *)	hub_provider_shutdown,
};

