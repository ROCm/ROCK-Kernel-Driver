/*******************************************************************************

  
  Copyright(c) 1999 - 2003 Intel Corporation. All rights reserved.
  
  This program is free software; you can redistribute it and/or modify it 
  under the terms of the GNU General Public License as published by the Free 
  Software Foundation; either version 2 of the License, or (at your option) 
  any later version.
  
  This program is distributed in the hope that it will be useful, but WITHOUT 
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for 
  more details.
  
  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59 
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
  
  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
  
  Contact Information:
  Linux NICS <linux.nics@intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
*******************************************************************************/

#define __IXGB_MAIN__

#include "ixgb.h"

char ixgb_driver_name[] = "ixgb";
char ixgb_driver_string[] = "Intel(R) PRO/10GbE Network Driver";
char ixgb_driver_version[] = "1.0.47-k1jg";
char ixgb_copyright[] = "Copyright (c) 2001-2003 Intel Corporation.";

/* ixgb_pci_tbl - PCI Device ID Table
 *
 * For selecting devices to load on private driver_data field (last one) 
 * stores an index into ixgb_strings.
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, String Index }
 */
static struct pci_device_id ixgb_pci_tbl[] = {
	/* Intel(R) PRO/10GbE Network Connection */
	{INTEL_VENDOR_ID, IXGB_DEVICE_ID_82597EX,
	 INTEL_SUBVENDOR_ID, IXGB_SUBDEVICE_ID_A11F, 0, 0, 0},
	{INTEL_VENDOR_ID, IXGB_DEVICE_ID_82597EX,
	 INTEL_SUBVENDOR_ID, IXGB_SUBDEVICE_ID_A01F, 0, 0, 0},
	/* Generic */
	{INTEL_VENDOR_ID, IXGB_DEVICE_ID_82597EX,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},

	/* required last entry */
	{0,}
};

MODULE_DEVICE_TABLE(pci, ixgb_pci_tbl);

static char *ixgb_strings[] = {
	"Intel(R) PRO/10GbE Network Connection"
};

/* Local Function Prototypes */

int ixgb_up(struct ixgb_adapter *adapter);
void ixgb_down(struct ixgb_adapter *adapter, boolean_t kill_watchdog);
void ixgb_reset(struct ixgb_adapter *adapter);

static int ixgb_init_module(void);
static void ixgb_exit_module(void);
static int ixgb_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void ixgb_remove(struct pci_dev *pdev);
static void ixgb_sw_init(struct ixgb_adapter *adapter);
static int ixgb_open(struct net_device *netdev);
static int ixgb_close(struct net_device *netdev);
static int ixgb_setup_tx_resources(struct ixgb_adapter *adapter);
static int ixgb_setup_rx_resources(struct ixgb_adapter *adapter);
static void ixgb_configure_tx(struct ixgb_adapter *adapter);
static void ixgb_configure_rx(struct ixgb_adapter *adapter);
static void ixgb_setup_rctl(struct ixgb_adapter *adapter);
static void ixgb_clean_tx_ring(struct ixgb_adapter *adapter);
static void ixgb_clean_rx_ring(struct ixgb_adapter *adapter);
static void ixgb_free_tx_resources(struct ixgb_adapter *adapter);
static void ixgb_free_rx_resources(struct ixgb_adapter *adapter);
static void ixgb_set_multi(struct net_device *netdev);
static void ixgb_watchdog(unsigned long data);
static inline boolean_t ixgb_tso(struct ixgb_adapter *adapter,
				 struct sk_buff *skb);
static int ixgb_xmit_frame(struct sk_buff *skb, struct net_device *netdev);
static void ixgb_tx_timeout(struct net_device *netdev);
static void ixgb_tx_timeout_task(struct net_device *netdev);
static void ixgb_vlan_rx_register(struct net_device *netdev,
				  struct vlan_group *grp);
static void ixgb_vlan_rx_add_vid(struct net_device *netdev, u16 vid);
static void ixgb_vlan_rx_kill_vid(struct net_device *netdev, u16 vid);
static void ixgb_restore_vlan(struct ixgb_adapter *adapter);
static struct net_device_stats *ixgb_get_stats(struct net_device *netdev);
static int ixgb_change_mtu(struct net_device *netdev, int new_mtu);
static int ixgb_set_mac(struct net_device *netdev, void *p);
static void ixgb_update_stats(struct ixgb_adapter *adapter);
static inline void ixgb_irq_disable(struct ixgb_adapter *adapter);
static inline void ixgb_irq_enable(struct ixgb_adapter *adapter);
static irqreturn_t ixgb_intr(int irq, void *data, struct pt_regs *regs);
static void ixgb_clean_tx_irq(struct ixgb_adapter *adapter);
#ifdef CONFIG_IXGB_NAPI
static int ixgb_poll(struct net_device *netdev, int *budget);
#else
static void ixgb_clean_rx_irq(struct ixgb_adapter *adapter);
#endif
static void ixgb_alloc_rx_buffers(struct ixgb_adapter *adapter);
static int ixgb_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd);
static inline void ixgb_rx_checksum(struct ixgb_adapter *adapter,
				    struct ixgb_rx_desc *rx_desc,
				    struct sk_buff *skb);
static int ixgb_notify_reboot(struct notifier_block *, unsigned long event,
			      void *ptr);
static int ixgb_suspend(struct pci_dev *pdev, u32 state);

struct notifier_block ixgb_notifier_reboot = {
	.notifier_call	= ixgb_notify_reboot,
	.next		= NULL,
	.priority	= 0
};

/* Exported from other modules */

extern void ixgb_check_options(struct ixgb_adapter *adapter);
extern int ixgb_ethtool_ioctl(struct net_device *netdev, struct ifreq *ifr);

static struct pci_driver ixgb_driver = {
	.name		= ixgb_driver_name,
	.id_table	= ixgb_pci_tbl,
	.probe		= ixgb_probe,
	.remove		= __devexit_p(ixgb_remove),
	/* Power Managment Hooks */
	.suspend	= NULL,
	.resume		= NULL
};

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION("Intel(R) PRO/10GbE Network Driver");
MODULE_LICENSE("GPL");

/* some defines for controlling descriptor fetches in h/w */
#define RXDCTL_PTHRESH_DEFAULT 128	/* chip considers prefech below this */
#define RXDCTL_HTHRESH_DEFAULT 16	/* chip will only prefetch if tail is 
					   pushed this many descriptors from head */
#define RXDCTL_WTHRESH_DEFAULT 16	/* chip writes back at this many or RXT0 */

/**
 * ixgb_init_module - Driver Registration Routine.
 *
 * ixgb_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 **/

static int __init
ixgb_init_module(void)
{
	int ret;
	IXGB_DBG("ixgb_init_module\n");

	printk(KERN_INFO "%s - version %s\n", ixgb_driver_string,
	       ixgb_driver_version);
	printk(KERN_INFO "%s\n", ixgb_copyright);
#ifdef CONFIG_IXGB_NAPI
	printk(KERN_INFO "NAPI Enabled\n");
#endif
	ret = pci_module_init(&ixgb_driver);
	if (ret >= 0) {
		register_reboot_notifier(&ixgb_notifier_reboot);
	}
	return ret;
}

module_init(ixgb_init_module);

/**
 * ixgb_exit_module - Driver Exit Cleanup Routine.
 *
 * ixgb_exit_module is called just before the driver is removed
 * from memory.
 **/

static void __exit
ixgb_exit_module(void)
{

	IXGB_DBG("ixgb_exit_module\n");
	unregister_reboot_notifier(&ixgb_notifier_reboot);
	pci_unregister_driver(&ixgb_driver);
}

module_exit(ixgb_exit_module);

/**
 * ixgb_up - Driver ifconfig UP routine.
 *
 * ixgb_up is called to initialize and bring online an interface.
 * @param adapter board private structure
 **/

int
ixgb_up(struct ixgb_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	IXGB_DBG("ixgb_up\n");

	if (request_irq(netdev->irq, &ixgb_intr, SA_SHIRQ | SA_SAMPLE_RANDOM,
			netdev->name, netdev)) {
		IXGB_DBG("%s: request_irq failed\n", netdev->name);
		return -1;
	}
	/* disable interrupts and get the hardware into a known state */
	IXGB_WRITE_REG(&adapter->hw, IMC, 0xffffffff);

	/* hardware was reset in probe/down, we need to reload some things */
	ixgb_set_multi(netdev);
	ixgb_restore_vlan(adapter);

	ixgb_configure_tx(adapter);
	ixgb_setup_rctl(adapter);
	ixgb_configure_rx(adapter);
	ixgb_alloc_rx_buffers(adapter);

	mod_timer(&adapter->watchdog_timer, jiffies);
	ixgb_irq_enable(adapter);

	IXGB_DBG("ixgb_up: RAH_0 is <%x>\n", IXGB_READ_REG(&adapter->hw, RAH));
	IXGB_DBG("ixgb_up: RDBAL is <%x>\n",
		 IXGB_READ_REG(&adapter->hw, RDBAL));
	return 0;
}

/**
 * ixgb_down - Driver ifconfig DOWN routine.
 *
 * ixgb_down is called to uninitialize and take offline an interface.
 * @param adapter board private structure
 * @param kill_watchdog
 **/
void
ixgb_down(struct ixgb_adapter *adapter, boolean_t kill_watchdog)
{
	struct net_device *netdev = adapter->netdev;

	IXGB_DBG("ixgb_down\n");

	ixgb_irq_disable(adapter);
	free_irq(netdev->irq, netdev);
	if (kill_watchdog)
		del_timer_sync(&adapter->watchdog_timer);
	adapter->link_speed = 0;
	adapter->link_duplex = 0;
	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	ixgb_reset(adapter);

	ixgb_clean_tx_ring(adapter);
	ixgb_clean_rx_ring(adapter);
}

/**
 * ixgb_reset - hardware reset.
 *
 * ixgb_reset is called to initialize hardware to a known state.
 * @param adapter board private structure
 **/
void
ixgb_reset(struct ixgb_adapter *adapter)
{
	IXGB_DBG("ixgb_reset\n");

	ixgb_adapter_stop(&adapter->hw);
	if (!ixgb_init_hw(&adapter->hw))
		IXGB_DBG("ixgb_init_hw failed.\n");
}

/**
 * ixgb_probe - Device Initialization Routine.
 * @param pdev PCI device information struct
 * @param ent entry in ixgb_pci_table
 *
 * Returns 0 on success, negative on failure
 **/

static int __devinit
ixgb_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *netdev = NULL;
	struct ixgb_adapter *adapter;
	static int cards_found = 0;
	unsigned long mmio_start;
	int mmio_len;
	int pci_using_dac;
	int i;

	IXGB_DBG("ixgb_probe\n");

	if ((i = pci_enable_device(pdev))) {
		IXGB_ERR("pci_enable_device failed\n");
		return i;
	}

	if (!(i = pci_set_dma_mask(pdev, DMA_64BIT_MASK))) {
		pci_using_dac = 1;
	} else {
		if ((i = pci_set_dma_mask(pdev, DMA_32BIT_MASK))) {
			IXGB_ERR("No usable DMA configuration, aborting\n");
			return i;
		}
		pci_using_dac = 0;
	}

	if ((i = pci_request_regions(pdev, ixgb_driver_name))) {
		IXGB_ERR("Failed to reserve PCI I/O and Memory resources.\n");
		return i;
	}

	pci_set_master(pdev);

	/* alloc_etherdev clears the memory for us */
	netdev = alloc_etherdev(sizeof (struct ixgb_adapter));
	if (!netdev) {
		IXGB_ERR("Unable to allocate net_device struct\n");
		goto err_alloc_etherdev;
	}

	SET_MODULE_OWNER(netdev);
	SET_NETDEV_DEV(netdev, &pdev->dev);

	pci_set_drvdata(pdev, netdev);
	adapter = netdev->priv;
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	adapter->hw.back = adapter;

	mmio_start = pci_resource_start(pdev, BAR_0);
	mmio_len = pci_resource_len(pdev, BAR_0);

	adapter->hw.hw_addr = ioremap(mmio_start, mmio_len);
	if (!adapter->hw.hw_addr)
		goto err_ioremap;

	for (i = BAR_1; i <= BAR_5; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		if (pci_resource_flags(pdev, i) & IORESOURCE_IO) {
			adapter->hw.io_base = pci_resource_start(pdev, i);
			break;
		}
	}
	IXGB_DBG("mmio_start<%lx> hw_addr<%p>\n", mmio_start,
		 adapter->hw.hw_addr);

	netdev->open = &ixgb_open;
	netdev->stop = &ixgb_close;
	netdev->hard_start_xmit = &ixgb_xmit_frame;
	netdev->get_stats = &ixgb_get_stats;
	netdev->set_multicast_list = &ixgb_set_multi;
	netdev->set_mac_address = &ixgb_set_mac;
	netdev->change_mtu = &ixgb_change_mtu;
	netdev->do_ioctl = &ixgb_ioctl;
	netdev->tx_timeout = &ixgb_tx_timeout;
	netdev->watchdog_timeo = HZ;
#ifdef CONFIG_IXGB_NAPI
	netdev->poll = &ixgb_poll;
	netdev->weight = 64;
#endif
	netdev->vlan_rx_register = ixgb_vlan_rx_register;
	netdev->vlan_rx_add_vid = ixgb_vlan_rx_add_vid;
	netdev->vlan_rx_kill_vid = ixgb_vlan_rx_kill_vid;

	netdev->irq = pdev->irq;
	netdev->mem_start = mmio_start;
	netdev->mem_end = mmio_start + mmio_len;
	netdev->base_addr = adapter->hw.io_base;

	adapter->bd_number = cards_found;
	adapter->id_string = ixgb_strings[ent->driver_data];
	adapter->link_speed = 0;
	adapter->link_duplex = 0;

	/* setup the private structure */
	ixgb_sw_init(adapter);

	netdev->features = NETIF_F_SG |
	    NETIF_F_HW_CSUM |
	    NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX | NETIF_F_HW_VLAN_FILTER;
#ifdef NETIF_F_TSO
	netdev->features |= NETIF_F_TSO;
#endif

	if (pci_using_dac)
		netdev->features |= NETIF_F_HIGHDMA;

	/* make sure the EEPROM is good */

	if (!ixgb_validate_eeprom_checksum(&adapter->hw)) {
		IXGB_DBG("Invalid EEPROM checksum.\n");
		goto err_eeprom;
	}

	ixgb_get_ee_mac_addr(&adapter->hw, netdev->dev_addr);

	if (!is_valid_ether_addr(netdev->dev_addr)) {
		IXGB_DBG("Invalid MAC address in EEPROM.\n");
		goto err_eeprom;
	}

	adapter->max_data_per_txd = IXGB_MAX_JUMBO_FRAME_SIZE;
	adapter->part_num = ixgb_get_ee_pba_number(&adapter->hw);

	init_timer(&adapter->watchdog_timer);
	adapter->watchdog_timer.function = &ixgb_watchdog;
	adapter->watchdog_timer.data = (unsigned long) adapter;

	INIT_WORK(&adapter->tx_timeout_task,
		  (void (*)(void *)) ixgb_tx_timeout_task, netdev);

	register_netdev(netdev);
	memcpy(adapter->ifname, netdev->name, IFNAMSIZ);
	adapter->ifname[IFNAMSIZ - 1] = 0;

	/* we're going to reset, so assume we have no link for now */

	netif_carrier_off(netdev);
	netif_stop_queue(netdev);

	printk(KERN_INFO "%s: %s\n", netdev->name, adapter->id_string);
	ixgb_check_options(adapter);

	/* reset the hardware with the new settings */
	ixgb_reset(adapter);

	cards_found++;
	return 0;

      err_eeprom:
	iounmap(adapter->hw.hw_addr);
      err_ioremap:
	pci_release_regions(pdev);
	free_netdev(netdev);
      err_alloc_etherdev:
	return -ENOMEM;
}

/**
 * ixgb_remove - Device Removal Routine.
 * @param pdev PCI device information struct
 *
 * ixgb_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/

static void __devexit
ixgb_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct ixgb_adapter *adapter = netdev->priv;

	IXGB_DBG("ixgb_remove\n");

	unregister_netdev(netdev);

#ifdef ETHTOOL_IDENTIFY
	ixgb_identify_stop(adapter);
#endif

	iounmap((void *) adapter->hw.hw_addr);
	pci_release_regions(pdev);

	free_netdev(netdev);
}

/**
 * ixgb_sw_init - Initialize general software structures (struct ixgb_adapter).
 * @param adapter board private structure to initialize
 *
 * ixgb_sw_init initializes the adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 **/

static void __devinit
ixgb_sw_init(struct ixgb_adapter *adapter)
{
	struct ixgb_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;

	IXGB_DBG("ixgb_sw_init\n");

	/* PCI config space info */

	/* FIXME: do not store, instead directly use struct pci_dev
	 * where needed
	 */
	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_id = pdev->subsystem_device;

	adapter->rx_buffer_len = IXGB_RXBUFFER_2048;

	hw->max_frame_size = netdev->mtu + ENET_HEADER_SIZE + ENET_FCS_LENGTH;

	if (hw->device_id == IXGB_DEVICE_ID_82597EX)
		hw->mac_type = ixgb_82597;
	else {
		/* should never have loaded on this device */
		printk(KERN_ERR "ixgb: unsupported device id\n");
	}

	/* enable flow control to be programmed */
	hw->fc.send_xon = 1;

	atomic_set(&adapter->irq_sem, 1);
}

/**
 * ixgb_open - Called when a network interface is made active.
 * @param netdev network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 **/

static int
ixgb_open(struct net_device *netdev)
{
	struct ixgb_adapter *adapter = netdev->priv;

	IXGB_DBG("ixgb_open\n");

	/* allocate transmit descriptors */

	if (ixgb_setup_tx_resources(adapter)) {
		IXGB_DBG("ixgb_open: failed ixgb_setup_tx_resources.\n");
		goto err_setup_tx;
	}

	/* allocate receive descriptors and buffers */

	if (ixgb_setup_rx_resources(adapter)) {
		IXGB_DBG("ixgb_open: failed ixgb_setup_rx_resources.\n");
		goto err_setup_rx;
	}
	if (ixgb_up(adapter))
		goto err_up;

	return 0;

      err_up:
	ixgb_free_rx_resources(adapter);
      err_setup_rx:
	ixgb_free_tx_resources(adapter);
      err_setup_tx:
	ixgb_reset(adapter);
	return -EBUSY;
}

/**
 * ixgb_close - Disables a network interface.
 * @param netdev network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 **/

static int
ixgb_close(struct net_device *netdev)
{
	struct ixgb_adapter *adapter = netdev->priv;

	IXGB_DBG("ixgb_close\n");

	ixgb_down(adapter, TRUE);

	ixgb_free_tx_resources(adapter);
	ixgb_free_rx_resources(adapter);

	return 0;
}

/**
 * ixgb_setup_tx_resources - allocate Tx resources (Descriptors).
 * @param adapter board private structure
 *
 * Return 0 on success, negative on failure
 **/

static int
ixgb_setup_tx_resources(struct ixgb_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct ixgb_desc_ring *txdr = &adapter->tx_ring;
	int size;

	IXGB_DBG("ixgb_setup_tx_resources\n");

	size = sizeof (struct ixgb_buffer) * txdr->count;
	txdr->buffer_info = kmalloc(size, GFP_KERNEL);
	if (!txdr->buffer_info) {
		return -ENOMEM;
	}
	memset(txdr->buffer_info, 0, size);

	/* round up to nearest 4K */
	txdr->size = txdr->count * sizeof (struct ixgb_tx_desc);
	IXGB_ROUNDUP(txdr->size, 4096);

	txdr->desc = pci_alloc_consistent(pdev, txdr->size, &txdr->dma);
	if (!txdr->desc) {
		kfree(txdr->buffer_info);
		return -ENOMEM;
	}
	memset(txdr->desc, 0, txdr->size);

	IXGB_DBG("txdr->desc <%p>\n", txdr->desc);
	IXGB_DBG("txdr->next_to_use = <%p>\n", &txdr->next_to_use);
	IXGB_DBG("txdr->next_to_clean = <%p>\n", &txdr->next_to_clean);

	txdr->next_to_use = 0;
	txdr->next_to_clean = 0;

	return 0;
}

/**
 * ixgb_configure_tx - Configure 82597 Transmit Unit after Reset.
 * @adapter board private structure
 *
 * Configure the Tx unit of the MAC after a reset.
 **/

static void
ixgb_configure_tx(struct ixgb_adapter *adapter)
{
	u32 tctl;
	u32 tdlen = adapter->tx_ring.count * sizeof (struct ixgb_tx_desc);
	uint64_t tdba = adapter->tx_ring.dma;
	struct ixgb_hw *hw = &adapter->hw;

	IXGB_DBG("ixgb_configure_tx\n");

	/* Setup the Base and Length of the Tx Descriptor Ring 
	 * tx_ring.dma can be either a 32 or 64 bit value 
	 */

	IXGB_WRITE_REG(hw, TDBAL, (tdba & 0x00000000ffffffffULL));
	IXGB_WRITE_REG(hw, TDBAH, (tdba >> 32));

	IXGB_WRITE_REG(hw, TDLEN, tdlen);

	/* Setup the HW Tx Head and Tail descriptor pointers */

	IXGB_WRITE_REG(hw, TDH, 0);
	IXGB_WRITE_REG(hw, TDT, 0);

	/* don't set up txdctl, it induces performance problems if
	 * configured incorrectly
	 txdctl  = TXDCTL_PTHRESH_DEFAULT; // prefetch txds below this threshold
	 txdctl |= (TXDCTL_HTHRESH_DEFAULT // only prefetch if there are this many ready
	 << IXGB_TXDCTL_HTHRESH_SHIFT);
	 IXGB_WRITE_REG (hw, TXDCTL, txdctl);
	 */

	/* Set the Tx Interrupt Delay register */

	IXGB_WRITE_REG(hw, TIDV, adapter->tx_int_delay);

	/* Program the Transmit Control Register */

	tctl = IXGB_TCTL_TCE | IXGB_TCTL_TXEN | IXGB_TCTL_TPDE;
	IXGB_WRITE_REG(hw, TCTL, tctl);

	/* Setup Transmit Descriptor Settings for this adapter */
	adapter->tx_cmd_type =
	    IXGB_TX_DESC_TYPE | IXGB_TX_DESC_CMD_RS
	    | (adapter->tx_int_delay_enable ? IXGB_TX_DESC_CMD_IDE : 0);
}

/**
 * ixgb_setup_rx_resources - allocate Rx resources (Descriptors).
 * @param adapter board private structure
 * 
 * Returns 0 on success, negative on failure
 **/

static int
ixgb_setup_rx_resources(struct ixgb_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct ixgb_desc_ring *rxdr = &adapter->rx_ring;
	int size;

	IXGB_DBG("ixgb_setup_rx_resources.\n");

	size = sizeof (struct ixgb_buffer) * rxdr->count;
	rxdr->buffer_info = kmalloc(size, GFP_KERNEL);
	if (!rxdr->buffer_info) {
		return -ENOMEM;
	}
	memset(rxdr->buffer_info, 0, size);

	/* Round up to nearest 4K */
	rxdr->size = rxdr->count * sizeof (struct ixgb_rx_desc);
	IXGB_ROUNDUP(rxdr->size, 4096);

	rxdr->desc = pci_alloc_consistent(pdev, rxdr->size, &rxdr->dma);

	if (!rxdr->desc) {
		IXGB_DBG("pci_alloc_consistent failed.\n");
		kfree(rxdr->buffer_info);
		return -ENOMEM;
	}
	memset(rxdr->desc, 0, rxdr->size);

	IXGB_DBG("rxdr->desc <%p>\n", rxdr->desc);
	IXGB_DBG("rxdr->next_to_use = <%p>\n", &rxdr->next_to_use);
	IXGB_DBG("rxdr->next_to_clean = <%p>\n", &rxdr->next_to_clean);

	rxdr->next_to_use = 0;
	rxdr->next_to_clean = 0;

	return 0;
}

/**
 * ixgb_setup_rctl - configure the receive control register.
 * @param adapter Board private structure
 **/

static void
ixgb_setup_rctl(struct ixgb_adapter *adapter)
{
	u32 rctl;

	rctl = IXGB_READ_REG(&adapter->hw, RCTL);

	IXGB_DBG("ixgb_setup_rctl\n");

	rctl &= ~(3 << IXGB_RCTL_MO_SHIFT);

	rctl |=
	    IXGB_RCTL_BAM | IXGB_RCTL_RDMTS_1_2 |
	    IXGB_RCTL_RXEN | IXGB_RCTL_CFF |
	    (adapter->hw.mc_filter_type << IXGB_RCTL_MO_SHIFT);

	rctl |= IXGB_RCTL_SECRC;

	switch (adapter->rx_buffer_len) {
	case IXGB_RXBUFFER_2048:
	default:
		rctl |= IXGB_RCTL_BSIZE_2048;
		break;
	case IXGB_RXBUFFER_4096:
		rctl |= IXGB_RCTL_BSIZE_4096;
		break;
	case IXGB_RXBUFFER_8192:
		rctl |= IXGB_RCTL_BSIZE_8192;
		break;
	case IXGB_RXBUFFER_16384:
		rctl |= IXGB_RCTL_BSIZE_16384;
		break;
	}
	IXGB_WRITE_REG(&adapter->hw, RCTL, rctl);
}

/**
 * ixgb_configure_rx - Configure 82597 Receive Unit after Reset.
 * @param adapter board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 **/

static void
ixgb_configure_rx(struct ixgb_adapter *adapter)
{
	uint64_t rdba = adapter->rx_ring.dma;
	u32 rdlen = adapter->rx_ring.count * sizeof (struct ixgb_rx_desc);
	struct ixgb_hw *hw = &adapter->hw;
	u32 rctl;
	u32 rxcsum;

	IXGB_DBG("ixgb_configure_rx\n");

	/* make sure receives are disabled while setting up the descriptors */
	rctl = IXGB_READ_REG(hw, RCTL);
	IXGB_WRITE_REG(hw, RCTL, rctl & ~IXGB_RCTL_RXEN);

	/* set the Receive Delay Timer Register */
	IXGB_WRITE_REG(hw, RDTR, adapter->rx_int_delay);

	/* Setup the Base and Length of the Rx Descriptor Ring */
	IXGB_WRITE_REG(hw, RDBAL, (rdba & 0x00000000ffffffffULL));
	IXGB_WRITE_REG(hw, RDBAH, (rdba >> 32));

	IXGB_WRITE_REG(hw, RDLEN, rdlen);

	/* Setup the HW Rx Head and Tail Descriptor Pointers */
	IXGB_WRITE_REG(hw, RDH, 0);
	IXGB_WRITE_REG(hw, RDT, 0);

	{
		u32 rxdctl;
		/* burst 16 or burst when RXT0 */
		rxdctl = RXDCTL_WTHRESH_DEFAULT << IXGB_RXDCTL_WTHRESH_SHIFT
		    | RXDCTL_HTHRESH_DEFAULT << IXGB_RXDCTL_HTHRESH_SHIFT
		    | RXDCTL_PTHRESH_DEFAULT << IXGB_RXDCTL_PTHRESH_SHIFT;
		IXGB_WRITE_REG(hw, RXDCTL, rxdctl);
	}

	if (adapter->raidc) {
		u32 raidc;
		u8 poll_threshold;

		/* Poll every rx_int_delay period, if RBD exists
		 * Receive Backlog Detection is set to <threshold> 
		 * Rx Descriptors
		 * max is 0x3F == set to poll when 504 RxDesc left 
		 * min is 0 */

		/* polling times are 1 == 0.8192us
		   2 == 1.6384us
		   3 == 3.2768us etc
		   ...
		   511 == 418 us
		 */
#define IXGB_RAIDC_POLL_DEFAULT 122	/* set to poll every ~100 us under load 
					   also known as 10000 interrupts / sec */

		/* divide this by 2^3 (8) to get a register size count */
		poll_threshold = ((adapter->rx_ring.count - 1) >> 3);
		/* poll at half of that size */
		poll_threshold >>= 1;
		/* make sure its not bigger than our max */
		poll_threshold &= 0x3F;

		raidc = IXGB_RAIDC_EN |	/* turn on raidc style moderation */
		    IXGB_RAIDC_RXT_GATE |	/* don't interrupt with rxt0 while
						   in RBD mode (polling) */
		    (IXGB_RAIDC_POLL_DEFAULT << IXGB_RAIDC_POLL_SHIFT) |
		    /* this sets the regular "min interrupt delay" */
		    (adapter->rx_int_delay << IXGB_RAIDC_DELAY_SHIFT) |
		    poll_threshold;

		IXGB_WRITE_REG(hw, RAIDC, raidc);
	}

	/* Enable Receive Checksum Offload for TCP and UDP */
	if (adapter->rx_csum == TRUE) {
		rxcsum = IXGB_READ_REG(hw, RXCSUM);
		rxcsum |= IXGB_RXCSUM_TUOFL;
		IXGB_WRITE_REG(hw, RXCSUM, rxcsum);
	}

	/* Enable Receives */

	IXGB_WRITE_REG(hw, RCTL, rctl);
}

/**
 * ixgb_free_tx_resources - Free Tx Resources.
 * @param adapter board private structure
 *
 * Free all transmit software resources
 **/

static void
ixgb_free_tx_resources(struct ixgb_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;

	IXGB_DBG("ixgb_free_tx_resources\n");

	ixgb_clean_tx_ring(adapter);

	kfree(adapter->tx_ring.buffer_info);
	adapter->tx_ring.buffer_info = NULL;

	pci_free_consistent(pdev, adapter->tx_ring.size, adapter->tx_ring.desc,
			    adapter->tx_ring.dma);

	adapter->tx_ring.desc = NULL;
}

/**
 * ixgb_clean_tx_ring - Free Tx Buffers.
 * @param adapter board private structure
 **/

static void
ixgb_clean_tx_ring(struct ixgb_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	unsigned long size;
	int i;

	IXGB_DBG("ixgb_clean_tx_ring\n");

	/* Free all the Tx ring sk_buffs */

	for (i = 0; i < adapter->tx_ring.count; i++) {
		if (adapter->tx_ring.buffer_info[i].skb) {

			pci_unmap_page(pdev,
				       adapter->tx_ring.buffer_info[i].dma,
				       adapter->tx_ring.buffer_info[i].length,
				       PCI_DMA_TODEVICE);

			dev_kfree_skb(adapter->tx_ring.buffer_info[i].skb);

			adapter->tx_ring.buffer_info[i].skb = NULL;
		}
	}

	size = sizeof (struct ixgb_buffer) * adapter->tx_ring.count;
	memset(adapter->tx_ring.buffer_info, 0, size);

	/* Zero out the descriptor ring */

	memset(adapter->tx_ring.desc, 0, adapter->tx_ring.size);

	adapter->tx_ring.next_to_use = 0;
	adapter->tx_ring.next_to_clean = 0;

	IXGB_WRITE_REG(&adapter->hw, TDH, 0);
	IXGB_WRITE_REG(&adapter->hw, TDT, 0);
}

/**
 * ixgb_free_rx_resources - Free Rx Resources.
 * @param adapter board private structure
 *
 * Free all receive software resources
 **/

static void
ixgb_free_rx_resources(struct ixgb_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;

	IXGB_DBG("ixgb_free_rx_resources\n");

	ixgb_clean_rx_ring(adapter);

	kfree(adapter->rx_ring.buffer_info);
	adapter->rx_ring.buffer_info = NULL;

	pci_free_consistent(pdev, adapter->rx_ring.size,
			    adapter->rx_ring.desc, adapter->rx_ring.dma);

	adapter->rx_ring.desc = NULL;
}

/**
 * ixgb_clean_rx_ring - Free Rx Buffers.
 * @param adapter board private structure
 **/

static void
ixgb_clean_rx_ring(struct ixgb_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	unsigned long size;
	int i;

	IXGB_DBG("ixgb_free_rx_ring\n");

	/* Free all the Rx ring sk_buffs */

	for (i = 0; i < adapter->rx_ring.count; i++) {
		if (adapter->rx_ring.buffer_info[i].skb) {

			pci_unmap_single(pdev,
					 adapter->rx_ring.buffer_info[i].dma,
					 adapter->rx_ring.buffer_info[i].length,
					 PCI_DMA_FROMDEVICE);

			dev_kfree_skb(adapter->rx_ring.buffer_info[i].skb);

			adapter->rx_ring.buffer_info[i].skb = NULL;
		}
	}

	size = sizeof (struct ixgb_buffer) * adapter->rx_ring.count;
	memset(adapter->rx_ring.buffer_info, 0, size);

	/* Zero out the descriptor ring */

	memset(adapter->rx_ring.desc, 0, adapter->rx_ring.size);

	adapter->rx_ring.next_to_clean = 0;
	adapter->rx_ring.next_to_use = 0;

	IXGB_WRITE_REG(&adapter->hw, RDH, 0);
	IXGB_WRITE_REG(&adapter->hw, RDT, 0);
}

/**
 * ixgb_set_multi - Multicast and Promiscuous mode set.
 * @param netdev network interface device structure
 *
 * The set_multi entry point is called whenever the multicast address
 * list or the network interface flags are updated.  This routine is
 * resposible for configuring the hardware for proper multicast,
 * promiscuous mode, and all-multi behavior.
 **/

void
ixgb_set_multi(struct net_device *netdev)
{
	struct ixgb_adapter *adapter = netdev->priv;
	struct ixgb_hw *hw = &adapter->hw;
	u32 rctl;
	int i;
	struct dev_mc_list *mc_ptr;

	IXGB_DBG("ixgb_set_multi <%x>\n", netdev->flags);

	/* Check for Promiscuous and All Multicast modes */

	rctl = IXGB_READ_REG(&adapter->hw, RCTL);

	if (netdev->flags & IFF_PROMISC) {
		rctl |= (IXGB_RCTL_UPE | IXGB_RCTL_MPE);
	} else if (netdev->flags & IFF_ALLMULTI) {
		rctl |= IXGB_RCTL_MPE;
		rctl &= ~IXGB_RCTL_UPE;
	} else {
		rctl &= ~(IXGB_RCTL_UPE | IXGB_RCTL_MPE);
	}

	if (netdev->mc_count > IXGB_MAX_NUM_MULTICAST_ADDRESSES) {
		rctl |= IXGB_RCTL_MPE;
		IXGB_WRITE_REG(hw, RCTL, rctl);
	} else {
		u8 mta[netdev->mc_count * IXGB_ETH_LENGTH_OF_ADDRESS];

		IXGB_WRITE_REG(hw, RCTL, rctl);

		for (i = 0, mc_ptr = netdev->mc_list; mc_ptr;
		     i++, mc_ptr = mc_ptr->next)
			memcpy(&mta[i * IXGB_ETH_LENGTH_OF_ADDRESS],
			       mc_ptr->dmi_addr, IXGB_ETH_LENGTH_OF_ADDRESS);

		ixgb_mc_addr_list_update(hw, mta, netdev->mc_count, 0);
	}
}

/**
 * ixgb_watchdog - Timer Call-back.
 * @param data pointer to adapter cast into an unsigned long
 **/

void
ixgb_watchdog(unsigned long data)
{
	struct ixgb_adapter *adapter = (struct ixgb_adapter *) data;
	struct net_device *netdev = adapter->netdev;

	ixgb_check_for_link(&adapter->hw);

	if (ixgb_check_for_bad_link(&adapter->hw)) {
		/* force the reset path */
		netif_stop_queue(netdev);
	}

	if (adapter->hw.link_up) {
		if (!netif_carrier_ok(netdev)) {
			printk(KERN_INFO "ixgb: %s NIC Link is Up %d Mbps %s\n",
			       netdev->name, 10000, "Full Duplex");
			adapter->link_speed = 10000;
			adapter->link_duplex = FULL_DUPLEX;
			netif_carrier_on(netdev);
			netif_wake_queue(netdev);
		}
	} else {
		if (netif_carrier_ok(netdev)) {
			printk(KERN_INFO "ixgb: %s NIC Link is Down\n",
			       netdev->name);
			adapter->link_speed = 0;
			adapter->link_duplex = 0;
			netif_carrier_off(netdev);
			netif_stop_queue(netdev);

			ixgb_down(adapter, FALSE);
			ixgb_up(adapter);
		}
	}

	ixgb_update_stats(adapter);

	/* Early detection of hung controller */
	{
		struct ixgb_desc_ring *txdr = &adapter->tx_ring;
		int i = txdr->next_to_clean;

		if (txdr->buffer_info[i].dma &&
		    time_after(jiffies, txdr->buffer_info[i].time_stamp + HZ) &&
		    !(IXGB_READ_REG(&adapter->hw, STATUS) & IXGB_STATUS_TXOFF))
		{
			IXGB_DBG
			    ("ixgb: %s Hung controller? Watchdog stopping queue\n",
			     netdev->name);
			netif_stop_queue(netdev);
		}
	}

	/* generate an interrupt to force clean up of any stragglers */
	IXGB_WRITE_REG(&adapter->hw, ICS, IXGB_INT_TXDW);

	/* Reset the timer */
	mod_timer(&adapter->watchdog_timer, jiffies + 2 * HZ);
}

#define IXGB_TX_FLAGS_CSUM	0x00000001
#define IXGB_TX_FLAGS_VLAN	0x00000002
#define IXGB_TX_FLAGS_TSO	0x00000004

/** Transmit Segmentation offload setup.
 * ixgb_tso - (Large Send) setup where the initial descriptor is prepared
 * @param adapter adapter specific information
 * @param skb the skb we are trying to set up for segmentation
 **/

static inline boolean_t
ixgb_tso(struct ixgb_adapter *adapter, struct sk_buff *skb)
{
#ifdef NETIF_F_TSO
	struct ixgb_context_desc *context_desc;
	int i;
	u8 ipcss, ipcso, tucss, tucso, hdr_len;
	u16 ipcse, tucse, mss;

	if (likely(skb_shinfo(skb)->tso_size)) {
		hdr_len = ((skb->h.raw - skb->data) + (skb->h.th->doff << 2));
		mss = skb_shinfo(skb)->tso_size;
		skb->nh.iph->tot_len = 0;
		skb->nh.iph->check = 0;
		skb->h.th->check = ~csum_tcpudp_magic(skb->nh.iph->saddr,
						      skb->nh.iph->daddr,
						      0, IPPROTO_TCP, 0);
		ipcss = skb->nh.raw - skb->data;
		ipcso = (void *) &(skb->nh.iph->check) - (void *) skb->data;
		ipcse = skb->h.raw - skb->data - 1;
		tucss = skb->h.raw - skb->data;
		tucso = (void *) &(skb->h.th->check) - (void *) skb->data;
		tucse = 0;

		i = adapter->tx_ring.next_to_use;
		context_desc = IXGB_CONTEXT_DESC(adapter->tx_ring, i);

		context_desc->ipcss = ipcss;
		context_desc->ipcso = ipcso;
		context_desc->ipcse = cpu_to_le16(ipcse);
		context_desc->tucss = tucss;
		context_desc->tucso = tucso;
		context_desc->tucse = cpu_to_le16(tucse);
		context_desc->mss = cpu_to_le16(mss);
		context_desc->hdr_len = hdr_len;
		context_desc->status = 0;
		context_desc->cmd_type_len = cpu_to_le32(IXGB_CONTEXT_DESC_TYPE
							 |
							 IXGB_CONTEXT_DESC_CMD_TSE
							 |
							 IXGB_CONTEXT_DESC_CMD_IP
							 |
							 IXGB_CONTEXT_DESC_CMD_TCP
							 |
							 IXGB_CONTEXT_DESC_CMD_RS
							 |
							 IXGB_CONTEXT_DESC_CMD_IDE
							 | (skb->len -
							    (hdr_len)));

		i = (i + 1) % adapter->tx_ring.count;
		adapter->tx_ring.next_to_use = i;

		return TRUE;
	}
#endif
	return FALSE;
}

/**
 * ixgb_tx_csum - prepare context descriptor for checksum offload.
 *
 * ixgb_tx_csum is called to prepare for checksumming a packet in hw.
 * @param adapter board private structure
 * @param skb structure containing data to send
 **/
static inline boolean_t
ixgb_tx_csum(struct ixgb_adapter *adapter, struct sk_buff *skb)
{
	struct ixgb_context_desc *context_desc;
	int i;
	u8 css, cso;

	if (likely(skb->ip_summed == CHECKSUM_HW)) {
		css = skb->h.raw - skb->data;
		cso = (skb->h.raw + skb->csum) - skb->data;
		i = adapter->tx_ring.next_to_use;
		context_desc = IXGB_CONTEXT_DESC(adapter->tx_ring, i);

		context_desc->tucss = css;
		context_desc->tucso = cso;
		context_desc->tucse = 0;
		/* zero out any previously existing data in one instruction */
		*(u32 *) & (context_desc->ipcss) = 0;
		context_desc->status = 0;
		context_desc->hdr_len = 0;
		context_desc->mss = 0;
		context_desc->cmd_type_len =
		    cpu_to_le32(IXGB_CONTEXT_DESC_TYPE
				| IXGB_TX_DESC_CMD_RS | IXGB_TX_DESC_CMD_IDE);

		i = (i + 1) % adapter->tx_ring.count;
		adapter->tx_ring.next_to_use = i;

		return TRUE;
	}

	return FALSE;
}

/**
 * ixgb_tx_map - private function for mapping send data to hardware addresses.
 *
 * @param adapter board private structure
 * @param skb structure containing data to send
 **/

static inline int
ixgb_tx_map(struct ixgb_adapter *adapter, struct sk_buff *skb)
{
	struct ixgb_desc_ring *tx_ring = &adapter->tx_ring;
	int len, offset, count, size, i;

	int f;
	len = skb->len - skb->data_len;

	i = (tx_ring->next_to_use + tx_ring->count - 1) % tx_ring->count;
	count = 0;

	offset = 0;

	while (len) {
		i = (i + 1) % tx_ring->count;
		size = min(len, adapter->max_data_per_txd);
		tx_ring->buffer_info[i].length = size;
		tx_ring->buffer_info[i].dma =
		    pci_map_single(adapter->pdev, skb->data + offset, size,
				   PCI_DMA_TODEVICE);

		tx_ring->buffer_info[i].time_stamp = jiffies;

		len -= size;
		offset += size;
		count++;
	}

	for (f = 0; f < skb_shinfo(skb)->nr_frags; f++) {
		struct skb_frag_struct *frag;

		frag = &skb_shinfo(skb)->frags[f];
		len = frag->size;
		offset = 0;

		while (len) {
			i = (i + 1) % tx_ring->count;
			size = min(len, adapter->max_data_per_txd);
			tx_ring->buffer_info[i].length = size;
			tx_ring->buffer_info[i].dma =
			    pci_map_page(adapter->pdev, frag->page,
					 frag->page_offset + offset, size,
					 PCI_DMA_TODEVICE);

			tx_ring->buffer_info[i].time_stamp = jiffies;
			len -= size;
			offset += size;
			count++;
		}
	}
	tx_ring->buffer_info[i].skb = skb;

	return count;
}

/**
 * ixgb_tx_queue - private function to start transmit on hardware.
 *
 * @param adapter board private structure
 * @param count number of tx_descriptors to initialize (consume)
 * @param vlan_id the vlan tag to insert (if necessary)
 * @param tx_flags special handling for this transmit, if any
 **/

static inline void
ixgb_tx_queue(struct ixgb_adapter *adapter, int count, int vlan_id,
	      int tx_flags)
{
	struct ixgb_desc_ring *tx_ring = &adapter->tx_ring;
	struct ixgb_tx_desc *tx_desc = NULL;
	u32 cmd_type_len = adapter->tx_cmd_type;
	u8 status = 0;
	u8 popts = 0;
	int i;

	if (tx_flags & IXGB_TX_FLAGS_TSO) {
		cmd_type_len |= IXGB_TX_DESC_CMD_TSE;
		popts |= (IXGB_TX_DESC_POPTS_IXSM | IXGB_TX_DESC_POPTS_TXSM);
	}

	if (tx_flags & IXGB_TX_FLAGS_CSUM)
		popts |= IXGB_TX_DESC_POPTS_TXSM;

	if (tx_flags & IXGB_TX_FLAGS_VLAN) {
		cmd_type_len |= IXGB_TX_DESC_CMD_VLE;
	}

	i = tx_ring->next_to_use;

	while (count--) {
		tx_desc = IXGB_TX_DESC(*tx_ring, i);
		tx_desc->buff_addr = cpu_to_le64(tx_ring->buffer_info[i].dma);
		tx_desc->cmd_type_len =
		    cpu_to_le32(cmd_type_len | tx_ring->buffer_info[i].length);
		tx_desc->status = status;
		tx_desc->popts = popts;
		tx_desc->vlan = cpu_to_le16(vlan_id);

		i = (i + 1) % tx_ring->count;
	}

	tx_desc->cmd_type_len |= cpu_to_le32(IXGB_TX_DESC_CMD_EOP);

	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64). */
	wmb();

	tx_ring->next_to_use = i;
	IXGB_WRITE_REG(&adapter->hw, TDT, i);
}

#define TXD_USE_COUNT(S, X) (((S) / (X)) + (((S) % (X)) ? 1 : 0))

/**
 * ixgb_xmit_frame - hard_start_xmit linked function, transmit entry point.
 *
 * ixgb_xmit_frame is called to send an skb on the wire.
 * @param skb contains data to send
 * @param netdev network interface device structure 
 **/

static int
ixgb_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct ixgb_adapter *adapter = netdev->priv;
	int vlan_id = 0;
	int tx_flags = 0, count;
	int f;

	count =
	    TXD_USE_COUNT(skb->len - skb->data_len, adapter->max_data_per_txd);

	if (count == 0) {
		dev_kfree_skb_any(skb);
		return 0;
	}

	for (f = 0; f < skb_shinfo(skb)->nr_frags; f++)
		count += TXD_USE_COUNT(skb_shinfo(skb)->frags[f].size,
				       adapter->max_data_per_txd);
#ifdef NETIF_F_TSO
	if ((skb_shinfo(skb)->tso_size) || (skb->ip_summed == CHECKSUM_HW))
		count++;
#else
	if (skb->ip_summed == CHECKSUM_HW)
		count++;
#endif

	if (unlikely(IXGB_DESC_UNUSED(&adapter->tx_ring) < count)) {
		netif_stop_queue(netdev);
		return 1;
	}

	if (adapter->vlgrp && vlan_tx_tag_present(skb)) {
		tx_flags |= IXGB_TX_FLAGS_VLAN;
		vlan_id = vlan_tx_tag_get(skb);
	}

	if (ixgb_tso(adapter, skb))
		tx_flags |= IXGB_TX_FLAGS_TSO;
	else if (ixgb_tx_csum(adapter, skb))
		tx_flags |= IXGB_TX_FLAGS_CSUM;

	count = ixgb_tx_map(adapter, skb);
	ixgb_tx_queue(adapter, count, vlan_id, tx_flags);
	netdev->trans_start = jiffies;

	return 0;
}

/**
 * ixgb_tx_timeout - Respond to a Tx Hang by resetting the adapter.
 * @param netdev network interface device structure
 **/

static void
ixgb_tx_timeout(struct net_device *netdev)
{
	struct ixgb_adapter *adapter = netdev->priv;

	IXGB_DBG("ixgb_tx_timeout\n");

	/* Do the reset outside of interrupt context */
	schedule_work(&adapter->tx_timeout_task);
}

/**
 * ixgb_tx_timeout_task - worker function to reset hardware and dump queues.
 * This function is pointed to by adapter->tx_timeout_task
 *
 * @param netdev network interface device structure 
 **/

static void
ixgb_tx_timeout_task(struct net_device *netdev)
{
	struct ixgb_adapter *adapter = netdev->priv;

	IXGB_DBG("ixgb_tx_timeout_task\n");

	netif_device_detach(netdev);
	ixgb_down(adapter, TRUE);
	ixgb_up(adapter);
	netif_device_attach(netdev);
}

/**
 * ixgb_get_stats - Get System Network Statistics.
 * @param netdev network interface device structure
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the timer callback.
 **/

static struct net_device_stats *
ixgb_get_stats(struct net_device *netdev)
{
	struct ixgb_adapter *adapter = netdev->priv;

	return &adapter->net_stats;
}

/**
 * ixgb_change_mtu - Change the Maximum Transfer Unit.
 * @param netdev network interface device structure
 * @param new_mtu new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 **/

static int
ixgb_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct ixgb_adapter *adapter = netdev->priv;
	u32 old_mtu = adapter->rx_buffer_len;
	int max_frame = new_mtu + ENET_HEADER_SIZE + ENET_FCS_LENGTH;

	IXGB_DBG("ixgb_change_mtu\n");

	if ((max_frame < IXGB_MIN_ENET_FRAME_SIZE_WITHOUT_FCS + ENET_FCS_LENGTH)
	    || (max_frame > IXGB_MAX_JUMBO_FRAME_SIZE + ENET_FCS_LENGTH)) {
		IXGB_ERR("Invalid MTU setting\n");
		return -EINVAL;
	}

	if ((max_frame <=
	     IXGB_MAX_ENET_FRAME_SIZE_WITHOUT_FCS + ENET_FCS_LENGTH)
	    || (max_frame <= IXGB_RXBUFFER_2048)) {
		adapter->rx_buffer_len = IXGB_RXBUFFER_2048;

	} else if (max_frame <= IXGB_RXBUFFER_4096) {
		adapter->rx_buffer_len = IXGB_RXBUFFER_4096;

	} else if (max_frame <= IXGB_RXBUFFER_8192) {
		adapter->rx_buffer_len = IXGB_RXBUFFER_8192;

	} else {
		adapter->rx_buffer_len = IXGB_RXBUFFER_16384;
	}

	if (old_mtu != adapter->rx_buffer_len && netif_running(netdev)) {

		ixgb_down(adapter, TRUE);
		ixgb_up(adapter);
	}

	if (adapter->hw.max_frame_size != max_frame) {
		struct ixgb_hw *hw = &adapter->hw;

		adapter->hw.max_frame_size = max_frame;

		IXGB_WRITE_REG(hw, MFS, hw->max_frame_size << IXGB_MFS_SHIFT);

		if (hw->max_frame_size >
		    IXGB_MAX_ENET_FRAME_SIZE_WITHOUT_FCS + ENET_FCS_LENGTH) {
			u32 ctrl0 = IXGB_READ_REG(hw, CTRL0);

			if (!(ctrl0 & IXGB_CTRL0_JFE)) {
				ctrl0 |= IXGB_CTRL0_JFE;
				IXGB_WRITE_REG(hw, CTRL0, ctrl0);
			}
		}

		printk(KERN_ERR "%s: ixgb_change_mtu MFS is set to <%x>\n",
		       adapter->netdev->name,
		       (IXGB_READ_REG(hw, MFS) >> IXGB_MFS_SHIFT));
	}

	netdev->mtu = new_mtu;

	return 0;
}

/**
 * ixgb_set_mac - Change the Ethernet Address of the NIC.
 * @param netdev network interface device structure
 * @param p pointer to an address structure
 * 
 * Returns 0 on success, negative on failure
 **/

static int
ixgb_set_mac(struct net_device *netdev, void *p)
{
	struct ixgb_adapter *adapter = netdev->priv;
	struct sockaddr *addr = (struct sockaddr *) p;

	IXGB_DBG("ixgb_set_mac\n");

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);

	ixgb_rar_set(&adapter->hw, addr->sa_data, 0);

	return 0;
}

/**
 * ixgb_update_stats - Update the board statistics counters.
 * @param adapter board private structure
 **/

static void
ixgb_update_stats(struct ixgb_adapter *adapter)
{
	adapter->stats.tprl += IXGB_READ_REG(&adapter->hw, TPRL);
	adapter->stats.tprh += IXGB_READ_REG(&adapter->hw, TPRH);
	adapter->stats.gprcl += IXGB_READ_REG(&adapter->hw, GPRCL);
	adapter->stats.gprch += IXGB_READ_REG(&adapter->hw, GPRCH);
	adapter->stats.bprcl += IXGB_READ_REG(&adapter->hw, BPRCL);
	adapter->stats.bprch += IXGB_READ_REG(&adapter->hw, BPRCH);
	adapter->stats.mprcl += IXGB_READ_REG(&adapter->hw, MPRCL);
	adapter->stats.mprch += IXGB_READ_REG(&adapter->hw, MPRCH);
	adapter->stats.uprcl += IXGB_READ_REG(&adapter->hw, UPRCL);
	adapter->stats.uprch += IXGB_READ_REG(&adapter->hw, UPRCH);
	adapter->stats.vprcl += IXGB_READ_REG(&adapter->hw, VPRCL);
	adapter->stats.vprch += IXGB_READ_REG(&adapter->hw, VPRCH);
	adapter->stats.jprcl += IXGB_READ_REG(&adapter->hw, JPRCL);
	adapter->stats.jprch += IXGB_READ_REG(&adapter->hw, JPRCH);
	adapter->stats.gorcl += IXGB_READ_REG(&adapter->hw, GORCL);
	adapter->stats.gorch += IXGB_READ_REG(&adapter->hw, GORCH);
	adapter->stats.torl += IXGB_READ_REG(&adapter->hw, TORL);
	adapter->stats.torh += IXGB_READ_REG(&adapter->hw, TORH);
	adapter->stats.rnbc += IXGB_READ_REG(&adapter->hw, RNBC);
	adapter->stats.ruc += IXGB_READ_REG(&adapter->hw, RUC);
	adapter->stats.roc += IXGB_READ_REG(&adapter->hw, ROC);
	adapter->stats.rlec += IXGB_READ_REG(&adapter->hw, RLEC);
	adapter->stats.crcerrs += IXGB_READ_REG(&adapter->hw, CRCERRS);
	adapter->stats.icbc += IXGB_READ_REG(&adapter->hw, ICBC);
	adapter->stats.ecbc += IXGB_READ_REG(&adapter->hw, ECBC);
	adapter->stats.mpc += IXGB_READ_REG(&adapter->hw, MPC);
	adapter->stats.tptl += IXGB_READ_REG(&adapter->hw, TPTL);
	adapter->stats.tpth += IXGB_READ_REG(&adapter->hw, TPTH);
	adapter->stats.gptcl += IXGB_READ_REG(&adapter->hw, GPTCL);
	adapter->stats.gptch += IXGB_READ_REG(&adapter->hw, GPTCH);
	adapter->stats.bptcl += IXGB_READ_REG(&adapter->hw, BPTCL);
	adapter->stats.bptch += IXGB_READ_REG(&adapter->hw, BPTCH);
	adapter->stats.mptcl += IXGB_READ_REG(&adapter->hw, MPTCL);
	adapter->stats.mptch += IXGB_READ_REG(&adapter->hw, MPTCH);
	adapter->stats.uptcl += IXGB_READ_REG(&adapter->hw, UPTCL);
	adapter->stats.uptch += IXGB_READ_REG(&adapter->hw, UPTCH);
	adapter->stats.vptcl += IXGB_READ_REG(&adapter->hw, VPTCL);
	adapter->stats.vptch += IXGB_READ_REG(&adapter->hw, VPTCH);
	adapter->stats.jptcl += IXGB_READ_REG(&adapter->hw, JPTCL);
	adapter->stats.jptch += IXGB_READ_REG(&adapter->hw, JPTCH);
	adapter->stats.gotcl += IXGB_READ_REG(&adapter->hw, GOTCL);
	adapter->stats.gotch += IXGB_READ_REG(&adapter->hw, GOTCH);
	adapter->stats.totl += IXGB_READ_REG(&adapter->hw, TOTL);
	adapter->stats.toth += IXGB_READ_REG(&adapter->hw, TOTH);
	adapter->stats.dc += IXGB_READ_REG(&adapter->hw, DC);
	adapter->stats.plt64c += IXGB_READ_REG(&adapter->hw, PLT64C);
	adapter->stats.tsctc += IXGB_READ_REG(&adapter->hw, TSCTC);
	adapter->stats.tsctfc += IXGB_READ_REG(&adapter->hw, TSCTFC);
	adapter->stats.ibic += IXGB_READ_REG(&adapter->hw, IBIC);
	adapter->stats.rfc += IXGB_READ_REG(&adapter->hw, RFC);
	adapter->stats.lfc += IXGB_READ_REG(&adapter->hw, LFC);
	adapter->stats.pfrc += IXGB_READ_REG(&adapter->hw, PFRC);
	adapter->stats.pftc += IXGB_READ_REG(&adapter->hw, PFTC);
	adapter->stats.mcfrc += IXGB_READ_REG(&adapter->hw, MCFRC);
	adapter->stats.mcftc += IXGB_READ_REG(&adapter->hw, MCFTC);
	adapter->stats.xonrxc += IXGB_READ_REG(&adapter->hw, XONRXC);
	adapter->stats.xontxc += IXGB_READ_REG(&adapter->hw, XONTXC);
	adapter->stats.xoffrxc += IXGB_READ_REG(&adapter->hw, XOFFRXC);
	adapter->stats.xofftxc += IXGB_READ_REG(&adapter->hw, XOFFTXC);
	adapter->stats.rjc += IXGB_READ_REG(&adapter->hw, RJC);

	/* Fill out the OS statistics structure */

	adapter->net_stats.rx_packets = adapter->stats.gprcl;
	adapter->net_stats.tx_packets = adapter->stats.gptcl;
	adapter->net_stats.rx_bytes = adapter->stats.gorcl;
	adapter->net_stats.tx_bytes = adapter->stats.gotcl;
	adapter->net_stats.multicast = adapter->stats.mprcl;
	adapter->net_stats.collisions = 0;

	/* ignore RLEC as it reports errors for padded (<64bytes) frames
	 * with a length in the type/len field */
	adapter->net_stats.rx_errors =
	    /* adapter->stats.rnbc + */ adapter->stats.crcerrs +
	    adapter->stats.ruc +
	    adapter->stats.roc /*+ adapter->stats.rlec */  +
	    adapter->stats.icbc +
	    adapter->stats.ecbc + adapter->stats.mpc;

	adapter->net_stats.rx_dropped = adapter->stats.mpc;

	/* see above
	 * adapter->net_stats.rx_length_errors = adapter->stats.rlec;
	 */

	adapter->net_stats.rx_crc_errors = adapter->stats.crcerrs;
	adapter->net_stats.rx_fifo_errors = adapter->stats.mpc;
	adapter->net_stats.rx_missed_errors = adapter->stats.mpc;
	adapter->net_stats.rx_over_errors = adapter->stats.mpc;

	adapter->net_stats.tx_errors = 0;
	adapter->net_stats.rx_frame_errors = 0;
	adapter->net_stats.tx_aborted_errors = 0;
	adapter->net_stats.tx_carrier_errors = 0;
	adapter->net_stats.tx_fifo_errors = 0;
	adapter->net_stats.tx_heartbeat_errors = 0;
	adapter->net_stats.tx_window_errors = 0;
}

/**
 * ixgb_irq_disable - Mask off interrupt generation on the NIC
 * @param adapter board private structure
 **/

static inline void
ixgb_irq_disable(struct ixgb_adapter *adapter)
{
	IXGB_DBG("ixgb_irq_disable\n");

	atomic_inc(&adapter->irq_sem);
	IXGB_WRITE_REG(&adapter->hw, IMC, ~0);
	synchronize_irq(adapter->netdev->irq);
}

/**
 * ixgb_irq_enable - Enable default interrupt generation settings.
 * @param adapter board private structure
 **/

static inline void
ixgb_irq_enable(struct ixgb_adapter *adapter)
{
	IXGB_DBG("ixgb_irq_enable\n");

	if (atomic_dec_and_test(&adapter->irq_sem)) {
		IXGB_WRITE_REG(&adapter->hw, IMS,
			       IXGB_INT_RXT0 | IXGB_INT_RXDMT0 | IXGB_INT_TXDW |
			       IXGB_INT_RXO | IXGB_INT_LSC);
	}
}

#define IXGB_MAX_INTR 10
/**
 * ixgb_intr - Interrupt Handler.
 * @param irq interrupt number
 * @param data pointer to a network interface device structure
 * @param regs CPU registers structure
 **/

static irqreturn_t
ixgb_intr(int irq, void *data, struct pt_regs *regs)
{
	struct net_device *netdev = (struct net_device *) data;
	struct ixgb_adapter *adapter = netdev->priv;
#ifdef CONFIG_IXGB_NAPI
	if (netif_rx_schedule_prep(netdev)) {
		ixgb_irq_disable(adapter);
		__netif_rx_schedule(netdev);
	}

	return IRQ_HANDLED; /* FIXME: check for shared interrupts */
#else
	struct ixgb_hw *hw = &adapter->hw;
	u32 icr;
	uint i = IXGB_MAX_INTR;
	boolean_t rxdmt0 = FALSE;
	int handled = 0;

	while (i && (icr = IXGB_READ_REG(hw, ICR))) {
		handled = 1;

		if (icr & IXGB_INT_RXDMT0)
			rxdmt0 = TRUE;

		if (unlikely(icr & (IXGB_INT_RXSEQ | IXGB_INT_LSC))) {
			mod_timer(&adapter->watchdog_timer, jiffies);
		}

		/* adapter->generate_int = 0; */
		ixgb_clean_rx_irq(adapter);
		ixgb_clean_tx_irq(adapter);

		i--;
	}

	/* if RAIDC:EN == 1 and ICR:RXDMT0 == 1, we need to
	 * set IMS:RXDMT0 to 1 to restart the RBD timer (POLL)
	 */
	if (rxdmt0 && adapter->raidc) {
		/* ready the timer by writing the clear reg */
		IXGB_WRITE_REG(hw, IMC, IXGB_INT_RXDMT0);
		/* now restart it, h/w will decide if its necessary */
		IXGB_WRITE_REG(hw, IMS, IXGB_INT_RXDMT0);
	}

	return IRQ_RETVAL(handled);
#endif				// NAPI else
}

#ifdef CONFIG_IXGB_NAPI
static int
ixgb_process_intr(struct net_device *netdev)
{
	struct ixgb_adapter *adapter = netdev->priv;
	u32 icr;
	int i = IXGB_MAX_INTR;
	int hasReceived = 0;

	while (i && (icr = IXGB_READ_REG(&adapter->hw, ICR))) {
		if (icr & IXGB_INT_RXT0)
			hasReceived = 1;

		if (!(icr & ~(IXGB_INT_RXT0)))
			break;

		if (icr & (IXGB_INT_RXSEQ | IXGB_INT_LSC)) {
			mod_timer(&adapter->watchdog_timer, jiffies);
		}

		ixgb_clean_tx_irq(adapter);
		i--;
	}

	return hasReceived;
}

#endif
/**
 * ixgb_clean_tx_irq - Reclaim resources after transmit completes.
 * @param adapter board private structure
 **/

static void
ixgb_clean_tx_irq(struct ixgb_adapter *adapter)
{
	struct ixgb_desc_ring *tx_ring = &adapter->tx_ring;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	int i = adapter->tx_ring.next_to_clean;
	struct ixgb_tx_desc *tx_desc = IXGB_TX_DESC(*tx_ring, i);
	while ((tx_desc->status & IXGB_TX_DESC_STATUS_DD)) {
		if (tx_desc->popts
		    & (IXGB_TX_DESC_POPTS_TXSM | IXGB_TX_DESC_POPTS_IXSM))
			adapter->hw_csum_tx_good++;

		if (tx_ring->buffer_info[i].dma) {
			pci_unmap_page(pdev, tx_ring->buffer_info[i].dma,
				       tx_ring->buffer_info[i].length,
				       PCI_DMA_TODEVICE);
			tx_ring->buffer_info[i].dma = 0;
		}

		if (tx_ring->buffer_info[i].skb) {
			dev_kfree_skb_any(tx_ring->buffer_info[i].skb);
			tx_ring->buffer_info[i].skb = NULL;
		}

		*(u32 *) & (tx_desc->status) = 0;

		i = (i + 1) % tx_ring->count;
		tx_desc = IXGB_TX_DESC(*tx_ring, i);
	}

	tx_ring->next_to_clean = i;

	if (netif_queue_stopped(netdev) && netif_carrier_ok(netdev) &&
	    (IXGB_DESC_UNUSED(tx_ring) > IXGB_TX_QUEUE_WAKE)) {

		netif_wake_queue(netdev);
	}
}

#ifdef CONFIG_IXGB_NAPI
static int
ixgb_poll(struct net_device *netdev, int *budget)
{
	struct ixgb_adapter *adapter = netdev->priv;
	struct ixgb_desc_ring *rx_ring = &adapter->rx_ring;
	struct pci_dev *pdev = adapter->pdev;
	struct ixgb_rx_desc *rx_desc;
	struct sk_buff *skb;
	u32 length;
	int i;
	int received = 0;
	int rx_work_limit = *budget;

	if (rx_work_limit > netdev->quota)
		rx_work_limit = netdev->quota;

	ixgb_process_intr(netdev);

	i = rx_ring->next_to_clean;
	rx_desc = IXGB_RX_DESC(*rx_ring, i);

	while ((rx_desc->status & IXGB_RX_DESC_STATUS_DD)) {
		if (--rx_work_limit < 0)
			goto not_done;

		pci_unmap_single(pdev,
				 rx_ring->buffer_info[i].dma,
				 rx_ring->buffer_info[i].length,
				 PCI_DMA_FROMDEVICE);

		skb = rx_ring->buffer_info[i].skb;
		length = le16_to_cpu(rx_desc->length);

		if (!(rx_desc->status & IXGB_RX_DESC_STATUS_EOP)) {

			/* All receives must fit into a single buffer */

			IXGB_DBG("Receive packet consumed multiple buffers\n");

			dev_kfree_skb_irq(skb);
			rx_desc->status = 0;
			rx_ring->buffer_info[i].skb = NULL;

			i = (i + 1) % rx_ring->count;

			rx_desc = IXGB_RX_DESC(*rx_ring, i);
			continue;
		}

		if (rx_desc->
		    errors & (IXGB_RX_DESC_ERRORS_CE | IXGB_RX_DESC_ERRORS_SE |
			      IXGB_RX_DESC_ERRORS_P | IXGB_RX_DESC_ERRORS_RXE))
		{

			IXGB_DBG("Receive Errors Reported by Hardware-%x.\n",
				 rx_desc->errors);

			dev_kfree_skb_irq(skb);
			rx_desc->status = 0;
			rx_ring->buffer_info[i].skb = NULL;
			i = (i + 1) % rx_ring->count;
			rx_desc = IXGB_RX_DESC(*rx_ring, i);
			continue;
		}

		/* Good Receive */
		skb_put(skb, length);

		/* Receive Checksum Offload */
		ixgb_rx_checksum(adapter, rx_desc, skb);

		skb->protocol = eth_type_trans(skb, netdev);
		if (adapter->vlgrp
		    && (rx_desc->status & IXGB_RX_DESC_STATUS_VP)) {
			vlan_hwaccel_receive_skb(skb, adapter->vlgrp,
				(rx_desc-> special & IXGB_RX_DESC_SPECIAL_VLAN_MASK));
		} else {
			netif_receive_skb(skb);
		}
		netdev->last_rx = jiffies;

		rx_desc->status = 0;
		rx_ring->buffer_info[i].skb = NULL;

		i = (i + 1) % rx_ring->count;

		rx_desc = IXGB_RX_DESC(*rx_ring, i);
		received++;
	}

	if (!received)
		received = 1;

	ixgb_alloc_rx_buffers(adapter);

	rx_ring->next_to_clean = i;
	netdev->quota -= received;
	*budget -= received;

	netif_rx_complete(netdev);

	/* NOTE: RAIDC will be automatically restarted by this enable */
	ixgb_irq_enable(adapter);
	return 0;

      not_done:

	ixgb_alloc_rx_buffers(adapter);

	rx_ring->next_to_clean = i;
	netdev->quota -= received;
	*budget -= received;

	return 1;
}
#else
/**
 * ixgb_clean_rx_irq - Send received data up the network stack.
 * @param adapter board private structure
 **/

static void
ixgb_clean_rx_irq(struct ixgb_adapter *adapter)
{
	struct ixgb_desc_ring *rx_ring = &adapter->rx_ring;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct ixgb_rx_desc *rx_desc;
	struct sk_buff *skb;
	u32 length;
	int i;

	i = rx_ring->next_to_clean;
	rx_desc = IXGB_RX_DESC(*rx_ring, i);

	while ((rx_desc->status & IXGB_RX_DESC_STATUS_DD)) {
		pci_unmap_single(pdev, rx_ring->buffer_info[i].dma,
				 rx_ring->buffer_info[i].length,
				 PCI_DMA_FROMDEVICE);

		skb = rx_ring->buffer_info[i].skb;
		length = le16_to_cpu(rx_desc->length);

		if (unlikely(!(rx_desc->status & IXGB_RX_DESC_STATUS_EOP))) {

			/* All receives must fit into a single buffer */

			IXGB_DBG("Receive packet consumed multiple buffers "
				 "length<%x>\n", length);

			dev_kfree_skb_irq(skb);
			rx_desc->status = 0;
			rx_ring->buffer_info[i].skb = NULL;
			i = (i + 1) % rx_ring->count;
			rx_desc = IXGB_RX_DESC(*rx_ring, i);
			continue;
		}

		if (unlikely(rx_desc->errors
			     & (IXGB_RX_DESC_ERRORS_CE | IXGB_RX_DESC_ERRORS_SE
				| IXGB_RX_DESC_ERRORS_P |
				IXGB_RX_DESC_ERRORS_RXE))) {

			IXGB_DBG("Receive Errors Reported by Hardware-%x.\n",
				 rx_desc->errors);

			dev_kfree_skb_irq(skb);
			rx_desc->status = 0;
			rx_ring->buffer_info[i].skb = NULL;
			i = (i + 1) % rx_ring->count;
			rx_desc = IXGB_RX_DESC(*rx_ring, i);
			continue;
		}

		/* Good Receive */
		skb_put(skb, length);

		/* Receive Checksum Offload */
		ixgb_rx_checksum(adapter, rx_desc, skb);

		skb->protocol = eth_type_trans(skb, netdev);
		if (adapter->vlgrp
		    && (rx_desc->status & IXGB_RX_DESC_STATUS_VP)) {
			vlan_hwaccel_rx(skb, adapter->vlgrp,
					(rx_desc->
					 special &
					 IXGB_RX_DESC_SPECIAL_VLAN_MASK));
		} else {
			netif_rx(skb);
		}

		netdev->last_rx = jiffies;

		rx_desc->status = 0;

		rx_ring->buffer_info[i].skb = NULL;
		i = (i + 1) % rx_ring->count;

		rx_desc = IXGB_RX_DESC(*rx_ring, i);
	}			/* while */

	rx_ring->next_to_clean = i;

	ixgb_alloc_rx_buffers(adapter);
}
#endif

/**
 * ixgb_alloc_rx_buffers - Replace used receive buffers.
 * @param adapter address of board private structure
 **/

static void
ixgb_alloc_rx_buffers(struct ixgb_adapter *adapter)
{
	struct ixgb_desc_ring *rx_ring = &adapter->rx_ring;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct ixgb_rx_desc *rx_desc;
	struct sk_buff *skb;
	int reserve_len;
	int i;
	int num_group_tail_writes;
	long cleancount;

	reserve_len = 2;

	i = rx_ring->next_to_use;
	cleancount = IXGB_DESC_UNUSED(rx_ring);

	/* lessen this to 4 if we're
	 * in the midst of raidc and rbd is occuring
	 * because we don't want to delay returning buffers when low
	 */
	num_group_tail_writes = adapter->raidc ? 4 : IXGB_RX_BUFFER_WRITE;

	/* leave one descriptor unused */
	while (--cleancount > 0) {
		rx_desc = IXGB_RX_DESC(*rx_ring, i);

		/* allocate a new one */
		skb = dev_alloc_skb(adapter->rx_buffer_len + reserve_len);

		if (unlikely(!skb)) {
			/* better luck next time around */
			IXGB_DBG("Could not allocate SKB\n");
			break;
		}
		/* Make buffer alignment 2 beyond a 16 byte boundary
		 * this will result in a 16 byte aligned IP header after
		 * the 14 byte MAC header is removed
		 */
		skb_reserve(skb, reserve_len);

		skb->dev = netdev;

		rx_ring->buffer_info[i].skb = skb;
		rx_ring->buffer_info[i].length = adapter->rx_buffer_len;
		rx_ring->buffer_info[i].dma =
		    pci_map_single(pdev, skb->data, adapter->rx_buffer_len,
				   PCI_DMA_FROMDEVICE);

		rx_desc->buff_addr = cpu_to_le64(rx_ring->buffer_info[i].dma);

		if (!(i % num_group_tail_writes)) {
			/* Force memory writes to complete before letting h/w
			 * know there are new descriptors to fetch.  (Only
			 * applicable for weak-ordered memory model archs,
			 * such as IA-64). */
			wmb();
			/* move tail */
			IXGB_WRITE_REG(&adapter->hw, RDT, i);
		}
		i = (i + 1) % rx_ring->count;
	}

	rx_ring->next_to_use = i;
}

/**
 * ixgb_ioctl - perform a command - e.g: ethtool:get_driver_info.
 * @param netdev network interface device structure
 * @param ifr data to be used/filled in by the ioctl command
 * @param cmd ioctl command to execute
 **/

int
ixgb_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{

	switch (cmd) {
	case SIOCETHTOOL:
		return ixgb_ethtool_ioctl(netdev, ifr);

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

/**
 * ixgb_vlan_rx_register - enables or disables vlan tagging/stripping.
 * 
 * @param netdev network interface device structure
 * @param grp indicates to enable or disable tagging/stripping
 **/

static void
ixgb_vlan_rx_register(struct net_device *netdev, struct vlan_group *grp)
{
	struct ixgb_adapter *adapter = netdev->priv;
	u32 ctrl, rctl;

	ixgb_irq_disable(adapter);
	adapter->vlgrp = grp;

	if (grp) {
		/* enable VLAN tag insert/strip */
		ctrl = IXGB_READ_REG(&adapter->hw, CTRL0);
		ctrl |= IXGB_CTRL0_VME;
		IXGB_WRITE_REG(&adapter->hw, CTRL0, ctrl);

		/* enable VLAN receive filtering */
		rctl = IXGB_READ_REG(&adapter->hw, RCTL);
		rctl |= IXGB_RCTL_VFE;
		rctl &= ~IXGB_RCTL_CFIEN;
		IXGB_WRITE_REG(&adapter->hw, RCTL, rctl);
	} else {
		/* disable VLAN tag insert/strip */
		ctrl = IXGB_READ_REG(&adapter->hw, CTRL0);
		ctrl &= ~IXGB_CTRL0_VME;
		IXGB_WRITE_REG(&adapter->hw, CTRL0, ctrl);

		/* disable VLAN filtering */
		rctl = IXGB_READ_REG(&adapter->hw, RCTL);
		rctl &= ~IXGB_RCTL_VFE;
		IXGB_WRITE_REG(&adapter->hw, RCTL, rctl);
	}

	ixgb_irq_enable(adapter);
}

/**
 * ixgb_vlan_rx_add_vid - adds a vlan id to be tagged/stripped in packet data.
 * @param netdev network interface device structure
 * @param vid the vlan to be added
 **/

static void
ixgb_vlan_rx_add_vid(struct net_device *netdev, u16 vid)
{
	struct ixgb_adapter *adapter = netdev->priv;
	u32 vfta, index;

	/* add VID to filter table */

	index = (vid >> 5) & 0x7F;
	vfta = IXGB_READ_REG_ARRAY(&adapter->hw, VFTA, index);
	vfta |= (1 << (vid & 0x1F));
	ixgb_write_vfta(&adapter->hw, index, vfta);
}

/**
 * ixgb_vlan_rx_kill_vid - removes a vlan id from tag/strip tables.
 * @param netdev network interface device structure
 * @param vid the vlan to be deleted
 **/

static void
ixgb_vlan_rx_kill_vid(struct net_device *netdev, u16 vid)
{
	struct ixgb_adapter *adapter = netdev->priv;
	u32 vfta, index;

	ixgb_irq_disable(adapter);

	if (adapter->vlgrp)
		adapter->vlgrp->vlan_devices[vid] = NULL;

	ixgb_irq_enable(adapter);

	/* remove VID from filter table */

	index = (vid >> 5) & 0x7F;
	vfta = IXGB_READ_REG_ARRAY(&adapter->hw, VFTA, index);
	vfta &= ~(1 << (vid & 0x1F));
	ixgb_write_vfta(&adapter->hw, index, vfta);
}

/**
 * ixgb_restore_vlan - restores vlan settings after adapter reset.
 * @param adapter the address of the board private structure
 **/
static void
ixgb_restore_vlan(struct ixgb_adapter *adapter)
{
	ixgb_vlan_rx_register(adapter->netdev, adapter->vlgrp);

	if (adapter->vlgrp) {
		u16 vid;
		for (vid = 0; vid < VLAN_GROUP_ARRAY_LEN; vid++) {
			if (!adapter->vlgrp->vlan_devices[vid])
				continue;
			ixgb_vlan_rx_add_vid(adapter->netdev, vid);
		}
	}
}

/**
 * ixgb_rx_checksum - Receive Checksum Offload for 82597.
 * @param adapter board private structure
 * @param rx_desc receive descriptor
 * @param skb socket buffer with received data
 **/

static inline void
ixgb_rx_checksum(struct ixgb_adapter *adapter,
		 struct ixgb_rx_desc *rx_desc, struct sk_buff *skb)
{
	/* Ignore Checksum bit is set OR 
	 * TCP Checksum has not been calculated 
	 */
	if ((rx_desc->status & IXGB_RX_DESC_STATUS_IXSM) ||
	    (!(rx_desc->status & IXGB_RX_DESC_STATUS_TCPCS))) {
		skb->ip_summed = CHECKSUM_NONE;
		return;
	}

	/* At this point we know the hardware did the TCP checksum 
	 * now look at the TCP checksum error bit
	 */
	if (rx_desc->errors & IXGB_RX_DESC_ERRORS_TCPE) {
		/* let the stack verify checksum errors */
		skb->ip_summed = CHECKSUM_NONE;
		adapter->hw_csum_rx_error++;
	} else {
		/* TCP checksum is good */
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		adapter->hw_csum_rx_good++;
	}
}

/**
 * ixgb_write_pci_cfg - write PCI configuration space.
 * @param hw board specific data structure
 * @param reg PCI configuration space register to write to
 * @param value Value to be written to reg
 **/

void
ixgb_write_pci_cfg(struct ixgb_hw *hw, u32 reg, u16 * value)
{
	struct ixgb_adapter *adapter = (struct ixgb_adapter *) hw->back;

	pci_write_config_word(adapter->pdev, reg, *value);
}

/**
 * ixgb_notify_reboot - handles OS notification of reboot event.
 * @param nb notifier block, unused
 * @param event Event being passed to driver to act upon
 * @param p A pointer to our net device
 **/
static int
ixgb_notify_reboot(struct notifier_block *nb, unsigned long event, void *p)
{
	struct pci_dev *pdev = NULL;

	switch (event) {
	case SYS_DOWN:
	case SYS_HALT:
	case SYS_POWER_OFF:
		while ((pdev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, pdev)) != NULL) {
			if (pci_dev_driver(pdev) == &ixgb_driver)
				ixgb_suspend(pdev, 3);
		}
	}
	return NOTIFY_DONE;
}

/**
 * ixgb_suspend - driver suspend function called from notify.
 * @param pdev pci driver structure used for passing to
 * @param state power state to enter 
 **/
static int
ixgb_suspend(struct pci_dev *pdev, u32 state)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct ixgb_adapter *adapter = netdev->priv;

	netif_device_detach(netdev);

	if (netif_running(netdev))
		ixgb_down(adapter, TRUE);

	pci_save_state(pdev, adapter->pci_state);

	state = (state > 0) ? 3 : 0;
	pci_set_power_state(pdev, state);
	msec_delay(200);

	return 0;
}

/* ixgb_main.c */
