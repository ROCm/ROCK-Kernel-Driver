/*
 * ibm_ocp_enet.c
 *
 * Ethernet driver for the built in ethernet on the IBM 4xx PowerPC
 * processors.
 * 
 * Added support for multiple PHY's and use of MII for PHY control
 * configurable and bug fixes.
 *
 * Based on  the Fast Ethernet Controller (FEC) driver for
 * Motorola MPC8xx and other contributions, see driver for contributers.
 *
 *      Armin Kuster akuster@mvista.com
 *      Sept, 2001
 *
 *      Original driver
 * 	Author: Johnnie Peters
 *	jpeters@mvista.com
 *
 * Copyright 2000 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>

#include <asm/ocp.h>
#include <asm/processor.h>	/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/crc32.h>

#include "ocp_zmii.h"
#include "ibm_ocp_enet.h"
#include "ibm_ocp_mal.h"

/* Forward declarations of some structures to support different PHYs */

static int emac_open(struct net_device *);
static int emac_start_xmit(struct sk_buff *, struct net_device *);
static struct net_device_stats *ppc405_enet_stats(struct net_device *);
static int ppc405_enet_close(struct net_device *);
static void ppc405_enet_set_multicast_list(struct net_device *);

static irqreturn_t ppc405_eth_wakeup(int, void *, struct pt_regs *);
static void ppc405_eth_txeob_dev(void *, u32);
static void ppc405_eth_rxeob_dev(void *, u32);
static void ppc405_eth_txde_dev(void *, u32);
static void ppc405_eth_rxde_dev(void *, u32);
static irqreturn_t ppc405_eth_mac(int, void *, struct pt_regs *);
static void ppc405_rx_fill(struct net_device *, int);
static int ppc405_rx_clean(struct net_device *);

int ocp_enet_mdio_read(struct net_device *dev, int reg, uint * value);
int ocp_enet_mdio_write(struct net_device *dev, int reg);
int ocp_enet_ioctl(struct net_device *, struct ifreq *rq, int cmd);

static struct net_device *emac_dev[EMAC_NUMS];

mii_list_t mii_cmds[NMII];

int emac_max;

static int skb_res = SKB_RES;
MODULE_PARM(skb_res, "i");
MODULE_PARM_DESC(skb_res, "Amount of data to reserve on skb buffs\n"
		 "The 405 handles a misaligned IP header fine but\n"
		 "this can help if you are routing to a tunnel or a\n"
		 "device that needs aligned data");


static void disable_mal_chan(struct ocp_enet_private *fep)
{
	mal_disable_tx_channels(fep->mal, fep->commac.tx_chan_mask);
	mal_disable_rx_channels(fep->mal, fep->commac.rx_chan_mask);
}

static void enable_mal_chan(struct ocp_enet_private *fep)
{
	mal_enable_tx_channels(fep->mal, fep->commac.tx_chan_mask);
	mal_enable_rx_channels(fep->mal, fep->commac.rx_chan_mask);
}

static void init_rings(struct net_device *dev)
{
	struct ocp_enet_private *ep = dev->priv;
	int loop;

	ep->tx_desc = (struct mal_descriptor *) ((char *) ep->mal->tx_virt_addr +
				      (ep->mal_tx_chan * MAL_DT_ALIGN));
	ep->rx_desc = (struct mal_descriptor *) ((char *) ep->mal->rx_virt_addr +
				      (ep->mal_rx_chan * MAL_DT_ALIGN));

	/* Fill in the transmit descriptor ring. */
	for (loop = 0; loop < NUM_TX_BUFF; loop++) {
		ep->tx_skb[loop] = (struct sk_buff *) NULL;
		ep->tx_desc[loop].ctrl = 0;
		ep->tx_desc[loop].data_len = 0;
		ep->tx_desc[loop].data_ptr = NULL;
	}
	ep->tx_desc[loop - 1].ctrl |= MAL_TX_CTRL_WRAP;

	/* Format the receive descriptor ring. */
	ep->rx_slot = 0;
	ppc405_rx_fill(dev, 0);
	if (ep->rx_slot != 0) {
		printk(KERN_ERR
		       "%s: Not enough mem for RxChain durning Open?\n",
		       dev->name);
		/*We couldn't fill the ring at startup?
		 *We could clean up and fail to open but right now we will try to
		 *carry on. It may be a sign of a bad NUM_RX_BUFF value
		 */
	}

	ep->tx_cnt = 0;
	ep->tx_slot = 0;
	ep->ack_slot = 0;
}

static int emac_open(struct net_device *dev)
{
	unsigned long mode_reg;
	struct ocp_enet_private *fep = dev->priv;
	volatile emac_t *emacp = fep->emacp;
	unsigned long emac_ier;

	if (!fep->link) {
		printk(KERN_NOTICE "%s: Cannot open interface without phy\n",
		       dev->name);
		return -ENODEV;
	}
	disable_mal_chan(fep);
	mal_set_rcbs(fep->mal, fep->mal_rx_chan, DESC_BUF_SIZE_REG);

	/* set the high address */
	out_be32(&emacp->em0iahr, (dev->dev_addr[0] << 8) | dev->dev_addr[1]);

	/* set the low address */
	out_be32(&emacp->em0ialr,
		 (dev->dev_addr[2] << 24) | (dev->dev_addr[3] << 16)
		 | (dev->dev_addr[4] << 8) | dev->dev_addr[5]);

	mii_do_cmd(dev, fep->phy->ack_int);
	mii_do_cmd(dev, fep->phy->config);
	process_mii_queue(dev);

	mii_display_status(dev);

	/* set receive fifo to 4k and tx fifo to 2k */
	mode_reg = EMAC_M1_RFS_4K | EMAC_M1_TX_FIFO_2K | EMAC_M1_APP;

	/* set speed (default is 10Mb) */
	if (fep->phy_speed == _100BASET) {
		mode_reg = mode_reg | EMAC_M1_MF_100MBPS;
		zmii_set_port_speed(100, dev);
	} else {
		mode_reg = mode_reg & ~EMAC_M1_MF_100MBPS;	/* 10 MBPS */
		zmii_set_port_speed(10, dev);
	}

	if (fep->phy_duplex == FULL)
		mode_reg = mode_reg | EMAC_M1_FDE | EMAC_M1_EIFC | EMAC_M1_IST;
	else
		mode_reg = mode_reg & ~(EMAC_M1_FDE | EMAC_M1_EIFC | EMAC_M1_ILE);	/* half duplex */

#ifdef CONFIG_IBM_EMAC4
	/* enable broadcast and individual address */
	out_be32(&emacp->em0rmr, EMAC_RMR_IAE | EMAC_RMR_BAE | 0x00000007);
	/* set transmit request threshold register */
	out_be32(&emacp->em0trtr, EMAC_TRTR_256);
	/* mode register settings */
	mode_reg |= EMAC_M1_OPB_CLK_66 | 0x00009000;
#else
	out_be32(&emacp->em0rmr, EMAC_RMR_IAE | EMAC_RMR_BAE);
	out_be32(&emacp->em0trtr, EMAC_TRTR_1600);
	mode_reg |= EMAC_M1_TR0_MULTI;
#endif

	out_be32(&emacp->em0mr1, mode_reg);

#if defined(CONFIG_440GP)
	/* set receive low/high water mark register */
	out_be32(&emacp->em0rwmr, 0x80009000);
	out_be32(&emacp->em0tmr1, 0xf8640000);
#elif defined(CONFIG_440GX)
	out_be32(&emacp->em0rwmr, 0x1000a200);
	out_be32(&emacp->em0tmr0, 0x00000007);
	out_be32(&emacp->em0tmr1, 0x88810000);
#else
	out_be32(&emacp->em0rwmr, 0x0f002000);
#endif /* CONFIG_440GP */

	/* set frame gap */
	out_be32(&emacp->em0ipgvr, CONFIG_IBM_OCP_ENET_GAP);

	emac_ier = EMAC_ISR_PP | EMAC_ISR_BP | EMAC_ISR_RP |
	    EMAC_ISR_SE | EMAC_ISR_PTLE | EMAC_ISR_ALE |
	    EMAC_ISR_BFCS | EMAC_ISR_ORE | EMAC_ISR_IRE;

	out_be32(&emacp->em0iser, emac_ier);

	/* FIXME: check failures */
	request_irq(dev->irq, ppc405_eth_mac, 0, dev->name, dev);
	request_irq(fep->wol_irq, ppc405_eth_wakeup, SA_SHIRQ, "EMAC WOL", dev);

	/* init buffer descriptors rings */
	init_rings(dev);

	/* enable all MAL transmit and receive channels */
	enable_mal_chan(fep);

	/* set transmit and receive enable */
	out_be32(&emacp->em0mr0, EMAC_M0_TXE | EMAC_M0_RXE);
	netif_start_queue(dev);

	return 0;
}

static int
emac_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned short ctrl;
	unsigned long flags;
	struct ocp_enet_private *fep = dev->priv;
	volatile emac_t *emacp = fep->emacp;

	save_flags(flags);
	cli();
	if (netif_queue_stopped(dev) || (fep->tx_cnt == NUM_TX_BUFF)) {

		fep->stats.tx_dropped++;
		restore_flags(flags);
		return -EBUSY;
	}

	if (++fep->tx_cnt == NUM_TX_BUFF)
		netif_stop_queue(dev);

	/* Store the skb buffer for later ack by the transmit end of buffer
	 * interrupt.
	 */
	fep->tx_skb[fep->tx_slot] = skb;
	consistent_sync((void *) skb->data, skb->len, PCI_DMA_TODEVICE);

	ctrl = EMAC_TX_CTRL_DFLT;
	if ((NUM_TX_BUFF - 1) == fep->tx_slot)
		ctrl |= MAL_TX_CTRL_WRAP;

	fep->tx_desc[fep->tx_slot].data_ptr = (char *) virt_to_phys(skb->data);
	fep->tx_desc[fep->tx_slot].data_len = (short) skb->len;
	fep->tx_desc[fep->tx_slot].ctrl = ctrl;

	/* Send the packet out. */
#ifdef CONFIG_IBM_EMAC4
	out_be32(&emacp->em0tmr0, EMAC_TXM0_GNP0 | 0x00000007);
#else
	out_be32(&emacp->em0tmr0, EMAC_TXM0_GNP0);
#endif

	if (++fep->tx_slot == NUM_TX_BUFF)
		fep->tx_slot = 0;

	fep->stats.tx_packets++;
	fep->stats.tx_bytes += skb->len;

	restore_flags(flags);

	return 0;
}

static int
ppc405_enet_close(struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	volatile emac_t *emacp = fep->emacp;

	disable_mal_chan(fep);

	out_be32(&emacp->em0mr0, EMAC_M0_SRST);
	udelay(10);

	if (emacp->em0mr0 & EMAC_M0_SRST) {
		/*not sure what to do here hopefully it clears before another open */
		printk(KERN_ERR "%s: Phy SoftReset didn't clear, no link?\n",
		       dev->name);
	}

	/* Free the irq's */
	free_irq(dev->irq, dev);
	free_irq(fep->wol_irq, dev);

	free_phy(dev);
	return 0;
}

static void
ppc405_enet_set_multicast_list(struct net_device *dev)
{
	struct ocp_enet_private *ep = dev->priv;
	volatile emac_t *emacp = ep->emacp;

	if (dev->flags & IFF_PROMISC) {

		/* If promiscuous mode is set then we do not need anything else */
		out_be32(&emacp->em0rmr, EMAC_RMR_PME);

	} else if (dev->flags & IFF_ALLMULTI || 32 < dev->mc_count) {

		/* Must be setting up to use multicast.  Now check for promiscuous
		 * multicast
		 */
		out_be32(&emacp->em0rmr,
			 EMAC_RMR_IAE | EMAC_RMR_BAE | EMAC_RMR_PMME);
	} else if (dev->flags & IFF_MULTICAST && 0 < dev->mc_count) {

		unsigned short em0gaht[4] = { 0, 0, 0, 0 };
		struct dev_mc_list *dmi;

		/* Need to hash on the multicast address. */
		for (dmi = dev->mc_list; dmi; dmi = dmi->next) {
			unsigned long mc_crc;
			unsigned int bit_number;

			mc_crc = ether_crc(6, (char *) dmi->dmi_addr);
			bit_number = 63 - (mc_crc >> 26);	/* MSB: 0 LSB: 63 */
			em0gaht[bit_number >> 4] |=
			    0x8000 >> (bit_number & 0x0f);
		}
		emacp->em0gaht1 = em0gaht[0];
		emacp->em0gaht2 = em0gaht[1];
		emacp->em0gaht3 = em0gaht[2];
		emacp->em0gaht4 = em0gaht[3];

		/* Turn on multicast addressing */
		out_be32(&emacp->em0rmr,
			 EMAC_RMR_IAE | EMAC_RMR_BAE | EMAC_RMR_MAE);

	} else {
		/* If multicast mode is not set then we are 
		 * turning it off at this point 
		 */
		out_be32(&emacp->em0rmr, EMAC_RMR_IAE | EMAC_RMR_BAE);

	}

	return;
}

static struct net_device_stats *
ppc405_enet_stats(struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	return &fep->stats;
}

static void  __devexit ocp_emac_remove_one (struct ocp_device *pdev)
{
	struct net_device *dev = ocp_get_drvdata(pdev);
	printk("removing net dev \n");
	ocp_force_power_off(pdev);
	unregister_netdev(dev);
}

struct mal_commac_ops emac_commac_ops = {
	.txeob = &ppc405_eth_txeob_dev,
	.txde = &ppc405_eth_txde_dev,
	.rxeob = &ppc405_eth_rxeob_dev,
	.rxde = &ppc405_eth_rxde_dev,
};

static int __devinit ocp_emac_probe(struct ocp_device *pdev)
{
	int err = 0;
	int i;
	struct net_device *dev;
	struct ocp_enet_private *ep;
	int emac_num = pdev->num;

	dev = init_etherdev(NULL, sizeof (struct ocp_enet_private));
	if (dev == NULL) {
		printk(KERN_ERR
		       "ibm_ocp_enet: Could not allocate ethernet device.\n");
		return -1;
	}

	ep = dev->priv;
	ep->ocpdev = pdev;
	ocp_set_drvdata(pdev, dev);

	ep->mal = &mal_table[0];
	/* FIXME: need a better way of determining these */
#ifdef CONFIG_440GX
	ep->mal_tx_chan = emac_num;
#else
	ep->mal_tx_chan = emac_num * 2;
#endif
	ep->mal_rx_chan = emac_num;

	ep->emacp = __ioremap(pdev->paddr, sizeof (emac_t), _PAGE_NO_CACHE);

	zmii_init(ZMII_AUTO, dev);
	find_phy(dev);
	if (!ep->phy)
		return -1;

	ep->link = 1;
	dev->irq = pdev->irq;
	ep->wol_irq = BL_MAC_WOL; /* FIXME: need a better way to get this */

	/* read the MAC Address */
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = __res.BD_EMAC_ADDR(emac_num, i);	/* Marco to disques array */

	ep->commac.ops = &emac_commac_ops;
	ep->commac.dev = dev;
	ep->commac.tx_chan_mask = MAL_CHAN_MASK(ep->mal_tx_chan);
	ep->commac.rx_chan_mask = MAL_CHAN_MASK(ep->mal_rx_chan);
	err = mal_register_commac(ep->mal, &ep->commac);
	if (err)
		return err; /* FIXME: cleanup needed? */
	

	/* Fill in the driver function table */
	dev->open = &emac_open;
	dev->hard_start_xmit = &emac_start_xmit;
	dev->stop = &ppc405_enet_close;
	dev->get_stats = &ppc405_enet_stats;
	dev->set_multicast_list = &ppc405_enet_set_multicast_list;
	dev->do_ioctl = &ocp_enet_ioctl;
	emac_dev[emac_num] = dev;

	/* Reset the EMAC */
	out_be32(&ep->emacp->em0mr0, EMAC_M0_SRST);
	udelay(10);

	if (in_be32(&ep->emacp->em0mr0) & EMAC_M0_SRST) {
		printk(KERN_NOTICE "%s: Cannot open interface without Link\n",
		       dev->name);
		return -1;
	}

	printk("IBM EMAC: %s: ", dev->name);
	printk("MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
			dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
			dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);

	return 0;

}

static struct ocp_device_id ocp_emac_id_tbl[] __devinitdata = {
	{OCP_VENDOR_IBM,OCP_FUNC_EMAC},
	{0,}
};

MODULE_DEVICE_TABLE(ocp,ocp_emac_id_tbl );

static struct ocp_driver ocp_emac_driver = {
	.name		= "ocp_emac",
	.id_table	= ocp_emac_id_tbl,
	.probe		= ocp_emac_probe,
	.remove		= __devexit_p(ocp_emac_remove_one),
#if defined(CONFIG_PM)
	.suspend	= ocp_emac_suspend,
	.resume		= ocp_emac_resume,
#endif /* CONFIG_PM */
};

static int __init
init_ppc405_enet(void)
{
	int i;

	for (i = 0; i < NMII - 1; i++)
		mii_cmds[i].mii_next = &mii_cmds[i + 1];
	mii_free = mii_cmds;

	return ocp_module_init(&ocp_emac_driver);
}

/*
 * int ocp_enet_mdio_read()
 *
 * Description:
 *   This routine reads from a specified register on a PHY over the MII
 *   Management Interface.
 *
 * Input(s):
 *   phy    - The address of the PHY to read from. May be 0 through 31.
 *   reg    - The PHY register to read. May be 0 through 31.
 *   *value - Storage for the value to read from the PHY register.
 *
 * Output(s):
 *   *value - The value read from the PHY register.
 *
 * Returns:
 *   0 if OK, otherwise -1 on error.
 *
 */
int
ocp_enet_mdio_read(struct net_device *dev, int reg, uint * value)
{
	register int i;
	uint32_t stacr;
	struct ocp_enet_private *fep = dev->priv;
	volatile emac_t *emacp = fep->emacp;

	/* Wait for data transfer complete bit */
	zmii_enable_port(dev);
	for (i = 0; i < OCP_RESET_DELAY; ++i) {
		if (emacp->em0stacr & EMAC_STACR_OC)
			break;
		udelay(MDIO_DELAY);	/* changed to 2 with new scheme -armin */
	}
	if ((emacp->em0stacr & EMAC_STACR_OC) == 0) {
		printk("OCP Reset timeout #1!\n");
		return -1;
	}

	/* Clear the speed bits and make a read request to the PHY */

	stacr = reg | ((fep->phy_addr & 0x1F) << 5);

	out_be32(&emacp->em0stacr, stacr);
	stacr = in_be32(&emacp->em0stacr);
	/* Wait for data transfer complete bit */
	for (i = 0; i < OCP_RESET_DELAY; ++i) {
		if ((stacr = in_be32(&emacp->em0stacr)) & EMAC_STACR_OC)
			break;
		udelay(MDIO_DELAY);
	}
	if ((stacr & EMAC_STACR_OC) == 0) {
		printk("OCP Reset timeout #2!\n");
		return -1;
	}

	/* Check for a read error */
	if (stacr & EMAC_STACR_PHYE) {
		return -1;
	}
	*value = (stacr >> 16);
	return 0;
}

/*
 * int ocp_enet_mdio_write()
 *
 * Description:
 *   This routine reads from a specified register on a PHY over the MII
 *   Management Interface.
 *
 * Input(s):
 *   phy    - The address of the PHY to read from. May be 0 through 31.
 *   reg    - The PHY register to read. May be 0 through 31.
 *
 * Output(s):
 *   value - The value writing to the PHY register.
 *
 * Returns:
 *   0 if OK, otherwise -1 on error.
 *
 */
int
ocp_enet_mdio_write(struct net_device *dev, int reg)
{
	register int i = 0;
	uint32_t stacr;
	struct ocp_enet_private *fep = dev->priv;
	volatile emac_t *emacp = fep->emacp;

	zmii_enable_port(dev);
	/* Wait for data transfer complete bit */
	for (i = 0; i < OCP_RESET_DELAY; ++i) {
		if (emacp->em0stacr & EMAC_STACR_OC)
			break;
		udelay(MDIO_DELAY);	/* changed to 2 with new scheme -armin */
	}
	if ((emacp->em0stacr & EMAC_STACR_OC) == 0) {
		printk("OCP Reset timeout!\n");
		return -1;
	}

	/* Clear the speed bits and make a read request to the PHY */

	stacr = reg | ((fep->phy_addr & 0x1F) << 5);
	out_be32(&emacp->em0stacr, stacr);

	/* Wait for data transfer complete bit */
	for (i = 0; i < OCP_RESET_DELAY; ++i) {
		if ((stacr = emacp->em0stacr) & EMAC_STACR_OC)
			break;
		udelay(MDIO_DELAY);
	}
	if ((emacp->em0stacr & EMAC_STACR_OC) == 0) {
		printk("OCP Reset timeout!\n");
		return -1;
	}

	/* Check for a read error */
	if ((stacr & EMAC_STACR_PHYE) != 0) {
		return -1;
	}

	return 0;
}

/*
 * int ocp_enet_ioctl()
 *
 * Description:
 *   This routine performs the specified I/O control command on the
 *   specified net device.
 *
 * Input(s):
 *   *dev - Pointer to the device structure for this driver.
 *   *rq  - Pointer to data to be written and/or storage for return data.
 *    cmd - I/O control command to perform.
 *
 *
 * Output(s):
 *   *rq  - If OK, pointer to return data for a read command.
 *
 * Returns:
 *   0 if OK, otherwise an error number on error.
 *
 */
int
ocp_enet_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct ocp_enet_private *fep = dev->priv;
	uint *data = (uint *) & rq->ifr_data;

	switch (cmd) {

	case SIOCDEVPRIVATE:
		data[0] = fep->phy_addr;
	 /*FALLTHRU*/ case SIOCDEVPRIVATE + 1:
		if (ocp_enet_mdio_read(dev, mk_mii_read(data[1]), &data[3]) < 0)
			return -EIO;

		return 0;

	case SIOCDEVPRIVATE + 2:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (ocp_enet_mdio_write(dev, mk_mii_write(data[1], data[2])) <
		    0)
			return -EIO;

		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

static irqreturn_t
ppc405_eth_wakeup(int irq, void *dev_instance, struct pt_regs *regs)
{
	/* On Linux the 405 ethernet will always be active if configured
	 * in.  This interrupt should never occur.
	 */
	printk(KERN_INFO "IBM EMAC: interrupt ppc405_eth_wakeup\n");
	return IRQ_HANDLED;
}


static void ppc405_eth_txeob_dev(void *p, u32 chanmask)
{
	struct net_device *dev = p;
	struct ocp_enet_private *fep = dev->priv;

	while (fep->tx_cnt &&
		!(fep->tx_desc[fep->ack_slot].ctrl & MAL_TX_CTRL_READY)) {

		/* Tell the system the transmit completed. */
		dev_kfree_skb_irq(fep->tx_skb[fep->ack_slot]);

		if (fep->tx_desc[fep->ack_slot].ctrl &
		    (EMAC_TX_ST_EC | EMAC_TX_ST_MC | EMAC_TX_ST_SC))
			fep->stats.collisions++;

		fep->tx_skb[fep->ack_slot] = (struct sk_buff *) NULL;
		if (++fep->ack_slot == NUM_TX_BUFF)
			fep->ack_slot = 0;

		fep->tx_cnt--;

		netif_wake_queue(dev);
	}

	return;
}

/*
  Fill/Re-fill the rx chain with valid ctrl/ptrs.
  This function will fill from rx_slot up to the parm end.
  So to completely fill the chain pre-set rx_slot to 0 and
  pass in an end of 0.
 */
static void
ppc405_rx_fill(struct net_device *dev, int end)
{
	int i;
	struct ocp_enet_private *fep = dev->priv;
	unsigned char *ptr;

	i = fep->rx_slot;
	do {
		if (fep->rx_skb[i] != NULL) {
			/*We will trust the skb is still in a good state */
			ptr = (char *) virt_to_phys(fep->rx_skb[i]->data);
		} else {

			fep->rx_skb[i] =
			    dev_alloc_skb(DESC_RX_BUF_SIZE + skb_res);

			if (fep->rx_skb[i] == NULL) {
				/* Keep rx_slot here, the next time clean/fill is called
				 * we will try again before the MAL wraps back here
				 * If the MAL tries to use this descriptor with
				 * the EMPTY bit off it will cause the
				 * rxde interrupt.  That is where we will
				 * try again to allocate an sk_buff.
				 */
				break;

			}

			if (skb_res)
				skb_reserve(fep->rx_skb[i], skb_res);

			consistent_sync((void *) fep->rx_skb[i]->
					data, DESC_RX_BUF_SIZE,
					PCI_DMA_BIDIRECTIONAL);
			ptr = (char *) virt_to_phys(fep->rx_skb[i]->data);
		}
		fep->rx_desc[i].ctrl = MAL_RX_CTRL_EMPTY | MAL_RX_CTRL_INTR |	/*could be smarter about this to avoid ints at high loads */
		    (i == (NUM_RX_BUFF - 1) ? MAL_RX_CTRL_WRAP : 0);

		fep->rx_desc[i].data_ptr = ptr;
		/*
		   * 440GP uses the previously reserved bits in the
		   * data_len to encode the upper 4-bits of the buffer
		   * physical address (ERPN). Initialize these.
		 */
		fep->rx_desc[i].data_len = 0;
	} while ((i = (i + 1) % NUM_RX_BUFF) != end);

	fep->rx_slot = i;
}

static int
ppc405_rx_clean(struct net_device *dev)
{
	int i;
	int error, frame_length;
	struct ocp_enet_private *fep = dev->priv;
	unsigned short ctrl;

	i = fep->rx_slot;

	do {
		if (fep->rx_skb[i] == NULL)
			continue;	/*we have already handled the packet but haved failed to alloc */
		/* 
		   since rx_desc is in uncached mem we don't keep reading it directly 
		   we pull out a local copy of ctrl and do the checks on the copy.
		 */
		ctrl = fep->rx_desc[i].ctrl;
		if (ctrl & MAL_RX_CTRL_EMPTY)
			break;	/*we don't have any more ready packets */

		if (ctrl & EMAC_BAD_RX_PACKET) {

			fep->stats.rx_errors++;
			fep->stats.rx_dropped++;

			if (ctrl & EMAC_RX_ST_OE)
				fep->stats.rx_fifo_errors++;
			if (ctrl & EMAC_RX_ST_AE)
				fep->stats.rx_frame_errors++;
			if (ctrl & EMAC_RX_ST_BFCS)
				fep->stats.rx_crc_errors++;
			if (ctrl & (EMAC_RX_ST_RP | EMAC_RX_ST_PTL |
				    EMAC_RX_ST_ORE | EMAC_RX_ST_IRE))
				fep->stats.rx_length_errors++;
		} else {

			/* Send the skb up the chain. */
			frame_length = fep->rx_desc[i].data_len - 4;

			skb_put(fep->rx_skb[i], frame_length);
			fep->rx_skb[i]->dev = dev;
			fep->rx_skb[i]->protocol =
			    eth_type_trans(fep->rx_skb[i], dev);

			error = netif_rx(fep->rx_skb[i]);
			if ((error == NET_RX_DROP) || (error == NET_RX_BAD)) {
				fep->stats.rx_dropped++;
			} else {
				fep->stats.rx_packets++;
				fep->stats.rx_bytes += frame_length;
			}
			fep->rx_skb[i] = NULL;
		}
	} while ((i = (i + 1) % NUM_RX_BUFF) != fep->rx_slot);
	return i;
}

static void ppc405_eth_rxeob_dev(void *p, u32 chanmask)
{
	struct net_device *dev = p;
	int n;
	
	n = ppc405_rx_clean(dev);
	ppc405_rx_fill(dev, n);
}

/*
 * This interrupt should never occurr, we don't program
 * the MAL for contiunous mode.
 */
static void ppc405_eth_txde_dev(void *p, u32 chanmask)
{
	struct net_device *dev = p;
	struct ocp_enet_private *fep = dev->priv;

	printk(KERN_WARNING "%s: Tx descriptor error\n", dev->name);

	ppc405_eth_emac_dump(dev);
	ppc405_eth_mal_dump(dev);

	/* Reenable the transmit channel */
	mal_enable_tx_channels(fep->mal, fep->commac.tx_chan_mask);
	return;
}

/*
 * This interrupt should be very rare at best.  This occurs when
 * the hardware has a problem with the receive descriptors.  The manual
 * states that it occurs when the hardware cannot the receive descriptor
 * empty bit is not set.  The recovery mechanism will be to
 * traverse through the descriptors, handle any that are marked to be
 * handled and reinitialize each along the way.  At that point the driver
 * will be restarted.
 */
static void ppc405_eth_rxde_dev(void *p, u32 chanmask)
{
	struct net_device *dev = p;
	struct ocp_enet_private *fep = dev->priv;

	printk(KERN_WARNING "%s: Rx descriptor error\n", dev->name);

	ppc405_eth_emac_dump(dev);
	ppc405_eth_mal_dump(dev);
	ppc405_eth_desc_dump(dev);


	fep->stats.rx_errors++;

	/* so do we have any good packets still? */
	ppc405_rx_clean(dev);
	
	/* When the interface is restarted it resets processing to the
	 * first descriptor in the table.  */
	fep->rx_slot = 0;
	ppc405_rx_fill(dev, 0);
	
	set_mal_dcrn(fep->mal, DCRN_MALRXEOBISR, MAL_CHAN_MASK(fep->mal_rx_chan));

	/* Clear the interrupt */
	set_mal_dcrn(fep->mal, DCRN_MALRXDEIR, MAL_CHAN_MASK(fep->mal_rx_chan));

	/* Reenable the receive channel */
	mal_enable_rx_channels(fep->mal, fep->commac.rx_chan_mask);
}

static irqreturn_t
ppc405_eth_mac(int irq, void *dev_instance, struct pt_regs *regs)
{
	unsigned long tmp_em0isr;
	struct net_device *dev = dev_instance;
	struct ocp_enet_private *fep = dev->priv;
	volatile emac_t *emacp = fep->emacp;

	/* EMAC interrupt */
	tmp_em0isr = in_be32(&emacp->em0isr);
	if (tmp_em0isr & (EMAC_ISR_TE0 | EMAC_ISR_TE1)) {
		/* This error is a hard transmit error - could retransmit */
		fep->stats.tx_errors++;

		/* Reenable the transmit channel */
		mal_enable_tx_channels(fep->mal, fep->commac.tx_chan_mask);
	} else {
		fep->stats.rx_errors++;
	}

/*	if (tmp_em0isr & EMAC_ISR_OVR ) fep->stats.ZZZ++;		*/
/*	if (tmp_em0isr & EMAC_ISR_PP ) fep->stats.ZZZ++;		*/
/*	if (tmp_em0isr & EMAC_ISR_BP ) fep->stats.ZZZ++;		*/
	if (tmp_em0isr & EMAC_ISR_RP)
		fep->stats.rx_length_errors++;
/*	if (tmp_em0isr & EMAC_ISR_SE ) fep->stats.ZZZ++;		*/
	if (tmp_em0isr & EMAC_ISR_ALE)
		fep->stats.rx_frame_errors++;
	if (tmp_em0isr & EMAC_ISR_BFCS)
		fep->stats.rx_crc_errors++;
	if (tmp_em0isr & EMAC_ISR_PTLE)
		fep->stats.rx_length_errors++;
	if (tmp_em0isr & EMAC_ISR_ORE)
		fep->stats.rx_length_errors++;
/*	if (tmp_em0isr & EMAC_ISR_IRE ) fep->stats.ZZZ++;		*/
/*	if (tmp_em0isr & EMAC_ISR_DBDM) fep->stats.ZZZ++;		*/
/*	if (tmp_em0isr & EMAC_ISR_DB0 ) fep->stats.ZZZ++;		*/
/*	if (tmp_em0isr & EMAC_ISR_SE0 ) fep->stats.ZZZ++;		*/
	if (tmp_em0isr & EMAC_ISR_TE0)
		fep->stats.tx_aborted_errors++;
/*	if (tmp_em0isr & EMAC_ISR_DB1 ) fep->stats.ZZZ++;		*/
/*	if (tmp_em0isr & EMAC_ISR_SE1 ) fep->stats.ZZZ++;		*/
/*	if (tmp_em0isr & EMAC_ISR_TE1) fep->stats.ZZZ++;		*/
/*	if (tmp_em0isr & EMAC_ISR_MOS ) fep->stats.ZZZ++;		*/
/*	if (tmp_em0isr & EMAC_ISR_MOF ) fep->stats.ZZZ++;		*/

	ppc405_bl_mac_eth_dump(dev, tmp_em0isr);

	out_be32(&emacp->em0isr, tmp_em0isr);
	
	return IRQ_HANDLED;
}

static void __exit
exit_ppc405_enet(void)
{
}

module_init(init_ppc405_enet);
module_exit(exit_ppc405_enet);
