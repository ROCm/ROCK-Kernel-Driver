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
#include <linux/module.h>
#include <asm/sn/sgi.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/iograph.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/xtalk/xwidget.h>
#include <asm/sn/pci/bridge.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/pci/pcibr.h>
#include <asm/sn/pci/pcibr_private.h>
#include <asm/sn/pci/pci_defs.h>
#include <asm/sn/prio.h>
#include <asm/sn/ioerror_handling.h>
#include <asm/sn/xtalk/xbow.h>
#include <asm/sn/ioc3.h>
#include <asm/sn/eeprom.h>
#include <asm/sn/sn1/bedrock.h>
#include <asm/sn/sn_private.h>
#if defined(CONFIG_SGI_IP35) || defined(CONFIG_IA64_SGI_SN1) || defined(CONFIG_IA64_GENERIC)
#include <asm/sn/sn1/hubio.h>
#include <asm/sn/sn1/hubio_next.h>
#endif

#define copyin(_a, _b, _c)	copy_from_user(_b, _a, _c)

#ifndef	DEBUG_PCIBA
#define	DEBUG_PCIBA		0
#endif

/* v_mapphys does not percolate page offset back. */
#define	PCIBA_ALIGN_CHECK	1

#include <asm/sn/pci/pciba.h>

/* grab an unused space code for "User DMA" space */
#ifndef	PCIBA_SPACE_UDMA
#define	PCIBA_SPACE_UDMA	(14)
#endif

#if DEBUG_REFCT
extern int		hwgraph_vertex_refct(vertex_hdl_t);
#endif
extern int		pci_user_dma_max_pages;

#define NEW(ptr)	(ptr = kmem_zalloc(sizeof (*(ptr)), KM_SLEEP))
#define DEL(ptr)	(kfree(ptr))

/* Oops -- no standard "pci address" type! */
typedef uint64_t        pciaddr_t;

/* ================================================================
 *            driver types
 */
typedef struct pciba_slot_s *pciba_slot_t;
typedef struct pciba_comm_s *pciba_comm_t;
typedef struct pciba_soft_s *pciba_soft_t;
typedef struct pciba_map_s *pciba_map_t, **pciba_map_h;
typedef struct pciba_dma_s *pciba_dma_t, **pciba_dma_h;
typedef struct pciba_bus_s *pciba_bus_t;

#define	TRACKED_SPACES	16
struct pciba_comm_s {
    devfs_handle_t	    conn;
    pciba_bus_t             bus;
    int			    refct;
    pciba_soft_t	    soft[TRACKED_SPACES][2];
    struct semaphore        lock;
    pciba_dma_t             dmap;
};

/* pciba_soft: device_info() for all openables */
struct pciba_soft_s {
    pciba_comm_t	    comm;
    devfs_handle_t          vhdl;
    int			    refct;
    pciio_space_t           space;
    size_t                  size;
    pciio_space_t           iomem;
    pciaddr_t               base;
    unsigned		    flags;
};

#define	pciba_soft_get(v)	(pciba_soft_t)hwgraph_fastinfo_get(v)
#define	pciba_soft_set(v,i)	hwgraph_fastinfo_set(v,(arbitrary_info_t)(i))

#define	pciba_soft_lock(soft)	down(&soft->comm->lock)
#define	pciba_soft_unlock(soft)	up(&soft->comm->lock)

/* pciba_map: data describing a mapping.
 * (ie. a user mmap request)
 */
struct pciba_map_s {
    pciba_map_t             next;
#ifdef LATER
    uthread_t              *uthread;
#endif
    __psunsigned_t          handle;
    uvaddr_t                uvaddr;
    size_t                  size;
    pciio_piomap_t          map;
    pciio_space_t           space;
    pciaddr_t               base;
    unsigned		    flags;
};

/* pciba_dma: data describing a DMA mapping.
 */
struct pciba_dma_s {
    pciba_dma_t             next;
    iopaddr_t               paddr;	/* starting phys addr */
    caddr_t                 kaddr;	/* starting kern addr */
    pciio_dmamap_t          map;	/* mapping resources (ugh!) */
    pciaddr_t               daddr;	/* starting pci addr */
    size_t                  pages;	/* size of block in pages */
    size_t                  bytes;	/* size of block in bytes */
    __psunsigned_t          handle;	/* mapping handle */
};

/* pciba_bus: common bus info for all openables
 * descended from the same master vertex.
 */
struct pciba_bus_s {
    struct semaphore        lock;
    pciba_map_t             maps;	/* stack of mappings */
    int			    refct;
};

#define	pciba_bus_lock(bus)	down(&bus->lock)
#define	pciba_bus_unlock(bus)	up(&bus->lock)

typedef union ioctl_arg_buffer_u {
    char                    data[IOCPARM_MASK + 1];
    uint8_t                 uc;
    uint16_t                us;
    uint32_t                ui;
    uint64_t                ud;
    caddr_t                 ca;
#if ULI
    struct uliargs          uli;
    struct uliargs32	    uli32;
#endif
} ioctl_arg_buffer_t;

/* ================================================================
 *            driver variables
 */
char                   *pciba_mversion = "mload version 7.0";
int                     pciba_devflag = 0x1 |
                                        0x200 |
                                        0x400;

/* this counts the reasons why we can not
 * currently unload this driver.
 */
atomic_t                pciba_prevent_unload = ATOMIC_INIT(0);

#if DEBUG_PCIBA
static struct reg_values space_v[] =
{
    {PCIIO_SPACE_NONE, "none"},
    {PCIIO_SPACE_ROM, "ROM"},
    {PCIIO_SPACE_IO, "I/O"},
    {PCIIO_SPACE_MEM, "MEM"},
    {PCIIO_SPACE_MEM32, "MEM(32)"},
    {PCIIO_SPACE_MEM64, "MEM(64)"},
    {PCIIO_SPACE_CFG, "CFG"},
    {PCIIO_SPACE_WIN(0), "WIN(0)"},
    {PCIIO_SPACE_WIN(1), "WIN(1)"},
    {PCIIO_SPACE_WIN(2), "WIN(2)"},
    {PCIIO_SPACE_WIN(3), "WIN(3)"},
    {PCIIO_SPACE_WIN(4), "WIN(4)"},
    {PCIIO_SPACE_WIN(5), "WIN(5)"},
    {PCIBA_SPACE_UDMA, "UDMA"},
    {PCIIO_SPACE_BAD, "BAD"},
    {0}
};

static struct reg_desc  space_desc[] =
{
    {0xFF, 0, "space", 0, space_v},
    {0}
};
#endif

char                    pciba_edge_lbl_base[] = "base";
char                    pciba_edge_lbl_cfg[] = "config";
char                    pciba_edge_lbl_dma[] = "dma";
char                    pciba_edge_lbl_intr[] = "intr";
char                    pciba_edge_lbl_io[] = "io";
char                    pciba_edge_lbl_mem[] = "mem";
char                    pciba_edge_lbl_rom[] = "rom";
char                   *pciba_edge_lbl_win[6] =
{"0", "1", "2", "3", "4", "5"};

#define	PCIBA_EDGE_LBL_BASE	pciba_edge_lbl_base
#define	PCIBA_EDGE_LBL_CFG	pciba_edge_lbl_cfg
#define	PCIBA_EDGE_LBL_DMA	pciba_edge_lbl_dma
#define	PCIBA_EDGE_LBL_INTR	pciba_edge_lbl_intr
#define	PCIBA_EDGE_LBL_IO	pciba_edge_lbl_io
#define	PCIBA_EDGE_LBL_MEM	pciba_edge_lbl_mem
#define	PCIBA_EDGE_LBL_ROM	pciba_edge_lbl_rom
#define	PCIBA_EDGE_LBL_WIN(n)	pciba_edge_lbl_win[n]

#define	PCIBA_EDGE_LBL_FLIP	pciba_edge_lbl_flip

static char             pciba_info_lbl_bus[] = "pciba_bus";

#define	PCIBA_INFO_LBL_BUS	pciba_info_lbl_bus

struct file_operations pciba_fops = {
	owner:	THIS_MODULE,
	llseek: NULL,
	read: NULL,
	write: NULL,
	readdir: NULL,
	poll: NULL,
	ioctl: NULL,
	mmap: NULL,
	open: NULL,
	flush: NULL,
	release: NULL,
	fsync: NULL,
	fasync: NULL,
	lock: NULL,
	readv: NULL,
	writev: NULL
};
	
/* ================================================================
 *            function table of contents
 */

void                    pciba_init(void);
int                     pciba_attach(devfs_handle_t);

static void		pciba_sub_attach(pciba_comm_t,
					 pciio_space_t, pciio_space_t, pciaddr_t,
					 devfs_handle_t, devfs_handle_t, char *);

static pciba_bus_t      pciba_find_bus(devfs_handle_t, int);
#ifdef LATER
static void             pciba_map_push(pciba_bus_t, pciba_map_t);
static pciba_map_t      pciba_map_pop_hdl(pciba_bus_t, __psunsigned_t);
static void             pciba_sub_detach(devfs_handle_t, char *);
static pciio_iter_f     pciba_unload_me;
#endif

int                     pciba_unload(void);
int                     pciba_unreg(void);
int                     pciba_detach(devfs_handle_t);

int                     pciba_open(dev_t *, int, int, struct cred *);
int                     pciba_close(dev_t);
int                     pciba_read(dev_t, cred_t *);
int                     pciba_write(dev_t, cred_t *);
int                     pciba_ioctl(dev_t, int, void *, int, cred_t *, int *);

int                     pciba_map(dev_t, vhandl_t *, off_t, size_t, uint32_t);
int                     pciba_unmap(dev_t, vhandl_t *);

#if ULI
void                    pciba_clearuli(struct uli *);
static intr_func_f      pciba_intr;
#endif /* Undef as it gets implemented */

/* ================================================================
 *            driver load, register, and setup
 */
void
pciba_init(void)
{

	/*
	 * What do we need to do here?
	 */
#if DEBUG_PCIBA
    printk("pciba_init()\n");
#endif
}

#ifdef LATER
#if HWG_PERF_CHECK && IP30 && !DEBUG
void
pciba_timeout(void *arg1, void *arg2)
{
    struct semaphore       *semap = (sema_t *) arg1;
    unsigned long          *cvalp = (unsigned long *) arg2;

    if (cvalp)
	cvalp[0] = RAW_COUNT();
    if (semap)
	up(semap);
}

volatile unsigned long  cNval[1];
struct semaphore        tsema;

void
pciba_timeout_test(void)
{
    unsigned long           c0val, cval;
    toid_t                  tid;

    extern void             hwg_hprint(unsigned long, char *);

    sema_init(&tsema, 0);

    cNval[0] = 0;
    c0val = RAW_COUNT();
    tid = timeout((void (*)()) pciba_timeout, (void *) 0, 1, (void *) cNval);
    DELAY(1000000);
    cval = cNval[0];
    if (cval == 0) {
	untimeout(tid);
	PRINT_ALERT("pciba: one-tick timeout did not happen in a second\n");
	return;
    }
    cval = cval - c0val;
    hwg_hprint(cval, "timeout(1)");

    cNval[0] = 0;
    c0val = RAW_COUNT();
    tid = timeout((void (*)()) pciba_timeout, (void *) &tsema, 2, (void *) cNval);

    /* FIXME : this probably needs to be down_interruptible() */

    if (down(&tsema) < 0) {	/* wait for the pciba_timeout */
	untimeout(tid);
	PRINT_WARNING("pciba: timeout(2) time check aborted\n");
	return;
    }
    cval = cNval[0];
    if (cval == 0) {
	untimeout(tid);
	PRINT_WARNING("pciba: timeout(2) time not logged\n");
	return;
    }
    cval = cval - c0val;
    hwg_hprint(cval, "timeout(2)");

    cNval[0] = 0;
    c0val = RAW_COUNT();
    tid = timeout((void (*)()) pciba_timeout, (void *) &tsema, HZ, (void *) cNval);

    /* FIXME : this probably needs to be down_interruptible() */

    if (down(&tsema) < 0) {	/* wait for the pciba_timeout */
	untimeout(tid);
	PRINT_WARNING("pciba: timeout(HZ) time check aborted\n");
	return;
    }
    cval = cNval[0];
    if (cval == 0) {
	untimeout(tid);
	PRINT_WARNING("pciba: timeout(HZ) time not logged\n");
	return;
    }
    cval = cval - c0val;
    hwg_hprint(cval, "timeout(HZ)");

    printk("verifying untimeout() cancells ...\n");
    cNval[0] = 0;
    tid = timeout((void (*)()) pciba_timeout, (void *) 0, 2, (void *) cNval);
    untimeout(tid);
    DELAY(1000000);
    cval = cNval[0];
    if (cval != 0) {
	PRINT_ALERT("pciba: unable to cancel two-tick timeout\n");
	cval -= c0val;
	hwg_hprint(cval, "CANCELLED timeout(2)");
    }
}
#endif

int
pciba_reg(void)
{
#if DEBUG_PCIBA
    printk("pciba_reg()\n");
#endif
    pciio_driver_register(-1, -1, "pciba_", 0);

#if HWG_PERF_CHECK && IP30 && !DEBUG
    printk("%s %d\n", __FUNCTION__, __LINE__);
pciba_timeout_test();
#endif

#if DEBUG_REFCT
    {
	char	       *cname = "pciba";
	char	       *dname = "ptv";
	char           *cpath0 = "node/xtalk/15";
	char           *uname0 = "0";
	char           *cpath1 = "node/xtalk/13";
	char           *uname1 = "1";
	devfs_handle_t conn;
	devfs_handle_t conv;
	devfs_handle_t vhdl;
	int		ret;

	printk("pciba refct tests:\n");

#define	SHOWREF(vhdl,func)	printk("ref=%d\t%s\t(%d) %v\n", hwgraph_vertex_refct(vhdl), #func, vhdl, vhdl);

	if (GRAPH_SUCCESS != (ret = hwgraph_path_add(hwgraph_root, cname, &conv)))
	    printk("\tunable to create conv (ret=%d)\n", ret);
	else {						SHOWREF(conv, hwgraph_path_add);
	    if (GRAPH_SUCCESS != (ret = hwgraph_traverse(hwgraph_root, cpath0, &conn)))
		printk("\tunable to find %s (ret=%d)\n", cpath0, ret);
	    else {					SHOWREF(conn, hwgraph_traverse);
		if (GRAPH_SUCCESS != (ret = hwgraph_char_device_add(conn, dname, "pciba_", &vhdl)))
		    printk("unable to create %v/%s (ret=%d)\n", conn, dname, ret);
		else {					SHOWREF(vhdl, hwgraph_char_device_add);
		    hwgraph_chmod(vhdl, 0666);		SHOWREF(vhdl, hwgraph_chmod);
		    if (GRAPH_SUCCESS != (ret = hwgraph_edge_add(conv, vhdl, uname0)))
			printk("unable to create %v/%s (ret=%d)\n", conn, uname0, vhdl, ret);
		    else				SHOWREF(vhdl, hwgraph_edge_add);
		    if (GRAPH_SUCCESS != (ret = hwgraph_vertex_unref(vhdl)))
			printk("unable to unref %v\n", vhdl);
		    else				SHOWREF(vhdl, hwgraph_vertex_unref);
		}
		if (GRAPH_SUCCESS != (ret = hwgraph_vertex_unref(conn)))
		    printk("unable to unref %v\n", conn);
		else					SHOWREF(conn, hwgraph_vertex_unref);
	    }
 
	    if (GRAPH_SUCCESS != (ret = hwgraph_traverse(hwgraph_root, cpath1, &conn)))
		printk("\tunable to find %s (ret=%d)\n", cpath1, ret);
	    else {					SHOWREF(conn, hwgraph_traverse);
		if (GRAPH_SUCCESS != (ret = hwgraph_char_device_add(conn, dname, "pciba_", &vhdl)))
		    printk("unable to create %v/%s (ret=%d)\n", conn, dname, ret);
		else {					SHOWREF(vhdl, hwgraph_char_device_add);
		    hwgraph_chmod(vhdl, 0666);		SHOWREF(vhdl, hwgraph_chmod);
		    if (GRAPH_SUCCESS != (ret = hwgraph_edge_add(conv, vhdl, uname1)))
			printk("unable to create %v/%s (ret=%d)\n", conn, uname1, vhdl, ret);
		    else				SHOWREF(vhdl, hwgraph_edge_add);
		    if (GRAPH_SUCCESS != (ret = hwgraph_vertex_unref(vhdl)))
			printk("unable to unref %v\n", vhdl);
		    else				SHOWREF(vhdl, hwgraph_vertex_unref);
		}
		if (GRAPH_SUCCESS != (ret = hwgraph_vertex_unref(conn)))
		    printk("unable to unref %v\n", conn);
		else					SHOWREF(conn, hwgraph_vertex_unref);
	    }
 
	    if (GRAPH_SUCCESS != (ret = hwgraph_traverse(hwgraph_root, cpath0, &conn)))
		printk("\tunable to find %s (ret=%d)\n", cpath0, ret);
	    else {					SHOWREF(conn, hwgraph_traverse);
		if (GRAPH_SUCCESS != (ret = hwgraph_traverse(conn, dname, &vhdl)))
		    printk("\tunable to find %v/%s (ret=%d)\n", conn, dname, ret);
		else {					SHOWREF(vhdl, hwgraph_traverse);
		    if (GRAPH_SUCCESS != (ret = hwgraph_edge_remove(conv, uname0, NULL)))
			printk("\tunable to remove edge %v/%s (ret=%d)\n", conv, uname0, ret);
		    else				SHOWREF(vhdl, hwgraph_edge_remove);
		    if (GRAPH_SUCCESS != (ret = hwgraph_edge_remove(conn, dname, NULL)))
			printk("\tunable to remove edge %v/%s (ret=%d)\n", conn, dname, ret);
		    else				SHOWREF(vhdl, hwgraph_edge_remove);
		    if (GRAPH_SUCCESS != (ret = hwgraph_vertex_unref(vhdl)))
			printk("unable to unref %v\n", vhdl);
		    else				SHOWREF(vhdl, hwgraph_vertex_unref);
		    if (GRAPH_SUCCESS == (ret = hwgraph_vertex_destroy(vhdl)))
			printk("\tvertex %d destroyed OK\n", vhdl);
		    else				SHOWREF(vhdl, hwgraph_vertex_destroy);
		}
		if (GRAPH_SUCCESS != (ret = hwgraph_vertex_unref(conn)))
		    printk("unable to unref %v\n", conn);
		else					SHOWREF(conn, hwgraph_vertex_unref);
	    }

	    if (GRAPH_SUCCESS != (ret = hwgraph_traverse(hwgraph_root, cpath1, &conn)))
		printk("\tunable to find %s (ret=%d)\n", cpath1, ret);
	    else {					SHOWREF(conn, hwgraph_traverse);
		if (GRAPH_SUCCESS != (ret = hwgraph_traverse(conn, dname, &vhdl)))
		    printk("\tunable to find %v/%s (ret=%d)\n", conn, dname, ret);
		else {					SHOWREF(vhdl, hwgraph_traverse);
		    if (GRAPH_SUCCESS != (ret = hwgraph_edge_remove(conv, uname1, NULL)))
			printk("\tunable to remove edge %v/%s (ret=%d)\n", conv, uname1, ret);
		    else				SHOWREF(vhdl, hwgraph_edge_remove);
		    if (GRAPH_SUCCESS != (ret = hwgraph_edge_remove(conn, dname, NULL)))
			printk("\tunable to remove edge %v/%s (ret=%d)\n", conn, dname, ret);
		    else				SHOWREF(vhdl, hwgraph_edge_remove);
		    if (GRAPH_SUCCESS != (ret = hwgraph_vertex_unref(vhdl)))
			printk("unable to unref %v\n", vhdl);
		    else				SHOWREF(vhdl, hwgraph_vertex_unref);
		    if (GRAPH_SUCCESS == (ret = hwgraph_vertex_destroy(vhdl)))
			printk("\tvertex %d destroyed OK\n", vhdl);
		    else				SHOWREF(vhdl, hwgraph_vertex_destroy);
		}
		if (GRAPH_SUCCESS != (ret = hwgraph_vertex_unref(conn)))
		    printk("unable to unref %v\n", conn);
		else					SHOWREF(conn, hwgraph_vertex_unref);
	    }

	    if (GRAPH_SUCCESS != (ret = hwgraph_edge_remove(hwgraph_root, cname, NULL)))
		printk("\tunable to remove edge %v/%s (ret=%d)\n", hwgraph_root, cname, ret);
	    else				SHOWREF(conv, hwgraph_edge_remove);
	    if (GRAPH_SUCCESS != (ret = hwgraph_vertex_unref(conv)))
		printk("unable to unref %v\n", conv);
	    else					SHOWREF(conv, hwgraph_vertex_unref);
	    if (GRAPH_SUCCESS == (ret = hwgraph_vertex_destroy(conv)))
		printk("\tvertex %d destroyed OK\n", conv);
	    else					SHOWREF(conv, hwgraph_vertex_destroy);
	}
    }
#endif

    return 0;
}

#endif
int
pciba_attach(devfs_handle_t hconn)
{
#if defined(PCIIO_SLOT_NONE)
    pciio_info_t            info = pciio_info_get(hconn);
    pciio_slot_t            slot = pciio_info_slot_get(info);
#endif
    pciba_comm_t	    comm;
    pciba_bus_t             bus;
    int                     ht;
    devfs_handle_t	    hbase;
    devfs_handle_t          gconn;
    devfs_handle_t          gbase;
    int                     win;
    int                     wins;
    pciio_space_t           space;
    pciaddr_t               base;

    int                     iwins;
    int                     mwins;

#if DEBUG_PCIBA
    printk("pciba_attach(%p)\n", hconn);
#endif

    /* Pick up "dualslot guest" vertex,
     * which gets all functionality except
     * config space access.
     */
    if ((GRAPH_SUCCESS !=
	 hwgraph_traverse(hconn, ".guest", &gconn)) ||
	(hconn == gconn))
	gconn = GRAPH_VERTEX_NONE;

    bus = pciba_find_bus(hconn, 1);
    bus->refct ++;

    /* set up data common to all pciba openables
     * on this connection point.
     */
    NEW(comm);
    comm->conn = hconn;
    comm->bus = bus;
    comm->refct = 0;
    sema_init(&comm->lock, 1);

#if !defined(PCIIO_SLOT_NONE)
    if (bus->refct == 1)
#else
    if (slot == PCIIO_SLOT_NONE)
#endif
    {
	pciio_info_t            pciio_info;
	devfs_handle_t          master;

	pciio_info = pciio_info_get(hconn);
	master = pciio_info_master_get(pciio_info);

	pciba_sub_attach(comm, PCIIO_SPACE_IO, PCIIO_SPACE_IO, 0, master, master, PCIBA_EDGE_LBL_IO);
	pciba_sub_attach(comm, PCIIO_SPACE_MEM, PCIIO_SPACE_MEM, 0, master, master, PCIBA_EDGE_LBL_MEM);
#if defined(PCIIO_SLOT_NONE)
	return 0;
#endif
    }

    ht = 0x7F & pciio_config_get(hconn, PCI_CFG_HEADER_TYPE, 1);

    wins = ((ht == 0x00) ? 6 :
	    (ht == 0x01) ? 2 :
	    0);

    mwins = iwins = 0;

    hbase = GRAPH_VERTEX_NONE;
    gbase = GRAPH_VERTEX_NONE;

    for (win = 0; win < wins; win++) {

	base = pciio_config_get(hconn, PCI_CFG_BASE_ADDR(win), 4);
	if (base & 1) {
	    space = PCIIO_SPACE_IO;
	    base &= 0xFFFFFFFC;
	} else if ((base & 7) == 4) {
	    space = PCIIO_SPACE_MEM;
	    base &= 0xFFFFFFF0;
	    base |= ((pciaddr_t) pciio_config_get(hconn, PCI_CFG_BASE_ADDR(win + 1), 4)) << 32;
	} else {
	    space = PCIIO_SPACE_MEM;
	    base &= 0xFFFFFFF0;
	}

	if (!base)
	    break;

#if PCIBA_ALIGN_CHECK
	if (base & (_PAGESZ - 1)) {
#if DEBUG_PCIBA
	    PRINT_WARNING("%p pciba: BASE%d not page aligned!\n"
		    "\tmmap this window at offset 0x%x via \".../pci/%s\"\n",
		    hconn, win, base,
		    (space == PCIIO_SPACE_IO) ? "io" : "mem");
#endif
	    continue;			/* next window */
	}
#endif

	if ((hbase == GRAPH_VERTEX_NONE) &&
	    ((GRAPH_SUCCESS !=
	      hwgraph_path_add(hconn, PCIBA_EDGE_LBL_BASE, &hbase)) ||
	     (hbase == GRAPH_VERTEX_NONE)))
	    break;			/* no base vertex, no more windows. */

	if ((gconn != GRAPH_VERTEX_NONE) &&
	    (gbase == GRAPH_VERTEX_NONE) &&
	    ((GRAPH_SUCCESS !=
	      hwgraph_path_add(gconn, PCIBA_EDGE_LBL_BASE, &gbase)) ||
	     (gbase == GRAPH_VERTEX_NONE)))
	    break;			/* no base vertex, no more windows. */

	pciba_sub_attach(comm, PCIIO_SPACE_WIN(win), space, base, hbase, gbase, PCIBA_EDGE_LBL_WIN(win));

	if (space == PCIIO_SPACE_IO) {
	    if (!iwins++) {
		pciba_sub_attach(comm, PCIIO_SPACE_WIN(win), space, base, hconn, gconn, PCIBA_EDGE_LBL_IO);
	    }
	} else {
	    if (!mwins++) {
		pciba_sub_attach(comm, PCIIO_SPACE_WIN(win), space, base, hconn, gconn, PCIBA_EDGE_LBL_MEM);
	    }
	}

	if ((base & 7) == 4)
	    win++;
    }

    pciba_sub_attach(comm, PCIIO_SPACE_CFG, PCIIO_SPACE_NONE, 0, hconn, gconn, PCIBA_EDGE_LBL_CFG);
    pciba_sub_attach(comm, PCIBA_SPACE_UDMA, PCIIO_SPACE_NONE, 0, hconn, gconn, PCIBA_EDGE_LBL_DMA);
#if ULI
    pciba_sub_attach(comm, PCIIO_SPACE_NONE, PCIIO_SPACE_NONE, 0, hconn, gconn, PCIBA_EDGE_LBL_INTR);
#endif

    /* XXX should ignore if device is an IOC3 */
    if (ht == 0x01)
	base = pciio_config_get(hconn, PCI_EXPANSION_ROM+8, 4);
    else
	base = pciio_config_get(hconn, PCI_EXPANSION_ROM, 4);

    base &= 0xFFFFF000;

    if (base) {
	if (base & (_PAGESZ - 1))
#if defined(SUPPORT_PRINTING_V_FORMAT)
	    PRINT_WARNING("%v pciba: ROM is 0x%x\n"
		    "\tnot page aligned, mmap will be difficult\n",
		    hconn, base);
#else
	    PRINT_WARNING("0x%x pciba: ROM is 0x%x\n"
		    "\tnot page aligned, mmap will be difficult\n",
		    hconn, base);
#endif
	pciba_sub_attach(comm, PCIIO_SPACE_ROM, PCIIO_SPACE_MEM, base, hconn, gconn, PCIBA_EDGE_LBL_ROM);
    }

#if !FICUS	/* FICUS shorts the refct by one on path_add */
    if (hbase != GRAPH_VERTEX_NONE)
	hwgraph_vertex_unref(hbase);

    if (gbase != GRAPH_VERTEX_NONE)
	hwgraph_vertex_unref(gbase);
#endif

    return 0;
}

static void
pciba_sub_attach2(pciba_comm_t comm,
		  pciio_space_t space,
		  pciio_space_t iomem,
		  pciaddr_t base,
		  devfs_handle_t from,
		  char *name,
		  char *suf,
		  unsigned bigend)
{
    char		nbuf[128];
    pciba_soft_t            soft;
    devfs_handle_t	handle = NULL;

    if (suf && *suf) {
	strcpy(nbuf, name);
	name = nbuf;
	strcat(name, suf);
    }

#if DEBUG_PCIBA
    printk("pciba_sub_attach2 %p/%s %p at %p[%x]\n",
	   from, name, space, space_desc, iomem, space_desc, base, from, name);
#endif

    if (space < TRACKED_SPACES)
	if ((soft = comm->soft[space][bigend]) != NULL) {
	    soft->refct ++;
	    hwgraph_edge_add(from, soft->vhdl, name);
	    return;
	}

    NEW(soft);
    if (!soft)
	return;

    soft->comm = comm;
    soft->space = space;
    soft->size = 0;
    soft->iomem = iomem;
    soft->base = base;
    soft->refct = 1;

    if (space == PCIIO_SPACE_NONE)
	soft->flags = 0;
    else if (bigend)
	soft->flags = PCIIO_BYTE_STREAM;
    else
	soft->flags = PCIIO_WORD_VALUES;

    handle = hwgraph_register(from, name,
		0, DEVFS_FL_AUTO_DEVNUM,
		0, 0,
		S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP, 0, 0,
		&pciba_fops, NULL);
    soft->vhdl = handle;
    pciba_soft_set(soft->vhdl, soft);
    if (space < TRACKED_SPACES)
	comm->soft[space][bigend] = soft;
    comm->refct ++;
}

static void
pciba_sub_attach1(pciba_comm_t comm,
		  pciio_space_t space,
		  pciio_space_t iomem,
		  pciaddr_t base,
		  devfs_handle_t hfrom,
		  devfs_handle_t gfrom,
		  char *name,
		  char *suf,
		  unsigned bigend)
{
    pciba_sub_attach2(comm, space, iomem, base, hfrom, name, suf, bigend);
    if ((gfrom != GRAPH_VERTEX_NONE) && (gfrom != hfrom))
	pciba_sub_attach2(comm, space, iomem, base, gfrom, name, suf, bigend);
}

static void
pciba_sub_attach(pciba_comm_t comm,
		 pciio_space_t space,
		 pciio_space_t iomem,
		 pciaddr_t base,
		 devfs_handle_t hfrom,
		 devfs_handle_t gfrom,
		 char *name)
{
    pciba_sub_attach1(comm, space, iomem, base, hfrom, gfrom, name, NULL, 0);
    if (iomem != PCIIO_SPACE_NONE) {
	pciba_sub_attach1(comm, space, iomem, base, hfrom, gfrom, name, "_le", 0);
	pciba_sub_attach1(comm, space, iomem, base, hfrom, gfrom, name, "_be", 1);
    }
}

#ifdef LATER
static void
pciba_reload_me(devfs_handle_t pconn_vhdl)
{
    devfs_handle_t            vhdl;

#if DEBUG_PCIBA
    printf("pciba_reload_me(%v)\n", pconn_vhdl);
#endif

    if (GRAPH_SUCCESS !=
	hwgraph_traverse(pconn_vhdl, PCIBA_EDGE_LBL_CFG, &vhdl))
	return;

    hwgraph_vertex_unref(vhdl);
}
#endif	/* LATER */

static                  pciba_bus_t
pciba_find_bus(devfs_handle_t pconn, int cflag)
{
    pciio_info_t            pciio_info;
    devfs_handle_t          master;
    arbitrary_info_t        ainfo;
    pciba_bus_t             bus;

    pciio_info = pciio_info_get(pconn);
    master = pciio_info_master_get(pciio_info);

    if (GRAPH_SUCCESS ==
	hwgraph_info_get_LBL(master, PCIBA_INFO_LBL_BUS, &ainfo))
	return (pciba_bus_t) ainfo;

    if (!cflag)
	return 0;

    NEW(bus);
    if (!bus)
	return 0;

    sema_init(&bus->lock, 1);

    ainfo = (arbitrary_info_t) bus;
    hwgraph_info_add_LBL(master, PCIBA_INFO_LBL_BUS, ainfo);
    hwgraph_info_get_LBL(master, PCIBA_INFO_LBL_BUS, &ainfo);
    if ((pciba_bus_t) ainfo != bus)
	DEL(bus);
#if DEBUG_PCIBA
    else
	printk("pcbia_find_bus: new bus at %p\n", master);
#endif

    return (pciba_bus_t) ainfo;
}

#ifdef LATER
static void
pciba_map_push(pciba_bus_t bus, pciba_map_t map)
{
#if DEBUG_PCIBA
    printk("pciba_map_push(bus=0x%x, map=0x%x, hdl=0x%x\n",
	   bus, map, map->handle);
#endif
    pciba_bus_lock(bus);
    map->next = bus->maps;
    bus->maps = map;
    pciba_bus_unlock(bus);
}

static                  pciba_map_t
pciba_map_pop_hdl(pciba_bus_t bus, __psunsigned_t handle)
{
    pciba_map_h             hdl;
    pciba_map_t             map;

    pciba_bus_lock(bus);
    for (hdl = &bus->maps; map = *hdl; hdl = &map->next)
	if (map->handle == handle) {
	    *hdl = map->next;
	    break;
	}
    pciba_bus_unlock(bus);
#if DEBUG_PCIBA
    printk("pciba_map_pop_va(bus=0x%x, handle=0x%x) returns map=0x%x\n",
	   bus, handle, map);
#endif
    return map;
}

/* ================================================================
 *            driver teardown, unregister and unload
 */
int
pciba_unload(void)
{
#if DEBUG_PCIBA
    printk("pciba_unload()\n");
#endif

    if (atomic_read(&pciba_prevent_unload))
	return -1;

    pciio_iterate("pciba_", pciba_unload_me);

    return 0;
}

int
pciba_unreg(void)
{

#if DEBUG_PCIBA
    printf("pciba_unreg()\n");
#endif

    if (atomic_read(&pciba_prevent_unload))
	return -1;

    pciio_driver_unregister("pciba_");
    return 0;
}

int
pciba_detach(devfs_handle_t conn)
{
    devfs_handle_t          base;
    pciba_bus_t             bus;
    devfs_handle_t          gconn;
    devfs_handle_t          gbase;

    pciio_info_t            pciio_info;
    devfs_handle_t          master;
    arbitrary_info_t        ainfo;
    int			    ret;

#if DEBUG_PCIBA
    printf("pciba_detach(%v)\n", conn);
#endif

    if ((GRAPH_SUCCESS !=
	 hwgraph_traverse(conn, ".guest", &gconn)) ||
	(conn == gconn))
	gconn = GRAPH_VERTEX_NONE;

    if (gconn != GRAPH_VERTEX_NONE) {
	pciba_sub_detach(gconn, PCIBA_EDGE_LBL_CFG);
	pciba_sub_detach(gconn, PCIBA_EDGE_LBL_DMA);
	pciba_sub_detach(gconn, PCIBA_EDGE_LBL_ROM);
#if ULI
	pciba_sub_detach(gconn, PCIBA_EDGE_LBL_INTR);
#endif
	if (GRAPH_SUCCESS == hwgraph_edge_remove(conn, PCIBA_EDGE_LBL_BASE, &gbase)) {
	    pciba_sub_detach(gconn, PCIBA_EDGE_LBL_MEM);
	    pciba_sub_detach(gconn, PCIBA_EDGE_LBL_IO);
	    pciba_sub_detach(gbase, "0");
	    pciba_sub_detach(gbase, "1");
	    pciba_sub_detach(gbase, "2");
	    pciba_sub_detach(gbase, "3");
	    pciba_sub_detach(gbase, "4");
	    pciba_sub_detach(gbase, "5");
	    hwgraph_vertex_unref(gbase);
	    if (GRAPH_SUCCESS != (ret = hwgraph_vertex_destroy(gbase))) {
#if defined(SUPPORT_PRINTING_V_FORMAT)
		PRINT_WARNING("pciba: hwgraph_vertex_destroy(%v/base) failed (%d)",
			conn, ret);
#else
		PRINT_WARNING("pciba: hwgraph_vertex_destroy(0x%x/base) failed (%d)",
			conn, ret);
#endif
#if DEBUG_REFCT
		printk("\tretained refct %d\n", hwgraph_vertex_refct(gbase));
#endif
	    }
	}
    }

    pciba_sub_detach(conn, PCIBA_EDGE_LBL_CFG);
    pciba_sub_detach(conn, PCIBA_EDGE_LBL_DMA);
    pciba_sub_detach(conn, PCIBA_EDGE_LBL_ROM);
#if ULI
    pciba_sub_detach(conn, PCIBA_EDGE_LBL_INTR);
#endif

    if (GRAPH_SUCCESS == hwgraph_edge_remove(conn, PCIBA_EDGE_LBL_BASE, &base)) {
	pciba_sub_detach(conn, PCIBA_EDGE_LBL_MEM);
	pciba_sub_detach(conn, PCIBA_EDGE_LBL_IO);
	pciba_sub_detach(base, "0");
	pciba_sub_detach(base, "1");
	pciba_sub_detach(base, "2");
	pciba_sub_detach(base, "3");
	pciba_sub_detach(base, "4");
	pciba_sub_detach(base, "5");
	hwgraph_vertex_unref(base);
	if (GRAPH_SUCCESS != (ret = hwgraph_vertex_destroy(base))) {
#if defined(SUPPORT_PRINTING_V_FORMAT)
	    PRINT_WARNING(CE_WARN, "pciba: hwgraph_vertex_destroy(%v/base) failed (%d)",
		    conn, ret);
#else
	    PRINT_WARNING(CE_WARN, "pciba: hwgraph_vertex_destroy(0x%x/base) failed (%d)",
		    conn, ret);
#endif
#if DEBUG_REFCT
	    printk("\tretained refct %d\n", hwgraph_vertex_refct(base));
#endif
	}
    }

    bus = pciba_find_bus(conn, 0);
    if (bus && !--(bus->refct)) {

	pciio_info = pciio_info_get(conn);

	master = pciio_info_master_get(pciio_info);

	pciba_sub_detach(master, PCIBA_EDGE_LBL_IO);
	pciba_sub_detach(master, PCIBA_EDGE_LBL_MEM);
	pciba_sub_detach(master, PCIBA_EDGE_LBL_CFG);
	hwgraph_info_remove_LBL(master, PCIBA_INFO_LBL_BUS, &ainfo);

#if DEBUG_PCIBA
	printf("pcbia_detach: DEL(bus) at %v\n", master);
#endif
	DEL(bus);
    } 

    return 0;
}

static void
pciba_sub_detach1(devfs_handle_t conn,
		  char *name,
		  char *suf)
{
    devfs_handle_t         vhdl;
    pciba_soft_t            soft;
    pciba_comm_t	    comm;
    int			    ret;
    char		nbuf[128];

    if (suf && *suf) {
	strcpy(nbuf, name);
	name = nbuf;
	strcat(name, suf);
    }

    if ((GRAPH_SUCCESS == hwgraph_edge_remove(conn, name, &vhdl)) &&
	((soft = pciba_soft_get(vhdl)) != NULL)) {
#if DEBUG_PCIBA
#if defined(SUPPORT_PRINTING_V_FORMAT)
	prink("pciba_sub_detach(%v,%s)\n", conn, name);
#else
	prink("pciba_sub_detach(0x%x,%s)\n", conn, name);
#endif
#endif

	hwgraph_vertex_unref(soft->vhdl);
#if DEBUG_REFCT
	printk("\tadjusted refct %d (soft ref: %d)\n",
		hwgraph_vertex_refct(vhdl),
		soft->refct);
#endif
	if (!--(soft->refct)) {
	    comm = soft->comm;
	    if (!--(comm->refct)) {
		DEL(comm);
	    }
	    pciba_soft_set(vhdl, 0);
	    DEL(soft);

	    hwgraph_vertex_unref(vhdl);
	    if (GRAPH_SUCCESS != (ret = hwgraph_vertex_destroy(vhdl))) {
#if defined(SUPPORT_PRINTING_V_FORMAT)
		PRINT_WARNING("pciba: hwgraph_vertex_destroy(0x%x/%s) failed (%d)",
			conn, name, ret);
#else
		PRINT_WARNING("pciba: hwgraph_vertex_destroy(%v/%s) failed (%d)",
			conn, name, ret);
#endif
#if DEBUG_REFCT
		printk("\tretained refct %d\n", hwgraph_vertex_refct(vhdl));
#endif
	    }
	}
    }
}

static void
pciba_sub_detach(devfs_handle_t conn,
		 char *name)
{
    pciba_sub_detach1(conn, name, "");
    pciba_sub_detach1(conn, name, "_le");
    pciba_sub_detach1(conn, name, "_be");
}

static void
pciba_unload_me(devfs_handle_t pconn_vhdl)
{
    devfs_handle_t         c_vhdl;

#if DEBUG_PCIBA
    printf("pciba_unload_me(%v)\n", pconn_vhdl);
#endif

    if (GRAPH_SUCCESS !=
	hwgraph_traverse(pconn_vhdl, PCIBA_EDGE_LBL_CFG, &c_vhdl))
	return;

    hwgraph_vertex_unref(c_vhdl);
}

/* ================================================================
 *            standard unix entry points
 */

/*ARGSUSED */
int
pciba_open(dev_t *devp, int flag, int otyp, struct cred *crp)
{

#if DEBUG_PCIBA
    printf("pciba_open(%V)\n", *devp);
#endif
    return 0;
}

/*ARGSUSED */
int
pciba_close(dev_t dev)
{
    devfs_handle_t         vhdl = dev_to_vhdl(dev);
    pciba_soft_t            soft = pciba_soft_get(vhdl);

#if DEBUG_PCIBA
    printf("pciba_close(%V)\n", dev);
#endif

    /* if there is pending DMA for this device, hit the
     * device over the head with a baseball bat and
     * release the system memory resources.
     */
    if (soft && soft->comm->dmap) {
	pciba_dma_t             next;
	pciba_dma_t             dmap;

	pciba_soft_lock(soft);
	if (dmap = soft->comm->dmap) {
	    soft->comm->dmap = 0;

	    pciio_reset(soft->comm->conn);

	    do {
		if (!dmap->kaddr)
		    break;
		if (!dmap->paddr)
		    break;
		if (dmap->bytes < NBPP)
		    break;
		next = dmap->next;
		kvpfree(dmap->kaddr, dmap->bytes / NBPP);
		dmap->paddr = 0;
		dmap->bytes = 0;
		DEL(dmap);
	    } while (dmap = next);
	}
	pciba_soft_unlock(soft);
    }
    return 0;
}

/* ARGSUSED */
int
pciba_read(dev_t dev, cred_t *crp)
{
#if DEBUG_PCIBA
    printf("pciba_read(%V)\n", dev);
#endif

    return EINVAL;
}

/* ARGSUSED */
int
pciba_write(dev_t dev, cred_t *crp)
{
#if DEBUG_PCIBA
    printf("pciba_write(%V)\n", dev);
#endif

    return EINVAL;
}

/*ARGSUSED */
int
pciba_ioctl(dev_t dev, int cmd, void *uarg, int mode, cred_t *crp, int *rvalp)
{
    devfs_handle_t          vhdl;
    pciba_soft_t            soft;
    pciio_space_t           space;
    ioctl_arg_buffer_t      arg;
    int                     psize;
    int                     err = 0;

#if ULI
    char		    abi = get_current_abi();
    pciio_intr_t            intr=0;
    device_desc_t           desc;
    cpuid_t                 intrcpu;
    unsigned                lines;
    struct uli             *uli = 0;
#endif
    unsigned                flags;
    void                   *kaddr = 0;
    iopaddr_t               paddr;
    pciba_dma_h             dmah;
    pciba_dma_t             dmap = 0;
    pciio_dmamap_t          dmamap = 0;
    size_t                  bytes;
    int                     pages;
    pciaddr_t               daddr;

#if DEBUG_PCIBA
    printf("pciba_ioctl(%V,0x%x)\n", dev, cmd);
#endif

    psize = (cmd >> 16) & IOCPARM_MASK;

#if ULI
    ASSERT(sizeof(struct uliargs) > 8);	/* prevent CFG access conflict */
    ASSERT(sizeof(struct uliargs) <= IOCPARM_MASK);
#endif

    arg.ca = uarg;

    if ((psize > 0) && (cmd & (IOC_OUT | IOC_IN))) {
	if (psize > sizeof(arg))
	    err = EINVAL;		/* "bad parameter size */
	else {
	    if (cmd & IOC_OUT)
		bzero(arg.data, psize);
	    if ((cmd & IOC_IN) &&
		(copyin(uarg, arg.data, psize) < 0))
		err = EFAULT;		/* "parameter copyin failed" */
	}
    }
    vhdl = dev_to_vhdl(dev);
    soft = pciba_soft_get(vhdl);
    space = soft->space;

    if (err == 0) {
	err = EINVAL;			/* "invalid ioctl for this vertex" */
	switch (space) {
#if ULI
	case PCIIO_SPACE_NONE:		/* the "intr" vertex */
	    /* PCIIOCSETULI: set up user interrupts.
	     */
	    lines = cmd & 15;
	    if (ABI_IS_64BIT(abi)) {
		if (cmd != PCIIOCSETULI(lines)) {
		    err = EINVAL;		/* "invalid ioctl for this vertex" */
		    break;
		}
	    }
	    else {
		struct uliargs uliargs;

		if (cmd != PCIIOCSETULI32(lines)) {
		    err = EINVAL;		/* "invalid ioctl for this vertex" */
		    break;
		}
		
		uliargs32_to_uliargs(&arg.uli32, &uliargs);
		arg.uli = uliargs;
	    }
	    desc = device_desc_dup(soft->comm->conn);
	    device_desc_flags_set(desc, (device_desc_flags_get(desc) |
					 D_INTR_NOTHREAD));
	    device_desc_intr_swlevel_set(desc, INTR_SWLEVEL_NOTHREAD_DEFAULT);
	    device_desc_intr_name_set(desc, "PCIBA");
	    device_desc_default_set(soft->comm->conn, desc);

	    /* When designating interrupts, the slot number
	     * is taken from the connection point.
	     * Bits 0..3 are used to select INTA..INTD; more
	     * than one bit can be specified. These should
	     * be constructed using PCIIO_INTR_LINE_[ABCD].
	     */
	    intr = pciio_intr_alloc
		(soft->comm->conn, desc, lines, soft->vhdl);
	    if (intr == 0) {
		err = ENOMEM;		/* "insufficient resources" */
		break;
	    }
	    intrcpu = cpuvertex_to_cpuid(pciio_intr_cpu_get(intr));

	    if (err = new_uli(&arg.uli, &uli, intrcpu)) {
		break;			/* "unable to set up ULI" */
	    }
	    atomic_inc(&pciba_prevent_unload);

	    pciio_intr_connect(intr, pciba_intr, uli, (void *) 0);

	    /* NOTE: don't set the teardown function
	     * until the interrupt is connected.
	     */
	    uli->teardownarg1 = (__psint_t) intr;
	    uli->teardown = pciba_clearuli;

	    arg.uli.id = uli->index;

	    if (!ABI_IS_64BIT(abi)) {
		struct uliargs32 uliargs32;
		uliargs_to_uliargs32(&arg.uli, &uliargs32);
		arg.uli32 = uliargs32;
	    }

	    err = 0;
	    break;
#endif

	case PCIBA_SPACE_UDMA:		/* the "dma" vertex */

	    switch (cmd) {

	    case PCIIOCDMAALLOC:
		/* PCIIOCDMAALLOC: allocate a chunk of physical
		 * memory and set it up for DMA. Return the
		 * PCI address that gets to it.
		 * NOTE: this allocates memory local to the
		 * CPU doing the ioctl, not local to the
		 * device that will be doing the DMA.
		 */

		if (!_CAP_ABLE(CAP_DEVICE_MGT)) {
		    err = EPERM;
		    break;
		}
		/* separate the halves of the incoming parameter */
		flags = arg.ud >> 32;
		bytes = arg.ud & 0xFFFFFFFF;

#if DEBUG_PCIBA
		printf("pciba: user wants 0x%x bytes of DMA, flags 0x%x\n",
		       bytes, flags);
#endif

		/* round up the requested size to the next highest page */
		pages = (bytes + NBPP - 1) / NBPP;

		/* make sure the requested size is something reasonable */
		if (pages > pci_user_dma_max_pages) {
#if DEBUG_PCIBA
		    printf("pciba: request for too much buffer space\n");
#endif
		    err = EINVAL;
		    break;		/* "request for too much buffer space" */
		}
		/* "correct" number of bytes */
		bytes = pages * NBPP;

		/* allocate the space */
		/* XXX- force to same node as the device? */
		/* XXX- someday, we want to handle user buffers,
		 *    and noncontiguous pages, but this will
		 *      require either fancy mapping or handing
		 *      a list of blocks back to the user. For
		 *      now, just tell users to allocate a lot of
		 *      individual single-pages and manage their
		 *      scatter-gather manually.
		 */
		kaddr = kvpalloc(pages, VM_DIRECT | KM_NOSLEEP, 0);
		if (kaddr == 0) {
#if DEBUG_PCIBA
		    printf("pciba: unable to get %d contiguous pages\n", pages);
#endif
		    err = EAGAIN;	/* "insufficient resources, try again later" */
		    break;
		}
#if DEBUG_PCIBA
		printf("pciba: kaddr is 0x%x\n", kaddr);
#endif
		paddr = kvtophys(kaddr);

		daddr = pciio_dmatrans_addr
		    (soft->comm->conn, 0, paddr, bytes, flags);
		if (daddr == 0) {	/* "no direct path available" */
#if DEBUG_PCIBA
		    printf("pciba: dmatrans failed, trying dmamap\n");
#endif
		    dmamap = pciio_dmamap_alloc
			(soft->comm->conn, 0, bytes, flags);
		    if (dmamap == 0) {
#if DEBUG_PCIBA
			printf("pciba: unable to allocate dmamap\n");
#endif
			err = ENOMEM;
			break;		/* "out of mapping resources" */
		    }
		    daddr = pciio_dmamap_addr
			(dmamap, paddr, bytes);
		    if (daddr == 0) {
#if DEBUG_PCIBA
			printf("pciba: dmamap_addr failed\n");
#endif
			err = EINVAL;
			break;		/* "can't get there from here" */
		    }
		}
#if DEBUG_PCIBA
		printf("pciba: daddr is 0x%x\n", daddr);
#endif
		NEW(dmap);
		if (!dmap) {
		    err = ENOMEM;
		    break;		/* "no memory available" */
		}
		dmap->bytes = bytes;
		dmap->pages = pages;
		dmap->paddr = paddr;
		dmap->kaddr = kaddr;
		dmap->map = dmamap;
		dmap->daddr = daddr;
		dmap->handle = 0;

#if DEBUG_PCIBA
		printf("pciba: dmap 0x%x contains va 0x%x bytes 0x%x pa 0x%x pages 0x%x daddr 0x%x\n",
		       dmap, kaddr, bytes, paddr, pages, daddr);
#endif

		arg.ud = dmap->daddr;

		err = 0;
		break;

	    case PCIIOCDMAFREE:
		/* PCIIOCDMAFREE: Find the chunk of
		 * User DMA memory, and release its
		 * resources back to the system.
		 */

		if (!_CAP_ABLE(CAP_DEVICE_MGT)) {
		    err = EPERM;	/* "you can't do that" */
		    break;
		}
		if (soft->comm->dmap == NULL) {
		    err = EINVAL;	/* "no User DMA to free" */
		    break;
		}
		/* find the request. */
		daddr = arg.ud;
		err = EINVAL;		/* "block not found" */
		pciba_soft_lock(soft);
		for (dmah = &soft->comm->dmap; dmap = *dmah; dmah = &dmap->next) {
		    if (dmap->daddr == daddr) {
			if (dmap->handle != 0) {
			    dmap = 0;	/* don't DEL this dmap! */
			    err = EINVAL;	/* "please unmap first" */
			    break;		/* break outa for loop. */
			}
			*dmah = dmap->next;

			if (dmamap = dmap->map) {
			    pciio_dmamap_free(dmamap);
			    dmamap = 0;	/* don't free it twice! */
			}
			kvpfree(dmap->kaddr, dmap->bytes / NBPP);
			DEL(dmap);
			dmap = 0;	/* don't link this back into the list! */
			err = 0;	/* "all done" */
			break;		/* break outa for loop. */
		    }
		}
		pciba_soft_unlock(soft);
		break;		/* break outa case PCIIOCDMAFREE: */
	    }
	    break;		/* break outa case PCIBA_SPACE_UDMA: */

	case PCIIO_SPACE_CFG:

	    /* PCIIOCCFG{RD,WR}: read and/or write
	     * PCI configuration space. If both,
	     * the read happens first (this becomes
	     * a swap operation, atomic with respect
	     * to other updates through this path).
	     *
	     * Should be *last* IOCTl command checked,
	     * so other patterns can nip useless codes
	     * out of the space this decodes.
	     */
	    err = EINVAL;
	    if ((psize > 0) || (psize <= 8) &&
		(((cmd & 0xFF) + psize) <= 256) &&
		(cmd & (IOC_IN | IOC_OUT))) {

		uint64_t                rdata;
		uint64_t                wdata;
		int                     shft;

		shft = 64 - (8 * psize);

		wdata = arg.ud >> shft;

		pciba_soft_lock(soft);

		if (cmd & IOC_OUT)
		    rdata = pciio_config_get(soft->comm->conn, cmd & 0xFFFF, psize);
		if (cmd & IOC_IN)
		    pciio_config_set(soft->comm->conn, cmd & 0xFFFF, psize, wdata);

		pciba_soft_unlock(soft);

		arg.ud = rdata << shft;
		err = 0;
		break;
	    }
	    break;
	}
    }
    /* done: come here if all went OK.
     */
    if ((err == 0) &&
	((cmd & IOC_OUT) && (psize > 0)) &&
	copyout(arg.data, uarg, psize))
	err = EFAULT;

    /* This gets delayed until after the copyout so we
     * do not free the dmap on a copyout error, or
     * alternately end up with a dangling allocated
     * buffer that the user never got back.
     */
    if ((err == 0) && dmap) {
	pciba_soft_lock(soft);
	dmap->next = soft->comm->dmap;
	soft->comm->dmap = dmap;
	pciba_soft_unlock(soft);
    }
    if (err) {
	/* Things went badly. Clean up.
	 */
#if ULI
	if (intr) {
	    pciio_intr_disconnect(intr);
	    pciio_intr_free(intr);
	}
	if (uli)
	    free_uli(uli);
#endif
	if (dmap) {
	    if (dmap->map && (dmap->map != dmamap))
		pciio_dmamap_free(dmap->map);
	    DEL(dmap);
	}
	if (dmamap)
	    pciio_dmamap_free(dmamap);
	if (kaddr)
	    kvpfree(kaddr, pages);
    }
    return *rvalp = err;
}

/* ================================================================
 *            mapping support
 */

/*ARGSUSED */
int
pciba_map(dev_t dev, vhandl_t *vt,
	  off_t off, size_t len, uint32_t prot)
{
    devfs_handle_t          vhdl = dev_to_vhdl(dev);
    pciba_soft_t            soft = pciba_soft_get(vhdl);
    devfs_handle_t          conn = soft->comm->conn;
    pciio_space_t           space = soft->space;
    size_t                  pages = (len + NBPP - 1) / NBPP;
    pciio_piomap_t          pciio_piomap = 0;
    caddr_t                 kaddr;
    pciba_map_t             map;
    pciba_dma_t             dmap;

#if DEBUG_PCIBA
    printf("pciba_map(%V,vt=0x%x)\n", dev, vt);
#endif

    if (space == PCIBA_SPACE_UDMA) {
	pciba_soft_lock(soft);

	for (dmap = soft->comm->dmap; dmap != NULL; dmap = dmap->next) {
	    if (off == dmap->daddr) {
		if (pages != dmap->pages) {
		    pciba_soft_unlock(soft);
		    return EINVAL;	/* "size mismatch" */
		}
		v_mapphys(vt, dmap->kaddr, dmap->bytes);
		dmap->handle = v_gethandle(vt);
		pciba_soft_unlock(soft);
#if DEBUG_PCIBA
		printf("pciba: mapped dma at kaddr 0x%x via handle 0x%x\n",
		       dmap->kaddr, dmap->handle);
#endif
		return 0;
	    }
	}
	pciba_soft_unlock(soft);
	return EINVAL;			/* "block not found" */
    }
    if (soft->iomem == PCIIO_SPACE_NONE)
	return EINVAL;			/* "mmap not supported" */

    kaddr = (caddr_t) pciio_pio_addr
	(conn, 0, space, off, len, &pciio_piomap, soft->flags | PCIIO_FIXED );

#if DEBUG_PCIBA
    printf("pciba: mapped %R[0x%x..0x%x] via map 0x%x to kaddr 0x%x\n",
	   space, space_desc, off, off + len - 1, pciio_piomap, kaddr);
#endif

    if (kaddr == NULL)
	return EINVAL;			/* "you can't get there from here" */

    NEW(map);
    if (map == NULL) {
	if (pciio_piomap)
	    pciio_piomap_free(pciio_piomap);
	return ENOMEM;			/* "unable to get memory resources */
    }
#ifdef LATER
    map->uthread = curuthread;
#endif
    map->handle = v_gethandle(vt);
    map->uvaddr = v_getaddr(vt);
    map->map = pciio_piomap;
    map->space = soft->iomem;
    map->base = soft->base + off;
    map->size = len;
    pciba_map_push(soft->comm->bus, map);

    /* Inform the system of the correct
     * kvaddr corresponding to the thing
     * that is being mapped.
     */
    v_mapphys(vt, kaddr, len);

    return 0;
}

/*ARGSUSED */
int
pciba_unmap(dev_t dev, vhandl_t *vt)
{
    devfs_handle_t         vhdl = dev_to_vhdl(dev);
    pciba_soft_t            soft = pciba_soft_get(vhdl);
    pciba_bus_t             bus = soft->comm->bus;
    pciba_map_t             map;
    __psunsigned_t          handle = v_gethandle(vt);

#if DEBUG_PCIBA
    printf("pciba_unmap(%V,vt=%x)\n", dev, vt);
#endif

    /* If this is a userDMA buffer,
     * make a note that it has been unmapped
     * so it can be released.
     */
    if (soft->comm->dmap) {
	pciba_dma_t             dmap;

	pciba_soft_lock(soft);
	for (dmap = soft->comm->dmap; dmap != NULL; dmap = dmap->next)
	    if (handle == dmap->handle) {
		dmap->handle = 0;
		pciba_soft_unlock(soft);
#if DEBUG_PCIBA
		printf("pciba: unmapped dma at kaddr 0x%x via handle 0x%x\n",
		       dmap->kaddr, handle);
#endif
		return 0;		/* found userPCI */
	    }
	pciba_soft_unlock(soft);
    }
    map = pciba_map_pop_hdl(bus, handle);
    if (map == NULL)
	return EINVAL;			/* no match */

    if (map->map)
	pciio_piomap_free(map->map);
    DEL(map);

    return (0);				/* all done OK */
}

#if ULI
void
pciba_clearuli(struct uli *uli)
{
    pciio_intr_t            intr = (pciio_intr_t) uli->teardownarg1;

#if DEBUG_PCIBA
    printf("pciba_clearuli(0x%x)\n", uli);
#endif

    pciio_intr_disconnect(intr);
    pciio_intr_free(intr);
    atomic_dec(&pciba_prevent_unload);
}

void
pciba_intr(intr_arg_t arg)
{
    struct uli             *uli = (struct uli *) arg;
    int                     ulinum = uli->index;

    extern void frs_handle_uli(void);

    if (ulinum >= 0 && ulinum < MAX_ULIS) {
	    uli_callup(ulinum);

	    if (private.p_frs_flags)
		    frs_handle_uli();
    }
}
#endif
#endif /* LATER - undef as we implement each routine */
