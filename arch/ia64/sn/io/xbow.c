/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2001 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/hack.h>
#include <asm/sn/pci/bridge.h>
#include <asm/sn/xtalk/xtalk_private.h>
#include <asm/sn/simulator.h>

/* #define DEBUG		1 */
/* #define XBOW_DEBUG	1 */


/*
 * Files needed to get the device driver entry points
 */

#include <asm/sn/xtalk/xbow.h>
#include <asm/sn/xtalk/xtalk.h>
#include <asm/sn/xtalk/xswitch.h>
#include <asm/sn/xtalk/xwidget.h>

#include <asm/sn/prio.h>
#include <asm/sn/hcl_util.h>


#define NEW(ptr)	(ptr = kmalloc(sizeof (*(ptr)), GFP_KERNEL))
#define DEL(ptr)	(kfree(ptr))

int                     xbow_devflag = D_MP;

/*
 * This file supports the Xbow chip.  Main functions: initializtion,
 * error handling, and GBR.
 */

/*
 * each vertex corresponding to an xbow chip
 * has a "fastinfo" pointer pointing at one
 * of these things.
 */
typedef struct xbow_soft_s *xbow_soft_t;

struct xbow_soft_s {
    devfs_handle_t            conn;	/* our connection point */
    devfs_handle_t            vhdl;	/* xbow's private vertex */
    devfs_handle_t            busv;	/* the xswitch vertex */
    xbow_t                 *base;	/* PIO pointer to crossbow chip */
    char                   *name;	/* hwgraph name */

    xbow_perf_t             xbow_perfcnt[XBOW_PERF_COUNTERS];
    xbow_perf_link_t        xbow_perflink[MAX_XBOW_PORTS];
    xbow_link_status_t      xbow_link_status[MAX_XBOW_PORTS];
    spinlock_t              xbow_perf_lock;
    int                     link_monitor;
    widget_cfg_t	   *wpio[MAX_XBOW_PORTS];	/* cached PIO pointer */

    /* Bandwidth allocation state. Bandwidth values are for the
     * destination port since contention happens there.
     * Implicit mapping from xbow ports (8..f) -> (0..7) array indices.
     */
    spinlock_t		    xbow_bw_alloc_lock;		/* bw allocation lock */
    unsigned long long	    bw_hiwm[MAX_XBOW_PORTS];	/* hiwater mark values */
    unsigned long long      bw_cur_used[MAX_XBOW_PORTS]; /* bw used currently */
};

#define xbow_soft_set(v,i)	hwgraph_fastinfo_set((v), (arbitrary_info_t)(i))
#define xbow_soft_get(v)	((xbow_soft_t)hwgraph_fastinfo_get((v)))

/*
 * Function Table of Contents
 */

void                    xbow_mlreset(xbow_t *);
void                    xbow_init(void);
int                     xbow_attach(devfs_handle_t);

int                     xbow_open(devfs_handle_t *, int, int, cred_t *);
int                     xbow_close(devfs_handle_t, int, int, cred_t *);

int                     xbow_map(devfs_handle_t, vhandl_t *, off_t, size_t, uint);
int                     xbow_unmap(devfs_handle_t, vhandl_t *);
int                     xbow_ioctl(devfs_handle_t, int, void *, int, struct cred *, int *);

int                     xbow_widget_present(xbow_t *, int);
static int              xbow_link_alive(xbow_t *, int);
devfs_handle_t            xbow_widget_lookup(devfs_handle_t, int);

#ifdef LATER
static void             xbow_setwidint(xtalk_intr_t);
static void             xbow_errintr_handler(intr_arg_t);
#endif
void                    xbow_intr_preset(void *, int, xwidgetnum_t, iopaddr_t, xtalk_intr_vector_t);



void                    xbow_update_perf_counters(devfs_handle_t);
xbow_perf_link_t       *xbow_get_perf_counters(devfs_handle_t);
int                     xbow_enable_perf_counter(devfs_handle_t, int, int, int);
xbow_link_status_t     *xbow_get_llp_status(devfs_handle_t);
void                    xbow_update_llp_status(devfs_handle_t);

int                     xbow_disable_llp_monitor(devfs_handle_t);
int                     xbow_enable_llp_monitor(devfs_handle_t);
int                     xbow_prio_bw_alloc(devfs_handle_t, xwidgetnum_t, xwidgetnum_t,
                                unsigned long long, unsigned long long);

xswitch_reset_link_f    xbow_reset_link;

void                    idbg_xbowregs(int64_t);

xswitch_provider_t      xbow_provider =
{
    xbow_reset_link,
};

/*
 *    xbow_mlreset: called at mlreset time if the
 *      platform specific code determines that there is
 *      a crossbow in a critical path that must be
 *      functional before the driver would normally get
 *      the device properly set up.
 *
 *      what do we need to do, that the boot prom can
 *      not be counted on to have already done, that is
 *      generic across all platforms using crossbows?
 */
/*ARGSUSED */
void
xbow_mlreset(xbow_t * xbow)
{
}

/*
 *    xbow_init: called with the rest of the device
 *      driver XXX_init routines. This platform *might*
 *      have a Crossbow chip, or even several, but it
 *      might have none. Register with the crosstalk
 *      generic provider so when we encounter the chip
 *      the right magic happens.
 */
void
xbow_init(void)
{

#if DEBUG && ATTACH_DEBUG
    printf("xbow_init\n");
#endif

    xwidget_driver_register(XXBOW_WIDGET_PART_NUM,
			    0, /* XXBOW_WIDGET_MFGR_NUM, */
			    "xbow_",
			    CDL_PRI_HI);	/* attach before friends */

    xwidget_driver_register(XBOW_WIDGET_PART_NUM,
			    XBOW_WIDGET_MFGR_NUM,
			    "xbow_",
			    CDL_PRI_HI);	/* attach before friends */
}

#ifdef XBRIDGE_REGS_SIM
/*    xbow_set_simulated_regs: sets xbow regs as needed
 *	for powering through the boot
 */
void
xbow_set_simulated_regs(xbow_t *xbow, int port)
{
    /*
     * turn on link
     */
    xbow->xb_link(port).link_status = (1<<31);
    /*
     * and give it a live widget too
     */
    xbow->xb_link(port).link_aux_status = XB_AUX_STAT_PRESENT;
    /*
     * zero the link control reg
     */
    xbow->xb_link(port).link_control = 0x0;
}
#endif /* XBRIDGE_REGS_SIM */

/*
 *    xbow_attach: the crosstalk provider has
 *      determined that there is a crossbow widget
 *      present, and has handed us the connection
 *      point for that vertex.
 *
 *      We not only add our own vertex, but add
 *      some "xtalk switch" data to the switch
 *      vertex (at the connect point's parent) if
 *      it does not have any.
 */

/*ARGSUSED */
int
xbow_attach(devfs_handle_t conn)
{
    /*REFERENCED */
    devfs_handle_t            vhdl;
    devfs_handle_t            busv;
    xbow_t                 *xbow;
    xbow_soft_t             soft;
    int                     port;
    xswitch_info_t          info;
#ifdef LATER
    xtalk_intr_t            intr_hdl;
    device_desc_t           dev_desc;
#endif
    char                    devnm[MAXDEVNAME], *s;
    xbowreg_t               id;
    int                     rev;
    int			    i;
    int			    xbow_num;
	
#if DEBUG && ATTACH_DEBUG
#if defined(SUPPORT_PRINTING_V_FORMAT
    printk("%v: xbow_attach\n", conn);
#else
    printk("0x%x: xbow_attach\n", conn);
#endif
#endif

    /*
     * Get a PIO pointer to the base of the crossbow
     * chip.
     */
#ifdef XBRIDGE_REGS_SIM
    printk("xbow_attach: XBRIDGE_REGS_SIM FIXME: allocating %ld bytes for xbow_s\n", sizeof(xbow_t));
    xbow = (xbow_t *) kmalloc(sizeof(xbow_t), GFP_KERNEL);
    /*
     * turn on ports e and f like in a real live ibrick
     */
    xbow_set_simulated_regs(xbow, 0xe);
    xbow_set_simulated_regs(xbow, 0xf);
#else
    xbow = (xbow_t *) xtalk_piotrans_addr(conn, 0, 0, sizeof(xbow_t), 0);
#endif /* XBRIDGE_REGS_SIM */

    /*
     * Locate the "switch" vertex: it is the parent
     * of our connection point.
     */
    busv = hwgraph_connectpt_get(conn);
#if DEBUG && ATTACH_DEBUG
    printk("xbow_attach: Bus Vertex 0x%p, conn 0x%p, xbow register 0x%p wid= 0x%x\n", busv, conn, xbow, *(volatile u32 *)xbow);
#endif

    ASSERT(busv != GRAPH_VERTEX_NONE);

    /*
     * Create our private vertex, and connect our
     * driver information to it. This makes it possible
     * for diagnostic drivers to open the crossbow
     * vertex for access to registers.
     */

    /*
     * We need to teach xbow drivers to provide the right set of
     * file ops.
     */
    vhdl = NULL;
    vhdl = hwgraph_register(conn, EDGE_LBL_XBOW,
                        0, DEVFS_FL_AUTO_DEVNUM,
                        0, 0,
                        S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP, 0, 0,
                        /* &hcl_fops */ (void *)&vhdl, NULL);
    if (!vhdl) {
        printk(KERN_WARNING "xbow_attach: Unable to create char device for xbow conn %p\n",
                (void *)conn);
    }

    /*
     * Allocate the soft state structure and attach
     * it to the xbow's vertex
     */
    NEW(soft);
    soft->conn = conn;
    soft->vhdl = vhdl;
    soft->busv = busv;
    soft->base = xbow;
    /* does the universe really need another macro?  */
    /* xbow_soft_set(vhdl, (arbitrary_info_t) soft); */
    hwgraph_fastinfo_set(vhdl, (arbitrary_info_t) soft);

#define XBOW_NUM_SUFFIX_FORMAT	"[xbow# %d]"

    /* Add xbow number as a suffix to the hwgraph name of the xbow.
     * This is helpful while looking at the error/warning messages.
     */
    xbow_num = 0;

    /*
     * get the name of this xbow vertex and keep the info.
     * This is needed during errors and interupts, but as
     * long as we have it, we can use it elsewhere.
     */
    s = dev_to_name(vhdl, devnm, MAXDEVNAME);
    soft->name = kmalloc(strlen(s) + strlen(XBOW_NUM_SUFFIX_FORMAT) + 1, 
			    GFP_KERNEL);
    sprintf(soft->name,"%s"XBOW_NUM_SUFFIX_FORMAT, s,xbow_num);

#ifdef XBRIDGE_REGS_SIM
    /* my o200/ibrick has id=0x2d002049, but XXBOW_WIDGET_PART_NUM is defined
     * as 0xd000, so I'm using that for the partnum bitfield.
     */
    printk("xbow_attach: XBRIDGE_REGS_SIM FIXME: need xb_wid_id value!!\n");
    id = 0x2d000049;
#else
    id = xbow->xb_wid_id;
#endif /* XBRIDGE_REGS_SIM */
    rev = XWIDGET_PART_REV_NUM(id);

    /*
     * Print the revision if DEBUG, or SHOW_REVS and kdebug,
     * or the xbow is downrev.
     *
     * If xbow is downrev, make it a WARNING that the
     * Crossbow is DOWNREV: these chips are not good
     * to have around, and the operator should be told.
     */
#ifdef	LATER
#if !DEBUG
    if (
#if SHOW_REVS
	   (kdebug) ||
#endif	/* SHOW_REVS */
	   (rev < XBOW_REV_1_1))
#endif	/* !DEBUG  */
	printk("%sCrossbow ASIC: rev %s (code=%d) at %s%s",
		(rev < XBOW_REV_1_1) ? "DOWNREV " : "",
		(rev == XBOW_REV_1_0) ? "1.0" :
		(rev == XBOW_REV_1_1) ? "1.1" :
		(rev == XBOW_REV_1_2) ? "1.2" :
		(rev == XBOW_REV_1_3) ? "1.3" :
		(rev == XBOW_REV_2_0) ? "2.0" :
		(rev == XXBOW_PART_REV_1_0) ? "Xbridge 1.0" :
		(rev == XXBOW_PART_REV_2_0) ? "Xbridge 2.0" :
		"unknown",
		rev, soft->name,
		(rev < XBOW_REV_1_1) ? "" : "\n");
#endif	/* LATER */
    mutex_spinlock_init(&soft->xbow_perf_lock);
    soft->xbow_perfcnt[0].xp_perf_reg = &xbow->xb_perf_ctr_a;
    soft->xbow_perfcnt[1].xp_perf_reg = &xbow->xb_perf_ctr_b;

    /* Initialization for GBR bw allocation */
    mutex_spinlock_init(&soft->xbow_bw_alloc_lock);

#define	XBOW_8_BIT_PORT_BW_MAX		(400 * 1000 * 1000)	/* 400 MB/s */
#define XBOW_16_BIT_PORT_BW_MAX		(800 * 1000 * 1000)	/* 800 MB/s */

    /* Set bandwidth hiwatermark and current values */
    for (i = 0; i < MAX_XBOW_PORTS; i++) {
	soft->bw_hiwm[i] = XBOW_16_BIT_PORT_BW_MAX;	/* for now */
	soft->bw_cur_used[i] = 0;
    }

    /*
     * Enable xbow error interrupts
     */
    xbow->xb_wid_control = (XB_WID_CTRL_REG_ACC_IE | XB_WID_CTRL_XTALK_IE);

    /*
     * take a census of the widgets present,
     * leaving notes at the switch vertex.
     */
    info = xswitch_info_new(busv);

    for (port = MAX_PORT_NUM - MAX_XBOW_PORTS;
	 port < MAX_PORT_NUM; ++port) {
	if (!xbow_link_alive(xbow, port)) {
#if DEBUG && XBOW_DEBUG
	    printk(KERN_INFO "0x%p link %d is not alive\n",
		    busv, port);
#endif
	    continue;
	}
	if (!xbow_widget_present(xbow, port)) {
#if DEBUG && XBOW_DEBUG
	    printk(KERN_INFO "0x%p link %d is alive but no widget is present\n", busv, port);
#endif
	    continue;
	}
#if DEBUG && XBOW_DEBUG
	printk(KERN_INFO "0x%p link %d has a widget\n",
		busv, port);
#endif

	xswitch_info_link_is_ok(info, port);
	/*
	 * Turn some error interrupts on
	 * and turn others off. The PROM has
	 * some things turned on we don't
	 * want to see (bandwidth allocation
	 * errors for instance); so if it
	 * is not listed here, it is not on.
	 */
	xbow->xb_link(port).link_control =
	    ( (xbow->xb_link(port).link_control
	/*
	 * Turn off these bits; they are non-fatal,
	 * but we might want to save some statistics
	 * on the frequency of these errors.
	 * XXX FIXME XXX
	 */
	    & ~XB_CTRL_RCV_CNT_OFLOW_IE
	    & ~XB_CTRL_XMT_CNT_OFLOW_IE
	    & ~XB_CTRL_BNDWDTH_ALLOC_IE
	    & ~XB_CTRL_RCV_IE)
	/*
	 * These are the ones we want to turn on.
	 */
	    | (XB_CTRL_ILLEGAL_DST_IE
	    | XB_CTRL_OALLOC_IBUF_IE
	    | XB_CTRL_XMT_MAX_RTRY_IE
	    | XB_CTRL_MAXREQ_TOUT_IE
	    | XB_CTRL_XMT_RTRY_IE
	    | XB_CTRL_SRC_TOUT_IE) );
    }

    xswitch_provider_register(busv, &xbow_provider);

    return 0;				/* attach successful */
}

/*ARGSUSED */
int
xbow_open(devfs_handle_t *devp, int oflag, int otyp, cred_t *credp)
{
    return 0;

}

/*ARGSUSED */
int
xbow_close(devfs_handle_t dev, int oflag, int otyp, cred_t *crp)
{
    return 0;
}

/*ARGSUSED */
int
xbow_map(devfs_handle_t dev, vhandl_t *vt, off_t off, size_t len, uint prot)
{
    devfs_handle_t            vhdl = dev_to_vhdl(dev);
    xbow_soft_t             soft = xbow_soft_get(vhdl);
    int                     error;

    ASSERT(soft);
    len = ctob(btoc(len));
    /* XXX- this ignores the offset!!! */
    error = v_mapphys(vt, (void *) soft->base, len);
    return error;
}

/*ARGSUSED */
int
xbow_unmap(devfs_handle_t dev, vhandl_t *vt)
{
    return 0;
}

/* This contains special-case code for grio. There are plans to make
 * this general sometime in the future, but till then this should
 * be good enough.
 */
xwidgetnum_t
xbow_widget_num_get(devfs_handle_t dev)
{
	devfs_handle_t	tdev;
	char		devname[MAXDEVNAME];
	xwidget_info_t	xwidget_info;
	int		i;

	vertex_to_name(dev, devname, MAXDEVNAME);

	/* If this is a pci controller vertex, traverse up using
	 * the ".." links to get to the widget.
	 */
	if (strstr(devname, EDGE_LBL_PCI) &&
			strstr(devname, EDGE_LBL_CONTROLLER)) {
		tdev = dev;
		for (i=0; i< 2; i++) {
			if (hwgraph_edge_get(tdev,
				HWGRAPH_EDGELBL_DOTDOT, &tdev) !=
					GRAPH_SUCCESS)
				return XWIDGET_NONE;
		}

		if ((xwidget_info = xwidget_info_chk(tdev)) != NULL) {
			return (xwidget_info_id_get(xwidget_info));
		} else {
			return XWIDGET_NONE;
		}
	}

	return XWIDGET_NONE;
}

int
xbow_ioctl(devfs_handle_t dev,
	   int cmd,
	   void *arg,
	   int flag,
	   struct cred *cr,
	   int *rvalp)
{
    devfs_handle_t            vhdl;
    int                     error = 0;

#if defined (DEBUG)
    int                     rc;
    devfs_handle_t            conn;
    struct xwidget_info_s  *xwidget_info;
    xbow_soft_t             xbow_soft;
#endif
    *rvalp = 0;

    vhdl = dev_to_vhdl(dev);
#if defined (DEBUG)
    xbow_soft = xbow_soft_get(vhdl);
    conn = xbow_soft->conn;

    xwidget_info = xwidget_info_get(conn);
    ASSERT_ALWAYS(xwidget_info != NULL);

    rc = xwidget_hwid_is_xswitch(&xwidget_info->w_hwid);
    ASSERT_ALWAYS(rc != 0);
#endif
    switch (cmd) {
#ifdef LATER
    case XBOWIOC_PERF_ENABLE:
    case XBOWIOC_PERF_DISABLE:
	{
	    struct xbow_perfarg_t   xbow_perf_en;

	    if (!_CAP_CRABLE(cr, CAP_DEVICE_MGT)) {
		error = EPERM;
		break;
	    }
	    if ((flag & FWRITE) == 0) {
		error = EBADF;
		break;
	    }
	    if (COPYIN(arg, &xbow_perf_en, sizeof(xbow_perf_en))) {
		error = EFAULT;
		break;
	    }
	    if (error = xbow_enable_perf_counter(vhdl,
						 xbow_perf_en.link,
						 (cmd == XBOWIOC_PERF_DISABLE) ? 0 : xbow_perf_en.mode,
						 xbow_perf_en.counter)) {
		error = EINVAL;
		break;
	    }
	    break;
	}
#endif

#ifdef LATER
    case XBOWIOC_PERF_GET:
	{
	    xbow_perf_link_t       *xbow_perf_cnt;

	    if ((flag & FREAD) == 0) {
		error = EBADF;
		break;
	    }
	    xbow_perf_cnt = xbow_get_perf_counters(vhdl);
	    ASSERT_ALWAYS(xbow_perf_cnt != NULL);

	    if (COPYOUT((void *) xbow_perf_cnt, (void *) arg,
			MAX_XBOW_PORTS * sizeof(xbow_perf_link_t))) {
		error = EFAULT;
		break;
	    }
	    break;
	}
#endif

    case XBOWIOC_LLP_ERROR_ENABLE:
	if ((error = xbow_enable_llp_monitor(vhdl)) != 0)
	    error = EINVAL;

	break;

    case XBOWIOC_LLP_ERROR_DISABLE:

	if ((error = xbow_disable_llp_monitor(vhdl)) != 0)
	    error = EINVAL;

	break;

#ifdef LATER
    case XBOWIOC_LLP_ERROR_GET:
	{
	    xbow_link_status_t     *xbow_llp_status;

	    if ((flag & FREAD) == 0) {
		error = EBADF;
		break;
	    }
	    xbow_llp_status = xbow_get_llp_status(vhdl);
	    ASSERT_ALWAYS(xbow_llp_status != NULL);

	    if (COPYOUT((void *) xbow_llp_status, (void *) arg,
			MAX_XBOW_PORTS * sizeof(xbow_link_status_t))) {
		error = EFAULT;
		break;
	    }
	    break;
	}
#endif

#ifdef LATER
    case GIOCSETBW:
	{
	    grio_ioctl_info_t info;
	    xwidgetnum_t src_widgetnum, dest_widgetnum;

	    if (COPYIN(arg, &info, sizeof(grio_ioctl_info_t))) {
		error = EFAULT;
		break;
	    }
#ifdef GRIO_DEBUG
	    printf("xbow:: prev_vhdl: %d next_vhdl: %d reqbw: %lld\n",
			info.prev_vhdl, info.next_vhdl, info.reqbw);
#endif /* GRIO_DEBUG */

	    src_widgetnum = xbow_widget_num_get(info.prev_vhdl);
	    dest_widgetnum = xbow_widget_num_get(info.next_vhdl);

	    /* Bandwidth allocation is bi-directional. Since bandwidth
	     * reservations have already been done at an earlier stage,
	     * we cannot fail here for lack of bandwidth.
	     */
	    xbow_prio_bw_alloc(dev, src_widgetnum, dest_widgetnum,
			0, info.reqbw);
	    xbow_prio_bw_alloc(dev, dest_widgetnum, src_widgetnum,
			0, info.reqbw);

	    break;
	}

    case GIOCRELEASEBW:
	{
	    grio_ioctl_info_t info;
	    xwidgetnum_t src_widgetnum, dest_widgetnum;

	    if (!cap_able(CAP_DEVICE_MGT)) {
		error = EPERM;
		break;
	    }

	    if (COPYIN(arg, &info, sizeof(grio_ioctl_info_t))) {
		error = EFAULT;
		break;
	    }
#ifdef GRIO_DEBUG
	    printf("xbow:: prev_vhdl: %d next_vhdl: %d reqbw: %lld\n",
			info.prev_vhdl, info.next_vhdl, info.reqbw);
#endif /* GRIO_DEBUG */

	    src_widgetnum = xbow_widget_num_get(info.prev_vhdl);
	    dest_widgetnum = xbow_widget_num_get(info.next_vhdl);

	    /* Bandwidth reservation is bi-directional. Hence, remove
	     * bandwidth reservations for both directions.
	     */
	    xbow_prio_bw_alloc(dev, src_widgetnum, dest_widgetnum,
			info.reqbw, (-1 * info.reqbw));
	    xbow_prio_bw_alloc(dev, dest_widgetnum, src_widgetnum,
			info.reqbw, (-1 * info.reqbw));

	    break;
	}
#endif

    default:
	break;

    }
    return error;
}

/*
 * xbow_widget_present: See if a device is present
 * on the specified port of this crossbow.
 */
int
xbow_widget_present(xbow_t * xbow, int port)
{
	if ( IS_RUNNING_ON_SIMULATOR() ) {
		if ( (port == 14) || (port == 15) ) {
			return 1;
		}
		else {
			return 0;
		}
	}
	else {
		return xbow->xb_link(port).link_aux_status & XB_AUX_STAT_PRESENT;
	}
}

static int
xbow_link_alive(xbow_t * xbow, int port)
{
    xbwX_stat_t             xbow_linkstat;

    xbow_linkstat.linkstatus = xbow->xb_link(port).link_status;
    return (xbow_linkstat.link_alive);
}

/*
 * xbow_widget_lookup
 *      Lookup the edges connected to the xbow specified, and
 *      retrieve the handle corresponding to the widgetnum
 *      specified.
 *      If not found, return 0.
 */
devfs_handle_t
xbow_widget_lookup(devfs_handle_t vhdl,
		   int widgetnum)
{
    xswitch_info_t          xswitch_info;
    devfs_handle_t            conn;

    xswitch_info = xswitch_info_get(vhdl);
    conn = xswitch_info_vhdl_get(xswitch_info, widgetnum);
    return conn;
}

/*
 * xbow_setwidint: called when xtalk
 * is establishing or migrating our
 * interrupt service.
 */
#ifdef LATER
static void
xbow_setwidint(xtalk_intr_t intr)
{
    xwidgetnum_t            targ = xtalk_intr_target_get(intr);
    iopaddr_t               addr = xtalk_intr_addr_get(intr);
    xtalk_intr_vector_t     vect = xtalk_intr_vector_get(intr);
    xbow_t                 *xbow = (xbow_t *) xtalk_intr_sfarg_get(intr);

    xbow_intr_preset((void *) xbow, 0, targ, addr, vect);
}
#endif	/* LATER */

/*
 * xbow_intr_preset: called during mlreset time
 * if the platform specific code needs to route
 * an xbow interrupt before the xtalk infrastructure
 * is available for use.
 *
 * Also called from xbow_setwidint, so we don't
 * replicate the guts of the routine.
 *
 * XXX- probably should be renamed xbow_wid_intr_set or
 * something to reduce confusion.
 */
/*ARGSUSED3 */
void
xbow_intr_preset(void *which_widget,
		 int which_widget_intr,
		 xwidgetnum_t targ,
		 iopaddr_t addr,
		 xtalk_intr_vector_t vect)
{
    xbow_t                 *xbow = (xbow_t *) which_widget;

    xbow->xb_wid_int_upper = ((0xFF000000 & (vect << 24)) |
			      (0x000F0000 & (targ << 16)) |
			      XTALK_ADDR_TO_UPPER(addr));
    xbow->xb_wid_int_lower = XTALK_ADDR_TO_LOWER(addr);
}

#define	XEM_ADD_STR(s)		printk("%s", (s))
#define	XEM_ADD_NVAR(n,v)	printk("\t%20s: 0x%x\n", (n), (v))
#define	XEM_ADD_VAR(v)		XEM_ADD_NVAR(#v,(v))
#define XEM_ADD_IOEF(n) 	if (IOERROR_FIELDVALID(ioe,n))		    \
				    XEM_ADD_NVAR("ioe." #n,		    \
						 IOERROR_GETVALUE(ioe,n))

#ifdef LATER
static void
xem_add_ioe(ioerror_t *ioe)
{
    XEM_ADD_IOEF(errortype);
    XEM_ADD_IOEF(widgetnum);
    XEM_ADD_IOEF(widgetdev);
    XEM_ADD_IOEF(srccpu);
    XEM_ADD_IOEF(srcnode);
    XEM_ADD_IOEF(errnode);
    XEM_ADD_IOEF(sysioaddr);
    XEM_ADD_IOEF(xtalkaddr);
    XEM_ADD_IOEF(busspace);
    XEM_ADD_IOEF(busaddr);
    XEM_ADD_IOEF(vaddr);
    XEM_ADD_IOEF(memaddr);
    XEM_ADD_IOEF(epc);
    XEM_ADD_IOEF(ef);
}

#define XEM_ADD_IOE()	(xem_add_ioe(ioe))
#endif	/* LATER */

int                     xbow_xmit_retry_errors = 0;

int
xbow_xmit_retry_error(xbow_soft_t soft,
		      int port)
{
    xswitch_info_t          info;
    devfs_handle_t            vhdl;
    widget_cfg_t           *wid;
    widgetreg_t             id;
    int                     part;
    int                     mfgr;

    wid = soft->wpio[port - BASE_XBOW_PORT];
    if (wid == NULL) {
	/* If we can't track down a PIO
	 * pointer to our widget yet,
	 * leave our caller knowing that
	 * we are interested in this
	 * interrupt if it occurs in
	 * the future.
	 */
	info = xswitch_info_get(soft->busv);
	if (!info)
	    return 1;
	vhdl = xswitch_info_vhdl_get(info, port);
	if (vhdl == GRAPH_VERTEX_NONE)
	    return 1;
	wid = (widget_cfg_t *) xtalk_piotrans_addr
	    (vhdl, 0, 0, sizeof *wid, 0);
	if (!wid)
	    return 1;
	soft->wpio[port - BASE_XBOW_PORT] = wid;
    }
    id = wid->w_id;
    part = XWIDGET_PART_NUM(id);
    mfgr = XWIDGET_MFG_NUM(id);

    /* If this thing is not a Bridge,
     * do not activate the WAR, and
     * tell our caller we do not need
     * to be called again.
     */
    if ((part != BRIDGE_WIDGET_PART_NUM) ||
	(mfgr != BRIDGE_WIDGET_MFGR_NUM)) {
		/* FIXME: add Xbridge to the WAR.
		 * Shouldn't hurt anything.  Later need to
		 * check if we can remove this.
                 */
    		if ((part != XBRIDGE_WIDGET_PART_NUM) ||
		    (mfgr != XBRIDGE_WIDGET_MFGR_NUM))
			return 0;
    }

    /* count how many times we
     * have picked up after
     * LLP Transmit problems.
     */
    xbow_xmit_retry_errors++;

    /* rewrite the control register
     * to fix things up.
     */
    wid->w_control = wid->w_control;
    wid->w_control;

    return 1;
}

void
xbow_update_perf_counters(devfs_handle_t vhdl)
{
    xbow_soft_t             xbow_soft = xbow_soft_get(vhdl);
    xbow_perf_t            *xbow_perf = xbow_soft->xbow_perfcnt;
    xbow_perf_link_t       *xbow_plink = xbow_soft->xbow_perflink;
    xbow_perfcount_t        perf_reg;
    unsigned long           s;
    int                     link, i;

    for (i = 0; i < XBOW_PERF_COUNTERS; i++, xbow_perf++) {
	if (xbow_perf->xp_mode == XBOW_MONITOR_NONE)
	    continue;

	s = mutex_spinlock(&xbow_soft->xbow_perf_lock);

	perf_reg.xb_counter_val = *(xbowreg_t *) xbow_perf->xp_perf_reg;

	link = perf_reg.xb_perf.link_select;

	(xbow_plink + link)->xlp_cumulative[xbow_perf->xp_curmode] +=
	    ((perf_reg.xb_perf.count - xbow_perf->xp_current) & XBOW_COUNTER_MASK);
	xbow_perf->xp_current = perf_reg.xb_perf.count;

	mutex_spinunlock(&xbow_soft->xbow_perf_lock, s);
    }
    /* Do port /mode multiplexing here */

#ifdef LATER
    (void) timeout(xbow_update_perf_counters,
		   (void *) (__psunsigned_t) vhdl, XBOW_PERF_TIMEOUT);
#endif

}

xbow_perf_link_t       *
xbow_get_perf_counters(devfs_handle_t vhdl)
{
    xbow_soft_t             xbow_soft = xbow_soft_get(vhdl);
    xbow_perf_link_t       *xbow_perf_link = xbow_soft->xbow_perflink;

    return xbow_perf_link;
}

int
xbow_enable_perf_counter(devfs_handle_t vhdl, int link, int mode, int counter)
{
    xbow_soft_t             xbow_soft = xbow_soft_get(vhdl);
    xbow_perf_t            *xbow_perf = xbow_soft->xbow_perfcnt;
    xbow_linkctrl_t         xbow_link_ctrl;
    xbow_t                 *xbow = xbow_soft->base;
    xbow_perfcount_t        perf_reg;
    unsigned long           s;
    int                     i;

    link -= BASE_XBOW_PORT;
    if ((link < 0) || (link >= MAX_XBOW_PORTS))
	return -1;

    if ((mode < XBOW_MONITOR_NONE) || (mode > XBOW_MONITOR_DEST_LINK))
	return -1;

    if ((counter < 0) || (counter >= XBOW_PERF_COUNTERS))
	return -1;

    s = mutex_spinlock(&xbow_soft->xbow_perf_lock);

    if ((xbow_perf + counter)->xp_mode && mode) {
	mutex_spinunlock(&xbow_soft->xbow_perf_lock, s);
	return -1;
    }
    for (i = 0; i < XBOW_PERF_COUNTERS; i++) {
	if (i == counter)
	    continue;
	if (((xbow_perf + i)->xp_link == link) &&
	    ((xbow_perf + i)->xp_mode)) {
	    mutex_spinunlock(&xbow_soft->xbow_perf_lock, s);
	    return -1;
	}
    }
    xbow_perf += counter;

    xbow_perf->xp_curlink = xbow_perf->xp_link = link;
    xbow_perf->xp_curmode = xbow_perf->xp_mode = mode;

    xbow_link_ctrl.xbl_ctrlword = xbow->xb_link_raw[link].link_control;
    xbow_link_ctrl.xb_linkcontrol.perf_mode = mode;
    xbow->xb_link_raw[link].link_control = xbow_link_ctrl.xbl_ctrlword;

    perf_reg.xb_counter_val = *(xbowreg_t *) xbow_perf->xp_perf_reg;
    perf_reg.xb_perf.link_select = link;
    *(xbowreg_t *) xbow_perf->xp_perf_reg = perf_reg.xb_counter_val;
    xbow_perf->xp_current = perf_reg.xb_perf.count;

#ifdef LATER
    (void) timeout(xbow_update_perf_counters,
		   (void *) (__psunsigned_t) vhdl, XBOW_PERF_TIMEOUT);
#endif

    mutex_spinunlock(&xbow_soft->xbow_perf_lock, s);

    return 0;
}

xbow_link_status_t     *
xbow_get_llp_status(devfs_handle_t vhdl)
{
    xbow_soft_t             xbow_soft = xbow_soft_get(vhdl);
    xbow_link_status_t     *xbow_llp_status = xbow_soft->xbow_link_status;

    return xbow_llp_status;
}

void
xbow_update_llp_status(devfs_handle_t vhdl)
{
    xbow_soft_t             xbow_soft = xbow_soft_get(vhdl);
    xbow_link_status_t     *xbow_llp_status = xbow_soft->xbow_link_status;
    xbow_t                 *xbow;
    xbwX_stat_t             lnk_sts;
    xbow_aux_link_status_t  aux_sts;
    int                     link;
    devfs_handle_t	    xwidget_vhdl;
    char		   *xwidget_name;	

    xbow = (xbow_t *) xbow_soft->base;
    for (link = 0; link < MAX_XBOW_PORTS; link++, xbow_llp_status++) {
	/* Get the widget name corresponding the current link.
	 * Note : 0 <= link < MAX_XBOW_PORTS(8).
	 * 	  BASE_XBOW_PORT(0x8) <= xwidget number < MAX_PORT_NUM (0x10)
	 */
	xwidget_vhdl = xbow_widget_lookup(xbow_soft->busv,link+BASE_XBOW_PORT);
	xwidget_name = xwidget_name_get(xwidget_vhdl);
	aux_sts.aux_linkstatus
	    = xbow->xb_link_raw[link].link_aux_status;
	lnk_sts.linkstatus = xbow->xb_link_raw[link].link_status_clr;

	if (lnk_sts.link_alive == 0)
	    continue;

	xbow_llp_status->rx_err_count +=
	    aux_sts.xb_aux_linkstatus.rx_err_cnt;

	xbow_llp_status->tx_retry_count +=
	    aux_sts.xb_aux_linkstatus.tx_retry_cnt;

	if (lnk_sts.linkstatus & ~(XB_STAT_RCV_ERR | XB_STAT_XMT_RTRY_ERR | XB_STAT_LINKALIVE)) {
#ifdef	LATER
	    printk(KERN_WARNING  "link %d[%s]: bad status 0x%x\n",
		    link, xwidget_name, lnk_sts.linkstatus);
#endif
	}
    }
#ifdef LATER
    if (xbow_soft->link_monitor)
	(void) timeout(xbow_update_llp_status,
		       (void *) (__psunsigned_t) vhdl, XBOW_STATS_TIMEOUT);
#endif
}

int
xbow_disable_llp_monitor(devfs_handle_t vhdl)
{
    xbow_soft_t             xbow_soft = xbow_soft_get(vhdl);
    int                     port;

    for (port = 0; port < MAX_XBOW_PORTS; port++) {
	xbow_soft->xbow_link_status[port].rx_err_count = 0;
	xbow_soft->xbow_link_status[port].tx_retry_count = 0;
    }

    xbow_soft->link_monitor = 0;
    return 0;
}

int
xbow_enable_llp_monitor(devfs_handle_t vhdl)
{
    xbow_soft_t             xbow_soft = xbow_soft_get(vhdl);

#ifdef LATER
    (void) timeout(xbow_update_llp_status,
		   (void *) (__psunsigned_t) vhdl, XBOW_STATS_TIMEOUT);
#endif
    xbow_soft->link_monitor = 1;
    return 0;
}


int
xbow_reset_link(devfs_handle_t xconn_vhdl)
{
    xwidget_info_t          widget_info;
    xwidgetnum_t            port;
    xbow_t                 *xbow;
    xbowreg_t               ctrl;
    xbwX_stat_t             stat;
    unsigned                itick;
    unsigned                dtick;
    static int              ticks_per_ms = 0;

    if (!ticks_per_ms) {
	itick = get_timestamp();
	us_delay(1000);
	ticks_per_ms = get_timestamp() - itick;
    }
    widget_info = xwidget_info_get(xconn_vhdl);
    port = xwidget_info_id_get(widget_info);

#ifdef XBOW_K1PTR			/* defined if we only have one xbow ... */
    xbow = XBOW_K1PTR;
#else
    {
	devfs_handle_t            xbow_vhdl;
	xbow_soft_t             xbow_soft;

	hwgraph_traverse(xconn_vhdl, ".master/xtalk/0/xbow", &xbow_vhdl);
	xbow_soft = xbow_soft_get(xbow_vhdl);
	xbow = xbow_soft->base;
    }
#endif

    /*
     * This requires three PIOs (reset the link, check for the
     * reset, restore the control register for the link) plus
     * 10us to wait for the reset. We allow up to 1ms for the
     * widget to come out of reset before giving up and
     * returning a failure.
     */
    ctrl = xbow->xb_link(port).link_control;
    xbow->xb_link(port).link_reset = 0;
    itick = get_timestamp();
    while (1) {
	stat.linkstatus = xbow->xb_link(port).link_status;
	if (stat.link_alive)
	    break;
	dtick = get_timestamp() - itick;
	if (dtick > ticks_per_ms) {
	    return -1;			/* never came out of reset */
	}
	DELAY(2);			/* don't beat on link_status */
    }
    xbow->xb_link(port).link_control = ctrl;
    return 0;
}

/*
 * Dump xbow registers.
 * input parameter is either a pointer to
 * the xbow chip or the vertex handle for
 * an xbow vertex.
 */
void
idbg_xbowregs(int64_t regs)
{
    xbow_t                 *xbow;
    int                     i;
    xb_linkregs_t          *link;

#ifdef LATER
    if (dev_is_vertex((devfs_handle_t) regs)) {
	devfs_handle_t            vhdl = (devfs_handle_t) regs;
	xbow_soft_t             soft = xbow_soft_get(vhdl);

	xbow = soft->base;
    } else
#endif
    {
	xbow = (xbow_t *) regs;
    }

#ifdef LATER
    qprintf("Printing xbow registers starting at 0x%x\n", xbow);
    qprintf("wid %x status %x erruppr %x errlower %x control %x timeout %x\n",
	    xbow->xb_wid_id, xbow->xb_wid_stat, xbow->xb_wid_err_upper,
	    xbow->xb_wid_err_lower, xbow->xb_wid_control,
	    xbow->xb_wid_req_timeout);
    qprintf("intr uppr %x lower %x errcmd %x llp ctrl %x arb_reload %x\n",
	    xbow->xb_wid_int_upper, xbow->xb_wid_int_lower,
	    xbow->xb_wid_err_cmdword, xbow->xb_wid_llp,
	    xbow->xb_wid_arb_reload);
#endif

    for (i = 8; i <= 0xf; i++) {
	link = &xbow->xb_link(i);
#ifdef LATER
	qprintf("Link %d registers\n", i);
	qprintf("\tctrl %x stat %x arbuppr %x arblowr %x auxstat %x\n",
		link->link_control, link->link_status,
		link->link_arb_upper, link->link_arb_lower,
		link->link_aux_status);
#endif
    }
}


#define XBOW_ARB_RELOAD_TICKS		25
					/* granularity: 4 MB/s, max: 124 MB/s */
#define GRANULARITY			((100 * 1000000) / XBOW_ARB_RELOAD_TICKS)

#define XBOW_BYTES_TO_GBR(BYTES_per_s)	(int) (BYTES_per_s / GRANULARITY)

#define XBOW_GBR_TO_BYTES(cnt)		(bandwidth_t) ((cnt) * GRANULARITY)

#define CEILING_BYTES_TO_GBR(gbr, bytes_per_sec)	\
			((XBOW_GBR_TO_BYTES(gbr) < bytes_per_sec) ? gbr+1 : gbr)

#define XBOW_ARB_GBR_MAX		31

#define ABS(x)				((x > 0) ? (x) : (-1 * x))
					/* absolute value */

int
xbow_bytes_to_gbr(bandwidth_t old_bytes_per_sec, bandwidth_t bytes_per_sec)
{
    int                     gbr_granted;
    int                     new_total_gbr;
    int                     change_gbr;
    bandwidth_t             new_total_bw;

#ifdef GRIO_DEBUG
    printf("xbow_bytes_to_gbr: old_bytes_per_sec %lld bytes_per_sec %lld\n",
		old_bytes_per_sec, bytes_per_sec);
#endif	/* GRIO_DEBUG */

    gbr_granted = CEILING_BYTES_TO_GBR((XBOW_BYTES_TO_GBR(old_bytes_per_sec)),
			old_bytes_per_sec);
    new_total_bw = old_bytes_per_sec + bytes_per_sec;
    new_total_gbr = CEILING_BYTES_TO_GBR((XBOW_BYTES_TO_GBR(new_total_bw)),
			new_total_bw);

    change_gbr = new_total_gbr - gbr_granted;

#ifdef GRIO_DEBUG
    printf("xbow_bytes_to_gbr: gbr_granted %d new_total_gbr %d change_gbr %d\n",
		gbr_granted, new_total_gbr, change_gbr);
#endif	/* GRIO_DEBUG */

    return (change_gbr);
}

/* Conversion from GBR to bytes */
bandwidth_t
xbow_gbr_to_bytes(int gbr)
{
    return (XBOW_GBR_TO_BYTES(gbr));
}

/* Given the vhdl for the desired xbow, the src and dest. widget ids
 * and the req_bw value, this xbow driver entry point accesses the
 * xbow registers and allocates the desired bandwidth if available.
 *
 * If bandwidth allocation is successful, return success else return failure.
 */
int
xbow_prio_bw_alloc(devfs_handle_t vhdl,
		xwidgetnum_t src_wid,
		xwidgetnum_t dest_wid,
		unsigned long long old_alloc_bw,
		unsigned long long req_bw)
{
    xbow_soft_t             soft = xbow_soft_get(vhdl);
    volatile xbowreg_t     *xreg;
    xbowreg_t               mask;
    unsigned long           s;
    int                     error = 0;
    bandwidth_t             old_bw_BYTES, req_bw_BYTES;
    xbowreg_t               old_xreg;
    int                     old_bw_GBR, req_bw_GBR, new_bw_GBR;

#ifdef GRIO_DEBUG
    printf("xbow_prio_bw_alloc: vhdl %d src_wid %d dest_wid %d req_bw %lld\n",
		(int) vhdl, (int) src_wid, (int) dest_wid, req_bw);
#endif

    ASSERT(XBOW_WIDGET_IS_VALID(src_wid));
    ASSERT(XBOW_WIDGET_IS_VALID(dest_wid));

    s = mutex_spinlock(&soft->xbow_bw_alloc_lock);

    /* Get pointer to the correct register */
    xreg = XBOW_PRIO_ARBREG_PTR(soft->base, dest_wid, src_wid);

    /* Get mask for GBR count value */
    mask = XB_ARB_GBR_MSK << XB_ARB_GBR_SHFT(src_wid);

    req_bw_GBR = xbow_bytes_to_gbr(old_alloc_bw, req_bw);
    req_bw_BYTES = (req_bw_GBR < 0) ? (-1 * xbow_gbr_to_bytes(ABS(req_bw_GBR)))
		: xbow_gbr_to_bytes(req_bw_GBR);

#ifdef GRIO_DEBUG
    printf("req_bw %lld req_bw_BYTES %lld req_bw_GBR %d\n",
		req_bw, req_bw_BYTES, req_bw_GBR);
#endif	/* GRIO_DEBUG */

    old_bw_BYTES = soft->bw_cur_used[(int) dest_wid - MAX_XBOW_PORTS];
    old_xreg = *xreg;
    old_bw_GBR = (((*xreg) & mask) >> XB_ARB_GBR_SHFT(src_wid));

#ifdef GRIO_DEBUG
    ASSERT(XBOW_BYTES_TO_GBR(old_bw_BYTES) == old_bw_GBR);

    printf("old_bw_BYTES %lld old_bw_GBR %d\n", old_bw_BYTES, old_bw_GBR);

    printf("req_bw_BYTES %lld old_bw_BYTES %lld soft->bw_hiwm %lld\n",
		req_bw_BYTES, old_bw_BYTES,
		soft->bw_hiwm[(int) dest_wid - MAX_XBOW_PORTS]);
	   
#endif				/* GRIO_DEBUG */

    /* Accept the request only if we don't exceed the destination
     * port HIWATER_MARK *AND* the max. link GBR arbitration count
     */
    if (((old_bw_BYTES + req_bw_BYTES) <=
		soft->bw_hiwm[(int) dest_wid - MAX_XBOW_PORTS]) &&
		(req_bw_GBR + old_bw_GBR <= XBOW_ARB_GBR_MAX)) {

	new_bw_GBR = (old_bw_GBR + req_bw_GBR);

	/* Set this in the xbow link register */
	*xreg = (old_xreg & ~mask) | \
	    (new_bw_GBR << XB_ARB_GBR_SHFT(src_wid) & mask);

	soft->bw_cur_used[(int) dest_wid - MAX_XBOW_PORTS] =
			xbow_gbr_to_bytes(new_bw_GBR);
    } else {
	error = 1;
    }

    mutex_spinunlock(&soft->xbow_bw_alloc_lock, s);

    return (error);
}
