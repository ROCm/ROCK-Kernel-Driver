/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

/* This is to export entry points needed for IP interface */
#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif
#include <linux/version.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/utsname.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/errno.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#include <linux/blk.h>
#else
#include <linux/blkdev.h>
#endif
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/unistd.h>
#include <linux/timex.h>
#include <linux/timer.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <asm/pci.h>
#else
/* From drivers/scsi */
#include "sd.h"
#endif
#include "hosts.h"

#include "elx_os.h"
#include "prod_os.h"
#include "elx_util.h"
#include "elx_clock.h"
#include "elx_hw.h"
#include "elx_sli.h"
#include "elx_mem.h"
#include "elx_sched.h"
#include "elx.h"
#include "elx_logmsg.h"
#include "elx_disc.h"
#include "elx_scsi.h"
#include "elx_os_scsiport.h"
#include "elx_crtn.h"
#include "elx_cfgparm.h"
#include "lpfc_hba.h"
#include "lpfc_ip.h"
#include "lpfc_ioctl.h"
#include "lpfc_crtn.h"
#include "prod_crtn.h"

#include <linux/spinlock.h>
#include <linux/rtnetlink.h>

#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <asm/byteorder.h>

#ifdef powerpc
#ifndef BITS_PER_LONG
#define BITS_PER_LONG 64
#endif
#endif

#include <linux/module.h>

/* Configuration parameters defined */
#define LPFC_DEF_ICFG
#include "lpfc_diag.h"
#include "lpfc_cfgparm.h"
#include "lpfc_module_param.h"
#include "lpfc.conf"

#define LPFC_DRIVER_NAME    "lpfc"

#ifndef LPFC_DRIVER_VERSION
#define LPFC_DRIVER_VERSION "2.10f"
#define OSGT_DRIVER_VERSION "1.08"
#endif

#define  LPFC_MODULE_DESC "Emulex LightPulse FC SCSI " LPFC_DRIVER_VERSION

char *elx_drvr_name = LPFC_DRIVER_NAME;
char *lpfc_release_version = LPFC_DRIVER_VERSION;

MODULE_DESCRIPTION("Emulex LightPulse Fibre Channel driver - Open Source");
MODULE_AUTHOR("Emulex Corporation - tech.support@emulex.com");

char lpfc_os_name_version[256];
#define FC_EXTEND_TRANS_A 1
#define ScsiResult(host_code, scsi_code) (((host_code) << 16) | scsi_code)

/* Linux 2.4 compatibility */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#endif				/* < 2.6.0 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
int lpfc_detect(Scsi_Host_Template *);
int lpfc_DetectInstance(int, struct pci_dev *, uint32_t, Scsi_Host_Template *);
#endif
int lpfc_diag_init(void);
int lpfc_linux_attach(int, Scsi_Host_Template *, struct pci_dev *);
int lpfc_get_bind_type(elxHBA_t *);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
int lpfc_release(struct Scsi_Host *host);
#endif
int lpfc_diag_uninit(void);
int lpfc_linux_detach(int);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
void lpfc_select_queue_depth(struct Scsi_Host *, Scsi_Device *);
#else
static int lpfc_slave_configure(Scsi_Device *);
#endif

const char *lpfc_info(struct Scsi_Host *);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
static int lpfc_proc_info(char *, char **, off_t, int, int, int);
#else
static int lpfc_proc_info(struct Scsi_Host *, char *, char **, off_t, int, int);
#endif

int lpfc_device_queue_depth(elxHBA_t *, Scsi_Device *);
irqreturn_t lpfc_intr_handler(int, void *, struct pt_regs *);
void lpfc_local_timeout(unsigned long);
int lpfc_reset_bus_handler(Scsi_Cmnd * cmnd);

int lpfc_memmap(elxHBA_t *);
int lpfc_unmemmap(elxHBA_t *);
int lpfc_pcimap(elxHBA_t *);

int lpfc_mem_poolinit(elxHBA_t *);
int lpfc_config_setup(elxHBA_t *);
int lpfc_bind_setup(elxHBA_t *);
int lpfc_sli_setup(elxHBA_t *);
void lpfc_sli_brdreset(elxHBA_t *);
int lpfc_bind_wwpn(elxHBA_t *, char **, u_int);
int lpfc_bind_wwnn(elxHBA_t *, char **, u_int);
int lpfc_bind_did(elxHBA_t *, char **, u_int);
ELXSCSILUN_t *lpfc_tran_find_lun(ELX_SCSI_BUF_t *);

extern struct elx_mem_pool *elx_mem_dmapool[MAX_ELX_BRDS];
extern int elx_idx_dmapool[MAX_ELX_BRDS];
extern int elx_size_dmapool[MAX_ELX_BRDS];
extern spinlock_t elx_kmem_lock;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
extern int elx_biosparam(struct scsi_device *, struct block_device *,
			 sector_t capacity, int ip[]);
#else
extern int elx_biosparam(Disk *, kdev_t, int[]);
#endif
extern int elx_pci_getadd(struct pci_dev *, int, unsigned long *);
extern void elx_scsi_add_timer(Scsi_Cmnd *, int);

int lpfc_mem_poolinit(elxHBA_t *);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
ssize_t lpfc_sysfs_info_show(struct device *, char *);
#endif

/* Binding Definitions: Max string size  */
#define FC_MAX_DID_STRING       6
#define FC_MAX_WW_NN_PN_STRING 16

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

#if VARYIO == 20
#define VARYIO_ENTRY .can_do_varyio = 1,
#elif VARYIO == 3
#define VARYIO_ENTRY .vary_io =1,
#else
#define VARYIO_ENTRY
#endif

#if defined CONFIG_HIGHMEM
#if USE_HIGHMEM_IO ==2		// i386 & Redhat 2.1
#define HIGHMEM_ENTRY .can_dma_32 = 1,
#define SINGLE_SG_OK .single_sg_ok = 1,
#else
#if USE_HIGHMEM_IO ==3		// Redhat 3.0, Suse
#define HIGHMEM_ENTRY .highmem_io = 1,
#define SINGLE_SG_OK
#else				// any other
#define HIGHMEM_ENTRY
#define SINGLE_SG_OK
#endif
#endif
#else				// no highmem config
#define HIGHMEM_ENTRY
#define SINGLE_SG_OK
#endif
#endif

static Scsi_Host_Template driver_template = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	.next = NULL,
	.command = NULL,
	.slave_attach = NULL,
	.use_new_eh_code = 1,
	.proc_dir = NULL,
	.detect = lpfc_detect,
	.release = lpfc_release,
	VARYIO_ENTRY
	HIGHMEM_ENTRY
	SINGLE_SG_OK
#else
	.slave_configure = lpfc_slave_configure,
#endif
	.proc_name = LPFC_DRIVER_NAME,
	.proc_info = lpfc_proc_info,
	.module = THIS_MODULE,
	.name = LPFC_DRIVER_NAME,
	.info = lpfc_info,
	.queuecommand = elx_queuecommand,
	.eh_strategy_handler = NULL,
	.eh_abort_handler = elx_abort_handler,
	.eh_device_reset_handler = elx_reset_lun_handler,
	.eh_bus_reset_handler = lpfc_reset_bus_handler,
	.eh_host_reset_handler = NULL,
	.abort = NULL,
	.reset = NULL,
	.bios_param = elx_biosparam,
	.can_queue = LPFC_DFT_HBA_Q_DEPTH,
	.this_id = -1,
	.sg_tablesize = SG_ALL,
	.cmd_per_lun = 30,
	.present = 0,
	.unchecked_isa_dma = 0,
	.use_clustering = DISABLE_CLUSTERING,
	.emulated = 0
};

/* A chrdev is used for diagnostic interface */
int lpfcdiag_ioctl(struct inode *inode, struct file *file,
		   unsigned int cmd, unsigned long arg);
int lpfcdiag_open(struct inode *inode, struct file *file);
int lpfcdiag_release(struct inode *inode, struct file *file);

static struct file_operations lpfc_fops = {
	.ioctl = lpfcdiag_ioctl,
	.open = lpfcdiag_open,
	.release = lpfcdiag_release,
};

/*Other configuration parameters, not available to user*/
static int lpfc_pci_latency_clocks = 0;
static int lpfc_pci_cache_line = 0;
/*Other configuration parameters, not available to user*/
static int lpfc_mtu = 65280;	/* define IP max MTU size */
static int lpfc_rcv_buff_size = 4 * 1024;	/* define IP recv buffer size */

/* lpfc_detect_called can be either TRUE, FALSE or LPFN_PROBE_PENDING */
#define LPFN_PROBE_PENDING 2
static int lpfc_detect_called = FALSE;
static int (*lpfn_probe) (void) = NULL;

static int lpfc_major = 0;

int lpfc_nethdr = 1;

uint16_t lpfc_num_nodes = 128;	/* default number of NPort structs to alloc */
int lpfc_use_data_direction = 1;

LINUX_DRVR_t lpfcdrvr;
/* Can be used to map driver instance number and hardware adapter number */
int lpfc_instance[MAX_ELX_BRDS];
int lpfc_instcnt = 0;
uint32_t lpfc_diag_state = DDI_ONDI;
int lpfc_isr = 0;
int lpfc_tmr = 0;

/* Used for driver 1 sec clock tick */
int lpfc_clkCnt = 0;
struct timer_list lpfc_sec_clk;

extern elxDRVR_t elxDRVR;
extern int prodMallocCnt;
extern int prodMallocByte;
extern int prodFreeCnt;
extern int prodFreeByte;

extern char *lpfc_fcp_bind_WWPN[];
extern char *lpfc_fcp_bind_WWNN[];
extern char *lpfc_fcp_bind_DID[];

/* This is to export entry points needed for IP interface */
int lpfc_xmit(elxHBA_t *, struct sk_buff *);
int lpfc_ipioctl(int, void *);
#ifdef MODVERSIONS
EXPORT_SYMBOL(lpfc_xmit);
EXPORT_SYMBOL(lpfc_ipioctl);
#else
EXPORT_SYMBOL_NOVERS(lpfc_xmit);
EXPORT_SYMBOL_NOVERS(lpfc_ipioctl);
#endif				/* MODVERSIONS */

#if LINUX_VERSION_CODE <  KERNEL_VERSION(2,6,0)

int
lpfc_detect(Scsi_Host_Template * tmpt)
{
	struct pci_dev *pdev = NULL;
	int instance = 0;
	int i;
	/* To add another, add a line before the last element.
	 * Leave last element 0.
	 */
	uint32_t sType[] = {
		PCI_DEVICE_ID_VIPER,
		PCI_DEVICE_ID_THOR,
		PCI_DEVICE_ID_PEGASUS,
		PCI_DEVICE_ID_CENTAUR,
		PCI_DEVICE_ID_DRAGONFLY,
		PCI_DEVICE_ID_SUPERFLY,
		PCI_DEVICE_ID_RFLY,
		PCI_DEVICE_ID_PFLY,
		PCI_DEVICE_ID_TFLY,
		PCI_DEVICE_ID_LP101,
		0
	};

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#if VARYIO == 21
#ifdef SCSI_HOST_VARYIO
	SCSI_HOST_VARYIO(tmpt) = 1;
#endif
#endif
#endif
	printk(LPFC_MODULE_DESC "\n");
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

	/* Release the io_request_lock lock and reenable interrupts allowing
	 * the driver to sleep if necessary.
	 */
	spin_unlock(&io_request_lock);
#endif

	memset((char *)&elxDRVR, 0, sizeof (elxDRVR_t));
	memset((char *)&lpfcdrvr, 0, sizeof (LINUX_DRVR_t));
	elxDRVR.pDrvrOSEnv = &lpfcdrvr;
	for (i = 0; i < MAX_ELX_BRDS; i++) {
		lpfc_instance[i] = -1;
	}

	/* Initialize all per Driver locks */
	elx_clk_init_lock(0);

	/* Search for all Device IDs supported */
	i = 0;
	while (sType[i]) {
		instance = lpfc_DetectInstance(instance, pdev, sType[i], tmpt);
		i++;
	}

	if (instance) {
		lpfc_diag_init();	/* Initialize diagnostic interface */
	}

	/* This covers the case where the lpfn driver gets loaded before the
	 * lpfc driver detect completes.
	 */
	if (lpfc_detect_called == 2) {
		lpfc_detect_called = 1;
		if (lpfn_probe != NULL)
			lpfn_probe();

	} else
		lpfc_detect_called = 1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

	/* reacquire io_request_lock as the midlayer was holding it when it
	   called us */
	spin_lock(&io_request_lock);
#endif
	return (instance);
}

int
lpfc_DetectInstance(int instance,
		    struct pci_dev *pdev, uint type, Scsi_Host_Template * tmpt)
{

	/* PCI_SUBSYSTEM_IDS supported */
	while ((pdev = pci_find_subsys(PCI_VENDOR_ID_EMULEX, type,
				       PCI_ANY_ID, PCI_ANY_ID, pdev))) {
		if (pci_enable_device(pdev)) {
			continue;
		}
		if (pci_request_regions(pdev, LPFC_DRIVER_NAME)) {
			printk("lpfc pci I/O region is already in use. \n");
			printk
			    ("a driver for lpfc is already loaded on this system\n");
			continue;
		}

		if (lpfc_linux_attach(instance, tmpt, pdev)) {
			pci_release_regions(pdev);
			continue;
		}
		instance++;
	}

	return (instance);
}
#endif				/* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0) */

int
lpfc_diag_init(void)
{
	int result;

	result = register_chrdev(lpfc_major, "lpfcdfc", &lpfc_fops);
	if (result < 0) {
		return (result);
	}
	if (lpfc_major == 0)
		lpfc_major = result;	/* dynamic */

	return (0);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

int
lpfc_release(struct Scsi_Host *host)
{
	elxHBA_t *phba;
	int instance;
	phba = (elxHBA_t *) host->hostdata[0];
	instance = phba->brd_no;

	/*
	 * detach the board 
	 */
	lpfc_linux_detach(instance);

	lpfc_diag_uninit();

	return (0);
}
#endif				/* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0) */

int
lpfc_diag_uninit(void)
{
	if (lpfc_major) {
		unregister_chrdev(lpfc_major, "lpfcdfc");
		lpfc_major = 0;
	}
	return (0);
}

int
lpfc_linux_attach(int instance, Scsi_Host_Template * tmpt, struct pci_dev *pdev)
{
	struct Scsi_Host *host;
	elxHBA_t *phba;
	LINUX_HBA_t *plxhba;
	LPFCHBA_t *plhba;
	LPFC_NODELIST_t *ndlp;
	elxCfgParam_t *clp;
	int rc, i;
	unsigned long iflag;
	uint32_t timeout;
	int lpfc_max_target = 255;

	/*
	 * must have a valid pci_dev
	 */
	if (!pdev)
		return (1);

	/* Allocate memory to manage HBA dma pool.
	 * This next section sets thing up for linux_kmalloc and linux_kfree to work.
	 */
	elx_mem_dmapool[instance] =
	    kmalloc((sizeof (struct elx_mem_pool) * FC_MAX_POOL), GFP_ATOMIC);
	if (elx_mem_dmapool[instance] == 0)
		return (1);
	memset((void *)elx_mem_dmapool[instance], 0,
	       (sizeof (struct elx_mem_pool) * FC_MAX_POOL));
	elx_idx_dmapool[instance] = 0;
	elx_size_dmapool[instance] = FC_MAX_POOL;
	spin_lock_init(&elx_kmem_lock);

	/* 
	 * Allocate space for adapter info structure
	 */
	if (!
	    (phba =
	     (elxHBA_t *) elx_kmem_zalloc(sizeof (elxHBA_t), ELX_MEM_DELAY))) {
		return (1);
	}

	if (!
	    (phba->pHbaProto =
	     (void *)elx_kmem_zalloc(sizeof (LPFCHBA_t), ELX_MEM_DELAY))) {
		elx_kmem_free(phba, sizeof (elxHBA_t));
		return (1);
	}
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	if (!
	    (phba->pHbaOSEnv =
	     (void *)elx_kmem_zalloc(sizeof (LINUX_HBA_t), ELX_MEM_DELAY))) {
		elx_kmem_free(phba->pHbaProto, sizeof (LPFCHBA_t));
		elx_kmem_free(phba, sizeof (elxHBA_t));
		return (1);
	}
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;

	/* Initialize default values for configuration parameters */
	if (!
	    (phba->config =
	     (elxCfgParam_t *) elx_kmem_zalloc(sizeof (lpfc_icfgparam),
					       ELX_MEM_DELAY))) {
		elx_kmem_free(phba->pHbaOSEnv, sizeof (LINUX_HBA_t));
		elx_kmem_free(phba->pHbaProto, sizeof (LPFCHBA_t));
		elx_kmem_free(phba, sizeof (elxHBA_t));
		return (1);
	}
	memcpy(&phba->config[0], (uint8_t *) & lpfc_icfgparam[0],
	       sizeof (lpfc_icfgparam));
	clp = &phba->config[0];
	/*
	 * Set everything to the defaults
	 */
	for (i = 0; i < LPFC_TOTAL_NUM_OF_CFG_PARAM; i++)
		clp[i].a_current = clp[i].a_default;

	elxDRVR.pHba[instance] = phba;
	lpfc_instance[instance] = instance;

	/* Initialize plxhba */
	phba->brd_no = instance;
	phba->pci_id =
	    ((((uint32_t) pdev->device) << 16) | (uint32_t) (pdev->vendor));
	phba->pci_id = SWAP_LONG(phba->pci_id);

	/* Initialize plhba - lpfc specific */
	plhba->fc_nlpmap_start = (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start;
	plhba->fc_nlpmap_end = (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start;
	plhba->fc_nlpunmap_start =
	    (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start;
	plhba->fc_nlpunmap_end = (LPFC_NODELIST_t *) & plhba->fc_nlpunmap_start;
	plhba->fc_plogi_start = (LPFC_NODELIST_t *) & plhba->fc_plogi_start;
	plhba->fc_plogi_end = (LPFC_NODELIST_t *) & plhba->fc_plogi_start;
	plhba->fc_adisc_start = (LPFC_NODELIST_t *) & plhba->fc_adisc_start;
	plhba->fc_adisc_end = (LPFC_NODELIST_t *) & plhba->fc_adisc_start;
	plhba->fc_nlpbind_start = (LPFC_BINDLIST_t *) & plhba->fc_nlpbind_start;
	plhba->fc_nlpbind_end = (LPFC_BINDLIST_t *) & plhba->fc_nlpbind_start;

	/* Initialize plxhba - LINUX specific */
	plxhba->pcidev = pdev;
	init_waitqueue_head(&plxhba->linkevtwq);
	init_waitqueue_head(&plxhba->rscnevtwq);
	init_waitqueue_head(&plxhba->ctevtwq);
	spin_lock_init(&plxhba->iodonelock.elx_lock);

	if ((rc = lpfc_pcimap(phba))) {
		elx_kmem_free(phba->pHbaOSEnv, sizeof (LINUX_HBA_t));
		elx_kmem_free(phba->pHbaProto, sizeof (LPFCHBA_t));
		elx_kmem_free(phba->config, sizeof (lpfc_icfgparam));
		elx_kmem_free(phba, sizeof (elxHBA_t));
		elxDRVR.pHba[instance] = 0;
		return (1);
	}

	if ((rc = lpfc_memmap(phba))) {
		elx_kmem_free(phba->pHbaOSEnv, sizeof (LINUX_HBA_t));
		elx_kmem_free(phba->pHbaProto, sizeof (LPFCHBA_t));
		elx_kmem_free(phba->config, sizeof (lpfc_icfgparam));
		elx_kmem_free(phba, sizeof (elxHBA_t));
		elxDRVR.pHba[instance] = 0;
		return (1);
	}

	lpfc_instcnt++;
	elxDRVR.num_devs++;
	lpfc_config_setup(phba);	/* Setup configuration parameters */

	/*
	 * If the t.o value is not set, set it to 30
	 */
	if (clp[LPFC_CFG_SCSI_REQ_TMO].a_current == 0) {
		clp[LPFC_CFG_SCSI_REQ_TMO].a_current = 30;
	}

	if (clp[LPFC_CFG_DISC_THREADS].a_current) {
		/*
		 * Set to FC_NLP_REQ if automap is set to 0 since order of
		 * discovery does not matter if everything is persistently
		 * bound. 
		 */
		if (clp[LPFC_CFG_AUTOMAP].a_current == 0) {
			clp[LPFC_CFG_DISC_THREADS].a_current =
			    LPFC_MAX_DISC_THREADS;
		}
	}

	if (clp[LPFC_CFG_NETWORK_ON].a_current) {
		/* Setup nodelist entry to be used for IP Broadcasts */
		ndlp = &plhba->fc_nlp_bcast;
		memset(ndlp, 0, sizeof (LPFC_NODELIST_t));
		ndlp->nlp_DID = Bcast_DID;
		ndlp->nlp_portname.nameType = NAME_IEEE;
		ndlp->nlp_portname.IEEE[0] = 0xff;
		ndlp->nlp_portname.IEEE[1] = 0xff;
		ndlp->nlp_portname.IEEE[2] = 0xff;
		ndlp->nlp_portname.IEEE[3] = 0xff;
		ndlp->nlp_portname.IEEE[4] = 0xff;
		ndlp->nlp_portname.IEEE[5] = 0xff;
		ndlp->nle.nlp_failMask = ELX_DEV_LINK_DOWN;
		ndlp->nle.nlp_ip_info = CLASS3;
	}

	/* Initialize all per HBA locks */
	elx_drvr_init_lock(phba);
	elx_sli_init_lock(phba);
	elx_mem_init_lock(phba);
	elx_sch_init_lock(phba);
	elx_ioc_init_lock(phba);
	elx_disc_init_lock(phba);
	elx_hipri_init_lock(phba);	/* init High Priority Queue lock */

	/* Set up the HBA specific LUN device lookup routine */
	phba->elx_tran_find_lun = lpfc_tran_find_lun;

	lpfc_sli_setup(phba);	/* Setup SLI Layer to run over lpfc HBAs */
	elx_sli_setup(phba);	/* Initialize the SLI Layer */
	lpfc_mem_poolinit(phba);

	if (elx_mem_alloc(phba) == 0) {
		lpfc_instcnt--;
		elxDRVR.num_devs--;
		lpfc_unmemmap(phba);
		elx_kmem_free(phba->pHbaOSEnv, sizeof (LINUX_HBA_t));
		elx_kmem_free(phba->pHbaProto, sizeof (LPFCHBA_t));
		elx_kmem_free(phba->config, sizeof (lpfc_icfgparam));
		elx_kmem_free(phba, sizeof (elxHBA_t));
		elxDRVR.pHba[instance] = 0;
		return (1);
	}

	lpfc_bind_setup(phba);	/* Setup binding configuration parameters */

	elx_sched_init_hba(phba, clp[ELX_CFG_DFT_HBA_Q_DEPTH].a_current);

	/* Initialize HBA structure */
	plhba->fc_edtov = FF_DEF_EDTOV;
	plhba->fc_ratov = FF_DEF_RATOV;
	plhba->fc_altov = FF_DEF_ALTOV;
	plhba->fc_arbtov = FF_DEF_ARBTOV;

	/* Set the FARP and XRI timeout values now since they depend on fc_ratov. */
	plhba->fc_ipfarp_timeout = (3 * plhba->fc_ratov);
	plhba->fc_ipxri_timeout = (3 * plhba->fc_ratov);

	/* Initialise the network statistics structure */
	plhba->ip_stat =
	    (void *)elx_kmem_zalloc(sizeof (struct lpip_stats), ELX_MEM_DELAY);
	if (!plhba->ip_stat) {
		lpfc_instcnt--;
		elxDRVR.num_devs--;
		elx_sli_hba_down(phba);	/* Bring down the SLI Layer */
		elx_mem_free(phba);
		lpfc_unmemmap(phba);
		elx_kmem_free(phba->pHbaOSEnv, sizeof (LINUX_HBA_t));
		elx_kmem_free(phba->pHbaProto, sizeof (LPFCHBA_t));
		elx_kmem_free(phba->config, sizeof (lpfc_icfgparam));
		elx_kmem_free(phba, sizeof (elxHBA_t));
		elxDRVR.pHba[instance] = 0;
		return (1);
	}

	ELX_DRVR_LOCK(phba, iflag);

	if ((rc = elx_sli_hba_setup(phba))) {	/* Initialize the HBA */
		lpfc_instcnt--;
		elxDRVR.num_devs--;
		elx_sli_hba_down(phba);	/* Bring down the SLI Layer */
		ELX_DRVR_UNLOCK(phba, iflag);
		elx_mem_free(phba);
		lpfc_unmemmap(phba);
		elx_kmem_free(phba->pHbaOSEnv, sizeof (LINUX_HBA_t));
		elx_kmem_free(phba->pHbaProto, sizeof (LPFCHBA_t));
		elx_kmem_free(phba->config, sizeof (lpfc_icfgparam));
		elx_kmem_free(phba, sizeof (elxHBA_t));
		elxDRVR.pHba[instance] = 0;
		return (1);
	}
	ELX_DRVR_UNLOCK(phba, iflag);

	/* 
	 * Register this board
	 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	host = scsi_register(tmpt, sizeof (unsigned long));
#else
	host = scsi_host_alloc(tmpt, sizeof (unsigned long));
#endif
	plxhba->host = host;

	host->can_queue = clp[ELX_CFG_DFT_HBA_Q_DEPTH].a_current - 10;

	/*
	 * Adjust the number of id's
	 * Although max_id is an int, target id's are unsined chars
	 * Do not exceed 255, otherwise the device scan will wrap around
	 */
	if (clp[LPFC_CFG_MAX_TARGET].a_current > 255) {
		lpfc_max_target = 255;
	} else {
		lpfc_max_target = clp[LPFC_CFG_MAX_TARGET].a_current;
	}
	host->max_id = lpfc_max_target;
	host->max_lun = clp[ELX_CFG_MAX_LUN].a_current;
	host->unique_id = instance;

	/* Adapter ID - tell midlayer not to reserve an ID for us */
	host->this_id = -1;

	/*
	 * Setup the scsi timeout handler
	 */

	/*
	 * timeout value = greater of (2*RATOV, 5)
	 */
	timeout = (plhba->fc_ratov << 1) > 5 ? (plhba->fc_ratov << 1) : 5;
	elx_clk_set(phba, timeout, lpfc_scsi_timeout_handler,
		    (void *)(unsigned long)timeout, 0);

	/*
	 * Setup the ip timeout handler
	 */
	if (clp[LPFC_CFG_NETWORK_ON].a_current) {
		/*
		 * timeout value =  LPFC_IP_TOV
		 */
		timeout = LPFC_IP_TOV / 2;
		elx_clk_set(phba, timeout, lpfc_ip_timeout_handler,
			    (void *)(unsigned long)timeout, 0);
	}

	/*
	 * Starting with 2.4.0 kernel, Linux can support commands longer
	 * than 12 bytes. However, scsi_register() always sets it to 12.
	 * For it to be useful to the midlayer, we have to set it here.
	 */
	host->max_cmd_len = 16;

	/*
	 * Queue depths per lun
	 */
	host->cmd_per_lun = 1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

	host->select_queue_depths = lpfc_select_queue_depth;
#endif

	/*
	 * Save a pointer to device control in host and increment board
	 */
	host->hostdata[0] = (unsigned long)phba;
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,4)) && \
      LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))
	scsi_set_pci_device(host, pdev);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	pci_set_drvdata(pdev, host);
	scsi_add_host(host, &pdev->dev);
	scsi_scan_host(host);
#endif

	return (0);
}

int
lpfc_linux_detach(int instance)
{
	elxHBA_t *phba;
	ELX_SLI_t *psli;
	LINUX_HBA_t *plxhba;
	LPFCHBA_t *plhba;
	MBUF_INFO_t *buf_info;
	MBUF_INFO_t bufinfo;
	unsigned long iflag;

	buf_info = &bufinfo;

	phba = elxDRVR.pHba[instance];
	if (phba == NULL) {
		return (0);
	}
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	psli = &phba->sli;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	scsi_unregister(plxhba->host);
#else
	scsi_remove_host(plxhba->host);
	scsi_host_put(plxhba->host);
#endif

	ELX_DRVR_LOCK(phba, iflag);
	elx_sli_hba_down(phba);	/* Bring down the SLI Layer */
	if (phba->intr_inited) {
		(psli->sliinit.elx_sli_unregister_intr) ((void *)phba);
		phba->intr_inited = 0;
	}

	if (plxhba && plxhba->pcidev) {
		pci_release_regions(plxhba->pcidev);
	}

	lpfc_cleanup(phba, 0);
	lpfc_scsi_free(phba);

	elx_mem_free(phba);

	lpfc_unmemmap(phba);

	ELX_DRVR_UNLOCK(phba, iflag);

	if (phba->pHbaOSEnv)
		elx_kmem_free(phba->pHbaOSEnv, sizeof (LINUX_HBA_t));
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	if (plhba->ip_stat)
		elx_kmem_free(plhba->ip_stat, sizeof (struct lpip_stats));

	if (phba->pHbaProto)
		elx_kmem_free(phba->pHbaProto, sizeof (LPFCHBA_t));
	if (phba->config)
		elx_kmem_free(phba->config, sizeof (lpfc_icfgparam));

	elx_kmem_free(phba, sizeof (elxHBA_t));

	/* Free memory that managed the HBA dma pool.  This next section
	 * frees memory linux_kmalloc and linux_kfree used to work. */
	kfree(elx_mem_dmapool[instance]);

	for (; instance < lpfc_instcnt - 1; instance++) {
		lpfc_instance[instance] = lpfc_instance[instance + 1];
		elxDRVR.pHba[instance] = elxDRVR.pHba[instance + 1];
		elxDRVR.pHba[instance]->brd_no = instance;
		elx_mem_dmapool[instance] = elx_mem_dmapool[instance + 1];
		elx_idx_dmapool[instance] = elx_idx_dmapool[instance + 1];
		elx_size_dmapool[instance] = elx_size_dmapool[instance + 1];
	}

	elxDRVR.pHba[instance] = 0;
	lpfc_instance[instance] = -1;

	lpfc_instcnt--;
	elxDRVR.num_devs--;

	return (0);
}

static char lpfc_addrStr[18];

char *
lpfc_addr_sprintf(register uint8_t * ap)
{
	register int i;
	register char *cp = lpfc_addrStr;
	static char digits[] = "0123456789abcdef";

	for (i = 0; i < 8; i++) {
		*cp++ = digits[*ap >> 4];
		*cp++ = digits[*ap++ & 0xf];
		*cp++ = ':';
	}
	*--cp = 0;
	return (lpfc_addrStr);
}

const char *
lpfc_info(struct Scsi_Host *host)
{
	elxHBA_t *phba;
	LINUX_HBA_t *plxhba;
	LPFCHBA_t *plhba;
	struct pci_dev *pdev;
	elx_vpd_t *vp;
	int idx = 0;

	static char lpfcinfobuf[128];
	const char fmtstring[] =
	    "HBA: Emulex LightPulse LP%s (%d Gigabit) on PCI bus %02x device %02x irq %d";
	struct devName {
		uint32_t pcid;
		char *name;
		int speed;
	} devNameTab[] = {
		{
		PCI_DEVICE_ID_THOR, "10000", 2}, {
		PCI_DEVICE_ID_PEGASUS, "9802", 2}, {
		PCI_DEVICE_ID_CENTAUR, "9000", 1}, {
		PCI_DEVICE_ID_CENTAUR, "9002", 2}, {
		PCI_DEVICE_ID_DRAGONFLY, "8000", 1}, {
		PCI_DEVICE_ID_SUPERFLY, "7000", 1}, {
		PCI_DEVICE_ID_SUPERFLY, "7000E", 1}, {
		PCI_DEVICE_ID_RFLY, "952", 1}, {
		PCI_DEVICE_ID_PFLY, "982", 2}, {
		PCI_DEVICE_ID_TFLY, "1050", 2}, {
		PCI_DEVICE_ID_LP101, "101", 2}, {
		0, ""}
	};

	lpfcinfobuf[0] = '\0';
	phba = (elxHBA_t *) host->hostdata[0];
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	vp = &phba->vpd;
	if (!phba || !plhba || !plxhba)
		return lpfcinfobuf;
	pdev = plxhba->pcidev;

	if (pdev != NULL) {
		while (devNameTab[idx].pcid) {
			if (devNameTab[idx].pcid == pdev->device)
				break;
			idx++;
		}
		if (devNameTab[idx].pcid == PCI_DEVICE_ID_CENTAUR) {
			if (FC_JEDEC_ID(vp->rev.biuRev) == CENTAUR_2G_JEDEC_ID)
				idx++;	/* print 9002 string */
		} else if (devNameTab[idx].pcid == PCI_DEVICE_ID_SUPERFLY) {
			if (!((vp->rev.biuRev >= 1) && (vp->rev.biuRev <= 3)))
				idx++;	/* print 7000E string */
		}
		if (devNameTab[idx].pcid == 0) {
			sprintf(lpfcinfobuf,
				"HBA: Emulex LightPulse on PCI bus %02x device %02x irq %d",
				plxhba->pcidev->bus->number,
				plxhba->pcidev->devfn, plxhba->pcidev->irq);
		} else
			sprintf(lpfcinfobuf,
				fmtstring,
				devNameTab[idx].name,
				devNameTab[idx].speed,
				plxhba->pcidev->bus->number,
				plxhba->pcidev->devfn, plxhba->pcidev->irq);
	}
	return (lpfcinfobuf);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
static int
lpfc_proc_info(char *buf,
	       char **start, off_t offset, int length, int hostnum, int rw)
#else
static int
lpfc_proc_info(struct Scsi_Host *host,
	       char *buf, char **start, off_t offset, int count, int rw)
#endif
{

	elxHBA_t *phba;
	LINUX_HBA_t *plxhba;
	LPFCHBA_t *plhba;
	struct pci_dev *pdev;
	char fwrev[32];
	elx_vpd_t *vp;
	LPFC_NODELIST_t *nlp;
	int idx, i, j, incr;
	char hdw[9];
	int len = 0;

	/* If rw = 0, then read info
	 * If rw = 1, then write info (NYI)
	 */
	if (rw)
		return -EINVAL;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	for (idx = 0; idx < MAX_ELX_BRDS; idx++) {
		phba = elxDRVR.pHba[idx];
		plhba = (LPFCHBA_t *) phba->pHbaProto;
		plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
		if (plxhba->host->host_no == hostnum)
			break;
	}
#else
	phba = (elxHBA_t *) host->hostdata[0];
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	for (idx = 0; idx < MAX_ELX_BRDS; idx++) {
		if (phba == elxDRVR.pHba[idx])
			break;
	}
#endif
	if (idx == MAX_ELX_BRDS)
		return sprintf(buf,
			       "Cannot find adapter for requested host number.\n");

	if (!phba || !plhba || !plxhba)
		return len;	/*len = 0 */

	vp = &phba->vpd;
	pdev = plxhba->pcidev;

	len += sprintf(buf, LPFC_MODULE_DESC "\n");

	len += sprintf(buf + len, lpfc_info(plxhba->host));
	buf[len++] = '\n';

	len += sprintf(buf + len, "SerialNum: %s\n", phba->SerialNumber);

	lpfc_decode_firmware_rev(phba, fwrev, 1);
	len += sprintf(buf + len, "Firmware Version: %s\n", fwrev);

	len += sprintf(buf + len, "Hdw: ");
	/* Convert JEDEC ID to ascii for hardware version */
	incr = vp->rev.biuRev;
	for (i = 0; i < 8; i++) {
		j = (incr & 0xf);
		if (j <= 9)
			hdw[7 - i] = (char)((uint8_t) 0x30 + (uint8_t) j);
		else
			hdw[7 - i] =
			    (char)((uint8_t) 0x61 + (uint8_t) (j - 10));
		incr = (incr >> 4);
	}
	hdw[8] = 0;
	len += sprintf(buf + len, hdw);

	len += sprintf(buf + len, "\nVendorId: 0x%x\n",
		       ((((uint32_t) pdev->device) << 16) | (uint32_t) (pdev->
									vendor)));

	len += sprintf(buf + len, "Portname: ");
	len +=
	    sprintf(buf + len,
		    lpfc_addr_sprintf((uint8_t *) & plhba->fc_portname));

	len += sprintf(buf + len, "   Nodename: ");
	len +=
	    sprintf(buf + len,
		    lpfc_addr_sprintf((uint8_t *) & plhba->fc_nodename));

	switch (phba->hba_state) {
	case ELX_INIT_START:
	case ELX_INIT_MBX_CMDS:
	case ELX_LINK_DOWN:
		len += sprintf(buf + len, "\n\nLink Down\n");
		break;
	case ELX_LINK_UP:
	case ELX_LOCAL_CFG_LINK:
		len += sprintf(buf + len, "\n\nLink Up\n");
		break;
	case ELX_FLOGI:
	case ELX_FABRIC_CFG_LINK:
	case ELX_NS_REG:
	case ELX_NS_QRY:
	case ELX_BUILD_DISC_LIST:
	case ELX_DISC_AUTH:
	case ELX_CLEAR_LA:
		len += sprintf(buf + len, "\n\nLink Up - Discovery\n");
		break;
	case ELX_HBA_READY:
		len += sprintf(buf + len, "\n\nLink Up - Ready:\n");
		len += sprintf(buf + len, "   PortID 0x%x\n", plhba->fc_myDID);
		if (plhba->fc_topology == TOPOLOGY_LOOP) {
			if (plhba->fc_flag & FC_PUBLIC_LOOP)
				len += sprintf(buf + len, "   Public Loop\n");
			else
				len += sprintf(buf + len, "   Private Loop\n");
		} else {
			if (plhba->fc_flag & FC_FABRIC)
				len += sprintf(buf + len, "   Fabric\n");
			else
				len += sprintf(buf + len, "   Point-2-Point\n");
		}

		if (plhba->fc_linkspeed == LA_2GHZ_LINK)
			len += sprintf(buf + len, "   Current speed 2G\n");
		else
			len += sprintf(buf + len, "   Current speed 1G\n");

		nlp = plhba->fc_nlpmap_start;
		while (nlp != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start) {
			if (nlp->nlp_state == NLP_STE_MAPPED_NODE) {
				len +=
				    sprintf(buf + len,
					    "\nlpfc%dt%02x DID %06x WWPN ", idx,
					    FC_SCSID(nlp->nlp_pan,
						     nlp->nlp_sid),
					    nlp->nlp_DID);
				len +=
				    sprintf(buf + len,
					    lpfc_addr_sprintf((uint8_t *) &
							      nlp->
							      nlp_portname));
				len += sprintf(buf + len, " WWNN ");
				len +=
				    sprintf(buf + len,
					    lpfc_addr_sprintf((uint8_t *) &
							      nlp->
							      nlp_nodename));
			}
			if ((4096 - len) < 90)
				break;
			nlp = (LPFC_NODELIST_t *) nlp->nle.nlp_listp_next;
		}
		if (nlp != (LPFC_NODELIST_t *) & plhba->fc_nlpmap_start)
			len += sprintf(buf, "\n....");
		buf[len++] = '\n';
	}
	return (len);
}

uint32_t
lpfc_register_intr(elxHBA_t * arg)
{
	elxHBA_t *phba;
	struct pci_dev *pdev;
	LINUX_HBA_t *plxhba;
	unsigned long cflag;

	phba = (elxHBA_t *) arg;

	/*
	 * Get PCI for this board
	 */
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	pdev = plxhba->pcidev;
	if (!pdev)
		return (1);

	/* ihs->handler is not used here, instead we use our handler to call the
	   common base handler
	 */
	if (request_irq(pdev->irq, lpfc_intr_handler, SA_INTERRUPT | SA_SHIRQ,
			"lpfcdd", (void *)phba))
		return (2);

	ELX_CLK_LOCK(cflag);
	if (lpfc_clkCnt == 0) {
		/* Clock lock should already be inited */
		elx_clock_init();
		/* 
		 * add our timer routine to kernel's list
		 */
		lpfc_sec_clk.expires = (uint32_t) (HZ + jiffies);
		lpfc_sec_clk.function = lpfc_local_timeout;
		lpfc_sec_clk.data = (unsigned long)(&elxDRVR.elx_clock_info);
		init_timer(&lpfc_sec_clk);
		add_timer(&lpfc_sec_clk);
	}
	lpfc_clkCnt++;
	ELX_CLK_UNLOCK(cflag);

	return (0);
}

void
lpfc_unregister_intr(elxHBA_t * phba)
{
	struct pci_dev *pdev;
	LINUX_HBA_t *plxhba;
	ELX_SLI_t *psli;
	ELXCLOCK_t *x, *nextx;
	ELXCLOCK_INFO_t *clock_info;
	uint32_t ha_copy;
	unsigned long cflag;

	clock_info = &elxDRVR.elx_clock_info;
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	/*
	 * Get PCI for this board
	 */
	pdev = plxhba->pcidev;
	if (!pdev)
		return;

	psli = &phba->sli;
	/* Clear all interrupt enable conditions */
	(psli->sliinit.elx_sli_write_HC) (phba, 0);
	/* Clear all pending interrupts */
	ha_copy = readl(plxhba->HAregaddr);
	writel(ha_copy, plxhba->HAregaddr);

	free_irq(pdev->irq, phba);
	phba->intr_inited = 0;

	ELX_CLK_LOCK(cflag);
	if (lpfc_clkCnt == 1) {
		del_timer(&lpfc_sec_clk);
	}
	lpfc_clkCnt--;
	/* Go thru clock list a free any timers waiting to expire */
	x = (ELXCLOCK_t *) clock_info->elx_clkhdr.q_f;
	while (x != (ELXCLOCK_t *) & clock_info->elx_clkhdr) {
		nextx = x->cl_fw;
		if (phba == x->cl_phba) {
			/* Deque expired clock */
			elx_deque(x);
			/* Decrement count of unexpired clocks */
			clock_info->elx_clkhdr.q_cnt--;
			elx_mem_put(phba, MEM_CLOCK, (uint8_t *) x);
		}
		x = nextx;
	}
	ELX_CLK_UNLOCK(cflag);
	return;
}

void
lpfc_local_timeout(unsigned long data)
{
	elxHBA_t *phba;
	LINUX_HBA_t *plxhba;
	Scsi_Cmnd *cmnd, *next_cmnd;
	struct Scsi_Host *host;
	uint32_t i;
	unsigned long cflag;
	unsigned long iflag;
	unsigned long flag;
	unsigned long sflag;

	cflag = 0;
	ELX_CLK_LOCK(cflag);
	if (lpfc_clkCnt == 0) {
		del_timer(&lpfc_sec_clk);
		ELX_CLK_UNLOCK(cflag);
		return;
	}
	lpfc_tmr |= (uint32_t) (1 << smp_processor_id());
	ELX_CLK_UNLOCK(cflag);
	elx_timer(0);
	ELX_CLK_LOCK(cflag);
	/* Reset the 1 sec tick */
	lpfc_sec_clk.expires = (uint32_t) (HZ + jiffies);
	lpfc_sec_clk.function = lpfc_local_timeout;
	lpfc_sec_clk.data = (unsigned long)(&elxDRVR.elx_clock_info);
	init_timer(&lpfc_sec_clk);
	add_timer(&lpfc_sec_clk);
	ELX_CLK_UNLOCK(cflag);

	for (i = 0; i < lpfc_instcnt; i++) {
		if (elxDRVR.pHba[i]) {
			phba = elxDRVR.pHba[i];
			ELX_DRVR_LOCK(phba, iflag);
			plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
			host = plxhba->host;
			spin_lock_irqsave(&plxhba->iodonelock.elx_lock, flag);
			/* Flush all done commands for this hba */
			if (plxhba->iodone.q_first) {
				cmnd = (Scsi_Cmnd *) plxhba->iodone.q_first;
				plxhba->iodone.q_first = 0;
				plxhba->iodone.q_last = 0;
				plxhba->iodone.q_cnt = 0;
				spin_unlock_irqrestore(&plxhba->iodonelock.
						       elx_lock, flag);
				while (cmnd) {
					next_cmnd =
					    (Scsi_Cmnd *) cmnd->host_scribble;
					cmnd->host_scribble = 0;
					ELX_DRVR_UNLOCK(phba, iflag);
					LPFC_LOCK_SCSI_DONE;

					/* Give this command back to the OS */
					elx_scsi_add_timer(cmnd,
							   cmnd->
							   timeout_per_command);
					atomic_dec(&plxhba->cmnds_in_flight);
					cmnd->scsi_done(cmnd);

					LPFC_UNLOCK_SCSI_DONE;
					ELX_DRVR_LOCK(phba, iflag);
					cmnd = next_cmnd;
				}
			} else {
				spin_unlock_irqrestore(&plxhba->iodonelock.
						       elx_lock, flag);
			}
			ELX_DRVR_UNLOCK(phba, iflag);
		}
	}
	lpfc_tmr &= ~((uint32_t) (1 << smp_processor_id()));

	return;
}

int
lpfc_reset_bus_handler(Scsi_Cmnd * cmnd)
{
	elxHBA_t *phba;
	ELX_SCSI_BUF_t *elx_cmd;
	unsigned long iflag;
	int rc, tgt, lun;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

	/* release io_request_lock */
	spin_unlock_irq(&io_request_lock);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	phba = (elxHBA_t *) cmnd->device->host->hostdata[0];
	tgt = cmnd->device->id;
	lun = cmnd->device->lun;
#else
	phba = (elxHBA_t *) cmnd->host->hostdata[0];
	tgt = cmnd->target;
	lun = cmnd->lun;
#endif
	ELX_DRVR_LOCK(phba, iflag);

	rc = 0;
	if ((elx_cmd = elx_get_scsi_buf(phba))) {
		rc = lpfc_scsi_hba_reset(phba, elx_cmd);
		elx_free_scsi_buf(elx_cmd);
	}

	/* SCSI layer issued Bus Reset */
	elx_printf_log(phba->brd_no, &elx_msgBlk0714,	/* ptr to msg structure */
		       elx_mes0714,	/* ptr to msg */
		       elx_msgBlk0714.msgPreambleStr,	/* begin varargs */
		       tgt, lun, rc);	/* end varargs */

	ELX_DRVR_UNLOCK(phba, iflag);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

	/* reacquire io_request_lock for midlayer */
	spin_lock_irq(&io_request_lock);
#endif

	return (SUCCESS);

}				/* lpfc_reset_bus_handler */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

void
lpfc_select_queue_depth(struct Scsi_Host *host, Scsi_Device * scsi_devs)
{
	Scsi_Device *device;
	elxHBA_t *phba;

	phba = (elxHBA_t *) host->hostdata[0];
	for (device = scsi_devs; device != NULL; device = device->next) {
		if (device->host == host)
			lpfc_device_queue_depth(phba, device);
	}
}
#else
int
lpfc_slave_configure(Scsi_Device * scsi_devs)
{
	elxHBA_t *phba;
	phba = (elxHBA_t *) scsi_devs->host->hostdata[0];
	lpfc_device_queue_depth(phba, scsi_devs);
	return 0;
}
#endif

int
lpfc_device_queue_depth(elxHBA_t * phba, Scsi_Device * device)
{
	elxCfgParam_t *clp;

	clp = &phba->config[0];

	if (device->tagged_supported) {
#if LINUX_VERSION_CODE 	< KERNEL_VERSION(2,6,0)
		device->tagged_queue = 1;
#endif
		device->current_tag = 0;
		device->queue_depth = clp[ELX_CFG_DFT_LUN_Q_DEPTH].a_current;
	} else {
		device->queue_depth = 16;
	}
	return (device->queue_depth);
}

int
lpfcdiag_ioctl(struct inode *inode,
	       struct file *file, unsigned int cmd, unsigned long arg)
{
	int rc, fd;
	elxHBA_t *phba;
	ELXCMDINPUT_t *ci;
	unsigned long iflag;

	if (!arg)
		return (-EINVAL);

	ci = (ELXCMDINPUT_t *) kmalloc(sizeof (ELXCMDINPUT_t), GFP_ATOMIC);

	if (!ci)
		return (-ENOMEM);

	if (copy_from_user
	    ((uint8_t *) ci, (uint8_t *) arg, sizeof (ELXCMDINPUT_t))) {
		kfree(ci);
		return (-EIO);
	}

	fd = ci->elx_brd;
	if (fd >= lpfc_instcnt) {
		kfree(ci);
		return (-EINVAL);
	}
	if (!(phba = elxDRVR.pHba[fd])) {
		kfree(ci);
		return (-EINVAL);
	}

	/*
	 * call common base ioctl
	 */
	ELX_DRVR_LOCK(phba, iflag);
	rc = lpfc_diag_ioctl(phba, ci);
	ELX_DRVR_UNLOCK(phba, iflag);
	kfree(ci);
	return (-rc);
}

int
lpfcdiag_open(struct inode *inode, struct file *file)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	MOD_INC_USE_COUNT;
#else
	if (!try_module_get(THIS_MODULE)) {
		return (-ENODEV);
	}
#endif
	return (0);
}

int
lpfcdiag_release(struct inode *inode, struct file *file)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	MOD_DEC_USE_COUNT;
#else
	module_put(THIS_MODULE);
#endif
	return (0);
}

int
lpfc_memmap(elxHBA_t * phba)
{
	LINUX_HBA_t *plxhba;
	struct pci_dev *pdev;
	ELX_SLI_t *psli;
	int reg;
	unsigned long base;
	unsigned long bar0map, bar1map;

	/*
	 * Get PCI for board
	 */
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	pdev = plxhba->pcidev;
	if (!pdev) {
		return (1);
	}
	psli = &phba->sli;

	/* Configure DMA attributes. */
	if (pci_set_dma_mask(pdev, (uint64_t) 0xffffffffffffffffULL)) {
		if (pci_set_dma_mask(pdev, (uint64_t) 0xffffffff)) {
			return (1);
		}
	}

	/*
	 * address in first register
	 */
	reg = 0;
	reg = elx_pci_getadd(pdev, reg, &base);

	/*
	 * Mask the value to get the physical address
	 */
	base &= PCI_BASE_ADDRESS_MEM_MASK;
	bar0map = base;

	reg = elx_pci_getadd(pdev, reg, &base);
	base &= PCI_BASE_ADDRESS_MEM_MASK;
	bar1map = base;

	/* 
	 * Map adapter SLIM and Control Registers
	 */
	plxhba->pci_bar0_map =
	    elx_remap_pci_mem((unsigned long)bar0map, FF_SLIM_SIZE);
	if (plxhba->pci_bar0_map == ((void *)(-1))) {
		return (1);
	}

	plxhba->pci_bar1_map =
	    elx_remap_pci_mem((unsigned long)bar1map, FF_REG_AREA_SIZE);
	if (plxhba->pci_bar1_map == ((void *)(-1))) {
		elx_unmap_pci_mem((unsigned long)plxhba->pci_bar0_map);
		return (1);
	}

	/*
	 * Setup SLI2 interface
	 */
	if (phba->slim2p.virt == 0) {
		MBUF_INFO_t *buf_info;
		MBUF_INFO_t bufinfo;

		buf_info = &bufinfo;

		/*
		 * Allocate memory for SLI-2 structures
		 */
		buf_info->size = sizeof (SLI2_SLIM_t);

		buf_info->flags = ELX_MBUF_DMA;
		buf_info->align = PAGE_SIZE;
		buf_info->dma_handle = 0;
		buf_info->data_handle = 0;
		elx_malloc(phba, buf_info);
		if (buf_info->virt == NULL) {
			/*
			 * unmap adapter SLIM and Control Registers
			 */
			elx_unmap_pci_mem((unsigned long)plxhba->pci_bar1_map);
			elx_unmap_pci_mem((unsigned long)plxhba->pci_bar0_map);

			return (1);
		}

		phba->slim2p.virt = (uint8_t *) buf_info->virt;
		phba->slim2p.phys = buf_info->phys;
		phba->slim2p.data_handle = buf_info->data_handle;
		phba->slim2p.dma_handle = buf_info->dma_handle;
		/* The SLIM2 size is stored in the next field */
		phba->slim2p.next = (void *)(unsigned long)buf_info->size;
		memset((char *)phba->slim2p.virt, 0, sizeof (SLI2_SLIM_t));
	}
	return (0);
}

int
lpfc_unmemmap(elxHBA_t * phba)
{
	ELX_SLI_t *psli;
	LINUX_HBA_t *plxhba;

	psli = &phba->sli;
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	/* 
	 * unmap adapter SLIM and Control Registers
	 */
	elx_unmap_pci_mem((unsigned long)plxhba->pci_bar1_map);
	elx_unmap_pci_mem((unsigned long)plxhba->pci_bar0_map);

	/*
	 * Free resources associated with SLI2 interface
	 */
	if (phba->slim2p.virt) {
		MBUF_INFO_t *buf_info;
		MBUF_INFO_t bufinfo;

		buf_info = &bufinfo;
		buf_info->phys = phba->slim2p.phys;
		buf_info->data_handle = phba->slim2p.data_handle;
		buf_info->dma_handle = phba->slim2p.dma_handle;
		buf_info->flags = ELX_MBUF_DMA;

		buf_info->virt = (uint32_t *) phba->slim2p.virt;
		buf_info->size = (uint32_t) (unsigned long)phba->slim2p.next;
		elx_free(phba, buf_info);
	}
	return (0);
}

int
lpfc_pcimap(elxHBA_t * phba)
{
	LINUX_HBA_t *plxhba;
	struct pci_dev *pdev;
	uint16_t cmd;

	/*
	 * PCI for board
	 */
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	pdev = plxhba->pcidev;
	if (!pdev)
		return (1);

	/*
	 * bus mastering and parity checking enabled
	 */
	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	if (cmd & CMD_PARITY_CHK)
		cmd = CMD_CFG_VALUE;
	else
		cmd = (CMD_CFG_VALUE & ~(CMD_PARITY_CHK));

	pci_write_config_word(pdev, PCI_COMMAND, cmd);

	if (lpfc_pci_latency_clocks)
		pci_write_config_byte(pdev, PCI_LATENCY_TMR_REGISTER,
				      (uint8_t) lpfc_pci_latency_clocks);

	if (lpfc_pci_cache_line)
		pci_write_config_byte(pdev, PCI_CACHE_LINE_REGISTER,
				      (uint8_t) lpfc_pci_cache_line);

	/*
	 * Get the irq from the pdev structure
	 */
	phba->bus_intr_lvl = (int)pdev->irq;

	return (0);
}

void
lpfc_setup_slim_access(elxHBA_t * arg)
{
	LINUX_HBA_t *plxhba;
	elxHBA_t *phba;

	phba = (elxHBA_t *) arg;
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	plxhba->MBslimaddr = plxhba->pci_bar0_map;
	plxhba->HAregaddr = (uint32_t *) (plxhba->pci_bar1_map) + HA_REG_OFFSET;
	plxhba->HCregaddr = (uint32_t *) (plxhba->pci_bar1_map) + HC_REG_OFFSET;
	plxhba->CAregaddr = (uint32_t *) (plxhba->pci_bar1_map) + CA_REG_OFFSET;
	plxhba->HSregaddr = (uint32_t *) (plxhba->pci_bar1_map) + HS_REG_OFFSET;
	return;
}

uint32_t
lpfc_read_HA(elxHBA_t * arg)
{
	uint32_t status;
	LINUX_HBA_t *plxhba;
	elxHBA_t *phba;

	phba = (elxHBA_t *) arg;
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	status = readl(plxhba->HAregaddr);
	return (status);
}

uint32_t
lpfc_read_CA(elxHBA_t * arg)
{
	uint32_t status;
	LINUX_HBA_t *plxhba;
	elxHBA_t *phba;

	phba = (elxHBA_t *) arg;
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	status = readl(plxhba->CAregaddr);
	return (status);
}

uint32_t
lpfc_read_hbaregs_plus_offset(elxHBA_t * arg, uint32_t offset)
{
	uint32_t status;
	LINUX_HBA_t *plxhba;
	elxHBA_t *phba;

	phba = (elxHBA_t *) arg;
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;

	status = readl((plxhba->pci_bar1_map) + offset);
	return (status);
}

uint32_t
lpfc_read_HS(elxHBA_t * arg)
{
	uint32_t status;
	LINUX_HBA_t *plxhba;
	elxHBA_t *phba;

	phba = (elxHBA_t *) arg;
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	status = readl(plxhba->HSregaddr);
	return (status);
}

uint32_t
lpfc_read_HC(elxHBA_t * arg)
{
	uint32_t status;
	LINUX_HBA_t *plxhba;
	elxHBA_t *phba;

	phba = (elxHBA_t *) arg;
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;

	status = readl(plxhba->HCregaddr);
	return (status);
}

void
lpfc_write_HA(elxHBA_t * phba, uint32_t value)
{
	LINUX_HBA_t *plxhba;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	writel(value, plxhba->HAregaddr);
	return;
}

void
lpfc_write_CA(elxHBA_t * phba, uint32_t value)
{
	LINUX_HBA_t *plxhba;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	writel(value, plxhba->CAregaddr);
	return;
}

void
lpfc_write_hbaregs_plus_offset(elxHBA_t * arg, uint32_t offset, uint32_t value)
{
	LINUX_HBA_t *plxhba;
	elxHBA_t *phba;

	phba = (elxHBA_t *) arg;
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	writel(value, (plxhba->pci_bar1_map) + offset);
}

void
lpfc_write_HS(elxHBA_t * phba, uint32_t value)
{
	LINUX_HBA_t *plxhba;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	writel(value, plxhba->HSregaddr);
	return;
}

void
lpfc_write_HC(elxHBA_t * phba, uint32_t value)
{
	LINUX_HBA_t *plxhba;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	writel(value, plxhba->HCregaddr);
	return;
}

uint32_t
lpfc_intr_prep(elxHBA_t * phba)
{
	LINUX_HBA_t *plxhba;
	ELX_SLI_t *psli;
	uint32_t ha_copy;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	/* Ignore all interrupts during initialization. */
	if (phba->hba_state < ELX_LINK_DOWN) {
		return (0);
	}

	psli = &phba->sli;
	/* Read host attention register to determine interrupt source */
	ha_copy = readl(plxhba->HAregaddr);

	/* Clear Attention Sources, except ERATT (to preserve status) & LATT
	 *    (ha_copy & ~(HA_ERATT | HA_LATT));
	 */
	writel((ha_copy & ~(HA_LATT | HA_ERATT)), plxhba->HAregaddr);
	return (ha_copy);
}				/* lpfc_intr_prep */

void
lpfc_intr_post(elxHBA_t * arg)
{
	elxHBA_t *phba;

	phba = (elxHBA_t *) arg;
	return;
}

int
lpfc_ValidLun(ELXSCSITARGET_t * targetp, uint64_t lun)
{
	uint32_t rptLunLen;
	uint32_t *datap32;
	uint32_t lunvalue, i;

	if (targetp->rptLunState != REPORT_LUN_COMPLETE) {
		return 1;
	}

	if (targetp->RptLunData) {
		datap32 = (uint32_t *) targetp->RptLunData->virt;
		rptLunLen = SWAP_DATA(*datap32);
		for (i = 0; i < rptLunLen; i += 8) {
			datap32 += 2;
			lunvalue = (((*datap32) >> FC_LUN_SHIFT) & 0xff);
			if (lunvalue == (uint32_t) lun)
				return 1;
		}
		return 0;
	} else {
		return 1;
	}
}

void
lpfc_write_slim(elxHBA_t * phba, uint8_t * ptr, int offset, int cnt)
{
	LINUX_HBA_t *plxhba;
	uint32_t *slimp;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	slimp = (uint32_t *) (((uint8_t *) plxhba->MBslimaddr) + offset);

	/* Write cnt bytes to SLIM address pointed to by slimp */
	elx_write_toio((uint32_t *) ptr, slimp, cnt);
	return;
}

void
lpfc_read_slim(elxHBA_t * phba, uint8_t * ptr, int offset, int cnt)
{
	uint32_t *slimp;
	LINUX_HBA_t *plxhba;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	slimp = (uint32_t *) (((uint8_t *) plxhba->MBslimaddr) + offset);

	/* Read cnt bytes from SLIM address pointed to by slimp */
	elx_read_fromio(slimp, (uint32_t *) ptr, cnt);
	return;
}

void
elx_nodev_unsol_event(elxHBA_t * phba,
		      ELX_SLI_RING_t * pring, ELX_IOCBQ_t * piocbq)
{
	return;
}

void
lpfc_sli_brdreset(elxHBA_t * arg)
{
	LPFCHBA_t *plhba;
	elxHBA_t *phba;

	phba = (elxHBA_t *) arg;
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	plhba->fc_eventTag = 0;
	plhba->fc_myDID = 0;
	plhba->fc_prevDID = 0;
	plhba->power_up = 0;	/* Pegasus */
}

int
lpfc_sli_setup(elxHBA_t * phba)
{
	int i, totiocb;
	ELX_SLI_t *psli;
	ELX_RING_INIT_t *pring;
	elxCfgParam_t *clp;

	psli = &phba->sli;
	psli->sliinit.num_rings = MAX_CONFIGURED_RINGS;
	psli->fcp_ring = LPFC_FCP_RING;
	psli->next_ring = LPFC_FCP_NEXT_RING;
	psli->ip_ring = LPFC_IP_RING;

	clp = &phba->config[0];

	totiocb = 0;
	for (i = 0; i < psli->sliinit.num_rings; i++) {
		pring = &psli->sliinit.ringinit[i];
		switch (i) {
		case LPFC_ELS_RING:	/* ring 0 - ELS / CT */
			/* numCiocb and numRiocb are used in config_port */
			pring->numCiocb = SLI2_IOCB_CMD_R0_ENTRIES;
			pring->numRiocb = SLI2_IOCB_RSP_R0_ENTRIES;
			pring->fast_iotag = 0;
			pring->iotag_ctr = 0;
			pring->iotag_max = 4096;
			pring->num_mask = 4;
			pring->prt[0].profile = 0;	/* Mask 0 */
			pring->prt[0].rctl = FC_ELS_REQ;
			pring->prt[0].type = FC_ELS_DATA;
			pring->prt[0].elx_sli_rcv_unsol_event =
			    lpfc_els_unsol_event;
			pring->prt[1].profile = 0;	/* Mask 1 */
			pring->prt[1].rctl = FC_ELS_RSP;
			pring->prt[1].type = FC_ELS_DATA;
			pring->prt[1].elx_sli_rcv_unsol_event =
			    lpfc_els_unsol_event;
			pring->prt[2].profile = 0;	/* Mask 2 */
			pring->prt[2].rctl = FC_UNSOL_CTL;	/* NameServer Inquiry */
			pring->prt[2].type = FC_COMMON_TRANSPORT_ULP;	/* NameServer */
			pring->prt[2].elx_sli_rcv_unsol_event =
			    lpfc_ct_unsol_event;
			pring->prt[3].profile = 0;	/* Mask 3 */
			pring->prt[3].rctl = FC_SOL_CTL;	/* NameServer response */
			pring->prt[3].type = FC_COMMON_TRANSPORT_ULP;	/* NameServer */
			pring->prt[3].elx_sli_rcv_unsol_event =
			    lpfc_ct_unsol_event;
			break;
		case LPFC_IP_RING:	/* ring 1 - IP */
			/* numCiocb and numRiocb are used in config_port */
			pring->numCiocb = SLI2_IOCB_CMD_R1_ENTRIES;
			pring->numRiocb = SLI2_IOCB_CMD_R1_ENTRIES;
			if (clp[LPFC_CFG_NETWORK_ON].a_current) {
				pring->numCiocb += SLI2_IOCB_CMD_R1XTRA_ENTRIES;
				pring->numRiocb += SLI2_IOCB_CMD_R1XTRA_ENTRIES;
			}
			pring->iotag_ctr = 0;
			pring->iotag_max = clp[LPFC_CFG_XMT_Q_SIZE].a_current;
			pring->fast_iotag = 0;
			pring->num_mask = 1;
			pring->prt[0].profile = 0;	/* Mask 0 */
			pring->prt[0].rctl = FC_UNSOL_DATA;	/* Unsolicited Data */
			if (clp[LPFC_CFG_NETWORK_ON].a_current) {
				pring->prt[0].type = FC_LLC_SNAP;	/* LLC/SNAP */
			} else {
				pring->prt[0].type = 0;
			}
			pring->prt[0].elx_sli_rcv_unsol_event =
			    lpfc_ip_unsol_event;
			break;
		case LPFC_FCP_RING:	/* ring 2 - FCP */
			/* numCiocb and numRiocb are used in config_port */
			pring->numCiocb = SLI2_IOCB_CMD_R2_ENTRIES;
			pring->numRiocb = SLI2_IOCB_CMD_R2_ENTRIES;
			if (clp[LPFC_CFG_NETWORK_ON].a_current == 0) {
				pring->numCiocb += SLI2_IOCB_CMD_R1XTRA_ENTRIES;
				pring->numRiocb += SLI2_IOCB_CMD_R1XTRA_ENTRIES;
			}
			pring->numCiocb += SLI2_IOCB_CMD_R3XTRA_ENTRIES;
			pring->numRiocb += SLI2_IOCB_CMD_R3XTRA_ENTRIES;
			pring->iotag_ctr = 0;
			pring->iotag_max =
			    (clp[ELX_CFG_DFT_HBA_Q_DEPTH].a_current * 2);
			pring->fast_iotag = pring->iotag_max;
			pring->num_mask = 0;
			break;
		case LPFC_FCP_NEXT_RING:
			/* numCiocb and numRiocb are used in config_port */
			pring->numCiocb = SLI2_IOCB_CMD_R3_ENTRIES;
			pring->numRiocb = SLI2_IOCB_CMD_R3_ENTRIES;
			pring->fast_iotag = 0;
			pring->iotag_ctr = 0;
			pring->iotag_max = 4096;
			pring->num_mask = 0;
			pring->prt[0].profile = 0;	/* Mask 0 */
			pring->prt[0].rctl = FC_FCP_CMND;
			pring->prt[0].type = FC_FCP_DATA;
			pring->prt[0].elx_sli_rcv_unsol_event =
			    elx_nodev_unsol_event;
			break;
		}
		totiocb += (pring->numCiocb + pring->numRiocb);
	}
	if (totiocb > MAX_SLI2_IOCB) {
		/* Too many cmd / rsp ring entries in SLI2 SLIM */
		elx_printf_log(phba->brd_no, &elx_msgBlk0462,	/* ptr to msg structure */
			       elx_mes0462,	/* ptr to msg */
			       elx_msgBlk0462.msgPreambleStr,	/* begin varargs */
			       totiocb, MAX_SLI2_IOCB);	/* end varargs */
	}
	psli->sliinit.elx_sli_handle_eratt = lpfc_handle_eratt;
	psli->sliinit.elx_sli_handle_latt = lpfc_handle_latt;
	psli->sliinit.elx_sli_intr_post = lpfc_intr_post;
	psli->sliinit.elx_sli_intr_prep = lpfc_intr_prep;
	psli->sliinit.elx_sli_read_pci = elx_read_pci;
	psli->sliinit.elx_sli_read_pci_cmd = elx_read_pci_cmd;
	psli->sliinit.elx_sli_write_pci_cmd = elx_write_pci_cmd;
	psli->sliinit.elx_sli_read_slim = lpfc_read_slim;
	psli->sliinit.elx_sli_write_slim = lpfc_write_slim;
	psli->sliinit.elx_sli_config_port_prep = lpfc_config_port_prep;
	psli->sliinit.elx_sli_config_port_post = lpfc_config_port_post;
	psli->sliinit.elx_sli_config_pcb_setup = lpfc_config_pcb_setup;
	psli->sliinit.elx_sli_write_HA = lpfc_write_HA;
	psli->sliinit.elx_sli_write_CA = lpfc_write_CA;
	psli->sliinit.elx_sli_write_HS = lpfc_write_HS;
	psli->sliinit.elx_sli_write_HC = lpfc_write_HC;
	psli->sliinit.elx_sli_read_HA = lpfc_read_HA;
	psli->sliinit.elx_sli_read_CA = lpfc_read_CA;
	psli->sliinit.elx_sli_read_HS = lpfc_read_HS;
	psli->sliinit.elx_sli_read_HC = lpfc_read_HC;
	psli->sliinit.elx_sli_setup_slim_access = lpfc_setup_slim_access;
	psli->sliinit.elx_sli_register_intr = lpfc_register_intr;
	psli->sliinit.elx_sli_unregister_intr = lpfc_unregister_intr;
	psli->sliinit.elx_sli_brdreset = lpfc_sli_brdreset;
	psli->sliinit.elx_sli_hba_down_prep = lpfc_hba_down_prep;
#ifdef powerpc
	psli->sliinit.sli_flag = ELX_HGP_HOSTSLIM;
#else
	psli->sliinit.sli_flag = 0;
#endif
	return (0);
}

int
lpfc_mem_poolinit(elxHBA_t * phba)
{
	MEMSEG_t *mp;
	elxCfgParam_t *clp;

	clp = &phba->config[0];
	/* Initialize xmit/receive buffer structure */
	/* Three buffers per response entry will initially be posted to ELS ring */
	/* Pool 0: MEM_BUF is same pool as MEM_BPL */
	mp = &phba->memseg[MEM_BUF];
	mp->elx_memflag = ELX_MEM_DMA | ELX_MEM_GETMORE;
	mp->elx_memsize = 1024;
	mp->mem_hdr.q_max = MAX_SLI2_IOCB;
	mp->elx_lowmem = 0;
	mp->elx_himem = 0;

	/* Pool 1: MEM_MBOX */
	/* Initialize mailbox cmd buffer structure */
	mp = &phba->memseg[MEM_MBOX];
	mp->elx_memflag = ELX_MEM_GETMORE | ELX_MEM_BOUND;
	mp->elx_memsize = sizeof (ELX_MBOXQ_t);
	mp->mem_hdr.q_max = lpfc_discovery_threads + 8;
	mp->elx_lowmem = 8;
	mp->elx_himem = LPFC_MAX_DISC_THREADS;

	/* Pool 2: MEM_IOCB */
	/* Initialize iocb buffer structure */
	mp = &phba->memseg[MEM_IOCB];
	mp->elx_memsize = sizeof (ELX_IOCBQ_t);
	mp->elx_memflag = ELX_MEM_GETMORE | ELX_MEM_BOUND;
	mp->mem_hdr.q_max = (uint16_t) clp[ELX_CFG_NUM_IOCBS].a_current;
	mp->elx_lowmem = (2 * lpfc_discovery_threads) + 8;
	mp->elx_himem = LPFC_MAX_NUM_IOCBS;

	/* Pool 3: MEM_CLOCK */
	/* Initialize clock buffer structure */
	mp = &phba->memseg[MEM_CLOCK];
	mp->elx_memflag = ELX_MEM_GETMORE;
	mp->elx_memsize = sizeof (ELXCLOCK_t);
	mp->mem_hdr.q_max = 64;
	mp->elx_lowmem = 0;
	mp->elx_himem = 0;

	/* Pool 4: MEM_SCSI_BUF */
	/* Initialize SCSI buffer structure */
	mp = &phba->memseg[MEM_SCSI_BUF];
	mp->elx_memflag = ELX_MEM_GETMORE | ELX_MEM_BOUND;
	mp->elx_memsize = sizeof (ELX_SCSI_BUF_t);
	mp->mem_hdr.q_max = clp[ELX_CFG_NUM_BUFS].a_current;
	mp->elx_lowmem = 0;
	mp->elx_himem = LPFC_MAX_NUM_BUFS;

	/* Pool 5: MEM_FCP_CMND_BUF */
	/* Initialize FCP_CMND buffer structure */
	mp = &phba->memseg[MEM_FCP_CMND_BUF];
	mp->elx_memflag = ELX_MEM_DMA;
	mp->elx_memsize = sizeof (FCP_CMND);
	mp->mem_hdr.q_max = 0;
	mp->elx_lowmem = 0;
	mp->elx_himem = 0;

	/* Pool 6: MEM_NLP */
	/* Initialize nodelist buffer structure */
	mp = &phba->memseg[MEM_NLP];
	mp->elx_memflag = ELX_MEM_GETMORE;
	mp->elx_memsize = sizeof (LPFC_NODELIST_t);
	mp->mem_hdr.q_max = lpfc_num_nodes;
	mp->elx_lowmem = 0;
	mp->elx_himem = 0;

	/* Pool 7: MEM_BIND */
	/* Initialize bindlist buffer structure */
	mp = &phba->memseg[MEM_BIND];
	mp->elx_memflag = ELX_MEM_GETMORE;
	mp->elx_memsize = sizeof (LPFC_BINDLIST_t);
	mp->mem_hdr.q_max = 16;
	mp->elx_lowmem = 0;
	mp->elx_himem = 0;

	/* Pool 8: MEM_IP_BUF */
	/* Initialize IP buffer structure */
	mp = &phba->memseg[MEM_IP_BUF];
	mp->elx_memflag = ELX_MEM_GETMORE;
	mp->elx_memsize = sizeof (LPFC_IP_BUF_t);
	if (clp[LPFC_CFG_NETWORK_ON].a_current) {
		mp->mem_hdr.q_max = 64;
	} else {
		mp->mem_hdr.q_max = 0;
	}
	mp->elx_lowmem = 0;
	mp->elx_himem = 0;

	/* Pool 9: MEM_IP_RCV_BUF */
	/* Initialize IP rcv buffer structure */
	mp = &phba->memseg[MEM_IP_RCV_BUF];
	mp->elx_memflag =
	    ELX_MEM_GETMORE | ELX_MEM_ATTACH_IPBUF | ELX_MEM_BOUND;
	mp->elx_memsize = LPFC_IP_RCV_BUF_SIZE;
	if (clp[LPFC_CFG_NETWORK_ON].a_current) {
		mp->mem_hdr.q_max = clp[LPFC_CFG_POST_IP_BUF].a_current;
	} else {
		mp->mem_hdr.q_max = 0;
	}
	mp->elx_lowmem = 0;
	mp->elx_himem = LPFC_MAX_POST_IP_BUF;

	/* Pool 10: MEM_IP_MAP */
	/* Initialize IP Network Buffer to physical address or dma handle mapping pool. */
	mp = &phba->memseg[MEM_IP_MAP];
	mp->elx_memflag = ELX_MEM_GETMORE;
	mp->elx_memsize = sizeof (ELX_PHYS_NET_MAP_t);
	mp->mem_hdr.q_max = 64;
	mp->elx_lowmem = 0;

	/* Pool 11: MEM_SCSI_DMA_EXT */
	/* Init SCSI DMA ext structure, using same counts as Pool 4: MEM_SCSI_BUF */
	mp = &phba->memseg[MEM_SCSI_DMA_EXT];
	mp->elx_memflag = ELX_MEM_DMA | ELX_MEM_GETMORE | ELX_MEM_BOUND;
	mp->elx_memsize = 1024;
	mp->mem_hdr.q_max = clp[ELX_CFG_NUM_BUFS].a_current;
	mp->elx_lowmem = 0;
	mp->elx_himem = LPFC_MAX_NUM_BUFS;

	/* Pool 12: MEM_IP_DMA_EXT */
	/* Initialize IP DMA ext structure, using same counts as Pool 8: MEM_IP_BUF */
	mp = &phba->memseg[MEM_IP_DMA_EXT];
	mp->elx_memflag = ELX_MEM_DMA | ELX_MEM_GETMORE;
	mp->elx_memsize = 1024;
	if (clp[LPFC_CFG_NETWORK_ON].a_current) {
		mp->mem_hdr.q_max = 64;
	} else {
		mp->mem_hdr.q_max = 0;
	}
	mp->elx_lowmem = 0;
	mp->elx_himem = 0;

	return (0);
}

irqreturn_t
lpfc_intr_handler(int irq, void *dev_id, struct pt_regs * regs)
{
	elxHBA_t *phba;
	LINUX_HBA_t *plxhba;
	Scsi_Cmnd *cmnd;
	Scsi_Cmnd *next_cmnd;
	int i;
	unsigned long iflag, flag;
	unsigned long sflag;
	struct Scsi_Host *host;

	/* Sanity check dev_id parameter */
	phba = (elxHBA_t *) dev_id;
	if (!phba) {
		return IRQ_NONE;
	}

	/* More sanity checks on dev_id parameter.
	 * We have seen our interrupt service routine being called
	 * with the dev_id of another PCI card in the system.
	 * Here we verify the dev_id is really ours!
	 */
	for (i = 0; i < lpfc_instcnt; i++) {
		if (elxDRVR.pHba[i] == phba) {
			break;
		}
	}
	if (i == lpfc_instcnt) {
		return IRQ_NONE;
	}
	ELX_DRVR_LOCK(phba, iflag);

	lpfc_isr |= (uint32_t) (1 << smp_processor_id());

	/* Call SLI Layer to process interrupt */
	elx_sli_intr(phba);

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	host = plxhba->host;
	spin_lock_irqsave(&plxhba->iodonelock.elx_lock, flag);
	/* Flush all done commands */
	if (plxhba->iodone.q_first) {
		cmnd = (Scsi_Cmnd *) plxhba->iodone.q_first;
		plxhba->iodone.q_first = 0;
		plxhba->iodone.q_last = 0;
		plxhba->iodone.q_cnt = 0;
		spin_unlock_irqrestore(&plxhba->iodonelock.elx_lock, flag);
		while (cmnd) {
			next_cmnd = (Scsi_Cmnd *) cmnd->host_scribble;
			cmnd->host_scribble = 0;
			ELX_DRVR_UNLOCK(phba, iflag);
			LPFC_LOCK_SCSI_DONE;

			/* Give this command back to the OS */
			elx_scsi_add_timer(cmnd, cmnd->timeout_per_command);
			atomic_dec(&plxhba->cmnds_in_flight);
			cmnd->scsi_done(cmnd);

			LPFC_UNLOCK_SCSI_DONE;
			ELX_DRVR_LOCK(phba, iflag);
			cmnd = next_cmnd;
		}
	} else {
		spin_unlock_irqrestore(&plxhba->iodonelock.elx_lock, flag);
	}

	lpfc_isr &= ~((uint32_t) (1 << smp_processor_id()));

	ELX_DRVR_UNLOCK(phba, iflag);
	return IRQ_HANDLED;
}				/* lpfc_intr_handler */

int
lpfc_bind_setup(elxHBA_t * phba)
{
	elxCfgParam_t *clp;
	char **arrayp = 0;
	LPFCHBA_t *plhba;
	u_int cnt = 0;

	/* 
	 * Check if there are any WWNN / scsid bindings
	 */
	clp = &phba->config[0];
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	lpfc_get_bind_type(phba);

	switch (plhba->fcp_mapping) {
	case FCP_SEED_WWNN:
		arrayp = lpfc_fcp_bind_WWNN;
		cnt = 0;
		while (arrayp[cnt] != 0)
			cnt++;
		if (cnt && (*arrayp != 0)) {
			lpfc_bind_wwnn(phba, arrayp, cnt);
		}
		break;

	case FCP_SEED_WWPN:
		arrayp = lpfc_fcp_bind_WWPN;
		cnt = 0;
		while (arrayp[cnt] != 0)
			cnt++;
		if (cnt && (*arrayp != 0)) {
			lpfc_bind_wwpn(phba, arrayp, cnt);
		}
		break;

	case FCP_SEED_DID:
		if (clp[LPFC_CFG_BINDMETHOD].a_current != 4) {
			arrayp = lpfc_fcp_bind_DID;
			cnt = 0;
			while (arrayp[cnt] != 0)
				cnt++;
			if (cnt && (*arrayp != 0)) {
				lpfc_bind_did(phba, arrayp, cnt);
			}
		}
		break;
	}

	if (cnt && (*arrayp != 0) && (clp[LPFC_CFG_BINDMETHOD].a_current == 4)) {
		/* Using ALPA map with Persistent binding - ignoring ALPA map */
		elx_printf_log(phba->brd_no, &elx_msgBlk0411,	/* ptr to msg structure */
			       elx_mes0411,	/* ptr to msg */
			       elx_msgBlk0411.msgPreambleStr,	/* begin varargs */
			       clp[LPFC_CFG_BINDMETHOD].a_current, plhba->fcp_mapping);	/* end varargs */
	}

	if (clp[LPFC_CFG_SCAN_DOWN].a_current > 1) {
		/* Scan-down is out of range - ignoring scan-down */
		elx_printf_log(phba->brd_no, &elx_msgBlk0412,	/* ptr to msg structure */
			       elx_mes0412,	/* ptr to msg */
			       elx_msgBlk0412.msgPreambleStr,	/* begin varargs */
			       clp[LPFC_CFG_BINDMETHOD].a_current, plhba->fcp_mapping);	/* end varargs */
		clp[LPFC_CFG_SCAN_DOWN].a_current = 0;
	}
	return (0);
}

/******************************************************************************
* Function name : lpfc_config_setup
*
* Description   : Called from attach to setup configuration parameters for 
*                 adapter 
*                 The goal of this routine is to fill in all the a_current 
*                 members of the CfgParam structure for all configuration 
*                 parameters.
* Example:
* clp[LPFC_CFG_XXX].a_current = (uint32_t)value;
* value might be a define, a global variable, clp[LPFC_CFG_XXX].a_default,
* or some other enviroment specific way of initializing config parameters.
******************************************************************************/
MODULE_PARM(targetenable, "i");
int targetenable = 1;

int
lpfc_config_setup(elxHBA_t * phba)
{
	elxCfgParam_t *clp;
	LPFCHBA_t *plhba;
	ELX_SLI_t *psli;
	int i;
	int brd;
	int clpastringidx = 0;
	char clpastring[32];

	clp = &phba->config[0];
	plhba = (LPFCHBA_t *) phba->pHbaProto;
	psli = &phba->sli;
	brd = phba->brd_no;

	/*
	 * Read the configuration parameters. Also set to default if
	 * parameter value is out of allowed range.
	 */
	for (i = 0; i < LPFC_TOTAL_NUM_OF_CFG_PARAM; i++) {
		clp[i].a_current =
		    (uint32_t) ((ulong) fc_get_cfg_param(brd, i));

		if (i == ELX_CFG_DFT_HBA_Q_DEPTH)
			continue;

		if ((clp[i].a_current >= clp[i].a_low) &&
		    (clp[i].a_current <= clp[i].a_hi)) {
			/* we continue if the range check is satisfied
			 * however LPFC_CFG_TOPOLOGY has holes and both
			 * LPFC_CFG_FCP_CLASS AND LPFC_CFG_IP_CLASS need
			 * to readjusted iff they satisfy the range check
			 */
			if (i == LPFC_CFG_TOPOLOGY) {
				if (!(clp[i].a_current & 1))	/* odd values 1,3,5 are out */
					continue;
			} else if ((i == LPFC_CFG_FCP_CLASS)
				   || (i == LPFC_CFG_IP_CLASS)) {
				switch (clp[i].a_current) {
				case 2:
					clp[i].a_current = CLASS2;	/* CLASS2 = 1 */
					break;
				case 3:
					clp[i].a_current = CLASS3;	/* CLASS3 = 2 */
					break;
				}
				continue;
			} else
				continue;
		}

		/* The cr_count feature is disabled if cr_delay is set to 0.
		 * So do not bother user with messages about cr_count if cr_delay is 0 */
		if (i == LPFC_CFG_CR_COUNT)
			if (clp[LPFC_CFG_CR_DELAY].a_current == 0)
				continue;

		/* Display lpfc-param as lpfc_param */
		clpastringidx = 0;
		while ((clpastring[clpastringidx] =
			clp[i].a_string[clpastringidx])) {
			if (clpastring[clpastringidx] == '-')
				clpastring[clpastringidx] = '_';
			clpastringidx++;
		}
		elx_printf_log(phba->brd_no, &elx_msgBlk0413,	/* ptr to msg structure */
			       elx_mes0413,	/* ptr to msg */
			       elx_msgBlk0413.msgPreambleStr,	/* begin varargs */
			       clpastring, clp[i].a_low, clp[i].a_hi, clp[i].a_default);	/* end varargs */

		clp[i].a_current = clp[i].a_default;

	}

	switch (((SWAP_LONG(phba->pci_id)) >> 16) & 0xffff) {
	case PCI_DEVICE_ID_LP101:
		clp[ELX_CFG_DFT_HBA_Q_DEPTH].a_current = LPFC_LP101_HBA_Q_DEPTH;
		break;
	case PCI_DEVICE_ID_RFLY:
	case PCI_DEVICE_ID_PFLY:
	case PCI_DEVICE_ID_TFLY:
		clp[ELX_CFG_DFT_HBA_Q_DEPTH].a_current = LPFC_LC_HBA_Q_DEPTH;
		break;
	default:
		clp[ELX_CFG_DFT_HBA_Q_DEPTH].a_current = LPFC_DFT_HBA_Q_DEPTH;
	}
	if (clp[ELX_CFG_DFT_HBA_Q_DEPTH].a_current <
	    clp[ELX_CFG_NUM_BUFS].a_current) {
		/* HBA QUEUE DEPTH should be atleast as high as the NUM BUFS */
		clp[ELX_CFG_DFT_HBA_Q_DEPTH].a_current =
		    clp[ELX_CFG_NUM_BUFS].a_current;
	}
	if (clp[ELX_CFG_DFT_HBA_Q_DEPTH].a_current > LPFC_MAX_HBA_Q_DEPTH) {
		clp[ELX_CFG_DFT_HBA_Q_DEPTH].a_current = LPFC_MAX_HBA_Q_DEPTH;
	}

	phba->sli.ring[LPFC_IP_RING].txq.q_max =
	    clp[LPFC_CFG_XMT_Q_SIZE].a_current;

	plhba->lpfn_max_mtu = lpfc_mtu;
	if ((lpfc_rcv_buff_size % PAGE_SIZE) == 0)
		plhba->lpfn_rcv_buf_size = lpfc_rcv_buff_size;
	else {
		plhba->lpfn_rcv_buf_size =
		    ((lpfc_rcv_buff_size + PAGE_SIZE) & (PAGE_MASK));

		plhba->lpfn_rcv_buf_size -= 16;
	}

	return (0);
}

int
lpfc_bind_wwpn(elxHBA_t * phba, char **arrayp, u_int cnt)
{
	uint8_t *datap, *np;
	LPFC_BINDLIST_t *blp;
	LPFCHBA_t *plhba;
	NAME_TYPE pn;
	int i, entry, lpfc_num, rstatus;
	unsigned int sum;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	plhba->fcp_mapping = FCP_SEED_WWPN;
	np = (uint8_t *) & pn;

	for (entry = 0; entry < cnt; entry++) {
		datap = (uint8_t *) arrayp[entry];
		if (datap == 0)
			break;
		/* Determined the number of ASC hex chars in WWNN & WWPN */
		for (i = 0; i < FC_MAX_WW_NN_PN_STRING; i++) {
			if (elx_str_ctox(datap[i]) < 0)
				break;
		}
		if ((rstatus = lpfc_parse_binding_entry(phba, datap, np,
							i, sizeof (NAME_TYPE),
							LPFC_BIND_WW_NN_PN,
							&sum, entry,
							&lpfc_num)) > 0) {
			if (rstatus == LPFC_SYNTAX_OK_BUT_NOT_THIS_BRD)
				continue;

			/* For syntax error code definitions see LPFC_SYNTAX_ERR_ defines. */
			/* WWPN binding entry <num>: Syntax error code <code> */
			elx_printf_log(phba->brd_no, &elx_msgBlk0430,	/* ptr to msg structure */
				       elx_mes0430,	/* ptr to msg */
				       elx_msgBlk0430.msgPreambleStr,	/* begin varargs */
				       entry, rstatus);	/* end varargs */
			goto out;
		}

		/* Loop through all BINDLIST entries and find
		 * the next available entry.
		 */
		if ((blp =
		     (LPFC_BINDLIST_t *) elx_mem_get(phba, MEM_BIND)) == NULL) {
			/* WWPN binding entry: node table full */
			elx_printf_log(phba->brd_no, &elx_msgBlk0432,	/* ptr to msg structure */
				       elx_mes0432,	/* ptr to msg */
				       elx_msgBlk0432.msgPreambleStr);	/* begin & end varargs */
			goto out;
		}
		memset((void *)blp, 0, sizeof (LPFC_BINDLIST_t));
		blp->nlp_bind_type = FCP_SEED_WWPN;
		blp->nlp_sid = (sum & 0xff);
		memcpy(&blp->nlp_portname, (uint8_t *) & pn,
		       sizeof (NAME_TYPE));

		lpfc_nlp_bind(phba, blp);

	      out:
		np = (uint8_t *) & pn;
	}
	return (0);
}				/* lpfc_bind_wwpn */

int
lpfc_get_bind_type(elxHBA_t * phba)
{
	int bind_type;
	LPFCHBA_t *plhba;
	elxCfgParam_t *clp;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	clp = &phba->config[0];

	bind_type = clp[LPFC_CFG_BINDMETHOD].a_current;

	switch (bind_type) {
	case 1:
		plhba->fcp_mapping = FCP_SEED_WWNN;
		break;

	case 2:
		plhba->fcp_mapping = FCP_SEED_WWPN;
		break;

	case 3:
		plhba->fcp_mapping = FCP_SEED_DID;
		break;

	case 4:
		plhba->fcp_mapping = FCP_SEED_DID;
		break;
	}

	return 0;
}

int
lpfc_bind_wwnn(elxHBA_t * phba, char **arrayp, u_int cnt)
{
	uint8_t *datap, *np;
	LPFC_BINDLIST_t *blp;
	LPFCHBA_t *plhba;
	NAME_TYPE pn;
	int i, entry, lpfc_num, rstatus;
	unsigned int sum;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	plhba->fcp_mapping = FCP_SEED_WWNN;
	np = (uint8_t *) & pn;

	for (entry = 0; entry < cnt; entry++) {
		datap = (uint8_t *) arrayp[entry];
		if (datap == 0)
			break;
		/* Determined the number of ASC hex chars in WWNN & WWPN */
		for (i = 0; i < FC_MAX_WW_NN_PN_STRING; i++) {
			if (elx_str_ctox(datap[i]) < 0)
				break;
		}
		if ((rstatus = lpfc_parse_binding_entry(phba, datap, np,
							i, sizeof (NAME_TYPE),
							LPFC_BIND_WW_NN_PN,
							&sum, entry,
							&lpfc_num)) > 0) {
			if (rstatus == LPFC_SYNTAX_OK_BUT_NOT_THIS_BRD) {
				continue;
			}

			/* For syntax error code definitions see LPFC_SYNTAX_ERR_ defines. */
			/* WWNN binding entry <num>: Syntax error code <code> */
			elx_printf_log(phba->brd_no, &elx_msgBlk0431,	/* ptr to msg structure */
				       elx_mes0431,	/* ptr to msg */
				       elx_msgBlk0431.msgPreambleStr,	/* begin varargs */
				       entry, rstatus);	/* end varargs */
			goto out;
		}

		/* Loop through all BINDLIST entries and find
		 * the next available entry.
		 */
		if ((blp =
		     (LPFC_BINDLIST_t *) elx_mem_get(phba, MEM_BIND)) == NULL) {
			/* WWNN binding entry: node table full */
			elx_printf_log(phba->brd_no, &elx_msgBlk0433,	/* ptr to msg structure */
				       elx_mes0433,	/* ptr to msg */
				       elx_msgBlk0433.msgPreambleStr);	/* begin & end varargs */
			goto out;
		}
		memset((void *)blp, 0, sizeof (LPFC_BINDLIST_t));
		blp->nlp_bind_type = FCP_SEED_WWNN;
		blp->nlp_sid = (sum & 0xff);
		memcpy(&blp->nlp_nodename, (uint8_t *) & pn,
		       sizeof (NAME_TYPE));
		lpfc_nlp_bind(phba, blp);

	      out:
		np = (uint8_t *) & pn;
	}			/* for loop */
	return (0);
}				/* lpfc_bind_wwnn */

int
lpfc_bind_did(elxHBA_t * phba, char **arrayp, u_int cnt)
{
	uint8_t *datap, *np;
	LPFC_BINDLIST_t *blp;
	LPFCHBA_t *plhba;
	D_ID ndid;
	int i, entry, lpfc_num, rstatus;
	unsigned int sum;

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	plhba->fcp_mapping = FCP_SEED_DID;
	ndid.un.word = 0;
	np = (uint8_t *) & ndid.un.word;

	for (entry = 0; entry < cnt; entry++) {
		datap = (uint8_t *) arrayp[entry];
		if (datap == 0)
			break;
		/* Determined the number of ASC hex chars in DID */
		for (i = 0; i < FC_MAX_DID_STRING; i++) {
			if (elx_str_ctox(datap[i]) < 0)
				break;
		}
		if ((rstatus = lpfc_parse_binding_entry(phba, datap, np,
							i, sizeof (D_ID),
							LPFC_BIND_DID, &sum,
							entry,
							&lpfc_num)) > 0) {
			if (rstatus == LPFC_SYNTAX_OK_BUT_NOT_THIS_BRD)
				continue;

			/* For syntax error code definitions see LPFC_SYNTAX_ERR_ defines. */
			/* DID binding entry <num>: Syntax error code <code> */
			elx_printf_log(phba->brd_no, &elx_msgBlk0434,	/* ptr to msg structure */
				       elx_mes0434,	/* ptr to msg */
				       elx_msgBlk0434.msgPreambleStr,	/* begin varargs */
				       entry, rstatus);	/* end varargs */
			goto out;
		}

		/* Loop through all BINDLIST entries and find
		 * the next available entry.
		 */
		if ((blp =
		     (LPFC_BINDLIST_t *) elx_mem_get(phba, MEM_BIND)) == NULL) {
			/* DID binding entry: node table full */
			elx_printf_log(phba->brd_no, &elx_msgBlk0435,	/* ptr to msg structure */
				       elx_mes0435,	/* ptr to msg */
				       elx_msgBlk0435.msgPreambleStr);	/* begin & end varargs */
			goto out;
		}
		memset((void *)blp, 0, sizeof (LPFC_BINDLIST_t));
		blp->nlp_bind_type = FCP_SEED_DID;
		blp->nlp_sid = (sum & 0xff);
		blp->nlp_DID = SWAP_DATA(ndid.un.word);

		lpfc_nlp_bind(phba, blp);

	      out:

		np = (uint8_t *) & ndid.un.word;
	}
	return (0);
}

int
elx_initpci(struct dfc_info *di, elxHBA_t * phba)
{
	LPFCHBA_t *plhba;
	LINUX_HBA_t *plxhba;
	struct pci_dev *pdev;
	char lpfc_fwrevision[32];

	plhba = (LPFCHBA_t *) phba->pHbaProto;
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	pdev = plxhba->pcidev;
	/*
	   must have the pci struct
	 */
	if (!pdev)
		return (1);

	di->fc_ba.a_onmask = (ONDI_MBOX | ONDI_RMEM | ONDI_RPCI | ONDI_RCTLREG |
			      ONDI_IOINFO | ONDI_LNKINFO | ONDI_NODEINFO |
			      ONDI_CFGPARAM | ONDI_CT | ONDI_HBAAPI);
	di->fc_ba.a_offmask =
	    (OFFDI_MBOX | OFFDI_RMEM | OFFDI_WMEM | OFFDI_RPCI | OFFDI_WPCI |
	     OFFDI_RCTLREG | OFFDI_WCTLREG);

	if (lpfc_diag_state == DDI_ONDI)
		di->fc_ba.a_onmask |= ONDI_SLI2;
	else
		di->fc_ba.a_onmask |= ONDI_SLI1;
#ifdef powerpc
	di->fc_ba.a_onmask |= ONDI_BIG_ENDIAN;
#else
	di->fc_ba.a_onmask |= ONDI_LTL_ENDIAN;
#endif
	di->fc_ba.a_pci =
	    ((((uint32_t) pdev->device) << 16) | (uint32_t) (pdev->vendor));
	di->fc_ba.a_pci = SWAP_LONG(di->fc_ba.a_pci);
	di->fc_ba.a_ddi = lpfc_instance[phba->brd_no];
	if (pdev->bus)
		di->fc_ba.a_busid = (uint32_t) (pdev->bus->number);
	else
		di->fc_ba.a_busid = 0;
	di->fc_ba.a_devid = (uint32_t) (pdev->devfn);

	memcpy(di->fc_ba.a_drvrid, (void *)lpfc_release_version, 8);
	lpfc_decode_firmware_rev(phba, lpfc_fwrevision, 1);
	memcpy(di->fc_ba.a_fwname, (void *)lpfc_fwrevision, 32);

	return (0);
}

ELXSCSILUN_t *
lpfc_tran_find_lun(ELX_SCSI_BUF_t * elx_cmd)
{
	elxHBA_t *phba;
	ELXSCSILUN_t *lunp;

	phba = elx_cmd->scsi_hba;
	lunp = lpfc_find_lun(phba, elx_cmd->scsi_target, elx_cmd->scsi_lun, 1);
	return (lunp);
}

int
lpfc_utsname_nodename_check(void)
{
	if (system_utsname.nodename[0] == '\0')
		return (1);

	return (0);
}

char *
lpfc_get_OsNameVersion(int cmd)
{
	memset((void *)lpfc_os_name_version, 0, 256);

	switch (cmd) {
	case GET_OS_VERSION:
		elx_str_sprintf(lpfc_os_name_version, "%s %s %s",
				system_utsname.sysname, system_utsname.release,
				system_utsname.version);
		break;
	case GET_HOST_NAME:
		elx_str_sprintf(lpfc_os_name_version, "%s",
				system_utsname.nodename);
		break;
	}
	return (lpfc_os_name_version);
}

int
lpfc_xmit(elxHBA_t * phba, struct sk_buff *skb)
{
	LPFC_IP_BUF_t *lpfc_cmd;
	unsigned long iflag;
	int rc;
	LPFCHBA_t *plhba;
	uint16_t *dest_addr;
	uint32_t total_length;
	struct sk_buff *tmp_skb;
	uint32_t is_mcast, is_bcast, is_ucast;

	is_mcast = is_bcast = is_ucast = 0;
	plhba = (LPFCHBA_t *) phba->pHbaProto;

	ELX_DRVR_LOCK(phba, iflag);

	/* Get an IP buffer which has all memory 
	 * resources needed to initiate the I/O.
	 */

	lpfc_cmd = lpfc_get_ip_buf(phba);
	if (lpfc_cmd == 0) {
		plhba->ip_stat->lpfn_tx_dropped++;
		ELX_DRVR_UNLOCK(phba, iflag);
		/* error-out this command */
		return (ENOMEM);
	}
	/* store our command structure for later */
	lpfc_cmd->pOSCmd = (void *)skb;

	total_length = 0;
	tmp_skb = skb;
	while (tmp_skb) {
		total_length += tmp_skb->len;
		tmp_skb = tmp_skb->next;
	}

	/* setup a virtual ptr to the Network Header */
	lpfc_cmd->net_hdr = (LPFC_IPHDR_t *) skb->data;

	dest_addr =
	    (uint16_t *) & (lpfc_cmd->net_hdr->fcnet.fc_destname.IEEE[0]);

	if ((dest_addr[0] == 0xffff) && (dest_addr[1] == 0xffff) &&
	    (dest_addr[2] == 0xffff)) {
		is_bcast = 1;
	} else if (dest_addr[0] & 0x8000) {
		is_mcast = 1;
	} else {
		is_ucast = 1;
	}

	/* 
	   If the protocol is any thing other than IP or ARP drop the 
	   packet.
	 */
	if (lpfc_cmd->net_hdr &&
	    lpfc_cmd->net_hdr->llc.type != SWAP_DATA16(ETH_P_IP) &&
	    lpfc_cmd->net_hdr->llc.type != SWAP_DATA16(ETH_P_ARP)) {
		plhba->ip_stat->lpfn_tx_dropped++;
		lpfc_free_ip_buf(lpfc_cmd);
		ELX_DRVR_UNLOCK(phba, iflag);
		return (0);
	}
	/* Send the packet */
	rc = lpfc_ip_xmit(lpfc_cmd);
	if (rc) {
		lpfc_free_ip_buf(lpfc_cmd);
		rc = ENXIO;
		ELX_DRVR_UNLOCK(phba, iflag);
		return (rc);
	}

	if (is_bcast) {
		/* This is a broad cast packet */
		if (++plhba->ip_stat->lpfn_brdcstxmt_lsw == 0) {
			plhba->ip_stat->lpfn_brdcstxmt_msw++;
		}
	}
	if (is_mcast) {
		/* This is a multi cast packet */
		if (++plhba->ip_stat->lpfn_multixmt_lsw == 0) {
			plhba->ip_stat->lpfn_multixmt_msw++;
		}
	}
	if (is_ucast) {
		/* This is an uni cast packet */
		if (++plhba->ip_stat->lpfn_Ucstxmt_lsw == 0) {
			plhba->ip_stat->lpfn_Ucstxmt_msw++;
		}
	}
	/* Update the total txmited bytes statistics. */
	plhba->ip_stat->lpfn_xmtbytes_lsw += total_length;
	if (plhba->ip_stat->lpfn_xmtbytes_lsw < total_length)
		plhba->ip_stat->lpfn_xmtbytes_msw++;

	ELX_DRVR_UNLOCK(phba, iflag);
	return (rc);
}

int
lpfc_ipioctl(int cmd, void *s)
{
	elxHBA_t *phba;
	LPFCHBA_t *plhba;
	elxCfgParam_t *clp;
	NETDEVICE *dev;
	struct lpfn_probe *lp;
	int i, cnt = 0;
	switch (cmd) {
	case LPFN_PROBE:

		if (lpfc_detect_called == FALSE) {
			lp = (struct lpfn_probe *)s;
			lpfn_probe = lp->probe;
			lpfc_detect_called = LPFN_PROBE_PENDING;	/* defer calling this till after fc_detect */
			return (1);
		}

		for (i = 0; i < MAX_ELX_BRDS; i++) {
			if ((phba = elxDRVR.pHba[i])) {
				clp = &phba->config[0];
				plhba = (LPFCHBA_t *) phba->pHbaProto;
				if (clp[LPFC_CFG_NETWORK_ON].a_current == 0)
					continue;

				if (plhba->lpfn_dev == 0) {
					unsigned int alloc_size;

					/* ensure 32-byte alignment of the private area */
					alloc_size =
					    (sizeof (NETDEVICE) & 0xffffffc0) +
					    0x40;

					dev =
					    (NETDEVICE *) kmalloc(alloc_size,
								  GFP_ATOMIC);
					if (dev == NULL) {
						continue;
					}
					memset((char *)dev, 0, alloc_size);
					rtnl_lock();
					elx_str_cpy(dev->name, "lpfn%d");
					if (dev_alloc_name(dev, "lpfn%d") < 0) {
						rtnl_unlock();
						kfree((void *)dev);
						continue;
					}

					dev->priv = (void *)phba;
					plhba->lpfn_dev = (void *)dev;

					lp = (struct lpfn_probe *)s;
					/* Initialize the device structure. */
					dev->hard_start_xmit =
					    lp->hard_start_xmit;
					dev->get_stats = lp->get_stats;
					dev->open = lp->open;
					dev->stop = lp->stop;
					dev->hard_header = lp->hard_header;
					dev->rebuild_header =
					    lp->rebuild_header;
					dev->change_mtu = lp->change_mtu;
					plhba->lpfn_ip_rcv =
					    (void (*)
					     (elxHBA_t *, void *,
					      uint32_t))(lp->receive);

					/* Assume fc header + LLC/SNAP  24 bytes */
					dev->hard_header_len = 24;
					dev->type = ARPHRD_ETHER;
					dev->mtu = plhba->lpfn_max_mtu;
					dev->addr_len = 6;
					dev->tx_queue_len = 100;

					memset(dev->broadcast, 0xFF, 6);
					memcpy(dev->dev_addr, plhba->phys_addr,
					       6);

					/* New-style flags */
					dev->flags = IFF_BROADCAST;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
					dev_init_buffers(dev);
#endif
					register_netdevice(dev);
					rtnl_unlock();

					cnt++;
				}
			}
		}
		break;
	case LPFN_DETACH:
		for (i = 0; i < MAX_ELX_BRDS; i++) {
			if ((phba = elxDRVR.pHba[i])) {
				clp = &phba->config[i];
				plhba = (LPFCHBA_t *) phba->pHbaProto;
				if (clp[LPFC_CFG_NETWORK_ON].a_current == 0)
					continue;
				if ((dev = plhba->lpfn_dev)) {
					unregister_netdev(dev);
					dev->priv = NULL;
					plhba->lpfn_dev = 0;
					cnt++;
				}
			}
		}
		break;
	case LPFN_DFC:
		break;
	default:
		return (0);
	}
	return (cnt);
}

int
lpfc_bufmap(elxHBA_t * phba, uint8_t * bp, uint32_t len, elx_dma_addr_t * phys)
{
	MBUF_INFO_t *buf_info;
	MBUF_INFO_t bufinfo;

	buf_info = &bufinfo;
	buf_info->phys = INVALID_PHYS;
	buf_info->virt = bp;
	buf_info->size = len;
	buf_info->flags = ELX_MBUF_PHYSONLY;
	elx_malloc(phba, buf_info);

	if (buf_info->phys == INVALID_PHYS)
		return (0);
	phys[0] = buf_info->phys;
	return (1);
}

int
lpfc_ip_prep_io(elxHBA_t * phba, LPFC_IP_BUF_t * pib)
{
	LINUX_HBA_t *plxhba;
	LPFC_IPHDR_t *pnethdr;
	ULP_BDE64 *topbpl;
	ULP_BDE64 *bpl;
	DMABUF_t *bmp;
	DMABUF_t *last_bmp;
	IOCB_t *cmd;
	struct sk_buff *skb;
	struct sk_buff *cur_skb;
	struct sk_buff *next_skb;
	elx_dma_addr_t physaddr;
	ELX_PHYS_NET_MAP_t *p_tmp_buff;
	uint32_t cnt, rc;
	uint32_t num_bmps, num_bde, max_bde;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;

	bpl = pib->ip_bpl;

	max_bde = LPFC_IP_INITIAL_BPL_SIZE;

	/* Get nonDMA ptrs from pib */
	skb = (struct sk_buff *)pib->pOSCmd;
	pnethdr = pib->net_hdr;
	cmd = &pib->cur_iocbq.iocb;

	/* These are needed if we chain BPLs */
	last_bmp = pib->dma_ext;
	num_bmps = 1;
	topbpl = 0;
	num_bde = 0;

	/* This next section finishes building the BPL for the I/O from the
	 * skb buffer chain and updates the IOCB accordingly.
	 */
	cnt = 0;
	rc = 0;
	cur_skb = skb;
	while (cur_skb) {
		next_skb = cur_skb->next;

		/* If this skb has data */
		if (cur_skb->len) {
			cnt += cur_skb->len;

			/* Check to see if current BPL is full of BDEs */
			if (num_bde == max_bde) {
				if ((bmp =
				     (DMABUF_t *) elx_mem_get(phba,
							      MEM_BPL)) == 0) {
					rc = 1;
					goto out;
				}
				max_bde = ((1024 / sizeof (ULP_BDE64)) - 3);
				/* Fill in continuation entry to next bpl */
				bpl->addrHigh = putPaddrHigh(bmp->phys);
				bpl->addrHigh = PCIMEM_LONG(bpl->addrHigh);
				bpl->addrLow = putPaddrLow(bmp->phys);
				bpl->addrLow = PCIMEM_LONG(bpl->addrLow);
				bpl->tus.f.bdeFlags = BPL64_SIZE_WORD;
				num_bde++;
				if (num_bmps == 1) {
					cmd->un.xseq64.bdl.bdeSize +=
					    (num_bde * sizeof (ULP_BDE64));
				} else {
					topbpl->tus.f.bdeSize =
					    (num_bde * sizeof (ULP_BDE64));
					topbpl->tus.w =
					    PCIMEM_LONG(topbpl->tus.w);
				}
				topbpl = bpl;
				bpl = (ULP_BDE64 *) bmp->virt;
				last_bmp->next = (void *)bmp;
				last_bmp = bmp;
				num_bde = 0;
				num_bmps++;
			}

			if (lpfc_bufmap
			    (phba, cur_skb->data, cur_skb->len,
			     &physaddr) == 0) {
				rc = 1;
				goto out;
			}

			/* Optimization.  If the sk_buff from the OS contains exactly one
			 * entry, store the one phyaddr in data structure's local ELX_PHYS_NET_MAP_t
			 * type.  Otherwise get a memory buffer to contain the list of physaddr's mapped
			 * back to each sk_buff in the chain.
			 */
			if (next_skb == NULL) {
				pib->elx_phys_net_map.phys_addr = physaddr;
				pib->elx_phys_net_map.p_sk_buff = cur_skb;
			} else {
				/* The sk_buff contains more than one buffer.  Store the sk_buff and the physical
				 * address for later processing by lpfc_ip_unprep_io.  The sk_buff is provided
				 * as a check since the unprep has to access two lists.
				 */
				if ((p_tmp_buff =
				     (ELX_PHYS_NET_MAP_t *) elx_mem_get(phba,
									MEM_IP_MAP))
				    == NULL) {
					rc = 1;
					goto out;
				}

				p_tmp_buff->p_sk_buff = cur_skb;
				p_tmp_buff->phys_addr = physaddr;
				elx_tqs_enqueue(&pib->elx_phys_net_map_list,
						p_tmp_buff, p_next);
			}

			bpl->addrLow = PCIMEM_LONG(putPaddrLow(physaddr));
			bpl->addrHigh = PCIMEM_LONG(putPaddrHigh(physaddr));
			bpl->tus.f.bdeSize = cur_skb->len;
			bpl->tus.f.bdeFlags = BDE64_SIZE_WORD;
			bpl->tus.w = PCIMEM_LONG(bpl->tus.w);
			bpl++;
			num_bde++;
		}
		cur_skb = next_skb;
	}

      out:
	bpl->addrHigh = 0;
	bpl->addrLow = 0;
	bpl->tus.w = 0;
	pib->totalSize = cnt;
	if (num_bmps == 1) {
		cmd->un.xseq64.bdl.bdeSize += (num_bde * sizeof (ULP_BDE64));
	} else {
		topbpl->tus.f.bdeSize = (num_bde * sizeof (ULP_BDE64));
		topbpl->tus.w = PCIMEM_LONG(topbpl->tus.w);
	}
	cmd->ulpBdeCount = 1;
	cmd->ulpLe = 1;		/* Set the LE bit in the iocb */

	return (rc);
}

int
lpfc_ip_unprep_io(elxHBA_t * phba, LPFC_IP_BUF_t * pib, uint32_t free_msg)
{
	int free_phys_rsc;
	struct sk_buff *skb;
	struct sk_buff *curr_skb;
	MBUF_INFO_t *buf_info;
	MBUF_INFO_t bufinfo;
	ELX_PHYS_NET_MAP_t *p_tmp_buff;

	free_phys_rsc = 0;
	buf_info = &bufinfo;
	skb = (struct sk_buff *)pib->pOSCmd;
	while (skb) {
		curr_skb = skb;
		skb = skb->next;

		/* Free the PCI mapping of the message block. */
		buf_info->flags = ELX_MBUF_PHYSONLY;
		buf_info->size = curr_skb->len;

		if (skb == NULL) {
			p_tmp_buff = &pib->elx_phys_net_map;
			buf_info->phys = pib->elx_phys_net_map.phys_addr;
		} else {
			p_tmp_buff =
			    elx_tqs_dequeuefirst(&pib->elx_phys_net_map_list,
						 p_next);
			buf_info->phys = p_tmp_buff->phys_addr;
			free_phys_rsc = 1;
		}

		/* Make sure the ip buffer in the pOSCmd matches that stored in the pib's 
		 * ELX_PHYS_NET_MAP_t data member. 
		 */

		if (p_tmp_buff->p_sk_buff != curr_skb) {
			elx_printf_log(phba->brd_no, &elx_msgBlk0609,	/* ptr to msg structure */
				       elx_mes0609,	/* ptr to msg */
				       elx_msgBlk0609.msgPreambleStr,	/* begin varargs */
				       pib, p_tmp_buff->p_sk_buff, curr_skb, p_tmp_buff->phys_addr);	/* end varargs */
		}

		/* Free the dma mapping now. */
		elx_free(phba, buf_info);

		/* Free the ELX_PHYS_NET_MAP_t resources. */
		if (free_phys_rsc) {
			elx_mem_put(phba, MEM_IP_MAP, (uint8_t *) p_tmp_buff);
		}

		/* Free the message block */
		if (free_msg) {
			if (in_irq()) {
				dev_kfree_skb_irq(curr_skb);
			} else {
				dev_kfree_skb(curr_skb);
			}
		}
	}

	return 0;
}

int
lpfc_sleep(elxHBA_t * phba, fcEVTHDR_t * ep)
{
	LINUX_HBA_t *plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;

	ep->e_mode |= E_SLEEPING_MODE;
	switch (ep->e_mask) {
	case FC_REG_LINK_EVENT:
		return (elx_sleep(phba, &plxhba->linkevtwq, 0));
	case FC_REG_RSCN_EVENT:
		return (elx_sleep(phba, &plxhba->rscnevtwq, 0));
	case FC_REG_CT_EVENT:
		return (elx_sleep(phba, &plxhba->ctevtwq, 0));
	}
	return (0);
}

void
lpfc_wakeup(elxHBA_t * phba, fcEVTHDR_t * ep)
{
	LINUX_HBA_t *plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;

	ep->e_mode &= ~E_SLEEPING_MODE;
	switch (ep->e_mask) {
	case FC_REG_LINK_EVENT:
		elx_wakeup(phba, &plxhba->linkevtwq);
		break;
	case FC_REG_RSCN_EVENT:
		elx_wakeup(phba, &plxhba->rscnevtwq);
		break;
	case FC_REG_CT_EVENT:
		elx_wakeup(phba, &plxhba->ctevtwq);
		break;
	}
	return;
}

void *
fc_get_cfg_param(int brd, int param)
{
	void *value;

	value = (void *)((ulong) (-1));
	switch (brd) {
	case 0:		/* HBA 0 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc0_log_verbose != -1)
				value = (void *)((ulong) lpfc0_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc0_num_iocbs != -1)
				value = (void *)((ulong) lpfc0_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc0_num_bufs != -1)
				value = (void *)((ulong) lpfc0_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc0_automap != -1)
				value = (void *)((ulong) lpfc0_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc0_fcp_bind_method != -1)
				value = (void *)((ulong) lpfc0_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc0_cr_delay != -1)
				value = (void *)((ulong) lpfc0_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc0_cr_count != -1)
				value = (void *)((ulong) lpfc0_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc0_tgt_queue_depth != -1)
				value = (void *)((ulong) lpfc0_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc0_lun_queue_depth != -1)
				value = (void *)((ulong) lpfc0_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc0_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc0_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc0_fcp_class != -1)
				value = (void *)((ulong) lpfc0_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc0_use_adisc != -1)
				value = (void *)((ulong) lpfc0_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc0_no_device_delay != -1)
				value = (void *)((ulong) lpfc0_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc0_network_on != -1)
				value = (void *)((ulong) lpfc0_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc0_post_ip_buf != -1)
				value = (void *)((ulong) lpfc0_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc0_xmt_que_size != -1)
				value = (void *)((ulong) lpfc0_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc0_ip_class != -1)
				value = (void *)((ulong) lpfc0_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc0_ack0 != -1)
				value = (void *)((ulong) lpfc0_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc0_topology != -1)
				value = (void *)((ulong) lpfc0_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc0_scan_down != -1)
				value = (void *)((ulong) lpfc0_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc0_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc0_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc0_nodev_holdio != -1)
				value = (void *)((ulong) lpfc0_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc0_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc0_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc0_check_cond_err != -1)
				value = (void *)((ulong) lpfc0_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc0_nodev_tmo != -1)
				value = (void *)((ulong) lpfc0_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc0_link_speed != -1)
				value = (void *)((ulong) lpfc0_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc0_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc0_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc0_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc0_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc0_fdmi_on != -1)
				value = (void *)((ulong) lpfc0_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc0_max_lun != -1)
				value = (void *)((ulong) lpfc0_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc0_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc0_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc0_max_target != -1)
				value = (void *)((ulong) lpfc0_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc0_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc0_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc0_lun_skip != -1)
				value = (void *)((ulong) lpfc0_lun_skip);
			break;
		default:
			break;
		}
		break;
	case 1:		/* HBA 1 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc1_log_verbose != -1)
				value = (void *)((ulong) lpfc1_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc1_num_iocbs != -1)
				value = (void *)((ulong) lpfc1_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc1_num_bufs != -1)
				value = (void *)((ulong) lpfc1_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc1_automap != -1)
				value = (void *)((ulong) lpfc1_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc1_fcp_bind_method != -1)
				value = (void *)((ulong) lpfc1_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc1_cr_delay != -1)
				value = (void *)((ulong) lpfc1_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc1_cr_count != -1)
				value = (void *)((ulong) lpfc1_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc1_tgt_queue_depth != -1)
				value = (void *)((ulong) lpfc1_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc1_lun_queue_depth != -1)
				value = (void *)((ulong) lpfc1_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc1_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc1_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc1_fcp_class != -1)
				value = (void *)((ulong) lpfc1_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc1_use_adisc != -1)
				value = (void *)((ulong) lpfc1_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc1_no_device_delay != -1)
				value = (void *)((ulong) lpfc1_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc1_network_on != -1)
				value = (void *)((ulong) lpfc1_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc1_post_ip_buf != -1)
				value = (void *)((ulong) lpfc1_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc1_xmt_que_size != -1)
				value = (void *)((ulong) lpfc1_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc1_ip_class != -1)
				value = (void *)((ulong) lpfc1_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc1_ack0 != -1)
				value = (void *)((ulong) lpfc1_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc1_topology != -1)
				value = (void *)((ulong) lpfc1_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc1_scan_down != -1)
				value = (void *)((ulong) lpfc1_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc1_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc1_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc1_nodev_holdio != -1)
				value = (void *)((ulong) lpfc1_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc1_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc1_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc1_check_cond_err != -1)
				value = (void *)((ulong) lpfc1_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc1_nodev_tmo != -1)
				value = (void *)((ulong) lpfc1_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc1_link_speed != -1)
				value = (void *)((ulong) lpfc1_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc1_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc1_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc1_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc1_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc1_fdmi_on != -1)
				value = (void *)((ulong) lpfc1_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc1_max_lun != -1)
				value = (void *)((ulong) lpfc1_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc1_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc1_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc1_max_target != -1)
				value = (void *)((ulong) lpfc1_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc1_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc1_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc1_lun_skip != -1)
				value = (void *)((ulong) lpfc1_lun_skip);
			break;
		}
		break;
	case 2:		/* HBA 2 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc2_log_verbose != -1)
				value = (void *)((ulong) lpfc2_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc2_num_iocbs != -1)
				value = (void *)((ulong) lpfc2_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc2_num_bufs != -1)
				value = (void *)((ulong) lpfc2_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc2_automap != -1)
				value = (void *)((ulong) lpfc2_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc2_fcp_bind_method != -1)
				value = (void *)((ulong) lpfc2_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc2_cr_delay != -1)
				value = (void *)((ulong) lpfc2_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc2_cr_count != -1)
				value = (void *)((ulong) lpfc2_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc2_tgt_queue_depth != -1)
				value = (void *)((ulong) lpfc2_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc2_lun_queue_depth != -1)
				value = (void *)((ulong) lpfc2_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc2_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc2_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc2_fcp_class != -1)
				value = (void *)((ulong) lpfc2_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc2_use_adisc != -1)
				value = (void *)((ulong) lpfc2_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc2_no_device_delay != -1)
				value = (void *)((ulong) lpfc2_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc2_network_on != -1)
				value = (void *)((ulong) lpfc2_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc2_post_ip_buf != -1)
				value = (void *)((ulong) lpfc2_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc2_xmt_que_size != -1)
				value = (void *)((ulong) lpfc2_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc2_ip_class != -1)
				value = (void *)((ulong) lpfc2_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc2_ack0 != -1)
				value = (void *)((ulong) lpfc2_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc2_topology != -1)
				value = (void *)((ulong) lpfc2_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc2_scan_down != -1)
				value = (void *)((ulong) lpfc2_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc2_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc2_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc2_nodev_holdio != -1)
				value = (void *)((ulong) lpfc2_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc2_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc2_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc2_check_cond_err != -1)
				value = (void *)((ulong) lpfc2_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc2_nodev_tmo != -1)
				value = (void *)((ulong) lpfc2_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc2_link_speed != -1)
				value = (void *)((ulong) lpfc2_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc2_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc2_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc2_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc2_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc2_fdmi_on != -1)
				value = (void *)((ulong) lpfc2_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc2_max_lun != -1)
				value = (void *)((ulong) lpfc2_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc2_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc2_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc2_max_target != -1)
				value = (void *)((ulong) lpfc2_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc2_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc2_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc2_lun_skip != -1)
				value = (void *)((ulong) lpfc2_lun_skip);
			break;
		}
		break;
	case 3:		/* HBA 3 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc3_log_verbose != -1)
				value = (void *)((ulong) lpfc3_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc3_num_iocbs != -1)
				value = (void *)((ulong) lpfc3_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc3_num_bufs != -1)
				value = (void *)((ulong) lpfc3_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc3_automap != -1)
				value = (void *)((ulong) lpfc3_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc3_fcp_bind_method != -1)
				value = (void *)((ulong) lpfc3_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc3_cr_delay != -1)
				value = (void *)((ulong) lpfc3_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc3_cr_count != -1)
				value = (void *)((ulong) lpfc3_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc3_tgt_queue_depth != -1)
				value = (void *)((ulong) lpfc3_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc3_lun_queue_depth != -1)
				value = (void *)((ulong) lpfc3_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc3_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc3_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc3_fcp_class != -1)
				value = (void *)((ulong) lpfc3_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc3_use_adisc != -1)
				value = (void *)((ulong) lpfc3_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc3_no_device_delay != -1)
				value = (void *)((ulong) lpfc3_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc3_network_on != -1)
				value = (void *)((ulong) lpfc3_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc3_post_ip_buf != -1)
				value = (void *)((ulong) lpfc3_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc3_xmt_que_size != -1)
				value = (void *)((ulong) lpfc3_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc3_ip_class != -1)
				value = (void *)((ulong) lpfc3_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc3_ack0 != -1)
				value = (void *)((ulong) lpfc3_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc3_topology != -1)
				value = (void *)((ulong) lpfc3_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc3_scan_down != -1)
				value = (void *)((ulong) lpfc3_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc3_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc3_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc3_nodev_holdio != -1)
				value = (void *)((ulong) lpfc3_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc3_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc3_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc3_check_cond_err != -1)
				value = (void *)((ulong) lpfc3_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc3_nodev_tmo != -1)
				value = (void *)((ulong) lpfc3_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc3_link_speed != -1)
				value = (void *)((ulong) lpfc3_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc3_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc3_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc3_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc3_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc3_fdmi_on != -1)
				value = (void *)((ulong) lpfc3_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc3_max_lun != -1)
				value = (void *)((ulong) lpfc3_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc3_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc3_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc3_max_target != -1)
				value = (void *)((ulong) lpfc3_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc3_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc3_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc3_lun_skip != -1)
				value = (void *)((ulong) lpfc3_lun_skip);
			break;
		}
		break;
	case 4:		/* HBA 4 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc4_log_verbose != -1)
				value = (void *)((ulong) lpfc4_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc4_num_iocbs != -1)
				value = (void *)((ulong) lpfc4_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc4_num_bufs != -1)
				value = (void *)((ulong) lpfc4_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc4_automap != -1)
				value = (void *)((ulong) lpfc4_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc4_fcp_bind_method != -1)
				value = (void *)((ulong) lpfc4_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc4_cr_delay != -1)
				value = (void *)((ulong) lpfc4_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc4_cr_count != -1)
				value = (void *)((ulong) lpfc4_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc4_tgt_queue_depth != -1)
				value = (void *)((ulong) lpfc4_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc4_lun_queue_depth != -1)
				value = (void *)((ulong) lpfc4_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc4_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc4_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc4_fcp_class != -1)
				value = (void *)((ulong) lpfc4_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc4_use_adisc != -1)
				value = (void *)((ulong) lpfc4_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc4_no_device_delay != -1)
				value = (void *)((ulong) lpfc4_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc4_network_on != -1)
				value = (void *)((ulong) lpfc4_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc4_post_ip_buf != -1)
				value = (void *)((ulong) lpfc4_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc4_xmt_que_size != -1)
				value = (void *)((ulong) lpfc4_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc4_ip_class != -1)
				value = (void *)((ulong) lpfc4_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc4_ack0 != -1)
				value = (void *)((ulong) lpfc4_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc4_topology != -1)
				value = (void *)((ulong) lpfc4_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc4_scan_down != -1)
				value = (void *)((ulong) lpfc4_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc4_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc4_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc4_nodev_holdio != -1)
				value = (void *)((ulong) lpfc4_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc4_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc4_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc4_check_cond_err != -1)
				value = (void *)((ulong) lpfc4_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc4_nodev_tmo != -1)
				value = (void *)((ulong) lpfc4_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc4_link_speed != -1)
				value = (void *)((ulong) lpfc4_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc4_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc4_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc4_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc4_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc4_fdmi_on != -1)
				value = (void *)((ulong) lpfc4_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc4_max_lun != -1)
				value = (void *)((ulong) lpfc4_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc4_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc4_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc4_max_target != -1)
				value = (void *)((ulong) lpfc4_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc4_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc4_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc4_lun_skip != -1)
				value = (void *)((ulong) lpfc4_lun_skip);
			break;
		}
		break;
	case 5:		/* HBA 5 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc5_log_verbose != -1)
				value = (void *)((ulong) lpfc5_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc5_num_iocbs != -1)
				value = (void *)((ulong) lpfc5_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc5_num_bufs != -1)
				value = (void *)((ulong) lpfc5_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc5_automap != -1)
				value = (void *)((ulong) lpfc5_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc5_fcp_bind_method != -1)
				value = (void *)((ulong) lpfc5_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc5_cr_delay != -1)
				value = (void *)((ulong) lpfc5_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc5_cr_count != -1)
				value = (void *)((ulong) lpfc5_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc5_tgt_queue_depth != -1)
				value = (void *)((ulong) lpfc5_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc5_lun_queue_depth != -1)
				value = (void *)((ulong) lpfc5_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc5_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc5_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc5_fcp_class != -1)
				value = (void *)((ulong) lpfc5_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc5_use_adisc != -1)
				value = (void *)((ulong) lpfc5_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc5_no_device_delay != -1)
				value = (void *)((ulong) lpfc5_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc5_network_on != -1)
				value = (void *)((ulong) lpfc5_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc5_post_ip_buf != -1)
				value = (void *)((ulong) lpfc5_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc5_xmt_que_size != -1)
				value = (void *)((ulong) lpfc5_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc5_ip_class != -1)
				value = (void *)((ulong) lpfc5_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc5_ack0 != -1)
				value = (void *)((ulong) lpfc5_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc5_topology != -1)
				value = (void *)((ulong) lpfc5_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc5_scan_down != -1)
				value = (void *)((ulong) lpfc5_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc5_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc5_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc5_nodev_holdio != -1)
				value = (void *)((ulong) lpfc5_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc5_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc5_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc5_check_cond_err != -1)
				value = (void *)((ulong) lpfc5_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc5_nodev_tmo != -1)
				value = (void *)((ulong) lpfc5_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc5_link_speed != -1)
				value = (void *)((ulong) lpfc5_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc5_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc5_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc5_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc5_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc5_fdmi_on != -1)
				value = (void *)((ulong) lpfc5_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc5_max_lun != -1)
				value = (void *)((ulong) lpfc5_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc5_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc5_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc5_max_target != -1)
				value = (void *)((ulong) lpfc5_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc5_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc5_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc5_lun_skip != -1)
				value = (void *)((ulong) lpfc5_lun_skip);
			break;
		}
		break;
	case 6:		/* HBA 6 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc6_log_verbose != -1)
				value = (void *)((ulong) lpfc6_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc6_num_iocbs != -1)
				value = (void *)((ulong) lpfc6_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc6_num_bufs != -1)
				value = (void *)((ulong) lpfc6_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc6_automap != -1)
				value = (void *)((ulong) lpfc6_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc6_fcp_bind_method != -1)
				value = (void *)((ulong) lpfc6_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc6_cr_delay != -1)
				value = (void *)((ulong) lpfc6_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc6_cr_count != -1)
				value = (void *)((ulong) lpfc6_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc6_tgt_queue_depth != -1)
				value = (void *)((ulong) lpfc6_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc6_lun_queue_depth != -1)
				value = (void *)((ulong) lpfc6_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc6_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc6_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc6_fcp_class != -1)
				value = (void *)((ulong) lpfc6_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc6_use_adisc != -1)
				value = (void *)((ulong) lpfc6_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc6_no_device_delay != -1)
				value = (void *)((ulong) lpfc6_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc6_network_on != -1)
				value = (void *)((ulong) lpfc6_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc6_post_ip_buf != -1)
				value = (void *)((ulong) lpfc6_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc6_xmt_que_size != -1)
				value = (void *)((ulong) lpfc6_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc6_ip_class != -1)
				value = (void *)((ulong) lpfc6_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc6_ack0 != -1)
				value = (void *)((ulong) lpfc6_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc6_topology != -1)
				value = (void *)((ulong) lpfc6_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc6_scan_down != -1)
				value = (void *)((ulong) lpfc6_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc6_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc6_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc6_nodev_holdio != -1)
				value = (void *)((ulong) lpfc6_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc6_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc6_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc6_check_cond_err != -1)
				value = (void *)((ulong) lpfc6_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc6_nodev_tmo != -1)
				value = (void *)((ulong) lpfc6_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc6_link_speed != -1)
				value = (void *)((ulong) lpfc6_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc6_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc6_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc6_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc6_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc6_fdmi_on != -1)
				value = (void *)((ulong) lpfc6_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc6_max_lun != -1)
				value = (void *)((ulong) lpfc6_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc6_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc6_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc6_max_target != -1)
				value = (void *)((ulong) lpfc6_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc6_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc6_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc6_lun_skip != -1)
				value = (void *)((ulong) lpfc6_lun_skip);
			break;
		}
		break;
	case 7:		/* HBA 7 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc7_log_verbose != -1)
				value = (void *)((ulong) lpfc7_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc7_num_iocbs != -1)
				value = (void *)((ulong) lpfc7_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc7_num_bufs != -1)
				value = (void *)((ulong) lpfc7_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc7_automap != -1)
				value = (void *)((ulong) lpfc7_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc7_fcp_bind_method != -1)
				value = (void *)((ulong) lpfc7_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc7_cr_delay != -1)
				value = (void *)((ulong) lpfc7_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc7_cr_count != -1)
				value = (void *)((ulong) lpfc7_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc7_tgt_queue_depth != -1)
				value = (void *)((ulong) lpfc7_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc7_lun_queue_depth != -1)
				value = (void *)((ulong) lpfc7_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc7_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc7_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc7_fcp_class != -1)
				value = (void *)((ulong) lpfc7_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc7_use_adisc != -1)
				value = (void *)((ulong) lpfc7_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc7_no_device_delay != -1)
				value = (void *)((ulong) lpfc7_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc7_network_on != -1)
				value = (void *)((ulong) lpfc7_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc7_post_ip_buf != -1)
				value = (void *)((ulong) lpfc7_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc7_xmt_que_size != -1)
				value = (void *)((ulong) lpfc7_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc7_ip_class != -1)
				value = (void *)((ulong) lpfc7_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc7_ack0 != -1)
				value = (void *)((ulong) lpfc7_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc7_topology != -1)
				value = (void *)((ulong) lpfc7_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc7_scan_down != -1)
				value = (void *)((ulong) lpfc7_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc7_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc7_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc7_nodev_holdio != -1)
				value = (void *)((ulong) lpfc7_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc7_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc7_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc7_check_cond_err != -1)
				value = (void *)((ulong) lpfc7_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc7_nodev_tmo != -1)
				value = (void *)((ulong) lpfc7_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc7_link_speed != -1)
				value = (void *)((ulong) lpfc7_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc7_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc7_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc7_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc7_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc7_fdmi_on != -1)
				value = (void *)((ulong) lpfc7_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc7_max_lun != -1)
				value = (void *)((ulong) lpfc7_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc7_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc7_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc7_max_target != -1)
				value = (void *)((ulong) lpfc7_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc7_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc7_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc7_lun_skip != -1)
				value = (void *)((ulong) lpfc7_lun_skip);
			break;
		}
		break;
	case 8:		/* HBA 8 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc8_log_verbose != -1)
				value = (void *)((ulong) lpfc8_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc8_num_iocbs != -1)
				value = (void *)((ulong) lpfc8_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc8_num_bufs != -1)
				value = (void *)((ulong) lpfc8_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc8_automap != -1)
				value = (void *)((ulong) lpfc8_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc8_fcp_bind_method != -1)
				value = (void *)((ulong) lpfc8_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc8_cr_delay != -1)
				value = (void *)((ulong) lpfc8_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc8_cr_count != -1)
				value = (void *)((ulong) lpfc8_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc8_tgt_queue_depth != -1)
				value = (void *)((ulong) lpfc8_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc8_lun_queue_depth != -1)
				value = (void *)((ulong) lpfc8_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc8_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc8_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc8_fcp_class != -1)
				value = (void *)((ulong) lpfc8_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc8_use_adisc != -1)
				value = (void *)((ulong) lpfc8_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc8_no_device_delay != -1)
				value = (void *)((ulong) lpfc8_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc8_network_on != -1)
				value = (void *)((ulong) lpfc8_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc8_post_ip_buf != -1)
				value = (void *)((ulong) lpfc8_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc8_xmt_que_size != -1)
				value = (void *)((ulong) lpfc8_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc8_ip_class != -1)
				value = (void *)((ulong) lpfc8_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc8_ack0 != -1)
				value = (void *)((ulong) lpfc8_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc8_topology != -1)
				value = (void *)((ulong) lpfc8_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc8_scan_down != -1)
				value = (void *)((ulong) lpfc8_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc8_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc8_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc8_nodev_holdio != -1)
				value = (void *)((ulong) lpfc8_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc8_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc8_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc8_check_cond_err != -1)
				value = (void *)((ulong) lpfc8_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc8_nodev_tmo != -1)
				value = (void *)((ulong) lpfc8_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc8_link_speed != -1)
				value = (void *)((ulong) lpfc8_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc8_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc8_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc8_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc8_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc8_fdmi_on != -1)
				value = (void *)((ulong) lpfc8_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc8_max_lun != -1)
				value = (void *)((ulong) lpfc8_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc8_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc8_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc8_max_target != -1)
				value = (void *)((ulong) lpfc8_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc8_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc8_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc8_lun_skip != -1)
				value = (void *)((ulong) lpfc8_lun_skip);
			break;
		}
		break;
	case 9:		/* HBA 9 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc9_log_verbose != -1)
				value = (void *)((ulong) lpfc9_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc9_num_iocbs != -1)
				value = (void *)((ulong) lpfc9_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc9_num_bufs != -1)
				value = (void *)((ulong) lpfc9_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc9_automap != -1)
				value = (void *)((ulong) lpfc9_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc9_fcp_bind_method != -1)
				value = (void *)((ulong) lpfc9_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc9_cr_delay != -1)
				value = (void *)((ulong) lpfc9_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc9_cr_count != -1)
				value = (void *)((ulong) lpfc9_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc9_tgt_queue_depth != -1)
				value = (void *)((ulong) lpfc9_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc9_lun_queue_depth != -1)
				value = (void *)((ulong) lpfc9_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc9_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc9_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc9_fcp_class != -1)
				value = (void *)((ulong) lpfc9_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc9_use_adisc != -1)
				value = (void *)((ulong) lpfc9_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc9_no_device_delay != -1)
				value = (void *)((ulong) lpfc9_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc9_network_on != -1)
				value = (void *)((ulong) lpfc9_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc9_post_ip_buf != -1)
				value = (void *)((ulong) lpfc9_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc9_xmt_que_size != -1)
				value = (void *)((ulong) lpfc9_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc9_ip_class != -1)
				value = (void *)((ulong) lpfc9_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc9_ack0 != -1)
				value = (void *)((ulong) lpfc9_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc9_topology != -1)
				value = (void *)((ulong) lpfc9_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc9_scan_down != -1)
				value = (void *)((ulong) lpfc9_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc9_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc9_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc9_nodev_holdio != -1)
				value = (void *)((ulong) lpfc9_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc9_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc9_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc9_check_cond_err != -1)
				value = (void *)((ulong) lpfc9_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc9_nodev_tmo != -1)
				value = (void *)((ulong) lpfc9_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc9_link_speed != -1)
				value = (void *)((ulong) lpfc9_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc9_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc9_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc9_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc9_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc9_fdmi_on != -1)
				value = (void *)((ulong) lpfc9_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc9_max_lun != -1)
				value = (void *)((ulong) lpfc9_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc9_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc9_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc9_max_target != -1)
				value = (void *)((ulong) lpfc9_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc9_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc9_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc9_lun_skip != -1)
				value = (void *)((ulong) lpfc9_lun_skip);
			break;
		}
		break;
	case 10:		/* HBA 10 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc10_log_verbose != -1)
				value = (void *)((ulong) lpfc10_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc10_num_iocbs != -1)
				value = (void *)((ulong) lpfc10_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc10_num_bufs != -1)
				value = (void *)((ulong) lpfc10_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc10_automap != -1)
				value = (void *)((ulong) lpfc10_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc10_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc10_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc10_cr_delay != -1)
				value = (void *)((ulong) lpfc10_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc10_cr_count != -1)
				value = (void *)((ulong) lpfc10_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc10_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc10_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc10_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc10_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc10_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc10_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc10_fcp_class != -1)
				value = (void *)((ulong) lpfc10_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc10_use_adisc != -1)
				value = (void *)((ulong) lpfc10_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc10_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc10_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc10_network_on != -1)
				value = (void *)((ulong) lpfc10_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc10_post_ip_buf != -1)
				value = (void *)((ulong) lpfc10_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc10_xmt_que_size != -1)
				value = (void *)((ulong) lpfc10_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc10_ip_class != -1)
				value = (void *)((ulong) lpfc10_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc10_ack0 != -1)
				value = (void *)((ulong) lpfc10_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc10_topology != -1)
				value = (void *)((ulong) lpfc10_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc10_scan_down != -1)
				value = (void *)((ulong) lpfc10_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc10_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc10_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc10_nodev_holdio != -1)
				value = (void *)((ulong) lpfc10_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc10_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc10_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc10_check_cond_err != -1)
				value = (void *)((ulong) lpfc10_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc10_nodev_tmo != -1)
				value = (void *)((ulong) lpfc10_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc10_link_speed != -1)
				value = (void *)((ulong) lpfc10_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc10_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc10_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc10_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc10_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc10_fdmi_on != -1)
				value = (void *)((ulong) lpfc10_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc10_max_lun != -1)
				value = (void *)((ulong) lpfc10_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc10_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc10_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc10_max_target != -1)
				value = (void *)((ulong) lpfc10_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc10_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc10_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc10_lun_skip != -1)
				value = (void *)((ulong) lpfc10_lun_skip);
			break;
		}
		break;
	case 11:		/* HBA 11 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc11_log_verbose != -1)
				value = (void *)((ulong) lpfc11_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc11_num_iocbs != -1)
				value = (void *)((ulong) lpfc11_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc11_num_bufs != -1)
				value = (void *)((ulong) lpfc11_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc11_automap != -1)
				value = (void *)((ulong) lpfc11_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc11_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc11_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc11_cr_delay != -1)
				value = (void *)((ulong) lpfc11_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc11_cr_count != -1)
				value = (void *)((ulong) lpfc11_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc11_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc11_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc11_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc11_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc11_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc11_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc11_fcp_class != -1)
				value = (void *)((ulong) lpfc11_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc11_use_adisc != -1)
				value = (void *)((ulong) lpfc11_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc11_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc11_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc11_network_on != -1)
				value = (void *)((ulong) lpfc11_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc11_post_ip_buf != -1)
				value = (void *)((ulong) lpfc11_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc11_xmt_que_size != -1)
				value = (void *)((ulong) lpfc11_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc11_ip_class != -1)
				value = (void *)((ulong) lpfc11_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc11_ack0 != -1)
				value = (void *)((ulong) lpfc11_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc11_topology != -1)
				value = (void *)((ulong) lpfc11_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc11_scan_down != -1)
				value = (void *)((ulong) lpfc11_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc11_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc11_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc11_nodev_holdio != -1)
				value = (void *)((ulong) lpfc11_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc11_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc11_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc11_check_cond_err != -1)
				value = (void *)((ulong) lpfc11_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc11_nodev_tmo != -1)
				value = (void *)((ulong) lpfc11_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc11_link_speed != -1)
				value = (void *)((ulong) lpfc11_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc11_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc11_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc11_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc11_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc11_fdmi_on != -1)
				value = (void *)((ulong) lpfc11_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc11_max_lun != -1)
				value = (void *)((ulong) lpfc11_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc11_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc11_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc11_max_target != -1)
				value = (void *)((ulong) lpfc11_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc11_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc11_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc11_lun_skip != -1)
				value = (void *)((ulong) lpfc11_lun_skip);
			break;
		}
		break;
	case 12:		/* HBA 12 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc12_log_verbose != -1)
				value = (void *)((ulong) lpfc12_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc12_num_iocbs != -1)
				value = (void *)((ulong) lpfc12_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc12_num_bufs != -1)
				value = (void *)((ulong) lpfc12_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc12_automap != -1)
				value = (void *)((ulong) lpfc12_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc12_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc12_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc12_cr_delay != -1)
				value = (void *)((ulong) lpfc12_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc12_cr_count != -1)
				value = (void *)((ulong) lpfc12_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc12_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc12_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc12_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc12_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc12_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc12_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc12_fcp_class != -1)
				value = (void *)((ulong) lpfc12_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc12_use_adisc != -1)
				value = (void *)((ulong) lpfc12_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc12_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc12_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc12_network_on != -1)
				value = (void *)((ulong) lpfc12_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc12_post_ip_buf != -1)
				value = (void *)((ulong) lpfc12_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc12_xmt_que_size != -1)
				value = (void *)((ulong) lpfc12_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc12_ip_class != -1)
				value = (void *)((ulong) lpfc12_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc12_ack0 != -1)
				value = (void *)((ulong) lpfc12_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc12_topology != -1)
				value = (void *)((ulong) lpfc12_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc12_scan_down != -1)
				value = (void *)((ulong) lpfc12_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc12_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc12_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc12_nodev_holdio != -1)
				value = (void *)((ulong) lpfc12_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc12_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc12_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc12_check_cond_err != -1)
				value = (void *)((ulong) lpfc12_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc12_nodev_tmo != -1)
				value = (void *)((ulong) lpfc12_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc12_link_speed != -1)
				value = (void *)((ulong) lpfc12_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc12_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc12_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc12_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc12_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc12_fdmi_on != -1)
				value = (void *)((ulong) lpfc12_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc12_max_lun != -1)
				value = (void *)((ulong) lpfc12_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc12_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc12_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc12_max_target != -1)
				value = (void *)((ulong) lpfc12_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc12_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc12_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc12_lun_skip != -1)
				value = (void *)((ulong) lpfc12_lun_skip);
			break;
		}
		break;
	case 13:		/* HBA 13 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc13_log_verbose != -1)
				value = (void *)((ulong) lpfc13_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc13_num_iocbs != -1)
				value = (void *)((ulong) lpfc13_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc13_num_bufs != -1)
				value = (void *)((ulong) lpfc13_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc13_automap != -1)
				value = (void *)((ulong) lpfc13_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc13_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc13_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc13_cr_delay != -1)
				value = (void *)((ulong) lpfc13_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc13_cr_count != -1)
				value = (void *)((ulong) lpfc13_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc13_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc13_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc13_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc13_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc13_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc13_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc13_fcp_class != -1)
				value = (void *)((ulong) lpfc13_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc13_use_adisc != -1)
				value = (void *)((ulong) lpfc13_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc13_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc13_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc13_network_on != -1)
				value = (void *)((ulong) lpfc13_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc13_post_ip_buf != -1)
				value = (void *)((ulong) lpfc13_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc13_xmt_que_size != -1)
				value = (void *)((ulong) lpfc13_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc13_ip_class != -1)
				value = (void *)((ulong) lpfc13_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc13_ack0 != -1)
				value = (void *)((ulong) lpfc13_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc13_topology != -1)
				value = (void *)((ulong) lpfc13_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc13_scan_down != -1)
				value = (void *)((ulong) lpfc13_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc13_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc13_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc13_nodev_holdio != -1)
				value = (void *)((ulong) lpfc13_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc13_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc13_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc13_check_cond_err != -1)
				value = (void *)((ulong) lpfc13_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc13_nodev_tmo != -1)
				value = (void *)((ulong) lpfc13_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc13_link_speed != -1)
				value = (void *)((ulong) lpfc13_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc13_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc13_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc13_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc13_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc13_fdmi_on != -1)
				value = (void *)((ulong) lpfc13_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc13_max_lun != -1)
				value = (void *)((ulong) lpfc13_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc13_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc13_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc13_max_target != -1)
				value = (void *)((ulong) lpfc13_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc13_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc13_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc13_lun_skip != -1)
				value = (void *)((ulong) lpfc13_lun_skip);
			break;
		}
		break;
	case 14:		/* HBA 14 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc14_log_verbose != -1)
				value = (void *)((ulong) lpfc14_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc14_num_iocbs != -1)
				value = (void *)((ulong) lpfc14_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc14_num_bufs != -1)
				value = (void *)((ulong) lpfc14_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc14_automap != -1)
				value = (void *)((ulong) lpfc14_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc14_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc14_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc14_cr_delay != -1)
				value = (void *)((ulong) lpfc14_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc14_cr_count != -1)
				value = (void *)((ulong) lpfc14_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc14_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc14_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc14_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc14_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc14_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc14_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc14_fcp_class != -1)
				value = (void *)((ulong) lpfc14_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc14_use_adisc != -1)
				value = (void *)((ulong) lpfc14_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc14_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc14_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc14_network_on != -1)
				value = (void *)((ulong) lpfc14_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc14_post_ip_buf != -1)
				value = (void *)((ulong) lpfc14_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc14_xmt_que_size != -1)
				value = (void *)((ulong) lpfc14_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc14_ip_class != -1)
				value = (void *)((ulong) lpfc14_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc14_ack0 != -1)
				value = (void *)((ulong) lpfc14_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc14_topology != -1)
				value = (void *)((ulong) lpfc14_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc14_scan_down != -1)
				value = (void *)((ulong) lpfc14_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc14_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc14_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc14_nodev_holdio != -1)
				value = (void *)((ulong) lpfc14_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc14_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc14_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc14_check_cond_err != -1)
				value = (void *)((ulong) lpfc14_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc14_nodev_tmo != -1)
				value = (void *)((ulong) lpfc14_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc14_link_speed != -1)
				value = (void *)((ulong) lpfc14_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc14_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc14_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc14_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc14_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc14_fdmi_on != -1)
				value = (void *)((ulong) lpfc14_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc14_max_lun != -1)
				value = (void *)((ulong) lpfc14_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc14_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc14_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc14_max_target != -1)
				value = (void *)((ulong) lpfc14_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc14_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc14_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc14_lun_skip != -1)
				value = (void *)((ulong) lpfc14_lun_skip);
			break;
		}
		break;
	case 15:		/* HBA 15 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc15_log_verbose != -1)
				value = (void *)((ulong) lpfc15_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc15_num_iocbs != -1)
				value = (void *)((ulong) lpfc15_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc15_num_bufs != -1)
				value = (void *)((ulong) lpfc15_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc15_automap != -1)
				value = (void *)((ulong) lpfc15_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc15_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc15_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc15_cr_delay != -1)
				value = (void *)((ulong) lpfc15_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc15_cr_count != -1)
				value = (void *)((ulong) lpfc15_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc15_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc15_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc15_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc15_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc15_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc15_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc15_fcp_class != -1)
				value = (void *)((ulong) lpfc15_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc15_use_adisc != -1)
				value = (void *)((ulong) lpfc15_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc15_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc15_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc15_network_on != -1)
				value = (void *)((ulong) lpfc15_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc15_post_ip_buf != -1)
				value = (void *)((ulong) lpfc15_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc15_xmt_que_size != -1)
				value = (void *)((ulong) lpfc15_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc15_ip_class != -1)
				value = (void *)((ulong) lpfc15_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc15_ack0 != -1)
				value = (void *)((ulong) lpfc15_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc15_topology != -1)
				value = (void *)((ulong) lpfc15_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc15_scan_down != -1)
				value = (void *)((ulong) lpfc15_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc15_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc15_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc15_nodev_holdio != -1)
				value = (void *)((ulong) lpfc15_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc15_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc15_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc15_check_cond_err != -1)
				value = (void *)((ulong) lpfc15_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc15_nodev_tmo != -1)
				value = (void *)((ulong) lpfc15_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc15_link_speed != -1)
				value = (void *)((ulong) lpfc15_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc15_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc15_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc15_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc15_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc15_fdmi_on != -1)
				value = (void *)((ulong) lpfc15_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc15_max_lun != -1)
				value = (void *)((ulong) lpfc15_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc15_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc15_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc15_max_target != -1)
				value = (void *)((ulong) lpfc15_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc15_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc15_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc15_lun_skip != -1)
				value = (void *)((ulong) lpfc15_lun_skip);
			break;
		}
		break;
	case 16:		/* HBA 16 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc16_log_verbose != -1)
				value = (void *)((ulong) lpfc16_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc16_num_iocbs != -1)
				value = (void *)((ulong) lpfc16_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc16_num_bufs != -1)
				value = (void *)((ulong) lpfc16_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc16_automap != -1)
				value = (void *)((ulong) lpfc16_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc16_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc16_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc16_cr_delay != -1)
				value = (void *)((ulong) lpfc16_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc16_cr_count != -1)
				value = (void *)((ulong) lpfc16_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc16_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc16_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc16_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc16_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc16_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc16_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc16_fcp_class != -1)
				value = (void *)((ulong) lpfc16_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc16_use_adisc != -1)
				value = (void *)((ulong) lpfc16_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc16_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc16_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc16_network_on != -1)
				value = (void *)((ulong) lpfc16_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc16_post_ip_buf != -1)
				value = (void *)((ulong) lpfc16_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc16_xmt_que_size != -1)
				value = (void *)((ulong) lpfc16_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc16_ip_class != -1)
				value = (void *)((ulong) lpfc16_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc16_ack0 != -1)
				value = (void *)((ulong) lpfc16_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc16_topology != -1)
				value = (void *)((ulong) lpfc16_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc16_scan_down != -1)
				value = (void *)((ulong) lpfc16_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc16_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc16_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc16_nodev_holdio != -1)
				value = (void *)((ulong) lpfc16_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc16_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc16_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc16_check_cond_err != -1)
				value = (void *)((ulong) lpfc16_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc16_nodev_tmo != -1)
				value = (void *)((ulong) lpfc16_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc16_link_speed != -1)
				value = (void *)((ulong) lpfc16_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc16_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc16_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc16_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc16_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc16_fdmi_on != -1)
				value = (void *)((ulong) lpfc16_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc16_max_lun != -1)
				value = (void *)((ulong) lpfc16_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc16_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc16_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc16_max_target != -1)
				value = (void *)((ulong) lpfc16_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc16_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc16_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc16_lun_skip != -1)
				value = (void *)((ulong) lpfc16_lun_skip);
			break;
		}
		break;
	case 17:		/* HBA 17 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc17_log_verbose != -1)
				value = (void *)((ulong) lpfc17_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc17_num_iocbs != -1)
				value = (void *)((ulong) lpfc17_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc17_num_bufs != -1)
				value = (void *)((ulong) lpfc17_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc17_automap != -1)
				value = (void *)((ulong) lpfc17_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc17_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc17_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc17_cr_delay != -1)
				value = (void *)((ulong) lpfc17_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc17_cr_count != -1)
				value = (void *)((ulong) lpfc17_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc17_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc17_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc17_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc17_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc17_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc17_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc17_fcp_class != -1)
				value = (void *)((ulong) lpfc17_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc17_use_adisc != -1)
				value = (void *)((ulong) lpfc17_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc17_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc17_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc17_network_on != -1)
				value = (void *)((ulong) lpfc17_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc17_post_ip_buf != -1)
				value = (void *)((ulong) lpfc17_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc17_xmt_que_size != -1)
				value = (void *)((ulong) lpfc17_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc17_ip_class != -1)
				value = (void *)((ulong) lpfc17_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc17_ack0 != -1)
				value = (void *)((ulong) lpfc17_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc17_topology != -1)
				value = (void *)((ulong) lpfc17_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc17_scan_down != -1)
				value = (void *)((ulong) lpfc17_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc17_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc17_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc17_nodev_holdio != -1)
				value = (void *)((ulong) lpfc17_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc17_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc17_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc17_check_cond_err != -1)
				value = (void *)((ulong) lpfc17_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc17_nodev_tmo != -1)
				value = (void *)((ulong) lpfc17_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc17_link_speed != -1)
				value = (void *)((ulong) lpfc17_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc17_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc17_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc17_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc17_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc17_fdmi_on != -1)
				value = (void *)((ulong) lpfc17_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc17_max_lun != -1)
				value = (void *)((ulong) lpfc17_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc17_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc17_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc17_max_target != -1)
				value = (void *)((ulong) lpfc17_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc17_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc17_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc17_lun_skip != -1)
				value = (void *)((ulong) lpfc17_lun_skip);
			break;
		}
		break;
	case 18:		/* HBA 18 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc18_log_verbose != -1)
				value = (void *)((ulong) lpfc18_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc18_num_iocbs != -1)
				value = (void *)((ulong) lpfc18_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc18_num_bufs != -1)
				value = (void *)((ulong) lpfc18_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc18_automap != -1)
				value = (void *)((ulong) lpfc18_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc18_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc18_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc18_cr_delay != -1)
				value = (void *)((ulong) lpfc18_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc18_cr_count != -1)
				value = (void *)((ulong) lpfc18_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc18_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc18_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc18_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc18_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc18_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc18_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc18_fcp_class != -1)
				value = (void *)((ulong) lpfc18_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc18_use_adisc != -1)
				value = (void *)((ulong) lpfc18_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc18_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc18_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc18_network_on != -1)
				value = (void *)((ulong) lpfc18_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc18_post_ip_buf != -1)
				value = (void *)((ulong) lpfc18_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc18_xmt_que_size != -1)
				value = (void *)((ulong) lpfc18_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc18_ip_class != -1)
				value = (void *)((ulong) lpfc18_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc18_ack0 != -1)
				value = (void *)((ulong) lpfc18_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc18_topology != -1)
				value = (void *)((ulong) lpfc18_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc18_scan_down != -1)
				value = (void *)((ulong) lpfc18_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc18_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc18_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc18_nodev_holdio != -1)
				value = (void *)((ulong) lpfc18_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc18_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc18_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc18_check_cond_err != -1)
				value = (void *)((ulong) lpfc18_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc18_nodev_tmo != -1)
				value = (void *)((ulong) lpfc18_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc18_link_speed != -1)
				value = (void *)((ulong) lpfc18_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc18_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc18_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc18_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc18_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc18_fdmi_on != -1)
				value = (void *)((ulong) lpfc18_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc18_max_lun != -1)
				value = (void *)((ulong) lpfc18_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc18_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc18_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc18_max_target != -1)
				value = (void *)((ulong) lpfc18_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc18_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc18_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc18_lun_skip != -1)
				value = (void *)((ulong) lpfc18_lun_skip);
			break;
		}
		break;
	case 19:		/* HBA 19 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc19_log_verbose != -1)
				value = (void *)((ulong) lpfc19_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc19_num_iocbs != -1)
				value = (void *)((ulong) lpfc19_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc19_num_bufs != -1)
				value = (void *)((ulong) lpfc19_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc19_automap != -1)
				value = (void *)((ulong) lpfc19_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc19_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc19_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc19_cr_delay != -1)
				value = (void *)((ulong) lpfc19_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc19_cr_count != -1)
				value = (void *)((ulong) lpfc19_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc19_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc19_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc19_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc19_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc19_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc19_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc19_fcp_class != -1)
				value = (void *)((ulong) lpfc19_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc19_use_adisc != -1)
				value = (void *)((ulong) lpfc19_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc19_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc19_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc19_network_on != -1)
				value = (void *)((ulong) lpfc19_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc19_post_ip_buf != -1)
				value = (void *)((ulong) lpfc19_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc19_xmt_que_size != -1)
				value = (void *)((ulong) lpfc19_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc19_ip_class != -1)
				value = (void *)((ulong) lpfc19_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc19_ack0 != -1)
				value = (void *)((ulong) lpfc19_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc19_topology != -1)
				value = (void *)((ulong) lpfc19_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc19_scan_down != -1)
				value = (void *)((ulong) lpfc19_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc19_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc19_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc19_nodev_holdio != -1)
				value = (void *)((ulong) lpfc19_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc19_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc19_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc19_check_cond_err != -1)
				value = (void *)((ulong) lpfc19_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc19_nodev_tmo != -1)
				value = (void *)((ulong) lpfc19_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc19_link_speed != -1)
				value = (void *)((ulong) lpfc19_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc19_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc19_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc19_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc19_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc19_fdmi_on != -1)
				value = (void *)((ulong) lpfc19_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc19_max_lun != -1)
				value = (void *)((ulong) lpfc19_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc19_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc19_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc19_max_target != -1)
				value = (void *)((ulong) lpfc19_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc19_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc19_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc19_lun_skip != -1)
				value = (void *)((ulong) lpfc19_lun_skip);
			break;
		}
		break;
	case 20:		/* HBA 20 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc20_log_verbose != -1)
				value = (void *)((ulong) lpfc20_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc20_num_iocbs != -1)
				value = (void *)((ulong) lpfc20_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc20_num_bufs != -1)
				value = (void *)((ulong) lpfc20_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc20_automap != -1)
				value = (void *)((ulong) lpfc20_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc20_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc20_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc20_cr_delay != -1)
				value = (void *)((ulong) lpfc20_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc20_cr_count != -1)
				value = (void *)((ulong) lpfc20_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc20_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc20_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc20_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc20_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc20_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc20_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc20_fcp_class != -1)
				value = (void *)((ulong) lpfc20_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc20_use_adisc != -1)
				value = (void *)((ulong) lpfc20_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc20_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc20_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc20_network_on != -1)
				value = (void *)((ulong) lpfc20_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc20_post_ip_buf != -1)
				value = (void *)((ulong) lpfc20_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc20_xmt_que_size != -1)
				value = (void *)((ulong) lpfc20_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc20_ip_class != -1)
				value = (void *)((ulong) lpfc20_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc20_ack0 != -1)
				value = (void *)((ulong) lpfc20_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc20_topology != -1)
				value = (void *)((ulong) lpfc20_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc20_scan_down != -1)
				value = (void *)((ulong) lpfc20_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc20_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc20_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc20_nodev_holdio != -1)
				value = (void *)((ulong) lpfc20_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc20_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc20_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc20_check_cond_err != -1)
				value = (void *)((ulong) lpfc20_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc20_nodev_tmo != -1)
				value = (void *)((ulong) lpfc20_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc20_link_speed != -1)
				value = (void *)((ulong) lpfc20_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc20_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc20_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc20_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc20_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc20_fdmi_on != -1)
				value = (void *)((ulong) lpfc20_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc20_max_lun != -1)
				value = (void *)((ulong) lpfc20_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc20_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc20_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc20_max_target != -1)
				value = (void *)((ulong) lpfc20_max_target);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc20_lun_skip != -1)
				value = (void *)((ulong) lpfc20_lun_skip);
			break;
		}
		break;
	case 21:		/* HBA 21 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc21_log_verbose != -1)
				value = (void *)((ulong) lpfc21_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc21_num_iocbs != -1)
				value = (void *)((ulong) lpfc21_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc21_num_bufs != -1)
				value = (void *)((ulong) lpfc21_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc21_automap != -1)
				value = (void *)((ulong) lpfc21_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc21_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc21_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc21_cr_delay != -1)
				value = (void *)((ulong) lpfc21_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc21_cr_count != -1)
				value = (void *)((ulong) lpfc21_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc21_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc21_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc21_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc21_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc21_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc21_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc21_fcp_class != -1)
				value = (void *)((ulong) lpfc21_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc21_use_adisc != -1)
				value = (void *)((ulong) lpfc21_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc21_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc21_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc21_network_on != -1)
				value = (void *)((ulong) lpfc21_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc21_post_ip_buf != -1)
				value = (void *)((ulong) lpfc21_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc21_xmt_que_size != -1)
				value = (void *)((ulong) lpfc21_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc21_ip_class != -1)
				value = (void *)((ulong) lpfc21_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc21_ack0 != -1)
				value = (void *)((ulong) lpfc21_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc21_topology != -1)
				value = (void *)((ulong) lpfc21_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc21_scan_down != -1)
				value = (void *)((ulong) lpfc21_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc21_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc21_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc21_nodev_holdio != -1)
				value = (void *)((ulong) lpfc21_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc21_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc21_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc21_check_cond_err != -1)
				value = (void *)((ulong) lpfc21_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc21_nodev_tmo != -1)
				value = (void *)((ulong) lpfc21_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc21_link_speed != -1)
				value = (void *)((ulong) lpfc21_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc21_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc21_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc21_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc21_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc21_fdmi_on != -1)
				value = (void *)((ulong) lpfc21_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc21_max_lun != -1)
				value = (void *)((ulong) lpfc21_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc21_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc21_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc21_max_target != -1)
				value = (void *)((ulong) lpfc21_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc21_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc21_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc21_lun_skip != -1)
				value = (void *)((ulong) lpfc21_lun_skip);
			break;
		}
		break;
	case 22:		/* HBA 22 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc22_log_verbose != -1)
				value = (void *)((ulong) lpfc22_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc22_num_iocbs != -1)
				value = (void *)((ulong) lpfc22_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc22_num_bufs != -1)
				value = (void *)((ulong) lpfc22_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc22_automap != -1)
				value = (void *)((ulong) lpfc22_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc22_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc22_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc22_cr_delay != -1)
				value = (void *)((ulong) lpfc22_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc22_cr_count != -1)
				value = (void *)((ulong) lpfc22_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc22_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc22_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc22_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc22_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc22_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc22_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc22_fcp_class != -1)
				value = (void *)((ulong) lpfc22_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc22_use_adisc != -1)
				value = (void *)((ulong) lpfc22_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc22_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc22_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc22_network_on != -1)
				value = (void *)((ulong) lpfc22_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc22_post_ip_buf != -1)
				value = (void *)((ulong) lpfc22_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc22_xmt_que_size != -1)
				value = (void *)((ulong) lpfc22_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc22_ip_class != -1)
				value = (void *)((ulong) lpfc22_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc22_ack0 != -1)
				value = (void *)((ulong) lpfc22_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc22_topology != -1)
				value = (void *)((ulong) lpfc22_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc22_scan_down != -1)
				value = (void *)((ulong) lpfc22_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc22_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc22_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc22_nodev_holdio != -1)
				value = (void *)((ulong) lpfc22_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc22_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc22_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc22_check_cond_err != -1)
				value = (void *)((ulong) lpfc22_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc22_nodev_tmo != -1)
				value = (void *)((ulong) lpfc22_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc22_link_speed != -1)
				value = (void *)((ulong) lpfc22_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc22_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc22_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc22_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc22_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc22_fdmi_on != -1)
				value = (void *)((ulong) lpfc22_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc22_max_lun != -1)
				value = (void *)((ulong) lpfc22_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc22_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc22_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc22_max_target != -1)
				value = (void *)((ulong) lpfc22_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc22_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc22_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc22_lun_skip != -1)
				value = (void *)((ulong) lpfc22_lun_skip);
			break;
		}
		break;
	case 23:		/* HBA 23 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc23_log_verbose != -1)
				value = (void *)((ulong) lpfc23_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc23_num_iocbs != -1)
				value = (void *)((ulong) lpfc23_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc23_num_bufs != -1)
				value = (void *)((ulong) lpfc23_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc23_automap != -1)
				value = (void *)((ulong) lpfc23_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc23_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc23_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc23_cr_delay != -1)
				value = (void *)((ulong) lpfc23_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc23_cr_count != -1)
				value = (void *)((ulong) lpfc23_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc23_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc23_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc23_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc23_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc23_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc23_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc23_fcp_class != -1)
				value = (void *)((ulong) lpfc23_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc23_use_adisc != -1)
				value = (void *)((ulong) lpfc23_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc23_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc23_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc23_network_on != -1)
				value = (void *)((ulong) lpfc23_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc23_post_ip_buf != -1)
				value = (void *)((ulong) lpfc23_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc23_xmt_que_size != -1)
				value = (void *)((ulong) lpfc23_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc23_ip_class != -1)
				value = (void *)((ulong) lpfc23_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc23_ack0 != -1)
				value = (void *)((ulong) lpfc23_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc23_topology != -1)
				value = (void *)((ulong) lpfc23_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc23_scan_down != -1)
				value = (void *)((ulong) lpfc23_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc23_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc23_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc23_nodev_holdio != -1)
				value = (void *)((ulong) lpfc23_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc23_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc23_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc23_check_cond_err != -1)
				value = (void *)((ulong) lpfc23_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc23_nodev_tmo != -1)
				value = (void *)((ulong) lpfc23_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc23_link_speed != -1)
				value = (void *)((ulong) lpfc23_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc23_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc23_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc23_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc23_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc23_fdmi_on != -1)
				value = (void *)((ulong) lpfc23_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc23_max_lun != -1)
				value = (void *)((ulong) lpfc23_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc23_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc23_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc23_max_target != -1)
				value = (void *)((ulong) lpfc23_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc23_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc23_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc23_lun_skip != -1)
				value = (void *)((ulong) lpfc23_lun_skip);
			break;
		}
		break;
	case 24:		/* HBA 24 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc24_log_verbose != -1)
				value = (void *)((ulong) lpfc24_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc24_num_iocbs != -1)
				value = (void *)((ulong) lpfc24_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc24_num_bufs != -1)
				value = (void *)((ulong) lpfc24_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc24_automap != -1)
				value = (void *)((ulong) lpfc24_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc24_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc24_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc24_cr_delay != -1)
				value = (void *)((ulong) lpfc24_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc24_cr_count != -1)
				value = (void *)((ulong) lpfc24_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc24_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc24_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc24_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc24_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc24_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc24_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc24_fcp_class != -1)
				value = (void *)((ulong) lpfc24_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc24_use_adisc != -1)
				value = (void *)((ulong) lpfc24_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc24_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc24_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc24_network_on != -1)
				value = (void *)((ulong) lpfc24_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc24_post_ip_buf != -1)
				value = (void *)((ulong) lpfc24_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc24_xmt_que_size != -1)
				value = (void *)((ulong) lpfc24_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc24_ip_class != -1)
				value = (void *)((ulong) lpfc24_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc24_ack0 != -1)
				value = (void *)((ulong) lpfc24_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc24_topology != -1)
				value = (void *)((ulong) lpfc24_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc24_scan_down != -1)
				value = (void *)((ulong) lpfc24_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc24_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc24_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc24_nodev_holdio != -1)
				value = (void *)((ulong) lpfc24_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc24_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc24_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc24_check_cond_err != -1)
				value = (void *)((ulong) lpfc24_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc24_nodev_tmo != -1)
				value = (void *)((ulong) lpfc24_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc24_link_speed != -1)
				value = (void *)((ulong) lpfc24_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc24_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc24_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc24_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc24_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc24_fdmi_on != -1)
				value = (void *)((ulong) lpfc24_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc24_max_lun != -1)
				value = (void *)((ulong) lpfc24_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc24_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc24_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc24_max_target != -1)
				value = (void *)((ulong) lpfc24_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc24_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc24_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc24_lun_skip != -1)
				value = (void *)((ulong) lpfc24_lun_skip);
			break;
		}
		break;
	case 25:		/* HBA 25 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc25_log_verbose != -1)
				value = (void *)((ulong) lpfc25_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc25_num_iocbs != -1)
				value = (void *)((ulong) lpfc25_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc25_num_bufs != -1)
				value = (void *)((ulong) lpfc25_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc25_automap != -1)
				value = (void *)((ulong) lpfc25_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc25_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc25_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc25_cr_delay != -1)
				value = (void *)((ulong) lpfc25_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc25_cr_count != -1)
				value = (void *)((ulong) lpfc25_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc25_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc25_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc25_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc25_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc25_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc25_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc25_fcp_class != -1)
				value = (void *)((ulong) lpfc25_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc25_use_adisc != -1)
				value = (void *)((ulong) lpfc25_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc25_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc25_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc25_network_on != -1)
				value = (void *)((ulong) lpfc25_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc25_post_ip_buf != -1)
				value = (void *)((ulong) lpfc25_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc25_xmt_que_size != -1)
				value = (void *)((ulong) lpfc25_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc25_ip_class != -1)
				value = (void *)((ulong) lpfc25_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc25_ack0 != -1)
				value = (void *)((ulong) lpfc25_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc25_topology != -1)
				value = (void *)((ulong) lpfc25_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc25_scan_down != -1)
				value = (void *)((ulong) lpfc25_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc25_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc25_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc25_nodev_holdio != -1)
				value = (void *)((ulong) lpfc25_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc25_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc25_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc25_check_cond_err != -1)
				value = (void *)((ulong) lpfc25_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc25_nodev_tmo != -1)
				value = (void *)((ulong) lpfc25_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc25_link_speed != -1)
				value = (void *)((ulong) lpfc25_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc25_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc25_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc25_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc25_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc25_fdmi_on != -1)
				value = (void *)((ulong) lpfc25_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc25_max_lun != -1)
				value = (void *)((ulong) lpfc25_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc25_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc25_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc25_max_target != -1)
				value = (void *)((ulong) lpfc25_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc25_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc25_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc25_lun_skip != -1)
				value = (void *)((ulong) lpfc25_lun_skip);
			break;
		}
		break;
	case 26:		/* HBA 26 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc26_log_verbose != -1)
				value = (void *)((ulong) lpfc26_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc26_num_iocbs != -1)
				value = (void *)((ulong) lpfc26_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc26_num_bufs != -1)
				value = (void *)((ulong) lpfc26_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc26_automap != -1)
				value = (void *)((ulong) lpfc26_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc26_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc26_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc26_cr_delay != -1)
				value = (void *)((ulong) lpfc26_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc26_cr_count != -1)
				value = (void *)((ulong) lpfc26_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc26_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc26_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc26_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc26_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc26_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc26_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc26_fcp_class != -1)
				value = (void *)((ulong) lpfc26_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc26_use_adisc != -1)
				value = (void *)((ulong) lpfc26_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc26_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc26_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc26_network_on != -1)
				value = (void *)((ulong) lpfc26_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc26_post_ip_buf != -1)
				value = (void *)((ulong) lpfc26_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc26_xmt_que_size != -1)
				value = (void *)((ulong) lpfc26_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc26_ip_class != -1)
				value = (void *)((ulong) lpfc26_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc26_ack0 != -1)
				value = (void *)((ulong) lpfc26_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc26_topology != -1)
				value = (void *)((ulong) lpfc26_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc26_scan_down != -1)
				value = (void *)((ulong) lpfc26_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc26_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc26_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc26_nodev_holdio != -1)
				value = (void *)((ulong) lpfc26_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc26_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc26_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc26_check_cond_err != -1)
				value = (void *)((ulong) lpfc26_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc26_nodev_tmo != -1)
				value = (void *)((ulong) lpfc26_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc26_link_speed != -1)
				value = (void *)((ulong) lpfc26_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc26_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc26_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc26_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc26_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc26_fdmi_on != -1)
				value = (void *)((ulong) lpfc26_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc26_max_lun != -1)
				value = (void *)((ulong) lpfc26_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc26_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc26_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc26_max_target != -1)
				value = (void *)((ulong) lpfc26_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc26_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc26_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc26_lun_skip != -1)
				value = (void *)((ulong) lpfc26_lun_skip);
			break;
		}
		break;
	case 27:		/* HBA 27 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc27_log_verbose != -1)
				value = (void *)((ulong) lpfc27_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc27_num_iocbs != -1)
				value = (void *)((ulong) lpfc27_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc27_num_bufs != -1)
				value = (void *)((ulong) lpfc27_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc27_automap != -1)
				value = (void *)((ulong) lpfc27_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc27_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc27_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc27_cr_delay != -1)
				value = (void *)((ulong) lpfc27_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc27_cr_count != -1)
				value = (void *)((ulong) lpfc27_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc27_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc27_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc27_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc27_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc27_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc27_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc27_fcp_class != -1)
				value = (void *)((ulong) lpfc27_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc27_use_adisc != -1)
				value = (void *)((ulong) lpfc27_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc27_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc27_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc27_network_on != -1)
				value = (void *)((ulong) lpfc27_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc27_post_ip_buf != -1)
				value = (void *)((ulong) lpfc27_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc27_xmt_que_size != -1)
				value = (void *)((ulong) lpfc27_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc27_ip_class != -1)
				value = (void *)((ulong) lpfc27_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc27_ack0 != -1)
				value = (void *)((ulong) lpfc27_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc27_topology != -1)
				value = (void *)((ulong) lpfc27_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc27_scan_down != -1)
				value = (void *)((ulong) lpfc27_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc27_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc27_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc27_nodev_holdio != -1)
				value = (void *)((ulong) lpfc27_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc27_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc27_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc27_check_cond_err != -1)
				value = (void *)((ulong) lpfc27_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc27_nodev_tmo != -1)
				value = (void *)((ulong) lpfc27_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc27_link_speed != -1)
				value = (void *)((ulong) lpfc27_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc27_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc27_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc27_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc27_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc27_fdmi_on != -1)
				value = (void *)((ulong) lpfc27_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc27_max_lun != -1)
				value = (void *)((ulong) lpfc27_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc27_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc27_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc27_max_target != -1)
				value = (void *)((ulong) lpfc27_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc27_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc27_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc27_lun_skip != -1)
				value = (void *)((ulong) lpfc27_lun_skip);
			break;
		}
		break;
	case 28:		/* HBA 28 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc28_log_verbose != -1)
				value = (void *)((ulong) lpfc28_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc28_num_iocbs != -1)
				value = (void *)((ulong) lpfc28_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc28_num_bufs != -1)
				value = (void *)((ulong) lpfc28_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc28_automap != -1)
				value = (void *)((ulong) lpfc28_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc28_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc28_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc28_cr_delay != -1)
				value = (void *)((ulong) lpfc28_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc28_cr_count != -1)
				value = (void *)((ulong) lpfc28_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc28_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc28_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc28_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc28_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc28_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc28_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc28_fcp_class != -1)
				value = (void *)((ulong) lpfc28_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc28_use_adisc != -1)
				value = (void *)((ulong) lpfc28_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc28_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc28_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc28_network_on != -1)
				value = (void *)((ulong) lpfc28_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc28_post_ip_buf != -1)
				value = (void *)((ulong) lpfc28_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc28_xmt_que_size != -1)
				value = (void *)((ulong) lpfc28_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc28_ip_class != -1)
				value = (void *)((ulong) lpfc28_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc28_ack0 != -1)
				value = (void *)((ulong) lpfc28_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc28_topology != -1)
				value = (void *)((ulong) lpfc28_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc28_scan_down != -1)
				value = (void *)((ulong) lpfc28_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc28_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc28_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc28_nodev_holdio != -1)
				value = (void *)((ulong) lpfc28_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc28_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc28_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc28_check_cond_err != -1)
				value = (void *)((ulong) lpfc28_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc28_nodev_tmo != -1)
				value = (void *)((ulong) lpfc28_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc28_link_speed != -1)
				value = (void *)((ulong) lpfc28_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc28_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc28_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc28_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc28_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc28_fdmi_on != -1)
				value = (void *)((ulong) lpfc28_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc28_max_lun != -1)
				value = (void *)((ulong) lpfc28_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc28_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc28_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc28_max_target != -1)
				value = (void *)((ulong) lpfc28_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc28_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc28_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc28_lun_skip != -1)
				value = (void *)((ulong) lpfc28_lun_skip);
			break;
		}
		break;
	case 29:		/* HBA 29 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc29_log_verbose != -1)
				value = (void *)((ulong) lpfc29_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc29_num_iocbs != -1)
				value = (void *)((ulong) lpfc29_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc29_num_bufs != -1)
				value = (void *)((ulong) lpfc29_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc29_automap != -1)
				value = (void *)((ulong) lpfc29_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc29_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc29_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc29_cr_delay != -1)
				value = (void *)((ulong) lpfc29_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc29_cr_count != -1)
				value = (void *)((ulong) lpfc29_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc29_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc29_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc29_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc29_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc29_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc29_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc29_fcp_class != -1)
				value = (void *)((ulong) lpfc29_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc29_use_adisc != -1)
				value = (void *)((ulong) lpfc29_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc29_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc29_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc29_network_on != -1)
				value = (void *)((ulong) lpfc29_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc29_post_ip_buf != -1)
				value = (void *)((ulong) lpfc29_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc29_xmt_que_size != -1)
				value = (void *)((ulong) lpfc29_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc29_ip_class != -1)
				value = (void *)((ulong) lpfc29_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc29_ack0 != -1)
				value = (void *)((ulong) lpfc29_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc29_topology != -1)
				value = (void *)((ulong) lpfc29_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc29_scan_down != -1)
				value = (void *)((ulong) lpfc29_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc29_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc29_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc29_nodev_holdio != -1)
				value = (void *)((ulong) lpfc29_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc29_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc29_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc29_check_cond_err != -1)
				value = (void *)((ulong) lpfc29_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc29_nodev_tmo != -1)
				value = (void *)((ulong) lpfc29_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc29_link_speed != -1)
				value = (void *)((ulong) lpfc29_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc29_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc29_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc29_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc29_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc29_fdmi_on != -1)
				value = (void *)((ulong) lpfc29_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc29_max_lun != -1)
				value = (void *)((ulong) lpfc29_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc29_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc29_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc29_max_target != -1)
				value = (void *)((ulong) lpfc29_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc29_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc29_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc29_lun_skip != -1)
				value = (void *)((ulong) lpfc29_lun_skip);
			break;
		}
		break;
	case 30:		/* HBA 30 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc30_log_verbose != -1)
				value = (void *)((ulong) lpfc30_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc30_num_iocbs != -1)
				value = (void *)((ulong) lpfc30_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc30_num_bufs != -1)
				value = (void *)((ulong) lpfc30_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc30_automap != -1)
				value = (void *)((ulong) lpfc30_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc30_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc30_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc30_cr_delay != -1)
				value = (void *)((ulong) lpfc30_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc30_cr_count != -1)
				value = (void *)((ulong) lpfc30_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc30_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc30_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc30_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc30_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc30_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc30_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc30_fcp_class != -1)
				value = (void *)((ulong) lpfc30_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc30_use_adisc != -1)
				value = (void *)((ulong) lpfc30_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc30_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc30_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc30_network_on != -1)
				value = (void *)((ulong) lpfc30_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc30_post_ip_buf != -1)
				value = (void *)((ulong) lpfc30_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc30_xmt_que_size != -1)
				value = (void *)((ulong) lpfc30_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc30_ip_class != -1)
				value = (void *)((ulong) lpfc30_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc30_ack0 != -1)
				value = (void *)((ulong) lpfc30_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc30_topology != -1)
				value = (void *)((ulong) lpfc30_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc30_scan_down != -1)
				value = (void *)((ulong) lpfc30_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc30_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc30_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc30_nodev_holdio != -1)
				value = (void *)((ulong) lpfc30_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc30_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc30_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc30_check_cond_err != -1)
				value = (void *)((ulong) lpfc30_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc30_nodev_tmo != -1)
				value = (void *)((ulong) lpfc30_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc30_link_speed != -1)
				value = (void *)((ulong) lpfc30_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc30_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc30_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc30_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc30_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc30_fdmi_on != -1)
				value = (void *)((ulong) lpfc30_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc30_max_lun != -1)
				value = (void *)((ulong) lpfc30_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc30_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc30_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc30_max_target != -1)
				value = (void *)((ulong) lpfc30_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc30_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc30_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc30_lun_skip != -1)
				value = (void *)((ulong) lpfc30_lun_skip);
			break;
		}
		break;
	case 31:		/* HBA 31 */
		switch (param) {
		case ELX_CFG_LOG_VERBOSE:	/* log-verbose */
			value = (void *)((ulong) lpfc_log_verbose);
			if (lpfc31_log_verbose != -1)
				value = (void *)((ulong) lpfc31_log_verbose);
			break;
		case ELX_CFG_NUM_IOCBS:	/* num-iocbs */
			value = (void *)((ulong) lpfc_num_iocbs);
			if (lpfc31_num_iocbs != -1)
				value = (void *)((ulong) lpfc31_num_iocbs);
			break;
		case ELX_CFG_NUM_BUFS:	/* num-bufs */
			value = (void *)((ulong) lpfc_num_bufs);
			if (lpfc31_num_bufs != -1)
				value = (void *)((ulong) lpfc31_num_bufs);
			break;
		case LPFC_CFG_AUTOMAP:	/* automap */
			value = (void *)((ulong) lpfc_automap);
			if (lpfc31_automap != -1)
				value = (void *)((ulong) lpfc31_automap);
			break;
		case LPFC_CFG_BINDMETHOD:	/* bind-method */
			value = (void *)((ulong) lpfc_fcp_bind_method);
			if (lpfc31_fcp_bind_method != -1)
				value =
				    (void *)((ulong) lpfc31_fcp_bind_method);
			break;
		case LPFC_CFG_CR_DELAY:	/* cr_delay */
			value = (void *)((ulong) lpfc_cr_delay);
			if (lpfc31_cr_delay != -1)
				value = (void *)((ulong) lpfc31_cr_delay);
			break;
		case LPFC_CFG_CR_COUNT:	/* cr_count */
			value = (void *)((ulong) lpfc_cr_count);
			if (lpfc31_cr_count != -1)
				value = (void *)((ulong) lpfc31_cr_count);
			break;
		case ELX_CFG_DFT_TGT_Q_DEPTH:	/* tgt_queue_depth */
			value = (void *)((ulong) lpfc_tgt_queue_depth);
			if (lpfc31_tgt_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc31_tgt_queue_depth);
			break;
		case ELX_CFG_DFT_LUN_Q_DEPTH:	/* lun_queue_depth */
			value = (void *)((ulong) lpfc_lun_queue_depth);
			if (lpfc31_lun_queue_depth != -1)
				value =
				    (void *)((ulong) lpfc31_lun_queue_depth);
			break;
		case ELX_CFG_EXTRA_IO_TMO:	/* fcpfabric-tmo */
			value = (void *)((ulong) lpfc_extra_io_tmo);
			if (lpfc31_extra_io_tmo != -1)
				value = (void *)((ulong) lpfc31_extra_io_tmo);
			break;
		case LPFC_CFG_FCP_CLASS:	/* fcp-class */
			value = (void *)((ulong) lpfc_fcp_class);
			if (lpfc31_fcp_class != -1)
				value = (void *)((ulong) lpfc31_fcp_class);
			break;
		case LPFC_CFG_USE_ADISC:	/* use-adisc */
			value = (void *)((ulong) lpfc_use_adisc);
			if (lpfc31_use_adisc != -1)
				value = (void *)((ulong) lpfc31_use_adisc);
			break;
		case ELX_CFG_NO_DEVICE_DELAY:	/* no-device-delay */
			value = (void *)((ulong) lpfc_no_device_delay);
			if (lpfc31_no_device_delay != -1)
				value =
				    (void *)((ulong) lpfc31_no_device_delay);
			break;
		case LPFC_CFG_NETWORK_ON:	/* network-on */
			value = (void *)((ulong) lpfc_network_on);
			if (lpfc31_network_on != -1)
				value = (void *)((ulong) lpfc31_network_on);
			break;
		case LPFC_CFG_POST_IP_BUF:	/* post-ip-buf */
			value = (void *)((ulong) lpfc_post_ip_buf);
			if (lpfc31_post_ip_buf != -1)
				value = (void *)((ulong) lpfc31_post_ip_buf);
			break;
		case LPFC_CFG_XMT_Q_SIZE:	/* xmt-que-size */
			value = (void *)((ulong) lpfc_xmt_que_size);
			if (lpfc31_xmt_que_size != -1)
				value = (void *)((ulong) lpfc31_xmt_que_size);
			break;
		case LPFC_CFG_IP_CLASS:	/* ip-class */
			value = (void *)((ulong) lpfc_ip_class);
			if (lpfc31_ip_class != -1)
				value = (void *)((ulong) lpfc31_ip_class);
			break;
		case LPFC_CFG_ACK0:	/* ack0 */
			value = (void *)((ulong) lpfc_ack0);
			if (lpfc31_ack0 != -1)
				value = (void *)((ulong) lpfc31_ack0);
			break;
		case LPFC_CFG_TOPOLOGY:	/* topology */
			value = (void *)((ulong) lpfc_topology);
			if (lpfc31_topology != -1)
				value = (void *)((ulong) lpfc31_topology);
			break;
		case LPFC_CFG_SCAN_DOWN:	/* scan-down */
			value = (void *)((ulong) lpfc_scan_down);
			if (lpfc31_scan_down != -1)
				value = (void *)((ulong) lpfc31_scan_down);
			break;
		case ELX_CFG_LINKDOWN_TMO:	/* linkdown-tmo */
			value = (void *)((ulong) lpfc_linkdown_tmo);
			if (lpfc31_linkdown_tmo != -1)
				value = (void *)((ulong) lpfc31_linkdown_tmo);
			break;
		case ELX_CFG_HOLDIO:	/* nodev-holdio */
			value = (void *)((ulong) lpfc_nodev_holdio);
			if (lpfc31_nodev_holdio != -1)
				value = (void *)((ulong) lpfc31_nodev_holdio);
			break;
		case ELX_CFG_DELAY_RSP_ERR:	/* delay-rsp-err */
			value = (void *)((ulong) lpfc_delay_rsp_err);
			if (lpfc31_delay_rsp_err != -1)
				value = (void *)((ulong) lpfc31_delay_rsp_err);
			break;
		case ELX_CFG_CHK_COND_ERR:	/* check-cond-err */
			value = (void *)((ulong) lpfc_check_cond_err);
			if (lpfc31_check_cond_err != -1)
				value = (void *)((ulong) lpfc31_check_cond_err);
			break;
		case ELX_CFG_NODEV_TMO:	/* nodev-tmo */
			value = (void *)((ulong) lpfc_nodev_tmo);
			if (lpfc31_nodev_tmo != -1)
				value = (void *)((ulong) lpfc31_nodev_tmo);
			break;
		case LPFC_CFG_LINK_SPEED:	/* link-speed */
			value = (void *)((ulong) lpfc_link_speed);
			if (lpfc31_link_speed != -1)
				value = (void *)((ulong) lpfc31_link_speed);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_TIME:	/* dqfull-throttle-up-time */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_time);
			if (lpfc31_dqfull_throttle_up_time != -1)
				value =
				    (void *)((ulong)
					     lpfc31_dqfull_throttle_up_time);
			break;
		case ELX_CFG_DQFULL_THROTTLE_UP_INC:	/* dqfull-throttle-up-inc */
			value = (void *)((ulong) lpfc_dqfull_throttle_up_inc);
			if (lpfc31_dqfull_throttle_up_inc != -1)
				value =
				    (void *)((ulong)
					     lpfc31_dqfull_throttle_up_inc);
			break;
		case LPFC_CFG_FDMI_ON:	/* fdmi-on */
			value = (void *)((ulong) lpfc_fdmi_on);
			if (lpfc31_fdmi_on != -1)
				value = (void *)((ulong) lpfc31_fdmi_on);
			break;
		case ELX_CFG_MAX_LUN:	/* max-lun */
			value = (void *)((ulong) lpfc_max_lun);
			if (lpfc31_max_lun != -1)
				value = (void *)((ulong) lpfc31_max_lun);
			break;
		case LPFC_CFG_DISC_THREADS:	/* discovery-threads */
			value = (void *)((ulong) lpfc_discovery_threads);
			if (lpfc31_discovery_threads != -1)
				value =
				    (void *)((ulong) lpfc31_discovery_threads);
			break;
		case LPFC_CFG_MAX_TARGET:	/* max-target */
			value = (void *)((ulong) lpfc_max_target);
			if (lpfc31_max_target != -1)
				value = (void *)((ulong) lpfc31_max_target);
			break;
		case LPFC_CFG_SCSI_REQ_TMO:	/* scsi-req-tmo */
			value = (void *)((ulong) lpfc_scsi_req_tmo);
			if (lpfc31_scsi_req_tmo != -1)
				value = (void *)((ulong) lpfc31_scsi_req_tmo);
			break;
		case ELX_CFG_LUN_SKIP:	/* lun-skip */
			value = (void *)((ulong) lpfc_lun_skip);
			if (lpfc31_lun_skip != -1)
				value = (void *)((ulong) lpfc31_lun_skip);
			break;
		}
		break;
	default:
		break;
	}
	return (value);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
DEVICE_ATTR(info, S_IRUGO, lpfc_sysfs_info_show, NULL);

static struct pci_device_id lpfc_id_table[] __devinitdata = {
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_VIPER, PCI_ANY_ID, PCI_ANY_ID, 0,
	 0, 0UL},
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_THOR, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 0UL},
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_PEGASUS, PCI_ANY_ID, PCI_ANY_ID, 0,
	 0, 0UL},
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_CENTAUR, PCI_ANY_ID, PCI_ANY_ID, 0,
	 0, 0UL},
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_DRAGONFLY, PCI_ANY_ID, PCI_ANY_ID,
	 0, 0, 0UL},
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_SUPERFLY, PCI_ANY_ID, PCI_ANY_ID,
	 0, 0, 0UL},
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_RFLY, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 0UL},
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_PFLY, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 0UL},
	{PCI_VENDOR_ID_EMULEX, PCI_DEVICE_ID_TFLY, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 0UL},
	{0, 0, 0, 0, 0, 0, 0UL},
};

MODULE_DEVICE_TABLE(pci, lpfc_id_table);

static int __devinit
lpfc_pci_detect(struct pci_dev *pdev, const struct pci_device_id *pid)
{
	int instance;

	if (lpfc_instcnt == MAX_ELX_BRDS)
		return -EPERM;	/* Only MAX_ELX_BRDS permitted */
	for (instance = 0; instance < MAX_ELX_BRDS; instance++) {
		if (lpfc_instance[instance] == -1)
			break;
	}

	if (pci_enable_device(pdev))
		return -ENODEV;
	if (pci_request_regions(pdev, LPFC_DRIVER_NAME)) {
		printk("lpfc PCI I/O region is already in use.\n");
		printk("a driver for lpfc is already loaded on this system.\n");
		return -ENODEV;
	}
	if (lpfc_linux_attach(instance, &driver_template, pdev)) {
		pci_release_regions(pdev);
		return -ENODEV;
	}

	device_create_file(&(pdev->dev), &dev_attr_info);
	return 0;
}

static void __devexit
lpfc_pci_release(struct pci_dev *pdev)
{
	struct Scsi_Host *host;
	elxHBA_t *phba;
	int instance;

	host = pci_get_drvdata(pdev);
	phba = (elxHBA_t *) host->hostdata[0];
	instance = phba->brd_no;

	/* removing hba's sysfs "info" attribute before driver unload */
	device_remove_file(&(pdev->dev), &dev_attr_info);

	/*
	 *  detach the board
	 */
	lpfc_linux_detach(instance);

	pci_set_drvdata(pdev, NULL);
}

static struct pci_driver
 lpfc_driver = {
	.name = LPFC_DRIVER_NAME,
	.id_table = lpfc_id_table,
	.probe = lpfc_pci_detect,
	.remove = __devexit_p(lpfc_pci_release),
};

static int __init
lpfc_init(void)
{
	int rc, i;

	printk(LPFC_MODULE_DESC "\n");

	memset((char *)&elxDRVR, 0, sizeof (elxDRVR_t));
	memset((char *)&lpfcdrvr, 0, sizeof (LINUX_DRVR_t));
	elxDRVR.pDrvrOSEnv = &lpfcdrvr;
	for (i = 0; i < MAX_ELX_BRDS; i++) {
		lpfc_instance[i] = -1;
	}

	/* Initialize all per Driver locks */
	elx_clk_init_lock(0);

	rc = pci_module_init(&lpfc_driver);

	if (lpfc_instcnt) {
		lpfc_diag_init();
		/* This covers the case where the lpfn driver gets loaded before the
		 * lpfc driver detect completes.
		 *  */
		if (lpfc_detect_called == LPFN_PROBE_PENDING) {
			if (lpfn_probe != NULL)
				lpfn_probe();
		}
		lpfc_detect_called = TRUE;
	}
	return rc;

}

static void __exit
lpfc_exit(void)
{
	pci_unregister_driver(&lpfc_driver);
	lpfc_diag_uninit();
}

ssize_t
lpfc_sysfs_info_show(struct device *dev, char *buf)
{
	struct Scsi_Host *host;

	host = pci_get_drvdata(to_pci_dev(dev));
	return lpfc_proc_info(host, buf, 0, 0, 0, 0);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#include "scsi_module.c"
#else
module_init(lpfc_init);
module_exit(lpfc_exit);
#endif
MODULE_LICENSE("GPL");
