/******************************************************************************/
/*                                                                            */
/* Broadcom BCM4400 Linux Network Driver, Copyright (c) 2002 Broadcom         */
/* Corporation.                                                               */
/* All rights reserved.                                                       */
/*                                                                            */
/* This program is free software; you can redistribute it and/or modify       */
/* it under the terms of the GNU General Public License as published by       */
/* the Free Software Foundation, located in the file LICENSE.                 */
/*                                                                            */
/* Change Log                                                                 */
/* 3.0.7 (10/31/03)                                                           */
/*  - Changed driver to power up the PHY in case it is powered down.          */
/* 3.0.6 (10/28/03)                                                           */
/*  - Fixed the problem of not enabling the activity LED when speed is forced.*/
/* 3.0.5 (10/24/03)                                                           */
/*  - Fixed a bug that assumes the number of tx buffer descriptor is always   */
/*    a power of 2.                                                           */
/* 3.0.4 (10/17/03)                                                           */
/*  - Added delay during chip resets to allow pending tx packets to drain.    */
/* 3.0.3 (10/08/03)                                                           */
/*  - More changes to make the link down work when changing link speed        */
/*  - Reenabled the MAC reset when resetting B0 chips.                        */
/*  - Removed the tx carrier error counter as it seems to be bogus sometimes  */
/* 3.0.2 (09/26/03)                                                           */
/*  - Minor changes to force a link down when changing speeds.                */
/*  - Removed symbol error checking during receive (causing good packets to   */
/*    to rejected in half duplex)                                             */
/* 3.0.1 (08/25/03)                                                           */
/*  - Fixed wol on B0 chips                                                   */
/* 3.0.0 (08/20/03)                                                           */
/*  - Added changes for 4401-B0 chips                                         */
/*  - Updated driver to work on 2.5 kernels                                   */
/*  - Added NAPI                                                              */
/*  - Added some additional ethtool ioctls                                    */
/* 2.0.5 (07/02/03)                                                           */
/*  - Added __devexit_p to bcm4400_remove_one                                 */
/*  - Changed Makefile to properly choose between kgcc/gcc                    */
/* 2.0.4 (06/26/03)                                                           */
/*  - More Changes to fix the target abort problem.                           */
/* 2.0.3 (06/25/03)                                                           */
/*  - Fixed target abort problem.                                             */
/* 2.0.0 (03/25/03)                                                           */
/*  - Fixed a crash problem under heavy traffic caused by reset and tasklet   */
/*    running at the same time.                                               */
/* 1.0.3  (2/25/03)                                                           */
/*  - Fixed various problems related to reset.                                */
/*  - Added magic packet WOL.                                                 */
/******************************************************************************/


char bcm4400_driver[] = "bcm4400";
char bcm4400_version[] = "3.0.7";
char bcm4400_date[] = "(10/31/03)";

#define B44UM
#include "b44mm.h"

/* A few user-configurable values. */

#define MAX_UNITS 16
/* Used to pass the full-duplex flag, etc. */
static int line_speed[MAX_UNITS] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static int auto_speed[MAX_UNITS] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
static int full_duplex[MAX_UNITS] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
static int rx_flow_control[MAX_UNITS] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static int tx_flow_control[MAX_UNITS] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static int auto_flow_control[MAX_UNITS] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

#define TX_DESC_CNT DEFAULT_TX_PACKET_DESC_COUNT
static unsigned int tx_pkt_desc_cnt[MAX_UNITS] =
	{TX_DESC_CNT,TX_DESC_CNT,TX_DESC_CNT,TX_DESC_CNT,TX_DESC_CNT,
	TX_DESC_CNT,TX_DESC_CNT,TX_DESC_CNT,TX_DESC_CNT,TX_DESC_CNT,
	TX_DESC_CNT,TX_DESC_CNT,TX_DESC_CNT,TX_DESC_CNT,TX_DESC_CNT,
	TX_DESC_CNT};

#define RX_DESC_CNT DEFAULT_RX_PACKET_DESC_COUNT
static unsigned int rx_pkt_desc_cnt[MAX_UNITS] =
	{RX_DESC_CNT,RX_DESC_CNT,RX_DESC_CNT,RX_DESC_CNT,RX_DESC_CNT,
	RX_DESC_CNT,RX_DESC_CNT,RX_DESC_CNT,RX_DESC_CNT,RX_DESC_CNT,
	RX_DESC_CNT,RX_DESC_CNT,RX_DESC_CNT,RX_DESC_CNT,RX_DESC_CNT,
	RX_DESC_CNT };

#ifdef BCM_WOL
static int enable_wol[MAX_UNITS] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
#endif

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (2*HZ)

#if (LINUX_VERSION_CODE < 0x02030d)
#define pci_resource_start(dev, bar)	(dev->base_address[bar] & PCI_BASE_ADDRESS_MEM_MASK)
#elif (LINUX_VERSION_CODE < 0x02032b)
#define pci_resource_start(dev, bar)	(dev->resource[bar] & PCI_BASE_ADDRESS_MEM_MASK)
#endif

#if (LINUX_VERSION_CODE < 0x02032b)
#define dev_kfree_skb_irq(skb)  dev_kfree_skb(skb)
#define netif_wake_queue(dev)	clear_bit(0, &dev->tbusy); mark_bh(NET_BH)
#define netif_stop_queue(dev)	set_bit(0, &dev->tbusy)

static inline void netif_start_queue(struct net_device *dev)
{
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;
}

#define netif_queue_stopped(dev)	dev->tbusy
#define netif_running(dev)		dev->start

static inline void tasklet_schedule(struct tasklet_struct *tasklet)
{
	queue_task(tasklet, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

static inline void tasklet_init(struct tasklet_struct *tasklet,
				void (*func)(unsigned long),
				unsigned long data)
{
        tasklet->next = NULL;
        tasklet->sync = 0;
        tasklet->routine = (void (*)(void *))func;
        tasklet->data = (void *)data;
}

#define tasklet_kill(tasklet)

#endif

#if (LINUX_VERSION_CODE < 0x020300)
struct pci_device_id {
	unsigned int vendor, device;		/* Vendor and device ID or PCI_ANY_ID */
	unsigned int subvendor, subdevice;	/* Subsystem ID's or PCI_ANY_ID */
	unsigned int class, class_mask;		/* (class,subclass,prog-if) triplet */
	unsigned long driver_data;		/* Data private to the driver */
};

#define PCI_ANY_ID		0

#define pci_set_drvdata(pdev, dev)
#define pci_get_drvdata(pdev) 0

#define pci_enable_device(pdev) 0

#define __devinit		__init
#define __devinitdata		__initdata
#define __devexit

#define SET_MODULE_OWNER(dev)
#define MODULE_DEVICE_TABLE(pci, pci_tbl)

#endif
 
#if (LINUX_VERSION_CODE < 0x020411)
#ifndef __devexit_p
#define __devexit_p(x)	x
#endif
#endif

#ifndef MODULE_LICENSE
#define MODULE_LICENSE(license)
#endif

#ifndef IRQ_RETVAL
typedef void irqreturn_t;
#define IRQ_RETVAL(x)
#endif

#if (LINUX_VERSION_CODE < 0x02032a)
static inline void *pci_alloc_consistent(struct pci_dev *pdev, size_t size,
					 dma_addr_t *dma_handle)
{
	void *virt_ptr;

	/* Maximum in slab.c */
	if (size > 131072)
		return 0;

	virt_ptr = kmalloc(size, GFP_KERNEL);
	*dma_handle = virt_to_bus(virt_ptr);
	return virt_ptr;
}
#define pci_free_consistent(dev, size, ptr, dma_ptr)	kfree(ptr)

#endif /*#if (LINUX_VERSION_CODE < 0x02032a) */


#if (LINUX_VERSION_CODE < 0x020329)
#define pci_set_dma_mask(pdev, mask) (0)
#else
#if (LINUX_VERSION_CODE < 0x020403)
int
pci_set_dma_mask(struct pci_dev *dev, dma_addr_t mask)
{
    if(! pci_dma_supported(dev, mask))
        return -EIO;

    dev->dma_mask = mask;

    return 0;
}
#endif
#endif

#if (LINUX_VERSION_CODE < 0x020402)
#define pci_request_regions(pdev, name) (0)
#define pci_release_regions(pdev)
#endif

#define BCM4400_PHY_LOCK(pUmDevice, flags)			\
{								\
	spin_lock_irqsave(&(pUmDevice)->phy_lock, flags);	\
}

#define BCM4400_PHY_UNLOCK(pUmDevice, flags)			\
{								\
	spin_unlock_irqrestore(&(pUmDevice)->phy_lock, flags);	\
}

void
bcm4400_intr_off(PUM_DEVICE_BLOCK pUmDevice)
{
	atomic_inc(&pUmDevice->intr_sem);
	b44_LM_DisableInterrupt(&pUmDevice->lm_dev);
#if (LINUX_VERSION_CODE >= 0x2051c)
	synchronize_irq(pUmDevice->dev->irq);
#else
	synchronize_irq();
#endif
}

void
bcm4400_intr_on(PUM_DEVICE_BLOCK pUmDevice)
{
	if (atomic_dec_and_test(&pUmDevice->intr_sem)) {
		b44_LM_EnableInterrupt(&pUmDevice->lm_dev);
	}
}

int b44_Packet_Desc_Size = sizeof(UM_PACKET);

#if defined(MODULE)
MODULE_AUTHOR("Michael Chan <mchan@broadcom.com>");
MODULE_DESCRIPTION("BCM4400 Driver");
MODULE_LICENSE("GPL");
MODULE_PARM(line_speed, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(auto_speed, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(rx_flow_control, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(tx_flow_control, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(auto_flow_control, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(tx_pkt_desc_cnt, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(rx_pkt_desc_cnt, "1-" __MODULE_STRING(MAX_UNITS) "i");
#ifdef BCM_WOL
MODULE_PARM(enable_wol, "1-" __MODULE_STRING(MAX_UNITS) "i");
#endif
#endif

#define RUN_AT(x) (jiffies + (x))

char kernel_version[] = UTS_RELEASE;

#define PCI_SUPPORT_VER2

#if ! defined(CAP_NET_ADMIN)
#define capable(CAP_XXX) (suser())
#endif


STATIC int bcm4400_open(struct net_device *dev);
STATIC void bcm4400_timer(unsigned long data);
STATIC void bcm4400_tx_timeout(struct net_device *dev);
STATIC int bcm4400_start_xmit(struct sk_buff *skb, struct net_device *dev);
STATIC irqreturn_t bcm4400_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
#ifdef BCM_TASKLET
STATIC void bcm4400_tasklet(unsigned long data);
#endif
STATIC int bcm4400_close(struct net_device *dev);
STATIC struct net_device_stats *bcm4400_get_stats(struct net_device *dev);
STATIC int bcm4400_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
STATIC void bcm4400_set_rx_mode(struct net_device *dev);
STATIC int bcm4400_set_mac_addr(struct net_device *dev, void *p);
STATIC int bcm4400_rxfill(PUM_DEVICE_BLOCK pUmDevice);
STATIC int bcm4400_freemem(struct net_device *dev);
STATIC void bcm4400_free_remaining_rx_bufs(UM_DEVICE_BLOCK *pUmDevice);
#ifdef BCM_NAPI_RXPOLL
STATIC int bcm4400_poll(struct net_device *dev, int *budget);
#endif


/* A list of all installed bcm4400 devices. */
static struct net_device *root_bcm4400_dev = NULL;

typedef enum {
	BCM4401 = 0,
	BCM4401B0,
} board_t;


/* indexed by board_t, above */
static struct {
	char *name;
} board_info[] __devinitdata = {
	{ "Broadcom BCM4401 100Base-T" },
	{ "Broadcom BCM4401-B0 100Base-T" },
	{ 0 },
	};

static struct pci_device_id bcm4400_pci_tbl[] __devinitdata = {
	{0x14e4, 0x4401, PCI_ANY_ID, PCI_ANY_ID, 0, 0, BCM4401 },
	{0x14e4, 0x4402, PCI_ANY_ID, PCI_ANY_ID, 0, 0, BCM4401B0 },
	{0x14e4, 0x170c, PCI_ANY_ID, PCI_ANY_ID, 0, 0, BCM4401B0 },
	{0,}
};

MODULE_DEVICE_TABLE(pci, bcm4400_pci_tbl);

#ifdef BCM_PROC_FS
extern int bcm4400_proc_create(void);
extern int bcm4400_proc_create_dev(struct net_device *dev);
extern int bcm4400_proc_remove_dev(struct net_device *dev);
#endif

static int __devinit bcm4400_init_board(struct pci_dev *pdev,
					struct net_device **dev_out,
					int board_idx)
{
	struct net_device *dev;
	PUM_DEVICE_BLOCK pUmDevice;
	PLM_DEVICE_BLOCK pDevice;
	int rc;

	*dev_out = NULL;

	/* dev zeroed in init_etherdev */
#if (LINUX_VERSION_CODE >= 0x20600)
	dev = alloc_etherdev(sizeof(*pUmDevice));
#else
	dev = init_etherdev(NULL, sizeof(*pUmDevice));
#endif
	if (dev == NULL) {
		printk (KERN_ERR "%s: unable to alloc new ethernet\n",
			bcm4400_driver);
		return -ENOMEM;
	}
	SET_MODULE_OWNER(dev);
#if (LINUX_VERSION_CODE >= 0x20600)
	SET_NETDEV_DEV(dev, &pdev->dev);
#endif
	pUmDevice = (PUM_DEVICE_BLOCK) dev->priv;

	/* enable device (incl. PCI PM wakeup), and bus-mastering */
	rc = pci_enable_device (pdev);
	if (rc)
		goto err_out;

	rc = pci_request_regions(pdev, bcm4400_driver);
	if (rc)
		goto err_out;

	pci_set_master(pdev);

	if (pci_set_dma_mask(pdev, 0xffffffffUL) != 0) {
		printk(KERN_ERR "System does not support DMA\n");
		pci_release_regions(pdev);
		goto err_out;
	}

	spin_lock_init(&pUmDevice->phy_lock);

	pUmDevice->dev = dev;
	pUmDevice->pdev = pdev;
	pUmDevice->mem_list_num = 0;
	pUmDevice->next_module = root_bcm4400_dev;
	pUmDevice->index = board_idx;
	root_bcm4400_dev = dev;

	pDevice = (PLM_DEVICE_BLOCK) pUmDevice;

	if (b44_LM_GetAdapterInfo(pDevice) != LM_STATUS_SUCCESS) {
		printk(KERN_ERR "Get Adapter info failed\n");
		rc = -ENODEV;
		goto err_out_unmap;
	}

	dev->mem_start = pci_resource_start(pdev, 0);
	dev->mem_end = dev->mem_start + sizeof(bcmenetregs_t) + 128; 
	dev->irq = pdev->irq;

	*dev_out = dev;
	return 0;

err_out_unmap:
	pci_release_regions(pdev);
	bcm4400_freemem(dev);

err_out:
#if (LINUX_VERSION_CODE < 0x020600)
	unregister_netdev(dev);
	kfree(dev);
#else
	free_netdev(dev);
#endif
	return rc;
}

static int __devinit
bcm4400_print_ver(void)
{
	printk(KERN_INFO "Broadcom 4401 Ethernet Driver %s ",
		bcm4400_driver);
	printk("ver. %s %s\n", bcm4400_version, bcm4400_date);
	return 0;
}

static int __devinit
bcm4400_init_one(struct pci_dev *pdev,
				       const struct pci_device_id *ent)
{
	struct net_device *dev = NULL;
	PUM_DEVICE_BLOCK pUmDevice;
	PLM_DEVICE_BLOCK pDevice;
	int i;
	static int board_idx = -1;
	static int printed_version = 0;
	struct pci_dev *amd_dev;

	board_idx++;

	if (!printed_version) {
		bcm4400_print_ver();
#ifdef BCM_PROC_FS
		bcm4400_proc_create();
#endif
		printed_version = 1;
	}

	i = bcm4400_init_board(pdev, &dev, board_idx);
	if (i < 0) {
		return i;
	}

	if (dev == NULL)
		return -ENOMEM;

	dev->open = bcm4400_open;
	dev->hard_start_xmit = bcm4400_start_xmit;
	dev->stop = bcm4400_close;
	dev->get_stats = bcm4400_get_stats;
	dev->set_multicast_list = bcm4400_set_rx_mode;
	dev->do_ioctl = bcm4400_ioctl;
	dev->set_mac_address = &bcm4400_set_mac_addr;
#if (LINUX_VERSION_CODE >= 0x20400)
	dev->tx_timeout = bcm4400_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
#endif
#ifdef BCM_NAPI_RXPOLL
	dev->poll = bcm4400_poll;
	dev->weight = 64;
#endif

	pUmDevice = (PUM_DEVICE_BLOCK) dev->priv;
	pDevice = (PLM_DEVICE_BLOCK) pUmDevice;

	dev->base_addr = pci_resource_start(pdev, 0);
	dev->irq = pdev->irq;

#if (LINUX_VERSION_CODE >= 0x20600)
	if (register_netdev(dev)) {
		printk(KERN_ERR "%s: Cannot register net device\n",
			bcm4400_driver);
	}
#endif

	pci_set_drvdata(pdev, dev);

	memcpy(dev->dev_addr, pDevice->NodeAddress, 6);
	pUmDevice->name = board_info[ent->driver_data].name,
	printk(KERN_INFO "%s: %s found at mem %lx, IRQ %d, ",
		dev->name, pUmDevice->name, dev->base_addr,
		dev->irq);
	printk("node addr ");
	for (i = 0; i < 6; i++) {
		printk("%2.2x", dev->dev_addr[i]);
	}
	printk("\n");

#ifdef BCM_PROC_FS
	bcm4400_proc_create_dev(dev);
#endif
#ifdef BCM_TASKLET
	tasklet_init(&pUmDevice->tasklet, bcm4400_tasklet,
		(unsigned long) pUmDevice);
#endif
	if ((amd_dev = pci_find_device(0x1022, 0x700c, NULL))) {
		u32 val;

		/* Found AMD 762 North bridge */
		pci_read_config_dword(amd_dev, 0x4c, &val);
		if ((val & 0x02) == 0) {
			pci_write_config_dword(amd_dev, 0x4c, val | 0x02);
			printk(KERN_INFO "%s: Setting AMD762 Northbridge to enable PCI ordering compliance\n", bcm4400_driver);
		}
	}
	return 0;

}


static void __devexit
bcm4400_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK)dev->priv;

#ifdef BCM_PROC_FS
	bcm4400_proc_remove_dev(dev); 
#endif

	unregister_netdev(dev);
	if (pUmDevice->lm_dev.pMappedMemBase)
		iounmap(pUmDevice->lm_dev.pMappedMemBase);

	pci_release_regions(pdev);

#if (LINUX_VERSION_CODE < 0x020600)
	kfree(dev);
#else
	free_netdev(dev);
#endif

	pci_set_drvdata(pdev, NULL);

/*	pci_power_off(pdev, -1);*/

}



STATIC int
bcm4400_open(struct net_device *dev)
{
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK)dev->priv;
	PLM_DEVICE_BLOCK pDevice = (PLM_DEVICE_BLOCK) pUmDevice;

	pUmDevice->delayed_link_ind = (4 * HZ) / pUmDevice->timer_interval;

	if (request_irq(dev->irq, &bcm4400_interrupt, SA_SHIRQ, dev->name, dev)) {
		return -EAGAIN;
	}

	pUmDevice->opened = 1;
	if (b44_LM_InitializeAdapter(pDevice) != LM_STATUS_SUCCESS) {
		free_irq(dev->irq, dev);
		bcm4400_freemem(dev);
		return -EAGAIN;
	}

	if (memcmp(dev->dev_addr, pDevice->NodeAddress, 6)) {
		b44_LM_SetMacAddress(pDevice, dev->dev_addr);
	}

	QQ_InitQueue(&pUmDevice->rx_out_of_buf_q.Container,
        MAX_RX_PACKET_DESC_COUNT);
	netif_start_queue(dev);

#if (LINUX_VERSION_CODE < 0x020300)
	MOD_INC_USE_COUNT;
#endif

	init_timer(&pUmDevice->timer);
	pUmDevice->timer.expires = RUN_AT(pUmDevice->timer_interval);
	pUmDevice->timer.data = (unsigned long)dev;
	pUmDevice->timer.function = &bcm4400_timer;
	add_timer(&pUmDevice->timer);

	atomic_set(&pUmDevice->intr_sem, 0);
	b44_LM_EnableInterrupt(pDevice);

	return 0;
}



STATIC void
bcm4400_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK)dev->priv;
	PLM_DEVICE_BLOCK pDevice = &pUmDevice->lm_dev;

	if (!pUmDevice->opened)
		return;

	if (atomic_read(&pUmDevice->intr_sem)) {
		pUmDevice->timer.expires = RUN_AT(pUmDevice->timer_interval);
		add_timer(&pUmDevice->timer);
		return;
	}

	if (pUmDevice->delayed_link_ind > 0) {
		pUmDevice->delayed_link_ind--;
		if (pUmDevice->delayed_link_ind == 0) {
			b44_MM_IndicateStatus(pDevice, pDevice->LinkStatus);
		}
	}
	if (!pUmDevice->interrupt) {
		if (REG_RD(pDevice, intstatus) & I_XI) {
			REG_WR(pDevice, gptimer, 2);
		}
#if (LINUX_VERSION_CODE < 0x02032b)
		if ((QQ_GetEntryCnt(&pDevice->TxPacketFreeQ.Container) !=
			pDevice->TxPacketDescCnt) &&
			((jiffies - dev->trans_start) > TX_TIMEOUT)) {

			printk(KERN_WARNING "%s: Tx hung\n", dev->name);
			bcm4400_tx_timeout(dev);
		}
#endif
	}
	if (QQ_GetEntryCnt(&pUmDevice->rx_out_of_buf_q.Container) >
		pUmDevice->rx_buf_repl_panic_thresh) {
		/* Generate interrupt and let isr allocate buffers */
		REG_WR(pDevice, gptimer, 2);
	}

	if (pUmDevice->link_interval == 0) {
		if (pDevice->corerev < 7) {
			b44_LM_PollLink(pDevice);
		}
		if (pDevice->LinkStatus == LM_STATUS_LINK_ACTIVE)
			b44_LM_StatsUpdate(pDevice);
		pUmDevice->link_interval = HZ / pUmDevice->timer_interval;
	}
	else {
		pUmDevice->link_interval--;
	}

	b44_LM_GetStats(pDevice);

	pUmDevice->timer.expires = RUN_AT(pUmDevice->timer_interval);
	add_timer(&pUmDevice->timer);
}

STATIC void
bcm4400_tx_timeout(struct net_device *dev)
{
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK)dev->priv;
	PLM_DEVICE_BLOCK pDevice = (PLM_DEVICE_BLOCK) pUmDevice;

	netif_stop_queue(dev);
	bcm4400_intr_off(pUmDevice);
	b44_LM_ResetAdapter(pDevice, TRUE);
	if (memcmp(dev->dev_addr, pDevice->NodeAddress, 6)) {
		b44_LM_SetMacAddress(pDevice, dev->dev_addr);
	}
	atomic_set(&pUmDevice->intr_sem, 1);
	bcm4400_intr_on(pUmDevice);
	netif_wake_queue(dev);
}

STATIC int
bcm4400_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK)dev->priv;
	PLM_DEVICE_BLOCK pDevice = (PLM_DEVICE_BLOCK) pUmDevice;
	PLM_PACKET pPacket;
	PUM_PACKET pUmPacket;

	if ((pDevice->LinkStatus == LM_STATUS_LINK_DOWN) ||
		!pDevice->InitDone || pDevice->InReset)
	{
		dev_kfree_skb(skb);
		return 0;
	}
	
#if (LINUX_VERSION_CODE < 0x02032b)
	if (test_and_set_bit(0, &dev->tbusy)) {
		return 1;
	}
#endif

	pPacket = (PLM_PACKET)
		QQ_PopHead(&pDevice->TxPacketFreeQ.Container);
	if (pPacket == 0) {
		netif_stop_queue(dev);
		pUmDevice->tx_full = 1;
		if (QQ_GetEntryCnt(&pDevice->TxPacketFreeQ.Container)) {
			netif_wake_queue(dev);
			pUmDevice->tx_full = 0;
		}
		return 1;
	}
	pUmPacket = (PUM_PACKET) pPacket;
	pUmPacket->skbuff = skb;

	if (atomic_read(&pDevice->SendDescLeft) == 0) {
		netif_stop_queue(dev);
		pUmDevice->tx_full = 1;
		QQ_PushHead(&pDevice->TxPacketFreeQ.Container, pPacket);
		if (atomic_read(&pDevice->SendDescLeft)) {
			netif_wake_queue(dev);
			pUmDevice->tx_full = 0;
		}
		return 1;
	}

	pPacket->u.Tx.FragCount = 1;

	b44_LM_SendPacket(pDevice, pPacket);

#if (LINUX_VERSION_CODE < 0x02032b)
	netif_wake_queue(dev);
#endif
	dev->trans_start = jiffies;
	return 0;
}

#ifdef BCM_NAPI_RXPOLL
STATIC int
bcm4400_poll(struct net_device *dev, int *budget)
{
	int orig_budget = *budget;
	int work_done;
	UM_DEVICE_BLOCK *pUmDevice = (UM_DEVICE_BLOCK *) dev->priv;
	LM_DEVICE_BLOCK *pDevice = &pUmDevice->lm_dev;
	LM_UINT32 intstatus;

	if (orig_budget > dev->quota)
		orig_budget = dev->quota;

	work_done = b44_LM_ServiceRxPoll(pDevice, orig_budget);
	*budget -= work_done;
	dev->quota -= work_done;

	if (QQ_GetEntryCnt(&pUmDevice->rx_out_of_buf_q.Container)) {
		bcm4400_rxfill(pUmDevice);
	}
	if (work_done) {
		b44_MM_IndicateRxPackets(pDevice);
		b44_LM_QueueRxPackets(pDevice);
	}
	if ((work_done < orig_budget) || atomic_read(&pUmDevice->intr_sem)) {

		netif_rx_complete(dev);
		pDevice->intmask |= (I_RI | I_RU | I_RO);
		REG_WR(pDevice, intmask, pDevice->intmask);
		pDevice->RxPoll = FALSE;
		intstatus = REG_RD(pDevice, intstatus);
		if (intstatus & (I_RI | I_RU | I_RO)) {
			REG_WR(pDevice, gptimer, 2);
		}
		return 0;
	}
	return 1;
}
#endif /* BCM_NAPI_RXPOLL */

STATIC irqreturn_t
bcm4400_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *)dev_instance;
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK)dev->priv;
	PLM_DEVICE_BLOCK pDevice = (PLM_DEVICE_BLOCK) pUmDevice;
#ifdef BCM_TASKLET
	int repl_buf_count;
#endif
	unsigned int handled = 1;

	if (!pDevice->InitDone) {
		handled = 0;
		return IRQ_RETVAL(handled);
	}

	if (atomic_read(&pUmDevice->intr_sem)) {
		handled = 0;
		return IRQ_RETVAL(handled);
	}

	if (test_and_set_bit(0, (void*)&pUmDevice->interrupt)) {
		printk(KERN_ERR "%s: Duplicate entry of the interrupt handler by "
			   "processor %d.\n",
			   dev->name, hard_smp_processor_id());
		handled = 0;
		return IRQ_RETVAL(handled);
	}


	b44_LM_ServiceInterrupts(pDevice);

#ifdef BCM_TASKLET
	repl_buf_count = QQ_GetEntryCnt(&pUmDevice->rx_out_of_buf_q.Container);
	if (repl_buf_count > pUmDevice->rx_buf_repl_thresh) {
		if ((repl_buf_count > pUmDevice->rx_buf_repl_panic_thresh) &&
			(!test_and_set_bit(0, &pUmDevice->tasklet_busy))) {
			bcm4400_rxfill(pUmDevice);
			clear_bit(0, (void*)&pUmDevice->tasklet_busy);
		}
		else if (!pUmDevice->tasklet_pending) {
			pUmDevice->tasklet_pending = 1;
			tasklet_schedule(&pUmDevice->tasklet);
		}
	}
#else
#ifdef BCM_NAPI_RXPOLL
	if (!pDevice->RxPoll &&
		QQ_GetEntryCnt(&pUmDevice->rx_out_of_buf_q.Container)) {
		pDevice->RxPoll = 1;
		b44_MM_ScheduleRxPoll(pDevice);
	}
#else
	if (QQ_GetEntryCnt(&pUmDevice->rx_out_of_buf_q.Container)) {
		bcm4400_rxfill(pUmDevice);
	}

	if (QQ_GetEntryCnt(&pDevice->RxPacketFreeQ.Container)) {
		b44_LM_QueueRxPackets(pDevice);
	}
#endif
#endif

	clear_bit(0, (void*)&pUmDevice->interrupt);
	if (pUmDevice->tx_queued) {
		pUmDevice->tx_queued = 0;
		netif_wake_queue(dev);
	}
	return IRQ_RETVAL(handled);
}


#ifdef BCM_TASKLET
STATIC void
bcm4400_tasklet(unsigned long data)
{
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK)data;

	/* RH 7.2 Beta 3 tasklets are reentrant */
	if (test_and_set_bit(0, &pUmDevice->tasklet_busy)) {
		pUmDevice->tasklet_pending = 0;
		return;
	}

	pUmDevice->tasklet_pending = 0;
	bcm4400_rxfill(pUmDevice);
	clear_bit(0, &pUmDevice->tasklet_busy);
}
#endif

STATIC int
bcm4400_close(struct net_device *dev)
{
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK)dev->priv;
	PLM_DEVICE_BLOCK pDevice = (PLM_DEVICE_BLOCK) pUmDevice;

#if (LINUX_VERSION_CODE < 0x02032b)
	dev->start = 0;
#endif
	netif_stop_queue(dev);
	pUmDevice->opened = 0;

	bcm4400_intr_off(pUmDevice);
	netif_carrier_off(dev);
#ifdef BCM_TASKLET
//	tasklet_disable(&pUmDevice->tasklet);
	tasklet_kill(&pUmDevice->tasklet);
#endif

	b44_LM_Halt(pDevice);
	pDevice->InitDone = 0;
	bcm4400_free_remaining_rx_bufs(pUmDevice);
	del_timer(&pUmDevice->timer);

	free_irq(dev->irq, dev);
#if (LINUX_VERSION_CODE < 0x020300)
	MOD_DEC_USE_COUNT;
#endif
#ifdef BCM_WOL
	if (pDevice->WakeUpMode != LM_WAKE_UP_MODE_NONE) {
		b44_LM_pmset(pDevice);
	}
	else
#endif
	{
		b44_LM_PowerDownPhy(pDevice);
	}

	bcm4400_freemem(dev);

	return 0;
}

STATIC void
bcm4400_free_remaining_rx_bufs(UM_DEVICE_BLOCK *pUmDevice)
{
	LM_DEVICE_BLOCK *pDevice = &pUmDevice->lm_dev;
	UM_PACKET *pUmPacket;
	int cnt, i;

	cnt = QQ_GetEntryCnt(&pUmDevice->rx_out_of_buf_q.Container);
	for (i = 0; i < cnt; i++) {
		if ((pUmPacket =
			QQ_PopHead(&pUmDevice->rx_out_of_buf_q.Container))
			!= 0) {

			b44_MM_FreeRxBuffer(pDevice, &pUmPacket->lm_packet);
			QQ_PushTail(&pDevice->RxPacketFreeQ.Container,
				pUmPacket);
		}
	}
}

STATIC int
bcm4400_freemem(struct net_device *dev)
{
	int i;
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK)dev->priv;

	for (i = 0; i < pUmDevice->mem_list_num; i++) {
		if (pUmDevice->mem_size_list[i] == 0) {
			kfree(pUmDevice->mem_list[i]);
		}
		else {
			pci_free_consistent(pUmDevice->pdev,
				(size_t) pUmDevice->mem_size_list[i],
				pUmDevice->mem_list[i],
				pUmDevice->dma_list[i]);
		}
	}
	pUmDevice->mem_list_num = 0;
	return 0;
}

STATIC struct net_device_stats *
bcm4400_get_stats(struct net_device *dev)
{
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK)dev->priv;
	LM_DEVICE_BLOCK *pDevice = &pUmDevice->lm_dev;
	struct net_device_stats *p_netstats = &pUmDevice->stats;

	p_netstats->rx_packets = pDevice->rx_pkts;
	p_netstats->tx_packets = pDevice->tx_pkts;
	p_netstats->rx_bytes = pDevice->rx_octets;
	p_netstats->tx_bytes = pDevice->tx_octets;
	p_netstats->tx_errors = pDevice->tx_jabber_pkts +
		pDevice->tx_oversize_pkts + pDevice->tx_underruns +
		pDevice->tx_excessive_cols + pDevice->tx_late_cols;
	p_netstats->multicast = pDevice->tx_multicast_pkts;
	p_netstats->collisions = pDevice->tx_total_cols;
	p_netstats->rx_length_errors = pDevice->rx_oversize_pkts +
		pDevice->rx_undersize;
	p_netstats->rx_over_errors = pDevice->rx_missed_pkts;
	p_netstats->rx_frame_errors = pDevice->rx_align_errs;
	p_netstats->rx_crc_errors = pDevice->rx_crc_errs;
	p_netstats->rx_errors = pDevice->rx_jabber_pkts +
		pDevice->rx_oversize_pkts + pDevice->rx_missed_pkts +
		pDevice->rx_crc_align_errs + pDevice->rx_undersize +
		pDevice->rx_crc_errs + pDevice->rx_align_errs +
		pDevice->rx_symbol_errs;
	p_netstats->tx_aborted_errors = pDevice->tx_underruns;
	p_netstats->tx_carrier_errors = pDevice->tx_carrier_lost;
	return p_netstats;
}

#ifdef SIOCETHTOOL

#ifdef ETHTOOL_GSTRINGS

#define ETH_NUM_STATS 21

struct {
	char string[ETH_GSTRING_LEN];
} bcm4400_stats_str_arr[ETH_NUM_STATS] = {
	{ "rx_packets" },
	{ "rx_multicast_packets" },
	{ "rx_broadcast_packets" },
	{ "rx_bytes" },
	{ "rx_fragments" },
	{ "rx_crc_errors" },
	{ "rx_align_errors" },
	{ "rx_pause_frames" },
	{ "rx_long_frames" },
	{ "rx_short_frames" },
	{ "rx_discards" },
	{ "tx_packets" },
	{ "tx_multicast_packets" },
	{ "tx_broadcast_packets" },
	{ "tx_bytes" },
	{ "tx_collisions" },
	{ "tx_deferred" },
	{ "tx_excess_collisions" },
	{ "tx_late_collisions" },
	{ "tx_pause_frames" },
	{ "tx_carrier_errors" },
};

#define STATS_OFFSET(offset_name) (OFFSETOF(LM_DEVICE_BLOCK, offset_name))

unsigned long bcm4400_stats_offset_arr[ETH_NUM_STATS] = {
	STATS_OFFSET(rx_good_pkts),
	STATS_OFFSET(rx_multicast_pkts),
	STATS_OFFSET(rx_broadcast_pkts),
	STATS_OFFSET(rx_octets),
	STATS_OFFSET(rx_fragment_pkts),
	STATS_OFFSET(rx_crc_errs),
	STATS_OFFSET(rx_align_errs),
	STATS_OFFSET(rx_pause_pkts),
	STATS_OFFSET(rx_oversize_pkts),
	STATS_OFFSET(rx_undersize),
	STATS_OFFSET(rx_missed_pkts),
	STATS_OFFSET(tx_good_pkts),
	STATS_OFFSET(tx_multicast_pkts),
	STATS_OFFSET(tx_broadcast_pkts),
	STATS_OFFSET(tx_octets),
	STATS_OFFSET(tx_total_cols),
	STATS_OFFSET(tx_defered),
	STATS_OFFSET(tx_excessive_cols),
	STATS_OFFSET(tx_late_cols),
	STATS_OFFSET(tx_pause_pkts),
	STATS_OFFSET(tx_carrier_lost),
};

#endif /* #ifdef ETHTOOL_GSTRINGS */

static int netdev_ethtool_ioctl(struct net_device *dev, void *useraddr)
{
	struct ethtool_cmd ethcmd;
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK)dev->priv;
	PLM_DEVICE_BLOCK pDevice = (PLM_DEVICE_BLOCK) pUmDevice;
	unsigned long flags;
		
	if (copy_from_user(&ethcmd, useraddr, sizeof(ethcmd)))
		return -EFAULT;

        switch (ethcmd.cmd) {
#ifdef ETHTOOL_GDRVINFO
        case ETHTOOL_GDRVINFO: {
		struct ethtool_drvinfo info = {ETHTOOL_GDRVINFO};

		strcpy(info.driver,  bcm4400_driver);
		strcpy(info.version, bcm4400_version);
		strcpy(info.bus_info, pUmDevice->pdev->slot_name);
#ifdef ETHTOOL_GSTATS
		info.n_stats = ETH_NUM_STATS;
#endif
		if (copy_to_user(useraddr, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
#endif
        case ETHTOOL_GSET: {
		ethcmd.supported =
			(SUPPORTED_10baseT_Half |
			SUPPORTED_10baseT_Full |
			SUPPORTED_100baseT_Half |
			SUPPORTED_100baseT_Full |
			SUPPORTED_Autoneg);
			ethcmd.supported |= SUPPORTED_TP;
			ethcmd.port = PORT_TP;

		ethcmd.transceiver = XCVR_INTERNAL;
		ethcmd.phy_address = 0;

		if (pUmDevice->line_speed == 100)
			ethcmd.speed = SPEED_100;
		else if (pUmDevice->line_speed == 10)
			ethcmd.speed = SPEED_10;
		else
			ethcmd.speed = 0;

		if (pDevice->DuplexMode == LM_DUPLEX_MODE_FULL)
			ethcmd.duplex = DUPLEX_FULL;
		else
			ethcmd.duplex = DUPLEX_HALF;

		if (pDevice->DisableAutoNeg == FALSE) {
			ethcmd.autoneg = AUTONEG_ENABLE;
			ethcmd.advertising = ADVERTISED_Autoneg;
			ethcmd.advertising |=
				ADVERTISED_TP;
			if (pDevice->Advertising &
				PHY_AN_AD_10BASET_HALF) {

				ethcmd.advertising |=
					ADVERTISED_10baseT_Half;
			}
			if (pDevice->Advertising &
				PHY_AN_AD_10BASET_FULL) {

				ethcmd.advertising |=
					ADVERTISED_10baseT_Full;
			}
			if (pDevice->Advertising &
				PHY_AN_AD_100BASETX_HALF) {

				ethcmd.advertising |=
					ADVERTISED_100baseT_Half;
			}
			if (pDevice->Advertising &
				PHY_AN_AD_100BASETX_FULL) {

				ethcmd.advertising |=
					ADVERTISED_100baseT_Full;
			}
		}
		else {
			ethcmd.autoneg = AUTONEG_DISABLE;
			ethcmd.advertising = 0;
		}

		if(copy_to_user(useraddr, &ethcmd, sizeof(ethcmd)))
			return -EFAULT;
		return 0;
	}
	case ETHTOOL_SSET: {
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (ethcmd.autoneg == AUTONEG_ENABLE) {
			pDevice->RequestedLineSpeed = LM_LINE_SPEED_AUTO;
			pDevice->RequestedDuplexMode = LM_DUPLEX_MODE_UNKNOWN;
			pDevice->DisableAutoNeg = FALSE;
		}
		else {
			if (ethcmd.speed == SPEED_1000) {
				return -EINVAL;
			}
			else if (ethcmd.speed == SPEED_100) {
				pDevice->RequestedLineSpeed =
					LM_LINE_SPEED_100MBPS;
			}
			else if (ethcmd.speed == SPEED_10) {
				pDevice->RequestedLineSpeed =
					LM_LINE_SPEED_10MBPS;
			}
			else {
				return -EINVAL;
			}

			pDevice->DisableAutoNeg = TRUE;
			if (ethcmd.duplex == DUPLEX_FULL) {
				pDevice->RequestedDuplexMode =
					LM_DUPLEX_MODE_FULL;
			}
			else {
				pDevice->RequestedDuplexMode =
					LM_DUPLEX_MODE_HALF;
			}
		}
		BCM4400_PHY_LOCK(pUmDevice, flags);
		b44_LM_SetupPhy(pDevice);
		BCM4400_PHY_UNLOCK(pUmDevice, flags);
		return 0;
	}
#ifdef ETHTOOL_GWOL
#ifdef BCM_WOL
	case ETHTOOL_GWOL: {
		struct ethtool_wolinfo wol = {ETHTOOL_GWOL};

		wol.supported = WAKE_MAGIC;
		if (pDevice->WakeUpMode == LM_WAKE_UP_MODE_MAGIC_PACKET)
		{
			wol.wolopts = WAKE_MAGIC;
		}
		else {
			wol.wolopts = 0;
		}
		if (copy_to_user(useraddr, &wol, sizeof(wol)))
			return -EFAULT;
		return 0;
	}
	case ETHTOOL_SWOL: {
		struct ethtool_wolinfo wol;

		if(!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (copy_from_user(&wol, useraddr, sizeof(wol)))
			return -EFAULT;
		
		if ((wol.wolopts & ~WAKE_MAGIC) != 0) {
			return -EINVAL;
		}
		if (wol.wolopts & WAKE_MAGIC) {
			pDevice->WakeUpMode = LM_WAKE_UP_MODE_MAGIC_PACKET;
		}
		else {
			pDevice->WakeUpMode = LM_WAKE_UP_MODE_NONE;
		}
		return 0;
        }
#endif
#endif
#ifdef ETHTOOL_GLINK
	case ETHTOOL_GLINK: {
		struct ethtool_value edata = {ETHTOOL_GLINK};

		if (pDevice->LinkStatus == LM_STATUS_LINK_ACTIVE)
			edata.data =  1;
		else
			edata.data =  0;
		if (copy_to_user(useraddr, &edata, sizeof(edata)))
			return -EFAULT;
		return 0;
	}
#endif
#ifdef ETHTOOL_NWAY_RST
	case ETHTOOL_NWAY_RST: {
		LM_UINT32 phyctrl;

		if(!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (pDevice->DisableAutoNeg) {
			return -EINVAL;
		}
		BCM4400_PHY_LOCK(pUmDevice, flags);
		b44_LM_ReadPhy(pDevice, PHY_CTRL_REG, &phyctrl);
		b44_LM_WritePhy(pDevice, PHY_CTRL_REG, phyctrl |
			PHY_CTRL_AUTO_NEG_ENABLE |
			PHY_CTRL_RESTART_AUTO_NEG);
		BCM4400_PHY_UNLOCK(pUmDevice, flags);
		return 0;
	}
#endif
#ifdef ETHTOOL_GSTRINGS
	case ETHTOOL_GSTRINGS: {
		struct ethtool_gstrings egstr = { ETHTOOL_GSTRINGS };

		if (copy_from_user(&egstr, useraddr, sizeof(egstr)))
			return -EFAULT;
		if (egstr.string_set != ETH_SS_STATS)
			return -EINVAL;
		egstr.len = ETH_NUM_STATS;
		if (copy_to_user(useraddr, &egstr, sizeof(egstr)))
			return -EFAULT;
		if (copy_to_user(useraddr + sizeof(egstr), 
			bcm4400_stats_str_arr, sizeof(bcm4400_stats_str_arr)))
			return -EFAULT;
		return 0;
	}
#endif
#ifdef ETHTOOL_GSTATS
	case ETHTOOL_GSTATS: {
		struct ethtool_stats estats = { ETHTOOL_GSTATS };
		uint64_t stats[ETH_NUM_STATS];
		char *cptr = (char *) pDevice;
		int i;

		if (copy_to_user(useraddr, &estats, sizeof(estats)))
			return -EFAULT;

		estats.n_stats = ETH_NUM_STATS;
		for (i = 0; i < ETH_NUM_STATS; i++) {
			stats[i] = *((LM_COUNTER *)
				(cptr + bcm4400_stats_offset_arr[i]));
		}
		if (copy_to_user(useraddr + sizeof(estats), &stats,
			sizeof(stats)))
			return -EFAULT;
		return 0;
	}
#endif
	}
	
	return -EOPNOTSUPP;
}
#endif

/* Provide ioctl() calls to examine the MII xcvr state. */
STATIC int bcm4400_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK)dev->priv;
	PLM_DEVICE_BLOCK pDevice = (PLM_DEVICE_BLOCK) pUmDevice;
	u16 *data = (u16 *)&rq->ifr_data;
	u32 value;
	unsigned long flags;

	switch(cmd) {
#ifdef SIOCGMIIPHY
	case SIOCGMIIPHY:
#endif
	case SIOCDEVPRIVATE:		/* Get the address of the PHY in use. */
		data[0] = pDevice->PhyAddr;

#ifdef SIOCGMIIREG
	case SIOCGMIIREG:
#endif
	case SIOCDEVPRIVATE+1:		/* Read the specified MII register. */
		BCM4400_PHY_LOCK(pUmDevice, flags);
		b44_LM_ReadPhy(pDevice, data[1] & 0x1f, (LM_UINT32 *) &value);
		BCM4400_PHY_UNLOCK(pUmDevice, flags);
		data[3] = value & 0xffff;
		return 0;

#ifdef SIOCSMIIREG
	case SIOCSMIIREG:
#endif
	case SIOCDEVPRIVATE+2:		/* Write the specified MII register */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		BCM4400_PHY_LOCK(pUmDevice, flags);
		b44_LM_WritePhy(pDevice, data[1] & 0x1f, data[2]);
		BCM4400_PHY_UNLOCK(pUmDevice, flags);
		return 0;

#ifdef SIOCETHTOOL
	case SIOCETHTOOL:
		return netdev_ethtool_ioctl(dev, (void *) rq->ifr_data);
#endif
	default:
		return -EOPNOTSUPP;
	}
	return -EOPNOTSUPP;
}

STATIC void bcm4400_set_rx_mode(struct net_device *dev)
{
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK)dev->priv;
	PLM_DEVICE_BLOCK pDevice = (PLM_DEVICE_BLOCK) pUmDevice;
	int i;
	struct dev_mc_list *mclist;

	b44_LM_MulticastClear(pDevice);
	for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next) {
		b44_LM_MulticastAdd(pDevice, (PLM_UINT8) &mclist->dmi_addr);
	}
	if (dev->flags & IFF_ALLMULTI) {
		if (!(pDevice->ReceiveMask & LM_ACCEPT_ALL_MULTICAST)) {
			b44_LM_SetReceiveMask(pDevice,
				pDevice->ReceiveMask | LM_ACCEPT_ALL_MULTICAST);
		}
	}
	else if (pDevice->ReceiveMask & LM_ACCEPT_ALL_MULTICAST) {
		b44_LM_SetReceiveMask(pDevice,
			pDevice->ReceiveMask & ~LM_ACCEPT_ALL_MULTICAST);
	}
	if (dev->flags & IFF_PROMISC) {
		if (!(pDevice->ReceiveMask & LM_PROMISCUOUS_MODE)) {
			b44_LM_SetReceiveMask(pDevice,
				pDevice->ReceiveMask | LM_PROMISCUOUS_MODE);
		}
	}
	else if (pDevice->ReceiveMask & LM_PROMISCUOUS_MODE) {
		b44_LM_SetReceiveMask(pDevice,
			pDevice->ReceiveMask & ~LM_PROMISCUOUS_MODE);
	}
}

/*
 * Set the hardware MAC address.
 */
STATIC int bcm4400_set_mac_addr(struct net_device *dev, void *p)
{
	struct sockaddr *addr=p;
	PLM_DEVICE_BLOCK pDevice = (PLM_DEVICE_BLOCK) dev->priv;

	if (netif_running(dev))
		return -EBUSY;
	memcpy(dev->dev_addr, addr->sa_data,dev->addr_len);
	b44_LM_SetMacAddress(pDevice, dev->dev_addr);
	return 0;
}


#if (LINUX_VERSION_CODE < 0x020300)

int
bcm4400_probe(struct net_device *dev)
{
	int cards_found = 0;
	struct pci_dev *pdev = NULL;
	struct pci_device_id *pci_tbl;
	u16 ssvid, ssid;

	if ( ! pci_present())
		return -ENODEV;

	pci_tbl = bcm4400_pci_tbl;
	while ((pdev = pci_find_class(PCI_CLASS_NETWORK_ETHERNET << 8, pdev))) {
		int idx;

		pci_read_config_word(pdev, PCI_SUBSYSTEM_VENDOR_ID, &ssvid);
		pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, &ssid);
		for (idx = 0; pci_tbl[idx].vendor; idx++) {
			if ((pci_tbl[idx].vendor == PCI_ANY_ID ||
				pci_tbl[idx].vendor == pdev->vendor) &&
				(pci_tbl[idx].device == PCI_ANY_ID ||
				pci_tbl[idx].device == pdev->device) &&
				(pci_tbl[idx].subvendor == PCI_ANY_ID ||
				pci_tbl[idx].subvendor == ssvid) &&
				(pci_tbl[idx].subdevice == PCI_ANY_ID ||
				pci_tbl[idx].subdevice == ssid))
			{

				break;
			}
		}
		if (pci_tbl[idx].vendor == 0)
			continue;


		if (bcm4400_init_one(pdev, &pci_tbl[idx]) == 0)
			cards_found++;
	}

	return cards_found ? 0 : -ENODEV;
}

#ifdef MODULE
int init_module(void)
{
	return bcm4400_probe(NULL);
}

void cleanup_module(void)
{
	struct net_device *next_dev;
	PUM_DEVICE_BLOCK pUmDevice;

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (root_bcm4400_dev) {
		pUmDevice = (PUM_DEVICE_BLOCK)root_bcm4400_dev->priv;
#ifdef BCM_PROC_FS
		bcm4400_proc_remove_dev(root_bcm4400_dev); 
#endif
		next_dev = pUmDevice->next_module;
		unregister_netdev(root_bcm4400_dev);
		if (pUmDevice->lm_dev.pMappedMemBase)
			iounmap(pUmDevice->lm_dev.pMappedMemBase);
#if (LINUX_VERSION_CODE < 0x020600)
		kfree(root_bcm4400_dev);
#else
		free_netdev(root_bcm4400_dev);
#endif
		root_bcm4400_dev = next_dev;
	}
}

#endif  /* MODULE */
#else	/* LINUX_VERSION_CODE < 0x020300 */

#if (LINUX_VERSION_CODE >= 0x020406)
static int bcm4400_suspend (struct pci_dev *pdev, u32 state)
#else
static void bcm4400_suspend (struct pci_dev *pdev)
#endif
{
	struct net_device *dev = (struct net_device *) pci_get_drvdata(pdev);
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK) dev->priv;
	PLM_DEVICE_BLOCK pDevice = (PLM_DEVICE_BLOCK) pUmDevice;

	if (!netif_running(dev))
#if (LINUX_VERSION_CODE >= 0x020406)
		return 0;
#else
		return;
#endif

	bcm4400_intr_off(pUmDevice);
	netif_carrier_off(dev);
	netif_device_detach (dev);

	/* Disable interrupts, stop Tx and Rx. */
	b44_LM_Halt(pDevice);
	bcm4400_free_remaining_rx_bufs(pUmDevice);

#ifdef BCM_WOL
	if (pDevice->WakeUpMode != LM_WAKE_UP_MODE_NONE) {
		b44_LM_pmset(pDevice);
	}
	else
#endif
	{
		b44_LM_PowerDownPhy(pDevice);
	}

/*	pci_power_off(pdev, -1);*/
#if (LINUX_VERSION_CODE >= 0x020406)
		return 0;
#endif
}


#if (LINUX_VERSION_CODE >= 0x020406)
static int bcm4400_resume(struct pci_dev *pdev)
#else
static void bcm4400_resume(struct pci_dev *pdev)
#endif
{
	struct net_device *dev = (struct net_device *) pci_get_drvdata(pdev);
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK) dev->priv;
	PLM_DEVICE_BLOCK pDevice = &pUmDevice->lm_dev;

	if (!netif_running(dev))
#if (LINUX_VERSION_CODE >= 0x020406)
		return 0;
#else
		return;
#endif
/*	pci_power_on(pdev);*/
	netif_device_attach(dev);
	b44_LM_InitializeAdapter(pDevice);
	if (memcmp(dev->dev_addr, pDevice->NodeAddress, 6)) {
		b44_LM_SetMacAddress(pDevice, dev->dev_addr);
	}
	atomic_set(&pUmDevice->intr_sem, 0);
	b44_LM_EnableInterrupt(pDevice);
#if (LINUX_VERSION_CODE >= 0x020406)
	return 0;
#endif
}


static struct pci_driver bcm4400_pci_driver = {
	name:		bcm4400_driver,
	id_table:	bcm4400_pci_tbl,
	probe:		bcm4400_init_one,
	remove:		__devexit_p(bcm4400_remove_one),
	suspend:	bcm4400_suspend,
	resume:		bcm4400_resume,
};


static int __init bcm4400_init_module (void)
{
	return pci_module_init(&bcm4400_pci_driver);
}


static void __exit bcm4400_cleanup_module (void)
{
	pci_unregister_driver(&bcm4400_pci_driver);
}


module_init(bcm4400_init_module);
module_exit(bcm4400_cleanup_module);
#endif

/*
 * Middle Module
 *
 */

#ifdef BCM_NAPI_RXPOLL
LM_STATUS
b44_MM_ScheduleRxPoll(LM_DEVICE_BLOCK *pDevice)
{
	struct net_device *dev = ((UM_DEVICE_BLOCK *) pDevice)->dev;

	if (netif_rx_schedule_prep(dev)) {
		__netif_rx_schedule(dev);
		return LM_STATUS_SUCCESS;
	}
	return LM_STATUS_FAILURE;
}
#endif

LM_STATUS
b44_MM_ReadConfig16(PLM_DEVICE_BLOCK pDevice, LM_UINT32 Offset,
	LM_UINT16 *pValue16)
{
	UM_DEVICE_BLOCK *pUmDevice;

	pUmDevice = (UM_DEVICE_BLOCK *) pDevice;
	pci_read_config_word(pUmDevice->pdev, Offset, (u16 *) pValue16);
	return LM_STATUS_SUCCESS;
}

LM_STATUS
b44_MM_ReadConfig32(PLM_DEVICE_BLOCK pDevice, LM_UINT32 Offset,
	LM_UINT32 *pValue32)
{
	UM_DEVICE_BLOCK *pUmDevice;

	pUmDevice = (UM_DEVICE_BLOCK *) pDevice;
	pci_read_config_dword(pUmDevice->pdev, Offset, (u32 *) pValue32);
	return LM_STATUS_SUCCESS;
}

LM_STATUS
b44_MM_WriteConfig16(PLM_DEVICE_BLOCK pDevice, LM_UINT32 Offset,
	LM_UINT16 Value16)
{
	UM_DEVICE_BLOCK *pUmDevice;

	pUmDevice = (UM_DEVICE_BLOCK *) pDevice;
	pci_write_config_word(pUmDevice->pdev, Offset, Value16);
	return LM_STATUS_SUCCESS;
}

LM_STATUS
b44_MM_WriteConfig32(PLM_DEVICE_BLOCK pDevice, LM_UINT32 Offset,
	LM_UINT32 Value32)
{
	UM_DEVICE_BLOCK *pUmDevice;

	pUmDevice = (UM_DEVICE_BLOCK *) pDevice;
	pci_write_config_dword(pUmDevice->pdev, Offset, Value32);
	return LM_STATUS_SUCCESS;
}

LM_STATUS
b44_MM_AllocateSharedMemory(PLM_DEVICE_BLOCK pDevice, LM_UINT32 BlockSize,
	PLM_VOID *pMemoryBlockVirt, PLM_PHYSICAL_ADDRESS pMemoryBlockPhy)
{
	PLM_VOID pvirt;
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK) pDevice;
	dma_addr_t mapping;

	pvirt = pci_alloc_consistent(pUmDevice->pdev, BlockSize,
					       &mapping);
	if (!pvirt) {
		return LM_STATUS_FAILURE;
	}
	pUmDevice->mem_list[pUmDevice->mem_list_num] = pvirt;
	pUmDevice->dma_list[pUmDevice->mem_list_num] = mapping;
	pUmDevice->mem_size_list[pUmDevice->mem_list_num++] = BlockSize;
	memset(pvirt, 0, BlockSize);
	*pMemoryBlockVirt = (PLM_VOID) pvirt;
	*pMemoryBlockPhy = (LM_PHYSICAL_ADDRESS) mapping;
	return LM_STATUS_SUCCESS;
}

LM_STATUS
b44_MM_AllocateMemory(PLM_DEVICE_BLOCK pDevice, LM_UINT32 BlockSize,
	PLM_VOID *pMemoryBlockVirt)
{
	PLM_VOID pvirt;
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK) pDevice;


	/* Maximum in slab.c */
	if (BlockSize > 131072) {
		goto b44_MM_Alloc_error;
	}

	pvirt = kmalloc(BlockSize, GFP_KERNEL);
	if (!pvirt) {
		goto b44_MM_Alloc_error;
	}
	pUmDevice->mem_list[pUmDevice->mem_list_num] = pvirt;
	pUmDevice->dma_list[pUmDevice->mem_list_num] = 0;
	pUmDevice->mem_size_list[pUmDevice->mem_list_num++] = 0;
	/* mem_size_list[i] == 0 indicates that the memory should be freed */
	/* using kfree */
	memset(pvirt, 0, BlockSize);
	*pMemoryBlockVirt = pvirt;
	return LM_STATUS_SUCCESS;

b44_MM_Alloc_error:
	printk(KERN_WARNING "%s: Memory allocation failed - buffer parameters may be set too high\n", pUmDevice->dev->name);
	return LM_STATUS_FAILURE;
}

LM_STATUS
b44_MM_MapMemBase(PLM_DEVICE_BLOCK pDevice)
{
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK) pDevice;

	pDevice->pMappedMemBase = ioremap_nocache(
		pci_resource_start(pUmDevice->pdev, 0),
			sizeof(bcmenetregs_t) + 128);
	return LM_STATUS_SUCCESS;
}

LM_STATUS
b44_MM_InitializeUmPackets(PLM_DEVICE_BLOCK pDevice)
{
	int i;
	struct sk_buff *skb;
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK) pDevice;
	PUM_PACKET pUmPacket;
	PLM_PACKET pPacket;

	for (i = 0; i < pDevice->RxPacketDescCnt; i++) {
		pPacket = QQ_PopHead(&pDevice->RxPacketFreeQ.Container);
		pUmPacket = (PUM_PACKET) pPacket;
		if (pPacket == 0) {
			printk(KERN_DEBUG "Bad RxPacketFreeQ\n");
		}
		skb = dev_alloc_skb(pPacket->u.Rx.RxBufferSize);
		if (skb == 0) {
			pUmPacket->skbuff = 0;
			QQ_PushTail(&pUmDevice->rx_out_of_buf_q.Container, pPacket);
			continue;
		}
		pUmPacket->skbuff = skb;
		pPacket->u.Rx.pRxBufferVirt = skb->tail;
		skb->dev = pUmDevice->dev;
		skb_reserve(skb, pDevice->rxoffset);
		QQ_PushTail(&pDevice->RxPacketFreeQ.Container, pPacket);
	}
	pUmDevice->rx_buf_repl_thresh = pDevice->RxPacketDescCnt / 4;
	pUmDevice->rx_buf_repl_panic_thresh = pDevice->RxPacketDescCnt * 3 / 4;

	return LM_STATUS_SUCCESS;
}

LM_STATUS
b44_MM_GetConfig(PLM_DEVICE_BLOCK pDevice)
{
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK) pDevice;
	int index = pUmDevice->index;

	if (auto_speed[index] == 0)
		pDevice->DisableAutoNeg = TRUE;
	else
		pDevice->DisableAutoNeg = FALSE;

	if (line_speed[index] == 0) {
		pDevice->RequestedLineSpeed = LM_LINE_SPEED_AUTO;
		pDevice->DisableAutoNeg = FALSE;
	}
	else {
		if (full_duplex[index]) {
			pDevice->RequestedDuplexMode = LM_DUPLEX_MODE_FULL;
		}
		else {
			pDevice->RequestedDuplexMode = LM_DUPLEX_MODE_HALF;
		}

		if (line_speed[index] == 100) {
			pDevice->RequestedLineSpeed = LM_LINE_SPEED_100MBPS;
		}
		else if (line_speed[index] == 10) {
			pDevice->RequestedLineSpeed = LM_LINE_SPEED_10MBPS;
		}
		else {
			pDevice->RequestedLineSpeed = LM_LINE_SPEED_AUTO;
			pDevice->DisableAutoNeg = FALSE;
			printk(KERN_WARNING "%s-%d: Invalid line_speed parameter (%d), using 0\n", bcm4400_driver, index, line_speed[index]);
		}

	}
	pDevice->FlowControlCap = 0;
	if (rx_flow_control[index] != 0) {
		pDevice->FlowControlCap |= LM_FLOW_CONTROL_RECEIVE_PAUSE;
	}
	if (tx_flow_control[index] != 0) {
		pDevice->FlowControlCap |= LM_FLOW_CONTROL_TRANSMIT_PAUSE;
	}
	if (auto_flow_control[index] != 0) {
		if (pDevice->DisableAutoNeg == FALSE) {

			pDevice->FlowControlCap |= LM_FLOW_CONTROL_AUTO_PAUSE;
			if ((tx_flow_control[index] == 0) &&
				(rx_flow_control[index] == 0)) {

				pDevice->FlowControlCap |=
					LM_FLOW_CONTROL_TRANSMIT_PAUSE |
					LM_FLOW_CONTROL_RECEIVE_PAUSE;
			}
		}
		else {
			printk(KERN_WARNING "%s-%d: Conflicting auto_flow_control parameter (%d), using 0\n", bcm4400_driver, index, auto_flow_control[index]);
		}

	}

	pUmDevice->timer_interval = HZ / 10;
	pUmDevice->link_interval = HZ / pUmDevice->timer_interval;

	if ((tx_pkt_desc_cnt[index] == 0) ||
		(tx_pkt_desc_cnt[index] > MAX_TX_PACKET_DESC_COUNT)) {

		printk(KERN_WARNING "%s-%d: Invalid tx_pkt_desc_cnt parameter (%d), using %d\n",
			bcm4400_driver, index, tx_pkt_desc_cnt[index],
			DEFAULT_TX_PACKET_DESC_COUNT);

		tx_pkt_desc_cnt[index] = DEFAULT_TX_PACKET_DESC_COUNT;
	}
	pDevice->TxPacketDescCnt = tx_pkt_desc_cnt[index];
	if ((rx_pkt_desc_cnt[index] == 0) ||
		(rx_pkt_desc_cnt[index] >= MAX_RX_PACKET_DESC_COUNT)) {

		printk(KERN_WARNING "%s-%d: Invalid rx_pkt_desc_cnt parameter (%d), using %d\n",
			bcm4400_driver, index, rx_pkt_desc_cnt[index],
			DEFAULT_RX_PACKET_DESC_COUNT);

		rx_pkt_desc_cnt[index] = DEFAULT_RX_PACKET_DESC_COUNT;
	}
	pDevice->RxPacketDescCnt = rx_pkt_desc_cnt[index];

#ifdef BCM_WOL
	if (enable_wol[index]) {
		pDevice->WakeUpMode = LM_WAKE_UP_MODE_MAGIC_PACKET;
	}
#endif
	return LM_STATUS_SUCCESS;
}

LM_STATUS
b44_MM_IndicateRxPackets(PLM_DEVICE_BLOCK pDevice)
{
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK) pDevice;
	PLM_PACKET pPacket;
	PUM_PACKET pUmPacket;
	struct sk_buff *skb;
	int size;

	while (1) {
		pPacket = (PLM_PACKET)
			QQ_PopHead(&pDevice->RxPacketReceivedQ.Container);
		if (pPacket == 0)
			break;
		pUmPacket = (PUM_PACKET) pPacket;
#if ! defined(NO_PCI_UNMAP)
		pci_unmap_single(pUmDevice->pdev,
				pci_unmap_addr(pUmPacket, map[0]),
				pPacket->u.Rx.RxBufferSize,
				PCI_DMA_FROMDEVICE);
#endif
		if ((pPacket->PacketStatus != LM_STATUS_SUCCESS) ||
			((size = pPacket->PacketSize) > 1518)) {

			/* reuse skb */
#ifdef BCM_TASKLET
			QQ_PushTail(&pUmDevice->rx_out_of_buf_q.Container, pPacket);
#else
			QQ_PushTail(&pDevice->RxPacketFreeQ.Container, pPacket);
#endif
			pUmDevice->rx_misc_errors++;
			continue;
		}
		skb = pUmPacket->skbuff;
		skb_put(skb, size);
		skb->pkt_type = 0;
		skb->protocol = eth_type_trans(skb, skb->dev);
		skb->ip_summed = CHECKSUM_NONE;
#ifdef BCM_NAPI_RXPOLL
		netif_receive_skb(skb);
#else
		netif_rx(skb);
#endif

#ifdef BCM_TASKLET
		pUmPacket->skbuff = 0;
		QQ_PushTail(&pUmDevice->rx_out_of_buf_q.Container, pPacket);
#else
		skb = dev_alloc_skb(pPacket->u.Rx.RxBufferSize);
		if (skb == 0) {
			pUmPacket->skbuff = 0;
			QQ_PushTail(&pUmDevice->rx_out_of_buf_q.Container, pPacket);
		}
		else {
			pUmPacket->skbuff = skb; 
			pPacket->u.Rx.pRxBufferVirt = skb->tail;
			skb->dev = pUmDevice->dev;
			skb_reserve(skb, pDevice->rxoffset);
			QQ_PushTail(&pDevice->RxPacketFreeQ.Container, pPacket);
		}
#endif
	}
	return LM_STATUS_SUCCESS;
}

/* Returns 1 if not all buffers are allocated */
STATIC int
bcm4400_rxfill(PUM_DEVICE_BLOCK pUmDevice)
{
	PLM_PACKET pPacket;
	PUM_PACKET pUmPacket;
	PLM_DEVICE_BLOCK pDevice = (PLM_DEVICE_BLOCK) pUmDevice;
	struct sk_buff *skb;
	int queue_rx = 0;
	int ret = 0;

	if (!pUmDevice->opened)
		return ret;

	while ((pUmPacket = (PUM_PACKET)
		QQ_PopHead(&pUmDevice->rx_out_of_buf_q.Container)) != 0) {
		pPacket = (PLM_PACKET) pUmPacket;
		if (pUmPacket->skbuff) {
			/* reuse an old skb */
			QQ_PushTail(&pDevice->RxPacketFreeQ.Container, pPacket);
			queue_rx = 1;
			continue;
		}
		if ((skb = dev_alloc_skb(pPacket->u.Rx.RxBufferSize)) == 0) {
			QQ_PushHead(&pUmDevice->rx_out_of_buf_q.Container,
				pPacket);
			ret = 1;
			break;
		}
		pUmPacket->skbuff = skb;
		pPacket->u.Rx.pRxBufferVirt = skb->tail;
		skb->dev = pUmDevice->dev;
		skb_reserve(skb, pDevice->rxoffset);
		QQ_PushTail(&pDevice->RxPacketFreeQ.Container, pPacket);
		queue_rx = 1;
	}
	if (queue_rx) {
		b44_LM_QueueRxPackets(pDevice);
	}
	return ret;
}

LM_STATUS
b44_MM_IndicateTxPackets(PLM_DEVICE_BLOCK pDevice)
{
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK) pDevice;
	PLM_PACKET pPacket;
	PUM_PACKET pUmPacket;
	struct sk_buff *skb;
#if ! defined(NO_PCI_UNMAP) && MAX_SKB_FRAGS
	int i;
#endif

	while (1) {
		pPacket = (PLM_PACKET)
			QQ_PopHead(&pDevice->TxPacketXmittedQ.Container);
		if (pPacket == 0)
			break;
		pUmPacket = (PUM_PACKET) pPacket;
		skb = pUmPacket->skbuff;
#if ! defined(NO_PCI_UNMAP)
		pci_unmap_single(pUmDevice->pdev,
				pci_unmap_addr(pUmPacket, map[0]),
				pci_unmap_len(pUmPacket, map_len[0]),
				PCI_DMA_TODEVICE);
#endif
		dev_kfree_skb_irq(skb);
		pUmPacket->skbuff = 0;
		QQ_PushTail(&pDevice->TxPacketFreeQ.Container, pPacket);
	}
	if (pUmDevice->tx_full) {
		if (QQ_GetEntryCnt(&pDevice->TxPacketFreeQ.Container) >=
			(pDevice->TxPacketDescCnt >> 1)) {

			pUmDevice->tx_full = 0;
			netif_wake_queue(pUmDevice->dev);
		}
	}
	return LM_STATUS_SUCCESS;
}

LM_STATUS
b44_MM_IndicateStatus(PLM_DEVICE_BLOCK pDevice, LM_STATUS Status)
{
	PUM_DEVICE_BLOCK pUmDevice = (PUM_DEVICE_BLOCK) pDevice;
	struct net_device *dev = pUmDevice->dev;
	LM_FLOW_CONTROL flow_control;

	if (!pUmDevice->opened)
		return LM_STATUS_SUCCESS;

	if (pUmDevice->delayed_link_ind > 0) {
		return LM_STATUS_SUCCESS;
	}
	else {
		if (Status == LM_STATUS_LINK_DOWN) {
			pUmDevice->line_speed = 0;
			netif_carrier_off(dev);
			printk(KERN_ERR "%s: %s NIC Link is Down\n", bcm4400_driver, dev->name);
		}
		else if (Status == LM_STATUS_LINK_ACTIVE) {
			netif_carrier_on(dev);
			printk(KERN_INFO "%s: %s NIC Link is Up, ", bcm4400_driver, dev->name);
		}
	}

	if (Status == LM_STATUS_LINK_ACTIVE) {
		if (pDevice->LineSpeed == LM_LINE_SPEED_100MBPS)
			pUmDevice->line_speed = 100;
		else if (pDevice->LineSpeed == LM_LINE_SPEED_10MBPS)
			pUmDevice->line_speed = 10;

		printk("%d Mbps ", pUmDevice->line_speed);

		if (pDevice->DuplexMode == LM_DUPLEX_MODE_FULL)
			printk("full duplex");
		else
			printk("half duplex");

		flow_control = pDevice->FlowControl &
			(LM_FLOW_CONTROL_RECEIVE_PAUSE |
			LM_FLOW_CONTROL_TRANSMIT_PAUSE);
		if (flow_control) {
			if (flow_control & LM_FLOW_CONTROL_RECEIVE_PAUSE) {
				printk(", receive ");
				if (flow_control & LM_FLOW_CONTROL_TRANSMIT_PAUSE)
					printk("& transmit ");
			}
			else {
				printk(", transmit ");
			}
			printk("flow control ON");
		}
		printk("\n");
	}
	return LM_STATUS_SUCCESS;
}

LM_STATUS
b44_MM_FreeRxBuffer(PLM_DEVICE_BLOCK pDevice, PLM_PACKET pPacket)
{
	PUM_PACKET pUmPacket;
	struct sk_buff *skb;

	if (pPacket == 0)
		return LM_STATUS_SUCCESS;
	pUmPacket = (PUM_PACKET) pPacket;
	if ((skb = pUmPacket->skbuff)) {
#if ! defined(NO_PCI_UNMAP)
		UM_DEVICE_BLOCK *pUmDevice = (UM_DEVICE_BLOCK *) pDevice;

		pci_unmap_single(pUmDevice->pdev,
				pci_unmap_addr(pUmPacket, map[0]),
				pPacket->u.Rx.RxBufferSize,
				PCI_DMA_FROMDEVICE);
#endif
		dev_kfree_skb(skb);
	}
	pUmPacket->skbuff = 0;
	return LM_STATUS_SUCCESS;
}


