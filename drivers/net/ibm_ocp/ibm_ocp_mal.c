/*
 * ibm_ocp_mal.c
 *
 *      Armin Kuster akuster@mvista.com
 *      Juen, 2002
 *
 * Copyright 2002 MontaVista Softare Inc.
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ocp.h>

#include "ibm_ocp_mal.h"

struct ibm_ocp_mal mal_table[NUM_MALS]; /* = 0 */

static void __exit exit_mal(struct ibm_ocp_mal *mal);

int mal_register_commac(struct ibm_ocp_mal *mal, struct mal_commac *commac)
{
	/* FIXME: locking? */

	/* Don't let multiple commacs claim the same channel */
	if ( (mal->tx_chan_mask & commac->tx_chan_mask) ||
	     (mal->rx_chan_mask & commac->rx_chan_mask) )
		return -EBUSY;

	mal->tx_chan_mask |= commac->tx_chan_mask;
	mal->rx_chan_mask |= commac->rx_chan_mask;

	list_add(&commac->list, &mal->commac);

	return 0;
}

int mal_set_rcbs(struct ibm_ocp_mal *mal, int channel, unsigned long size)
{
	switch (channel) {
	case 0:
		set_mal_dcrn(mal, DCRN_MALRCBS0, size);
		break;
#ifdef DCRN_MALRCBS1
	case 1:
		set_mal_dcrn(mal, DCRN_MALRCBS1, size);
		break;
#endif
#ifdef DCRN_MALRCBS2
	case 2:
		set_mal_dcrn(mal, DCRN_MALRCBS2, size);
		break;
#endif
#ifdef DCRN_MALRCBS3
	case 3:
		set_mal_dcrn(mal, DCRN_MALRCBS3, size);
		break;
#endif
	default:
		return -EINVAL;
	}

	return 0;
}

static irqreturn_t mal_serr(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ibm_ocp_mal *mal = dev_instance;
	unsigned long mal_error;

	/*
	 * This SERR applies to one of the devices on the MAL, here we charge
	 * it against the first EMAC registered for the MAL.
	 */

	mal_error = get_mal_dcrn(mal, DCRN_MALESR);

	printk(KERN_ERR "%s: System Error (MALESR=%lx)\n", 
	       "MAL" /* FIXME: get the name right */, mal_error);

	/* FIXME: decipher error */
	/* DIXME: distribute to commacs, if possible */

	/* Clear the error status register */
	set_mal_dcrn(mal, DCRN_MALESR, mal_error);
	
	return IRQ_HANDLED;
}

static irqreturn_t mal_txeob(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ibm_ocp_mal *mal = dev_instance;
	struct list_head *l;
	unsigned long isr;

	/* Loop to better handle lots of interrupts */

	isr = get_mal_dcrn(mal, DCRN_MALTXEOBISR);
	set_mal_dcrn(mal, DCRN_MALTXEOBISR, isr);

	list_for_each(l, &mal->commac) {
		struct mal_commac *mc = list_entry(l, struct mal_commac, list);

		if (isr & mc->tx_chan_mask) {
			mc->ops->txeob(mc->dev, isr & mc->tx_chan_mask);
		}
	}
	/* What if we don't get a hit in the list? */
	
	return IRQ_HANDLED;
}

static irqreturn_t mal_rxeob(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ibm_ocp_mal *mal = dev_instance;
	struct list_head *l;
	unsigned long isr;

	/*
	 * Protect against mal_rxde() modifying data structures this
	 * function is using. FIXME: synchronisation should move within
	 * the client drivers */
	disable_irq(mal->rxde_irq);


	/* FIXME: Loop to better handle lots of interrupts? */

	isr = get_mal_dcrn(mal, DCRN_MALRXEOBISR);
	set_mal_dcrn(mal, DCRN_MALRXEOBISR, isr);

	list_for_each(l, &mal->commac) {
		struct mal_commac *mc = list_entry(l, struct mal_commac, list);

		if (isr & mc->rx_chan_mask) {
			mc->ops->rxeob(mc->dev, isr & mc->rx_chan_mask);
		}
	}
	/* FIXME: What if we don't get a hit in the list? */

	enable_irq(mal->rxde_irq);
	
	return IRQ_HANDLED;
}

static irqreturn_t mal_txde(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ibm_ocp_mal *mal = dev_instance;
	struct list_head *l;
	unsigned long deir;

	deir = get_mal_dcrn(mal, DCRN_MALTXDEIR);

	/* FIXME: print which MAL correctly */
	printk(KERN_WARNING "%s: Tx descriptor error (MALTXDEIR=%lx)\n",
	       "MAL", deir);

	list_for_each(l, &mal->commac) {
		struct mal_commac *mc = list_entry(l, struct mal_commac, list);

		if (deir & mc->tx_chan_mask) {
			mc->ops->txde(mc->dev, deir & mc->tx_chan_mask);
		}
	}
	/* What if there is no match in the list? */
	
	return IRQ_HANDLED;
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
static irqreturn_t mal_rxde(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ibm_ocp_mal *mal = dev_instance;
	struct list_head *l;
	unsigned long deir;

	deir = get_mal_dcrn(mal, DCRN_MALRXDEIR);

	/*
	 * This really is needed.  This case encountered in stress testing.
	 */
	if (deir == 0)
		return IRQ_HANDLED;

	/* FIXME: print which MAL correctly */
	printk(KERN_WARNING "%s: Rx descriptor error (MALRXDEIR=%lx)\n",
	       "MAL", deir);


	/*
	 * Protect against ppc405_eth_rxeob modifying these same
	 * structures.  If an rxde interrupt occurs the hardware will
	 * have disabled that EMAC, but since there may be multiple
	 * EMACs on the same MAL another rxeob interrupt could occur
	 * for another EMAC prior to ppc405_eth_rxde() re-enabling
	 * EMACs below.  FIXME: this looks bogus
	 */
	disable_irq(BL_MAL_RXEOB);

	list_for_each(l, &mal->commac) {
		struct mal_commac *mc = list_entry(l, struct mal_commac, list);

		if (deir & mc->rx_chan_mask) {
			mc->ops->rxde(mc->dev, deir & mc->rx_chan_mask);
		}
	}

	enable_irq(BL_MAL_RXEOB);
	
	return IRQ_HANDLED;
}

static int __init init_mal(int n)
{
	struct ibm_ocp_mal *mal;
	int err;

	mal = &mal_table[n];


	switch (n) {
	case 0:
		mal->dcrbase = DCRN_MAL_BASE;
		break;
#ifdef DCRN_MAL1_BASE
	case 1:
		mal->dcrbase = DCRN_MAL1_BASE;
		break;
#endif
	default:
		BUG();
	}
	mal->serr_irq = BL_MAL_SERR;
	mal->txde_irq = BL_MAL_TXDE;
	mal->txeob_irq = BL_MAL_TXEOB;
	mal->rxde_irq = BL_MAL_RXDE;
	mal->rxeob_irq = BL_MAL_RXEOB;
	
	/* Wrong in general, but the best we have for now: */
	mal->num_tx_channels = 2*EMAC_NUMS;
	mal->num_rx_channels = EMAC_NUMS;

	/**************************/

	INIT_LIST_HEAD(&mal->commac);


	set_mal_dcrn(mal, DCRN_MALRXCARR, 0xFFFFFFFF);
	set_mal_dcrn(mal, DCRN_MALTXCARR, 0xFFFFFFFF);

	set_mal_dcrn(mal, DCRN_MALCR, MALCR_MMSR);	/* 384 */
	/* FIXME: Add delay */

	/* Set the MAL configuration register */
	set_mal_dcrn(mal, DCRN_MALCR,
		     MALCR_PLBB | MALCR_OPBBL | MALCR_LEA |
		     MALCR_PLBLT_DEFAULT);

	/* It would be nice to allocate buffers separately for each
	 * channel, but we can't because the channels share the upper
	 * 13 bits of address lines.  Each channels buffer must also
	 * be 4k aligned, so we allocate 4k for each channel.  This is
	 * inefficient FIXME: do better, if possible */

	mal->tx_virt_addr = consistent_alloc(GFP_KERNEL,
					     MAL_DT_ALIGN * mal->num_tx_channels,
					     &mal->tx_phys_addr);

	/* God, oh, god, I hate DCRs */
	set_mal_dcrn(mal, DCRN_MALTXCTP0R, mal->tx_phys_addr);
#ifdef DCRN_MALTXCTP1R
	set_mal_dcrn(mal, DCRN_MALTXCTP1R, mal->tx_phys_addr + MAL_DT_ALIGN);
#endif /* DCRN_MALTXCTP1R */
#ifdef DCRN_MALTXCTP2R
	set_mal_dcrn(mal, DCRN_MALTXCTP2R, mal->tx_phys_addr + 2*MAL_DT_ALIGN);
#endif /* DCRN_MALTXCTP2R */
#ifdef DCRN_MALTXCTP3R
	set_mal_dcrn(mal, DCRN_MALTXCTP3R, mal->tx_phys_addr + 3*MAL_DT_ALIGN);
#endif /* DCRN_MALTXCTP3R */
#ifdef DCRN_MALTXCTP4R
	set_mal_dcrn(mal, DCRN_MALTXCTP4R, mal->tx_phys_addr + 4*MAL_DT_ALIGN);
#endif /* DCRN_MALTXCTP4R */
#ifdef DCRN_MALTXCTP5R
	set_mal_dcrn(mal, DCRN_MALTXCTP5R, mal->tx_phys_addr + 5*MAL_DT_ALIGN);
#endif /* DCRN_MALTXCTP5R */
#ifdef DCRN_MALTXCTP6R
	set_mal_dcrn(mal, DCRN_MALTXCTP6R, mal->tx_phys_addr + 6*MAL_DT_ALIGN);
#endif /* DCRN_MALTXCTP6R */
#ifdef DCRN_MALTXCTP7R
	set_mal_dcrn(mal, DCRN_MALTXCTP7R, mal->tx_phys_addr + 7*MAL_DT_ALIGN);
#endif /* DCRN_MALTXCTP7R */

	mal->rx_virt_addr = consistent_alloc(GFP_KERNEL,
					     MAL_DT_ALIGN * mal->num_rx_channels,
					     &mal->rx_phys_addr);

	set_mal_dcrn(mal, DCRN_MALRXCTP0R, mal->rx_phys_addr);
#ifdef DCRN_MALRXCTP1R
	set_mal_dcrn(mal, DCRN_MALRXCTP1R, mal->rx_phys_addr + MAL_DT_ALIGN);
#endif /* DCRN_MALRXCTP1R */
#ifdef DCRN_MALRXCTP2R
	set_mal_dcrn(mal, DCRN_MALRXCTP2R, mal->rx_phys_addr + 2*MAL_DT_ALIGN);
#endif /* DCRN_MALRXCTP2R */
#ifdef DCRN_MALRXCTP3R
	set_mal_dcrn(mal, DCRN_MALRXCTP3R, mal->rx_phys_addr + 3*MAL_DT_ALIGN);
#endif /* DCRN_MALRXCTP3R */
	
	err = request_irq(mal->serr_irq, mal_serr, 0 ,"MAL SERR", mal);
	if (err)
		goto fail;
	err = request_irq(mal->txde_irq, mal_txde,0, "MAL TX DE ", mal);
	if (err)
		goto fail;
	err = request_irq(mal->txeob_irq, mal_txeob, 0, "MAL TX EOB", mal);
	if (err)
		goto fail;
	err = request_irq(mal->rxde_irq, mal_rxde, 0, "MAL RX DE", mal);
	if (err)
		goto fail;
	err = request_irq(mal->rxeob_irq, mal_rxeob, 0, "MAL RX EOB", mal);
	if (err)
		goto fail;

	set_mal_dcrn(mal, DCRN_MALIER,
		     MALIER_DE | MALIER_NE | MALIER_TE |
		     MALIER_OPBE | MALIER_PLBE);

	return 0;

 fail:
	exit_mal(mal);
	return err;
}

static void __exit exit_mal(struct ibm_ocp_mal *mal)
{
	/* FIXME: shut down the MAL */
	free_irq(mal->serr_irq, mal);
	free_irq(mal->txde_irq, mal);
	free_irq(mal->txeob_irq, mal);
	free_irq(mal->rxde_irq, mal);
	free_irq(mal->rxeob_irq, mal);
	
	if (mal->tx_virt_addr)
		consistent_free(mal->tx_virt_addr);
	if (mal->rx_virt_addr)
		consistent_free(mal->rx_virt_addr);

	memset(mal, 0, sizeof(*mal));
}

static int __init init_mals(void)
{
	int i;
	int err;

	printk(KERN_DEBUG "init_mals()\n");

	for (i = 0; i < NUM_MALS; i++) {
		err = init_mal(i);
		if (err)
			return err; /* FIXME: cleanup initalized MALs */
	}

	return 0;
}

static void __exit exit_mals(void)
{
	int i;

	for (i = 0; i < NUM_MALS; i++)
		exit_mal(&mal_table[i]);
}

module_init(init_mals);
module_exit(exit_mals);
