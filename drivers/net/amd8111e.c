
/* Advanced  Micro Devices Inc. AMD8111E Linux Network Driver 
 * Copyright (C) 2002 Advanced Micro Devices 
 *
 * 
 * Copyright 2001,2002 Jeff Garzik <jgarzik@mandrakesoft.com> [ 8139cp.c,tg3.c ]
 * Copyright (C) 2001, 2002 David S. Miller (davem@redhat.com)[ tg3.c]
 * Copyright 1996-1999 Thomas Bogendoerfer [ pcnet32.c ]
 * Derived from the lance driver written 1993,1994,1995 by Donald Becker.
 * Copyright 1993 United States Government as represented by the
 *	Director, National Security Agency.[ pcnet32.c ]
 * Carsten Langgaard, carstenl@mips.com [ pcnet32.c ]
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 
 * USA
  
Module Name:

	amd8111e.c

Abstract:
	
 	 AMD8111 based 10/100 Ethernet Controller Driver. 

Environment:

	Kernel Mode

Revision History:

*/


#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/ctype.h>	
#include <linux/crc32.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
#define AMD8111E_VLAN_TAG_USED 1
#else
#define AMD8111E_VLAN_TAG_USED 0
#endif

#include "amd8111e.h"
#define MODULE_NAME	"amd8111e"
#define MODULE_VERSION	"3.0.0"
MODULE_AUTHOR("Advanced Micro Devices, Inc.");
MODULE_DESCRIPTION ("AMD8111 based 10/100 Ethernet Controller. Driver Version 3.0.0"); 
MODULE_LICENSE("GPL");

MODULE_PARM(speed_duplex, "1-" __MODULE_STRING (MAX_UNITS) "i");
MODULE_PARM_DESC(speed_duplex, "Set device speed and duplex modes, 0: Auto Negotitate, 1: 10Mbps Half Duplex, 2: 10Mbps Full Duplex, 3: 100Mbps Half Duplex, 4: 100Mbps Full Duplex");

static struct pci_device_id amd8111e_pci_tbl[] __devinitdata = {
		
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD8111E_7462,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ 0, }

};

/*
This function will set PHY speed. During initialization sets the original speed to 100 full.
*/
static void amd8111e_set_ext_phy(struct net_device *dev)
{
	struct amd8111e_priv *lp = (struct amd8111e_priv *)dev->priv;
	unsigned long  reg_val = 0;
	void * mmio = lp->mmio;
	struct amd8111e_link_config *link_config = &lp->link_config;
	
	if(!lp->opened){
		/* Initializing SPEED_100 and DUPLEX_FULL as original values */
		link_config->orig_speed = SPEED_100;
		link_config->orig_duplex = DUPLEX_FULL;
		link_config->orig_phy_option = XPHYSP |XPHYFD;
	}
	reg_val = lp->ext_phy_option;

	/* Disable port manager */
	writel((u32) EN_PMGR, mmio + CMD3 );

	/* Reset PHY */
	writel((u32)XPHYRST | lp->ext_phy_option, mmio + CTRL2);

	/* Enable port manager */
	writel((u32)VAL1 | EN_PMGR, mmio + CMD3 );
}

/* 
This function will unmap skb->data space and will free 
all transmit and receive skbuffs.
*/
static int amd8111e_free_skbs(struct net_device *dev)
{
	struct amd8111e_priv *lp = (struct amd8111e_priv *)dev->priv;
	struct sk_buff* rx_skbuff;
	int i;

	/* Freeing transmit skbs */
	for(i = 0; i < NUM_TX_BUFFERS; i++){
		if(lp->tx_skbuff[i]){
			pci_unmap_single(lp->pci_dev,lp->tx_dma_addr[i],					lp->tx_skbuff[i]->len,PCI_DMA_TODEVICE);
			dev_kfree_skb (lp->tx_skbuff[i]);
			lp->tx_skbuff[i] = NULL;
			lp->tx_dma_addr[i] = 0;
		}
	}
	/* Freeing previously allocated receive buffers */
	for (i = 0; i < NUM_RX_BUFFERS; i++){
		rx_skbuff = lp->rx_skbuff[i];
		if(rx_skbuff != NULL){
			pci_unmap_single(lp->pci_dev,lp->rx_dma_addr[i],
				  lp->rx_buff_len - 2,PCI_DMA_FROMDEVICE);
			dev_kfree_skb(lp->rx_skbuff[i]);
			lp->rx_skbuff[i] = NULL;
			lp->rx_dma_addr[i] = 0;
		}
	}
	
	return 0;
}

/*
 This will set the receive buffer length corresponding to the mtu size of network interface.
*/
static inline void amd8111e_set_rx_buff_len(struct net_device* dev)
{
	struct amd8111e_priv* lp = dev->priv;
	unsigned int mtu = dev->mtu;
	
	if (mtu > ETH_DATA_LEN){
		/* MTU + ethernet header + FCS + optional VLAN tag */
		lp->rx_buff_len = mtu + ETH_HLEN + 8;
		lp->options |= OPTION_JUMBO_ENABLE;
	} else{
		lp->rx_buff_len = PKT_BUFF_SZ;
		lp->options &= ~OPTION_JUMBO_ENABLE;
	}
}

/* 
This function will free all the previously allocated buffers, determine new receive buffer length  and will allocate new receive buffers. This function also allocates and initializes both the transmitter and receive hardware descriptors.
 */
static int amd8111e_init_ring(struct net_device *dev)
{
	struct amd8111e_priv *lp = (struct amd8111e_priv *)dev->priv;
	int i;

	lp->rx_idx = lp->tx_idx = 0;
	lp->tx_complete_idx = 0;
	lp->tx_ring_idx = 0;
	

	if(lp->opened)
		/* Free previously allocated transmit and receive skbs */
		amd8111e_free_skbs(dev);	

	else{
		 /* allocate the tx and rx descriptors */
	     	if((lp->tx_ring = pci_alloc_consistent(lp->pci_dev, 
			sizeof(struct amd8111e_tx_dr)*NUM_TX_RING_DR,
			&lp->tx_ring_dma_addr)) == NULL)
		
			goto err_no_mem;
	
	     	if((lp->rx_ring = pci_alloc_consistent(lp->pci_dev, 
			sizeof(struct amd8111e_rx_dr)*NUM_RX_RING_DR,
			&lp->rx_ring_dma_addr)) == NULL)
		
			goto err_free_tx_ring;

	}
	/* Set new receive buff size */
	amd8111e_set_rx_buff_len(dev);

	/* Allocating receive  skbs */
	for (i = 0; i < NUM_RX_BUFFERS; i++) {

		if (!(lp->rx_skbuff[i] = dev_alloc_skb(lp->rx_buff_len))) {
				/* Release previos allocated skbs */
				for(--i; i >= 0 ;i--)
					dev_kfree_skb(lp->rx_skbuff[i]);
				goto err_free_rx_ring;
		}
		skb_reserve(lp->rx_skbuff[i],2);
	}
        /* Initilaizing receive descriptors */
	for (i = 0; i < NUM_RX_BUFFERS; i++) {
		lp->rx_dma_addr[i] = pci_map_single(lp->pci_dev, 
			lp->rx_skbuff[i]->data,lp->rx_buff_len-2, PCI_DMA_FROMDEVICE);

		lp->rx_ring[i].buff_phy_addr = cpu_to_le32(lp->rx_dma_addr[i]);
		lp->rx_ring[i].buff_count = cpu_to_le16(lp->rx_buff_len);
		lp->rx_ring[i].rx_dr_offset10 = cpu_to_le16(OWN_BIT);
	}

	/* Initializing transmit descriptors */
	for (i = 0; i < NUM_TX_RING_DR; i++) {
		lp->tx_ring[i].buff_phy_addr = 0;
		lp->tx_ring[i].tx_dr_offset2 = 0;
		lp->tx_ring[i].buff_count = 0;
	}

	return 0;

err_free_rx_ring:
	
	pci_free_consistent(lp->pci_dev, 
		sizeof(struct amd8111e_rx_dr)*NUM_RX_RING_DR,lp->rx_ring,
		lp->rx_ring_dma_addr);

err_free_tx_ring:
	
	pci_free_consistent(lp->pci_dev,
		 sizeof(struct amd8111e_tx_dr)*NUM_TX_RING_DR,lp->tx_ring, 
		 lp->tx_ring_dma_addr);

err_no_mem:
	return -ENOMEM;
}
/* 
This function initializes the device registers  and starts the device.  
*/
static int amd8111e_restart(struct net_device *dev)
{
	struct amd8111e_priv *lp = (struct amd8111e_priv* )dev->priv;
	void * mmio = lp->mmio;
	int i,reg_val;

	/* stop the chip */
	 writel(RUN, mmio + CMD0);

	if(amd8111e_init_ring(dev))
		return -ENOMEM;
	
	amd8111e_set_ext_phy(dev);

	/* set control registers */
	reg_val = readl(mmio + CTRL1);
	
	writel( reg_val| XMTSP_128 | CACHE_ALIGN | B1_MASK, mmio + CTRL1 );

	/* enable interrupt */
	writel( APINT5EN | APINT4EN | APINT3EN | APINT2EN | APINT1EN | 
		APINT0EN | MIIPDTINTEN | MCCIINTEN | MCCINTEN | MREINTEN |
		SPNDINTEN | MPINTEN | SINTEN | STINTEN, mmio + INTEN0);

	writel(VAL3 | LCINTEN | VAL1 | TINTEN0 | VAL0 | RINTEN0, mmio + INTEN0);

	/* initialize tx and rx ring base addresses */
	writel((u32)lp->tx_ring_dma_addr,mmio + XMT_RING_BASE_ADDR0);
	writel((u32)lp->rx_ring_dma_addr,mmio+ RCV_RING_BASE_ADDR0);

	writew((u32)NUM_TX_RING_DR, mmio + XMT_RING_LEN0);
	writew((u16)NUM_RX_RING_DR, mmio + RCV_RING_LEN0);

	if(lp->options & OPTION_JUMBO_ENABLE){
		writel((u32)VAL2|JUMBO, mmio + CMD3);
		/* Reset REX_UFLO */
		writel( REX_UFLO, mmio + CMD2);
		/* Should not set REX_UFLO for jumbo frames */
		writel( VAL0 | APAD_XMT | REX_RTRY, mmio + CMD2);
	}else
		writel( VAL0 | APAD_XMT | REX_RTRY|REX_UFLO, mmio + CMD2);

#if AMD8111E_VLAN_TAG_USED
	writel((u32) VAL2|VSIZE|VL_TAG_DEL, mmio + CMD3);
#endif
	writel( VAL0 | APAD_XMT | REX_RTRY, mmio + CMD2 );
	
	/* Setting the MAC address to the device */
	for(i = 0; i < ETH_ADDR_LEN; i++)
		writeb( dev->dev_addr[i], mmio + PADR + i ); 
	
	/* set RUN bit to start the chip */
	writel(VAL2 | RDMD0, mmio + CMD0);
	writel(VAL0 | INTREN | RUN, mmio + CMD0);
	
	return 0;
}
/* 
This function clears necessary the device registers. 
*/	
static void amd8111e_init_hw_default( struct amd8111e_priv* lp)
{
	unsigned int reg_val;
	unsigned int logic_filter[2] ={0,};
	void * mmio = lp->mmio;


	/* AUTOPOLL0 Register *//*TBD default value is 8100 in FPS */
	writew( 0x8101, mmio + AUTOPOLL0);

	/* Clear RCV_RING_BASE_ADDR */
	writel(0, mmio + RCV_RING_BASE_ADDR0);

	/* Clear XMT_RING_BASE_ADDR */
	writel(0, mmio + XMT_RING_BASE_ADDR0);
	writel(0, mmio + XMT_RING_BASE_ADDR1);
	writel(0, mmio + XMT_RING_BASE_ADDR2);
	writel(0, mmio + XMT_RING_BASE_ADDR3);

	/* Clear CMD0  */
	writel(CMD0_CLEAR,mmio + CMD0);
	
	/* Clear CMD2 */
	writel(CMD2_CLEAR, mmio +CMD2);

	/* Clear CMD7 */
	writel(CMD7_CLEAR , mmio + CMD7);

	/* Clear DLY_INT_A and DLY_INT_B */
	writel(0x0, mmio + DLY_INT_A);
	writel(0x0, mmio + DLY_INT_B);

	/* Clear FLOW_CONTROL */
	writel(0x0, mmio + FLOW_CONTROL);

	/* Clear INT0  write 1 to clear register */
	reg_val = readl(mmio + INT0);
	writel(reg_val, mmio + INT0);

	/* Clear STVAL */
	writel(0x0, mmio + STVAL);

	/* Clear INTEN0 */
	writel( INTEN0_CLEAR, mmio + INTEN0);

	/* Clear LADRF */
	writel(0x0 , mmio + LADRF);

	/* Set SRAM_SIZE & SRAM_BOUNDARY registers  */
	writel( 0x80010,mmio + SRAM_SIZE);

	/* Clear RCV_RING0_LEN */
	writel(0x0, mmio +  RCV_RING_LEN0);

	/* Clear XMT_RING0/1/2/3_LEN */
	writel(0x0, mmio +  XMT_RING_LEN0);
	writel(0x0, mmio +  XMT_RING_LEN1);
	writel(0x0, mmio +  XMT_RING_LEN2);
	writel(0x0, mmio +  XMT_RING_LEN3);

	/* Clear XMT_RING_LIMIT */
	writel(0x0, mmio + XMT_RING_LIMIT);

	/* Clear MIB */
	writew(MIB_CLEAR, mmio + MIB_ADDR);

	/* Clear LARF */
	AMD8111E_WRITE_REG64(mmio, LADRF,logic_filter);

	/* SRAM_SIZE register */
	reg_val = readl(mmio + SRAM_SIZE);
	
	if(lp->options & OPTION_JUMBO_ENABLE)
		writel( VAL2|JUMBO, mmio + CMD3);
#if AMD8111E_VLAN_TAG_USED
	writel(VAL2|VSIZE|VL_TAG_DEL, mmio + CMD3 );
#endif
	/* CMD2 register */
	reg_val = readl(mmio + CMD2);

}

/* 
This function disables the interrupt and clears all the pending 
interrupts in INT0
 */
static void amd8111e_disable_interrupt(struct amd8111e_priv* lp)
{	
	u32 intr0;

	/* Disable interrupt */
	writel(INTREN, lp->mmio + CMD0);
	
	/* Clear INT0 */
	intr0 = readl(lp->mmio + INT0);
	writel(intr0, lp->mmio + INT0);

}

/*
This function stops the chip. 
*/
static void amd8111e_stop_chip(struct amd8111e_priv* lp)
{
	writel(RUN, lp->mmio + CMD0);
}

/* 
This function frees the  transmiter and receiver descriptor rings.
*/
static void amd8111e_free_ring(struct amd8111e_priv* lp)
{	

	/* Free transmit and receive skbs */
	amd8111e_free_skbs(lp->amd8111e_net_dev);

	/* Free transmit and receive descriptor rings */
	if(lp->rx_ring){
		pci_free_consistent(lp->pci_dev, 
			sizeof(struct amd8111e_rx_dr)*NUM_RX_RING_DR,
			lp->rx_ring, lp->rx_ring_dma_addr);
		lp->rx_ring = NULL;
	}
	
	if(lp->tx_ring){
		pci_free_consistent(lp->pci_dev, 
			sizeof(struct amd8111e_tx_dr)*NUM_TX_RING_DR,
			lp->tx_ring, lp->tx_ring_dma_addr);

		lp->tx_ring = NULL;
	}

}
#if AMD8111E_VLAN_TAG_USED	
/* 
This is the receive indication function for packets with vlan tag.
*/	
static int amd8111e_vlan_rx(struct amd8111e_priv *lp, struct sk_buff *skb, u16 vlan_tag)
{
	return vlan_hwaccel_rx(skb, lp->vlgrp, vlan_tag);
}
#endif

/*
This function will free all the transmit skbs that are actually transmitted by the device. It will check the ownership of the skb before freeing the skb. 
*/
static int amd8111e_tx(struct net_device *dev)
{
	struct amd8111e_priv* lp = dev->priv;
	int tx_index = lp->tx_complete_idx & TX_RING_DR_MOD_MASK;
	int status;
	
	/* Complete all the transmit packet */
	while (lp->tx_complete_idx != lp->tx_idx){
		tx_index =  lp->tx_complete_idx & TX_RING_DR_MOD_MASK;
		status = le16_to_cpu(lp->tx_ring[tx_index].tx_dr_offset2);

		if(status & OWN_BIT)
			break;	/* It still hasn't been Txed */

		lp->tx_ring[tx_index].buff_phy_addr = 0;

		/* We must free the original skb */
		if (lp->tx_skbuff[tx_index]) {
			pci_unmap_single(lp->pci_dev, lp->tx_dma_addr[tx_index],
				  	lp->tx_skbuff[tx_index]->len,
					PCI_DMA_TODEVICE);
			dev_kfree_skb_irq (lp->tx_skbuff[tx_index]);
			lp->tx_skbuff[tx_index] = 0;
			lp->tx_dma_addr[tx_index] = 0;
		}
		lp->tx_complete_idx++;	

		if (netif_queue_stopped(dev) &&
			lp->tx_complete_idx > lp->tx_idx - NUM_TX_BUFFERS +2){
			/* The ring is no longer full, clear tbusy. */
			netif_wake_queue (dev);
		}
	}
	return 0;
}

/* 
This function will check the ownership of receive buffers and descriptors. It will indicate to kernel up to half the number of maximum receive buffers in the descriptor ring, in a single receive interrupt. It will also replenish the descriptors with new skbs.
*/
static int amd8111e_rx(struct net_device *dev)
{
	struct amd8111e_priv *lp = dev->priv;
	struct sk_buff *skb,*new_skb;
	int rx_index = lp->rx_idx & RX_RING_DR_MOD_MASK;
	int min_pkt_len, status;
	int num_rx_pkt = 0;
	int max_rx_pkt = NUM_RX_BUFFERS/2;
	short pkt_len;
#if AMD8111E_VLAN_TAG_USED		
	short vtag;
#endif
	
	/* If we own the next entry, it's a new packet. Send it up. */
	while(++num_rx_pkt <= max_rx_pkt){
		if(lp->rx_ring[rx_index].rx_dr_offset10 & OWN_BIT)
			return 0;
	       
		/* check if err summary bit is set */ 
		if(le16_to_cpu(lp->rx_ring[rx_index].rx_dr_offset10) & ERR_BIT){
			/* 
			 * There is a tricky error noted by John Murphy,
			 * <murf@perftech.com> to Russ Nelson: Even with full-sized
			 * buffers it's possible for a jabber packet to use two
			 * buffers, with only the last correctly noting the error.			 */
			/* reseting flags */
			lp->rx_ring[rx_index].rx_dr_offset10 &= 
						cpu_to_le16(RESET_RX_FLAGS);
			goto err_next_pkt;
		}
		/* check for STP and ENP */
		status = le16_to_cpu(lp->rx_ring[rx_index].rx_dr_offset10);
		if(!((status & STP_BIT) && (status & ENP_BIT))){
			/* reseting flags */
			lp->rx_ring[rx_index].rx_dr_offset10 &= 
						cpu_to_le16(RESET_RX_FLAGS);
			goto err_next_pkt;
		}
		pkt_len = le16_to_cpu(lp->rx_ring[rx_index].msg_count) - 4;

#if AMD8111E_VLAN_TAG_USED		
		vtag = le16_to_cpu(lp->rx_ring[rx_index].rx_dr_offset10) & TT_MASK;
		/*MAC will strip vlan tag*/ 
		if(lp->vlgrp != NULL && vtag !=0)
			min_pkt_len =MIN_PKT_LEN - 4;
		else
#endif
			min_pkt_len =MIN_PKT_LEN;

		if (pkt_len < min_pkt_len) {
			lp->rx_ring[rx_index].rx_dr_offset10 &= 
				cpu_to_le16(RESET_RX_FLAGS);
			lp->stats.rx_errors++;
			goto err_next_pkt;
		}
		if(!(new_skb = dev_alloc_skb(lp->rx_buff_len))){
			/* if allocation fail, 
				ignore that pkt and go to next one */
			lp->rx_ring[rx_index].rx_dr_offset10 &= 
				cpu_to_le16(RESET_RX_FLAGS);
			lp->stats.rx_errors++;
			goto err_next_pkt;
		}
		
		skb_reserve(new_skb, 2);
		skb = lp->rx_skbuff[rx_index];
		pci_unmap_single(lp->pci_dev,lp->rx_dma_addr[rx_index],
			lp->rx_buff_len-2, PCI_DMA_FROMDEVICE);
		skb_put(skb, pkt_len);
		skb->dev = dev;
		lp->rx_skbuff[rx_index] = new_skb;
		new_skb->dev = dev;
		lp->rx_dma_addr[rx_index] = pci_map_single(lp->pci_dev,
			new_skb->data, lp->rx_buff_len-2,PCI_DMA_FROMDEVICE);
	
		skb->protocol = eth_type_trans(skb, dev);

#if AMD8111E_VLAN_TAG_USED		
		
		vtag = lp->rx_ring[rx_index].rx_dr_offset10 & TT_MASK;
		if(lp->vlgrp != NULL && (vtag == TT_VLAN_TAGGED)){
			amd8111e_vlan_rx(lp, skb,
				    lp->rx_ring[rx_index].tag_ctrl_info);
		} else
#endif
			
			dev->last_rx = jiffies;
			netif_rx (skb);
	
err_next_pkt:
		lp->rx_ring[rx_index].buff_phy_addr
			 = cpu_to_le32(lp->rx_dma_addr[rx_index]);
		lp->rx_ring[rx_index].buff_count = 
				cpu_to_le16(lp->rx_buff_len-2);
		lp->rx_ring[rx_index].rx_dr_offset10 |= cpu_to_le16(OWN_BIT);
		rx_index = (++lp->rx_idx) & RX_RING_DR_MOD_MASK;
	}

	return 0;
}

/* 
This function will store the original speed to restore later, if autoneg is turned on. This speed will be set later when the autoneg is turned off. If the link status indicates that link is down, that will be indicated to the kernel */

static int amd8111e_link_change(struct net_device* dev)
{	
	struct amd8111e_priv *lp = dev->priv;
	int status0,speed;

	/* read the link change */
     	status0 = readl(lp->mmio + STAT0);
	
	if(status0 & LINK_STATS){
		if(status0 & AUTONEG_COMPLETE){
			/* keeping the original speeds */
			if((lp->link_config.speed != SPEED_INVALID)&&
				(lp->link_config.duplex != DUPLEX_INVALID)){
			lp->link_config.orig_speed = lp->link_config.speed;
			lp->link_config.orig_duplex = lp->link_config.duplex;
			lp->link_config.orig_phy_option = lp->ext_phy_option;
			}
	
			lp->link_config.speed = SPEED_INVALID;
			lp->link_config.duplex = DUPLEX_INVALID;
			lp->link_config.autoneg = AUTONEG_ENABLE;
			netif_carrier_on(dev);
			return 0;
		}
		if(status0 & FULL_DPLX)
			lp->link_config.duplex = DUPLEX_FULL;
		else 
			lp->link_config.duplex = DUPLEX_HALF;
		speed = (status0 & SPEED_MASK) >> 7;
		if(speed == PHY_SPEED_10)
			lp->link_config.speed = SPEED_10;
		else if(speed == PHY_SPEED_100)
			lp->link_config.speed = SPEED_100;
		lp->link_config.autoneg = AUTONEG_DISABLE;
		netif_carrier_on(dev);
	}
	else{	
		lp->link_config.speed = SPEED_INVALID;
		lp->link_config.duplex = DUPLEX_INVALID;
		lp->link_config.autoneg = AUTONEG_INVALID;
		netif_carrier_off(dev);
	}
		
	return 0;
}
/*
This function reads the mib counters. 	 
*/
static int amd8111e_read_mib(void* mmio, u8 MIB_COUNTER)
{
	unsigned int  status;
	unsigned  int data;
	unsigned int repeat = REPEAT_CNT;

	writew( MIB_RD_CMD | MIB_COUNTER, mmio + MIB_ADDR);
	do {
		status = readw(mmio + MIB_ADDR);
		udelay(2);	/* controller takes MAX 2 us to get mib data */
	}
	while (--repeat && (status & MIB_CMD_ACTIVE));

	data = readl(mmio + MIB_DATA);
	return data;
}

/*
This function retuurns the reads the mib registers and returns the hardware statistics. It adds the previous statistics with new values.*/ 
static struct net_device_stats *amd8111e_get_stats(struct net_device * dev)
{
	struct amd8111e_priv *lp = dev->priv;
	void * mmio = lp->mmio;
	unsigned long flags;
	struct net_device_stats *prev_stats = &lp->prev_stats;
	struct net_device_stats* new_stats = &lp->stats;
	
	if(!lp->opened)
		return prev_stats;	
	spin_lock_irqsave (&lp->lock, flags);

	/* stats.rx_packets */
	new_stats->rx_packets = prev_stats->rx_packets+
		amd8111e_read_mib(mmio, rcv_broadcast_pkts)+
		amd8111e_read_mib(mmio, rcv_multicast_pkts)+
		amd8111e_read_mib(mmio, rcv_unicast_pkts);

	/* stats.tx_packets */
	new_stats->tx_packets = prev_stats->tx_packets+
		amd8111e_read_mib(mmio, xmt_packets);

	/*stats.rx_bytes */
	new_stats->rx_bytes = prev_stats->rx_bytes+
		amd8111e_read_mib(mmio, rcv_octets);

	/* stats.tx_bytes */
	new_stats->tx_bytes = prev_stats->tx_bytes+
		amd8111e_read_mib(mmio, xmt_octets);

	/* stats.rx_errors */
	new_stats->rx_errors = prev_stats->rx_errors+
		amd8111e_read_mib(mmio, rcv_undersize_pkts)+
		amd8111e_read_mib(mmio, rcv_fragments)+
		amd8111e_read_mib(mmio, rcv_jabbers)+
		amd8111e_read_mib(mmio, rcv_alignment_errors)+
		amd8111e_read_mib(mmio, rcv_fcs_errors)+
		amd8111e_read_mib(mmio, rcv_miss_pkts);

	/* stats.tx_errors */
	new_stats->tx_errors = prev_stats->tx_errors+
		amd8111e_read_mib(mmio, xmt_underrun_pkts);

	/* stats.rx_dropped*/
	new_stats->rx_dropped = prev_stats->rx_dropped+
		amd8111e_read_mib(mmio, rcv_miss_pkts);

	/* stats.tx_dropped*/
	new_stats->tx_dropped = prev_stats->tx_dropped+
		amd8111e_read_mib(mmio,  xmt_underrun_pkts);

	/* stats.multicast*/
	new_stats->multicast = prev_stats->multicast+
		amd8111e_read_mib(mmio, rcv_multicast_pkts);

	/* stats.collisions*/
	new_stats->collisions = prev_stats->collisions+
		amd8111e_read_mib(mmio, xmt_collisions);

	/* stats.rx_length_errors*/
	new_stats->rx_length_errors = prev_stats->rx_length_errors+
		amd8111e_read_mib(mmio, rcv_undersize_pkts)+
		amd8111e_read_mib(mmio, rcv_oversize_pkts);

	/* stats.rx_over_errors*/
	new_stats->rx_over_errors = prev_stats->rx_over_errors+
		amd8111e_read_mib(mmio, rcv_miss_pkts);

	/* stats.rx_crc_errors*/
	new_stats->rx_crc_errors = prev_stats->rx_crc_errors+
		amd8111e_read_mib(mmio, rcv_fcs_errors);

	/* stats.rx_frame_errors*/
	new_stats->rx_frame_errors = prev_stats->rx_frame_errors+
		amd8111e_read_mib(mmio, rcv_alignment_errors);

	/* stats.rx_fifo_errors */
	new_stats->rx_fifo_errors = prev_stats->rx_fifo_errors+
		amd8111e_read_mib(mmio, rcv_miss_pkts);

	/* stats.rx_missed_errors */
	new_stats->rx_missed_errors = prev_stats->rx_missed_errors+
		amd8111e_read_mib(mmio, rcv_miss_pkts);

	/* stats.tx_aborted_errors*/
	new_stats->tx_aborted_errors = prev_stats->tx_aborted_errors+
		amd8111e_read_mib(mmio, xmt_excessive_collision);

	/* stats.tx_carrier_errors*/
	new_stats->tx_carrier_errors = prev_stats->tx_carrier_errors+
		amd8111e_read_mib(mmio, xmt_loss_carrier);

	/* stats.tx_fifo_errors*/
	new_stats->tx_fifo_errors = prev_stats->tx_fifo_errors+
		amd8111e_read_mib(mmio, xmt_underrun_pkts);

	/* stats.tx_window_errors*/
	new_stats->tx_window_errors = prev_stats->tx_window_errors+
		amd8111e_read_mib(mmio, xmt_late_collision);

	spin_unlock_irqrestore (&lp->lock, flags);

	return new_stats;
}

/*
This is device interrupt function. It handles transmit, receive and link change interrupts.
*/
static irqreturn_t
amd8111e_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{

	struct net_device * dev = (struct net_device *) dev_id;
	struct amd8111e_priv *lp = dev->priv;
	void * mmio = lp->mmio;
	unsigned int intr0;
	int handled = 0;

	if(dev == NULL)
		return IRQ_NONE;

	spin_lock (&lp->lock);
	/* disabling interrupt */
	writel(INTREN, mmio + CMD0);

	/* Read interrupt status */
	intr0 = readl(mmio + INT0);

	/* Process all the INT event until INTR bit is clear. */

	if (!(intr0 & INTR))
		goto err_no_interrupt;

	handled = 1;
	/* Current driver processes 3 interrupts : RINT,TINT,LCINT */
	writel(intr0, mmio + INT0);

	/* Check if Receive Interrupt has occurred. */
	if(intr0 & RINT0){
		amd8111e_rx(dev);
		writel(VAL2 | RDMD0, mmio + CMD0);
	}

	/* Check if  Transmit Interrupt has occurred. */
	if(intr0 & TINT0)
		amd8111e_tx(dev);
		
	/* Check if  Link Change Interrupt has occurred. */
	if (intr0 & LCINT)
		amd8111e_link_change(dev);
	
err_no_interrupt:
	writel( VAL0 | INTREN,mmio + CMD0);
	spin_unlock(&lp->lock);
	return IRQ_RETVAL(handled);

}
/*
This function closes the network interface and copies the new set of statistics into the previous statistics structure so that most recent statistics will be available after the interface is down.
*/
static int amd8111e_close(struct net_device * dev)
{
	struct amd8111e_priv *lp = dev->priv;
	netif_stop_queue(dev);
	
	spin_lock_irq(&lp->lock);
	
	amd8111e_disable_interrupt(lp);
	amd8111e_stop_chip(lp);
	amd8111e_free_ring(lp);
	
	netif_carrier_off(lp->amd8111e_net_dev);

	spin_unlock_irq(&lp->lock);

	free_irq(dev->irq, dev);
	memcpy(&lp->prev_stats,amd8111e_get_stats(dev), sizeof(lp->prev_stats));
	lp->opened = 0;
	return 0;
}
/* This function opens new interface.It requests irq for the device, initializes the device,buffers and descriptors, and starts the device. 
*/
static int amd8111e_open(struct net_device * dev )
{
	struct amd8111e_priv *lp = (struct amd8111e_priv *)dev->priv;

	if(dev->irq ==0 || request_irq(dev->irq, amd8111e_interrupt, SA_SHIRQ,
					 dev->name, dev)) 
		return -EAGAIN;

	spin_lock_irq(&lp->lock);

	amd8111e_init_hw_default(lp);

	if(amd8111e_restart(dev)){
		spin_unlock_irq(&lp->lock);
		return -ENOMEM;
	}
	
	lp->opened = 1;

	spin_unlock_irq(&lp->lock);

	netif_start_queue(dev);

	return 0;		
}
/* 
This function checks if there is any transmit  descriptors available to queue more packet.
*/
static int amd8111e_tx_queue_avail(struct amd8111e_priv* lp )
{	
	int tx_index = lp->tx_idx & TX_BUFF_MOD_MASK;
	if(lp->tx_skbuff[tx_index] != 0)
		return -1;
	else
		return 0;
	
}
/* 
This function will queue the transmit packets to the descriptors and will trigger the send operation. It also initializes the transmit descriptors with buffer physical address, byte count, ownership to hardware etc.
*/

static int amd8111e_start_xmit(struct sk_buff *skb, struct net_device * dev)
{
	struct amd8111e_priv *lp = dev->priv;
	int tx_index;
	unsigned long flags;

	spin_lock_irqsave(&lp->lock, flags);

	tx_index = lp->tx_idx & TX_RING_DR_MOD_MASK;

	lp->tx_ring[tx_index].buff_count = cpu_to_le16(skb->len);

	lp->tx_skbuff[tx_index] = skb;
	lp->tx_ring[tx_index].tx_dr_offset2 = 0;

#if AMD8111E_VLAN_TAG_USED
	if((lp->vlgrp != NULL) && vlan_tx_tag_present(skb)){

		lp->tx_ring[tx_index].tag_ctrl_cmd |= 
				cpu_to_le32(TCC_VLAN_INSERT);	
		lp->tx_ring[tx_index].tag_ctrl_info = 
				cpu_to_le16(vlan_tx_tag_get(skb));

	}
#endif
	lp->tx_dma_addr[tx_index] =
	    pci_map_single(lp->pci_dev, skb->data, skb->len, PCI_DMA_TODEVICE);
	lp->tx_ring[tx_index].buff_phy_addr =
	    (u32) cpu_to_le32(lp->tx_dma_addr[tx_index]);

	/*  Set FCS and LTINT bits */
	lp->tx_ring[tx_index].tx_dr_offset2 |=
	    cpu_to_le16(OWN_BIT | STP_BIT | ENP_BIT|ADD_FCS_BIT|LTINT_BIT);

	lp->tx_idx++;

	/* Trigger an immediate send poll. */
	writel( VAL1 | TDMD0, lp->mmio + CMD0);
	writel( VAL2 | RDMD0,lp->mmio + CMD0);

	dev->trans_start = jiffies;

	if(amd8111e_tx_queue_avail(lp) < 0){
		netif_stop_queue(dev);
	}
	spin_unlock_irqrestore(&lp->lock, flags);
	return 0;
}
/*
This function returns all the memory mapped registers of the device.
*/
static char* amd8111e_read_regs(struct amd8111e_priv* lp)
{    	
	void * mmio = lp->mmio;
        unsigned char * reg_buff;

     	int i;
     
     	reg_buff = kmalloc( AMD8111E_REG_DUMP_LEN,GFP_KERNEL);
	if(NULL == reg_buff)
		return NULL;
    	for (i=0; i < AMD8111E_REG_DUMP_LEN; i+=4)
		reg_buff[i]= readl(mmio + i);	
	return reg_buff;
}
/*
This function sets promiscuos mode, all-multi mode or the multicast address 
list to the device.
*/
static void amd8111e_set_multicast_list(struct net_device *dev)
{
	struct dev_mc_list* mc_ptr;
	struct amd8111e_priv *lp = dev->priv;
	u32 mc_filter[2] ;
	int i,bit_num;

	if(dev->flags & IFF_PROMISC){
		printk("%s: Setting  promiscuous mode.\n",dev->name);
		writel( VAL2 | PROM, lp->mmio + CMD2);
		return;
	}
	else
		writel( PROM, lp->mmio + CMD2);
	if(dev->flags & IFF_ALLMULTI || dev->mc_count > MAX_FILTER_SIZE){
		/* get all multicast packet */
		mc_filter[1] = mc_filter[0] = 0xffffffff;
		lp->mc_list = dev->mc_list;
		lp->options |= OPTION_MULTICAST_ENABLE;
		AMD8111E_WRITE_REG64(lp->mmio, LADRF,mc_filter);
		return;
	}
	if( dev->mc_count == 0 ){
		/* get only own packets */
		mc_filter[1] = mc_filter[0] = 0;
		lp->mc_list = 0;
		lp->options &= ~OPTION_MULTICAST_ENABLE;
		AMD8111E_WRITE_REG64(lp->mmio, LADRF,mc_filter);
		/* disable promiscous mode */
		writel(PROM, lp->mmio + CMD2);
		return;
	}
	/* load all the multicast addresses in the logic filter */
	lp->options |= OPTION_MULTICAST_ENABLE;
	lp->mc_list = dev->mc_list;
	mc_filter[1] = mc_filter[0] = 0;
	for (i = 0, mc_ptr = dev->mc_list; mc_ptr && i < dev->mc_count;
		     i++, mc_ptr = mc_ptr->next) {
		bit_num = ether_crc(ETH_ALEN, mc_ptr->dmi_addr) >> 26;
			
		mc_filter[bit_num >> 5] |= 1 << (bit_num & 31);
	}	

	AMD8111E_WRITE_REG64(lp->mmio, LADRF, mc_filter);
	return;
}
/*
This function handles all the  ethtool ioctls. It gives driver info, gets/sets driver speed, gets memory mapped register values, forces auto negotiation, sets/gets WOL options for ethtool application. 
*/
	
static int amd8111e_ethtool_ioctl(struct net_device* dev, void* useraddr)
{
	struct amd8111e_priv *lp = dev->priv;
	struct pci_dev *pci_dev = lp->pci_dev;
	u32 ethcmd;
	
	if( useraddr == NULL) 
		return -EINVAL;
	if(copy_from_user (&ethcmd, useraddr, sizeof (ethcmd)))
		return -EFAULT;
	
	switch(ethcmd){
	
	case ETHTOOL_GDRVINFO:{
		struct ethtool_drvinfo info = { ETHTOOL_GDRVINFO };
		strcpy (info.driver, MODULE_NAME);
		strcpy (info.version, MODULE_VERSION);
		memset(&info.fw_version, 0, sizeof(info.fw_version));
		strcpy (info.bus_info, pci_dev->slot_name);
		info.eedump_len = 0;
		info.regdump_len = AMD8111E_REG_DUMP_LEN;
		if (copy_to_user (useraddr, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	case ETHTOOL_GSET:{
		struct ethtool_cmd cmd = { ETHTOOL_GSET };

		if (!lp->opened) 
			return -EAGAIN;

		cmd.supported = SUPPORTED_Autoneg |
				SUPPORTED_100baseT_Half |
				SUPPORTED_100baseT_Full |
			  	SUPPORTED_10baseT_Half |
			        SUPPORTED_10baseT_Full |
			        SUPPORTED_MII;

		cmd.advertising = ADVERTISED_Autoneg |
				ADVERTISED_100baseT_Half |
				ADVERTISED_100baseT_Full |
			  	ADVERTISED_10baseT_Half |
			        ADVERTISED_10baseT_Full |
			        ADVERTISED_MII;
		cmd.speed = lp->link_config.speed;
		cmd.duplex = lp->link_config.duplex;
		cmd.port = 0;
		cmd.phy_address = PHY_ID;
		cmd.transceiver = XCVR_EXTERNAL;
		cmd.autoneg = lp->link_config.autoneg;
		cmd.maxtxpkt = 0; /* not implemented interrupt coalasing */
		cmd.maxrxpkt = 0; /* not implemented interrupt coalasing */
		if (copy_to_user(useraddr, &cmd, sizeof(cmd)))
			return -EFAULT;
		return 0;
		}
	case ETHTOOL_SSET: {
	
		struct ethtool_cmd cmd;

		if (!lp->opened)
			return -EAGAIN;
		if (copy_from_user(&cmd, useraddr, sizeof(cmd)))
			return -EFAULT;

		spin_lock_irq(&lp->lock);

		if(cmd.autoneg == AUTONEG_ENABLE){
			/* keeping the original speeds */
			if((lp->link_config.speed != SPEED_INVALID)&&
				(lp->link_config.duplex != DUPLEX_INVALID)){
			lp->link_config.orig_speed = lp->link_config.speed;
			lp->link_config.orig_duplex = lp->link_config.duplex;
			lp->link_config.orig_phy_option = lp->ext_phy_option;
			}

			lp->ext_phy_option = XPHYANE;
		}
		else if(cmd.speed == SPEED_100 && cmd.duplex ==  DUPLEX_HALF)
			lp->ext_phy_option = XPHYSP;
		else if(cmd.speed == SPEED_100 && cmd.duplex ==  DUPLEX_FULL)
			lp->ext_phy_option = XPHYSP |XPHYFD;
		else if(cmd.speed == SPEED_10 && cmd.duplex ==  DUPLEX_HALF)
			lp->ext_phy_option = 0;
		else if(cmd.speed == SPEED_10 && cmd.duplex ==  DUPLEX_FULL)
			lp->ext_phy_option = XPHYFD;
		else {	
			/* setting the original speed */
			cmd.speed = lp->link_config.orig_speed;
			cmd.duplex = lp->link_config.orig_duplex;
			lp->ext_phy_option = lp->link_config.orig_phy_option;
		}
		lp->link_config.autoneg = cmd.autoneg;
		if (cmd.autoneg == AUTONEG_ENABLE) {
			
			lp->link_config.speed = SPEED_INVALID;
			lp->link_config.duplex = DUPLEX_INVALID;
		} else {
			lp->link_config.speed = cmd.speed;
			lp->link_config.duplex = cmd.duplex;
		}
		amd8111e_set_ext_phy(dev);
		spin_unlock_irq(&lp->lock);
		return 0;
	}
	case ETHTOOL_GREGS: {
		struct ethtool_regs regs;
		u8 *regbuf;
		int ret;

		if (copy_from_user(&regs, useraddr, sizeof(regs)))
			return -EFAULT;
		if (regs.len > AMD8111E_REG_DUMP_LEN)
			regs.len = AMD8111E_REG_DUMP_LEN;
		regs.version = 0;
		if (copy_to_user(useraddr, &regs, sizeof(regs)))
			return -EFAULT;

		regbuf = amd8111e_read_regs(lp);
		if (!regbuf)
			return -ENOMEM;

		useraddr += offsetof(struct ethtool_regs, data);
		ret = 0;
		if (copy_to_user(useraddr, regbuf, regs.len))
			ret = -EFAULT;
		kfree(regbuf);
		return ret;
	}
	case ETHTOOL_NWAY_RST: {
		int ret;
		spin_lock_irq(&lp->lock);
		if(lp->link_config.autoneg == AUTONEG_ENABLE){
			lp->ext_phy_option = XPHYANE;
			amd8111e_set_ext_phy(dev);
			ret = 0;
		}else
			ret =  -EINVAL;
		spin_unlock_irq(&lp->lock);
		return ret;
	}
	case ETHTOOL_GLINK: {
		struct ethtool_value val = { ETHTOOL_GLINK };

		val.data = netif_carrier_ok(dev) ? 1 : 0;
		if (copy_to_user(useraddr, &val, sizeof(val)))
			return -EFAULT;
	}
	case ETHTOOL_GWOL: {
		struct ethtool_wolinfo wol_info = { ETHTOOL_GWOL };

		wol_info.supported = WAKE_MAGIC|WAKE_PHY;
		wol_info.wolopts = 0;
		if (lp->options & OPTION_WOL_ENABLE)
			wol_info.wolopts = WAKE_MAGIC;
		memset(&wol_info.sopass, 0, sizeof(wol_info.sopass));
		if (copy_to_user(useraddr, &wol_info, sizeof(wol_info)))
			return -EFAULT;
		return 0;
	}
	case ETHTOOL_SWOL: {
		struct ethtool_wolinfo wol_info;

		if (copy_from_user(&wol_info, useraddr, sizeof(wol_info)))
			return -EFAULT;
		if (wol_info.wolopts & ~(WAKE_MAGIC |WAKE_PHY))
			return -EINVAL;
		spin_lock_irq(&lp->lock);
		if(wol_info.wolopts & WAKE_MAGIC)
			lp->options |= 
				(OPTION_WOL_ENABLE | OPTION_WAKE_MAGIC_ENABLE);
		else if(wol_info.wolopts & WAKE_PHY)
			lp->options |= 
				(OPTION_WOL_ENABLE | OPTION_WAKE_PHY_ENABLE);
		else
			lp->options &= ~OPTION_WOL_ENABLE; 
		spin_unlock_irq(&lp->lock);
		return 0;
	}
	
	default:
		break;
	}
		return -EOPNOTSUPP;
}
static int amd8111e_read_phy(struct amd8111e_priv* lp, int phy_id, int reg, u32* val)
{
	void * mmio = lp->mmio;
	unsigned int reg_val;
	unsigned int repeat= REPEAT_CNT;

	reg_val = readl(mmio + PHY_ACCESS);
	while (reg_val & PHY_CMD_ACTIVE)
		reg_val = readl( mmio + PHY_ACCESS );

	writel( PHY_RD_CMD | ((phy_id & 0x1f) << 21) |
			   ((reg & 0x1f) << 16),  mmio +PHY_ACCESS);
	do{
		reg_val = readl(mmio + PHY_ACCESS);
		udelay(30);  /* It takes 30 us to read/write data */
	} while (--repeat && (reg_val & PHY_CMD_ACTIVE));
	if(reg_val & PHY_RD_ERR)
		goto err_phy_read;
	
	*val = reg_val & 0xffff;
	return 0;
err_phy_read:	
	*val = 0;
	return -EINVAL;
	
}
static int amd8111e_write_phy(struct amd8111e_priv* lp,int phy_id, int reg, u32 val)
{
	unsigned int repeat = REPEAT_CNT
	void * mmio = lp->mmio;
	unsigned int reg_val;
	

	reg_val = readl(mmio + PHY_ACCESS);
	while (reg_val & PHY_CMD_ACTIVE)
		reg_val = readl( mmio + PHY_ACCESS );

	writel( PHY_WR_CMD | ((phy_id & 0x1f) << 21) |
			   ((reg & 0x1f) << 16)|val, mmio + PHY_ACCESS);

	do{
		reg_val = readl(mmio + PHY_ACCESS);
		udelay(30);  /* It takes 30 us to read/write the data */
	} while (--repeat && (reg_val & PHY_CMD_ACTIVE));
	
	if(reg_val & PHY_RD_ERR)
		goto err_phy_write;
	
	return 0;

err_phy_write:	
	return -EINVAL;
	
}
static int amd8111e_ioctl(struct net_device * dev , struct ifreq *ifr, int cmd)
{
	struct mii_ioctl_data *data = (struct mii_ioctl_data *)&ifr->ifr_data;
	struct amd8111e_priv *lp = dev->priv;
	int err;
	u32 mii_regval;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch(cmd) {
	case SIOCETHTOOL:
		return amd8111e_ethtool_ioctl(dev, (void *) ifr->ifr_data);
	case SIOCGMIIPHY:
		data->phy_id = PHY_ID;

	/* fallthru */
	case SIOCGMIIREG: 

		spin_lock_irq(&lp->lock);
		err = amd8111e_read_phy(lp, data->phy_id,
			data->reg_num & PHY_REG_ADDR_MASK, &mii_regval);
		spin_unlock_irq(&lp->lock);

		data->val_out = mii_regval;
		return err;

	case SIOCSMIIREG:

		spin_lock_irq(&lp->lock);
		err = amd8111e_write_phy(lp, data->phy_id,
			data->reg_num & PHY_REG_ADDR_MASK, data->val_in);
		spin_unlock_irq(&lp->lock);

		return err;

	default:
		/* do nothing */
		break;
	}
	return -EOPNOTSUPP;
}
/* 
This function changes the mtu of the device. It restarts the device  to initialize the descriptor with new receive buffers.
*/  
int amd8111e_change_mtu(struct net_device *dev, int new_mtu)
{
	struct amd8111e_priv *lp = dev->priv;
	int err;

	if ((new_mtu < AMD8111E_MIN_MTU) || (new_mtu > AMD8111E_MAX_MTU))
		return -EINVAL;

	if (!netif_running(dev)) {
		/* new_mtu will be used
		   when device starts netxt time */ 
		dev->mtu = new_mtu;
		return 0;
	}

	spin_lock_irq(&lp->lock);

        /* stop the chip */
	writel(RUN, lp->mmio + CMD0);

	dev->mtu = new_mtu;

	/* if (new_mtu > ETH_DATA_LEN)
		lp->options |= OPTION_JUMBO_ENABLE;
	else
		lp->options &= ~OPTION_JUMBO_ENABLE;
	*/
	err = amd8111e_restart(dev);
	spin_unlock_irq(&lp->lock);

	netif_start_queue(dev);
	return err;
}

#if AMD8111E_VLAN_TAG_USED
static void amd8111e_vlan_rx_register(struct net_device *dev, struct vlan_group *grp)
{
	struct  amd8111e_priv *lp = dev->priv;
	spin_lock_irq(&lp->lock);
	lp->vlgrp = grp;
	spin_unlock_irq(&lp->lock);
}
	
static void amd8111e_vlan_rx_kill_vid(struct net_device *dev, unsigned short vid)
{
	struct amd8111e_priv *lp = dev->priv;
	spin_lock_irq(&lp->lock);
	if (lp->vlgrp)
		lp->vlgrp->vlan_devices[vid] = NULL;
	spin_unlock_irq(&lp->lock);
}
#endif
static int amd8111e_enable_magicpkt(struct amd8111e_priv* lp)
{
	writel( VAL1|MPPLBA, lp->mmio + CMD3);
	writel( VAL0|MPEN_SW, lp->mmio + CMD7);
	return 0;
}

static int amd8111e_enable_link_change(struct amd8111e_priv* lp)
{
	/* Adapter is already stoped/suspended/interrupt-disabled */
	writel(VAL0|LCMODE_SW,lp->mmio + CMD7);
	return 0;
}	

/* 
This function sets the power state of the device. When the device go to lower power states 1,2, and 3 it enables the wake on lan 
*/  	
static int amd8111e_set_power_state(struct amd8111e_priv* lp, u32 state)
{
	u16 power_control;
	int pm = lp->pm_cap;

	pci_read_config_word(lp->pci_dev,
			     pm + PCI_PM_CTRL,
			     &power_control);

	power_control |= PCI_PM_CTRL_PME_STATUS;
	power_control &= ~(PCI_PM_CTRL_STATE_MASK);
	switch (state) {
	case 0:
		power_control |= 0;
		pci_write_config_word(lp->pci_dev,
				      pm + PCI_PM_CTRL,
				      power_control);
		return 0;

	case 1:
		power_control |= 1;
		break;

	case 2:
		power_control |= 2;
		break;

	case 3:
		power_control |= 3;
		break;
	default:

		printk(KERN_WARNING "%s: Invalid power state (%d) requested.\n",
		       lp->amd8111e_net_dev->name, state);
		return -EINVAL;
	}
	
	if(lp->options & OPTION_WAKE_MAGIC_ENABLE)
		amd8111e_enable_magicpkt(lp);	
	if(lp->options & OPTION_WAKE_PHY_ENABLE)
		amd8111e_enable_link_change(lp);	

	/*  Setting new power state. */
	pci_write_config_word(lp->pci_dev, pm + PCI_PM_CTRL, power_control);

	return 0;


}
static int amd8111e_suspend(struct pci_dev *pci_dev, u32 state)
{	
	struct net_device *dev = pci_get_drvdata(pci_dev);
	struct amd8111e_priv *lp = dev->priv;
	int err;
	
	if (!netif_running(dev))
		return 0;

	/* disable the interrupt */
	spin_lock_irq(&lp->lock);
	amd8111e_disable_interrupt(lp);
	spin_unlock_irq(&lp->lock);

	netif_device_detach(dev);
	
	/* stop chip */
	spin_lock_irq(&lp->lock);
	amd8111e_stop_chip(lp);
	spin_unlock_irq(&lp->lock);

	err = amd8111e_set_power_state(lp, state);
	if (err) {
		
		spin_lock_irq(&lp->lock);
		amd8111e_restart(dev);
		spin_unlock_irq(&lp->lock);

		netif_device_attach(dev);
	}
	return err;
}
static int amd8111e_resume(struct pci_dev *pci_dev)
{
	struct net_device *dev = pci_get_drvdata(pci_dev);
	struct amd8111e_priv *lp = dev->priv;
	int err;
	
	if (!netif_running(dev))
		return 0;

	err = amd8111e_set_power_state(lp, 0);
	if (err)
		return err;

	netif_device_attach(dev);

	spin_lock_irq(&lp->lock);
	amd8111e_restart(dev);
	spin_unlock_irq(&lp->lock);

	return 0;
}


static void __devexit amd8111e_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	if (dev) {
		unregister_netdev(dev);
		iounmap((void *) ((struct amd8111e_priv *)(dev->priv))->mmio);
		kfree(dev);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
	}
}

static int __devinit amd8111e_probe_one(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	int err,i,pm_cap;
	unsigned long reg_addr,reg_len;
	struct amd8111e_priv* lp;
	struct net_device* dev;
	unsigned int chip_version;

	err = pci_enable_device(pdev);
	if(err){
		printk(KERN_ERR "amd8111e: Cannot enable new PCI device,"
			"exiting.\n");
		return err;
	}

	if(!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)){
		printk(KERN_ERR "amd8111e: Cannot find PCI base address"
		       "exiting.\n");
		err = -ENODEV;
		goto err_disable_pdev;
	}

	err = pci_request_regions(pdev, MODULE_NAME);
	if(err){
		printk(KERN_ERR "amd8111e: Cannot obtain PCI resources, "
		       "exiting.\n");
		goto err_disable_pdev;
	}

	pci_set_master(pdev);

	/* Find power-management capability. */
	if((pm_cap = pci_find_capability(pdev, PCI_CAP_ID_PM))==0){
		printk(KERN_ERR "amd8111e: No Power Management capability, "
		       "exiting.\n");
		goto err_free_reg;
	}

	/* Initialize DMA */
	if(!pci_dma_supported(pdev, 0xffffffff)){
		printk(KERN_ERR "amd8111e: DMA not supported,"
			"exiting.\n");
		goto  err_free_reg;
	} else
		pdev->dma_mask = 0xffffffff;
	
	reg_addr = pci_resource_start(pdev, 0);
	reg_len = pci_resource_len(pdev, 0);

	dev = alloc_etherdev(sizeof(struct amd8111e_priv));
	if (!dev) {
		printk(KERN_ERR "amd8111e: Etherdev alloc failed, exiting.\n");
		err = -ENOMEM;
		goto err_free_reg;
	}

	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

#if AMD8111E_VLAN_TAG_USED
	dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX ;
	dev->vlan_rx_register =amd8111e_vlan_rx_register;
	dev->vlan_rx_kill_vid = amd8111e_vlan_rx_kill_vid;
#endif	
	
	lp = dev->priv;
	memset (lp, 0, sizeof (*lp));
	lp->pci_dev = pdev;
	lp->amd8111e_net_dev = dev;
	lp->pm_cap = pm_cap;

	spin_lock_init(&lp->lock);

	lp->mmio = ioremap(reg_addr, reg_len);
	if (lp->mmio == 0) {
		printk(KERN_ERR "amd8111e: Cannot map device registers, "
		       "exiting\n");
		err = -ENOMEM;
		goto err_free_dev;
	}
	
	/* Initializing MAC address */
	for(i = 0; i < ETH_ADDR_LEN; i++)
			dev->dev_addr[i] =readb(lp->mmio + PADR + i);
	/* Setting user defined speed */
	if (speed_duplex[card_idx] > sizeof(speed_duplex_mapping))
		lp->ext_phy_option = XPHYANE;
	else
		lp->ext_phy_option = 
				speed_duplex_mapping[speed_duplex[card_idx]];
	/* Initialize driver entry points */
	dev->open = amd8111e_open;
	dev->hard_start_xmit = amd8111e_start_xmit;
	dev->stop = amd8111e_close;
	dev->get_stats = amd8111e_get_stats;
	dev->set_multicast_list = amd8111e_set_multicast_list;
	dev->do_ioctl = amd8111e_ioctl;
	dev->change_mtu = amd8111e_change_mtu;
	dev->irq =pdev->irq;

#if AMD8111E_VLAN_TAG_USED
	dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
	dev->vlan_rx_register =amd8111e_vlan_rx_register;
	dev->vlan_rx_kill_vid = amd8111e_vlan_rx_kill_vid;
#endif	
	
	/* Set receive buffer length and set jumbo option*/
	amd8111e_set_rx_buff_len(dev);
	

	/* dev->tx_timeout = tg3_tx_timeout; */
	/* dev->watchdog_timeo = TG3_TX_TIMEOUT; */

	err = register_netdev(dev);
	if (err) {
		printk(KERN_ERR "amd8111e: Cannot register net device, "
		       "exiting.\n");
		goto err_iounmap;
	}

	pci_set_drvdata(pdev, dev);
	
	/*  display driver and device information */

    chip_version = (readl(lp->mmio + CHIPID) & 0xf0000000)>>28;
    printk("%s: AMD-8111e Driver Version: %s\n",dev->name,MODULE_VERSION);
    printk("%s: [ Rev %x ] PCI 10/100BaseT Ethernet ", dev->name, chip_version);
    for (i = 0; i < 6; i++)
	printk("%2.2x%c", dev->dev_addr[i],i == 5 ? ' ' : ':');
     printk("\n");	
	return 0;
err_iounmap:
	iounmap((void *) lp->mmio);

err_free_dev:
	kfree(dev);

err_free_reg:
	pci_release_regions(pdev);

err_disable_pdev:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	return err;

}

static struct pci_driver amd8111e_driver = {
	name:		MODULE_NAME,
	id_table:	amd8111e_pci_tbl,
	probe:		amd8111e_probe_one,
	remove:		__devexit_p(amd8111e_remove_one),
	suspend:	amd8111e_suspend,
	resume:		amd8111e_resume
};

static int __init amd8111e_init(void)
{
	return pci_module_init(&amd8111e_driver);
}

static void __exit amd8111e_cleanup(void)
{
	pci_unregister_driver(&amd8111e_driver);
}

module_init(amd8111e_init);
module_exit(amd8111e_cleanup);
