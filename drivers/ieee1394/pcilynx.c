/*
 * ti_pcilynx.c - Texas Instruments PCILynx driver
 * Copyright (C) 1999,2000 Andreas Bombe <andreas.bombe@munich.netsurf.de>,
 *                         Stephan Linz <linz@mazet.de>
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"
#include "pcilynx.h"


#if MAX_PCILYNX_CARDS > PCILYNX_MINOR_ROM_START
#error Max number of cards is bigger than PCILYNX_MINOR_ROM_START - this does not work.
#endif

/* print general (card independent) information */
#define PRINT_G(level, fmt, args...) printk(level "pcilynx: " fmt "\n" , ## args)
/* print card specific information */
#define PRINT(level, card, fmt, args...) printk(level "pcilynx%d: " fmt "\n" , card , ## args)

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
#define PRINT_GD(level, fmt, args...) printk(level "pcilynx: " fmt "\n" , ## args)
#define PRINTD(level, card, fmt, args...) printk(level "pcilynx%d: " fmt "\n" , card , ## args)
#else
#define PRINT_GD(level, fmt, args...) do {} while (0)
#define PRINTD(level, card, fmt, args...) do {} while (0)
#endif


static struct ti_lynx cards[MAX_PCILYNX_CARDS];
static int num_of_cards = 0;
static struct hpsb_host_template lynx_template;

/*
 * PCL handling functions.
 */

static pcl_t alloc_pcl(struct ti_lynx *lynx)
{
        u8 m;
        int i, j;

        spin_lock(&lynx->lock);
        /* FIXME - use ffz() to make this readable */
        for (i = 0; i < (LOCALRAM_SIZE / 1024); i++) {
                m = lynx->pcl_bmap[i];
                for (j = 0; j < 8; j++) {
                        if (m & 1<<j) {
                                continue;
                        }
                        m |= 1<<j;
                        lynx->pcl_bmap[i] = m;
                        spin_unlock(&lynx->lock);
                        return 8 * i + j;
                }
        }
        spin_unlock(&lynx->lock);

        return -1;
}


#if 0
static void free_pcl(struct ti_lynx *lynx, pcl_t pclid)
{
        int off, bit;

        off = pclid / 8;
        bit = pclid % 8;

        if (pclid < 0) {
                return;
        }

        spin_lock(&lynx->lock);
        if (lynx->pcl_bmap[off] & 1<<bit) {
                lynx->pcl_bmap[off] &= ~(1<<bit);
        } else {
                PRINT(KERN_ERR, lynx->id, 
                      "attempted to free unallocated PCL %d", pclid);
        }
        spin_unlock(&lynx->lock);
}

/* functions useful for debugging */        
static void pretty_print_pcl(const struct ti_pcl *pcl)
{
        int i;

        printk("PCL next %08x, userdata %08x, status %08x, remtrans %08x, nextbuf %08x\n",
               pcl->next, pcl->user_data, pcl->pcl_status, 
               pcl->remaining_transfer_count, pcl->next_data_buffer);

        printk("PCL");
        for (i=0; i<13; i++) {
                printk(" c%x:%08x d%x:%08x",
                       i, pcl->buffer[i].control, i, pcl->buffer[i].pointer);
                if (!(i & 0x3) && (i != 12)) printk("\nPCL");
        }
        printk("\n");
}
        
static void print_pcl(const struct ti_lynx *lynx, pcl_t pclid)
{
        struct ti_pcl pcl;

        get_pcl(lynx, pclid, &pcl);
        pretty_print_pcl(&pcl);
}
#endif


static int add_card(struct pci_dev *dev, const struct pci_device_id *devid);
static void remove_card(struct pci_dev *dev);



/***********************************
 * IEEE-1394 functionality section *
 ***********************************/


static int get_phy_reg(struct ti_lynx *lynx, int addr)
{
        int retval;
        int i = 0;

        unsigned long flags;

        if (addr > 15) {
                PRINT(KERN_ERR, lynx->id, __FUNCTION__
                      ": PHY register address %d out of range", addr);
                return -1;
        }

        spin_lock_irqsave(&lynx->phy_reg_lock, flags);

        reg_write(lynx, LINK_PHY, LINK_PHY_READ | LINK_PHY_ADDR(addr));
        do {
                retval = reg_read(lynx, LINK_PHY);

                if (i > 10000) {
                        PRINT(KERN_ERR, lynx->id, __FUNCTION__ 
                              ": runaway loop, aborting");
                        retval = -1;
                        break;
                }
                i++;
        } while ((retval & 0xf00) != LINK_PHY_RADDR(addr));

        reg_write(lynx, LINK_INT_STATUS, LINK_INT_PHY_REG_RCVD);
        spin_unlock_irqrestore(&lynx->phy_reg_lock, flags);

        if (retval != -1) {
                return retval & 0xff;
        } else {
                return -1;
        }
}

static int set_phy_reg(struct ti_lynx *lynx, int addr, int val)
{
        unsigned long flags;

        if (addr > 15) {
                PRINT(KERN_ERR, lynx->id, __FUNCTION__
                      ": PHY register address %d out of range", addr);
                return -1;
        }

        if (val > 0xff) {
                PRINT(KERN_ERR, lynx->id, __FUNCTION__
                      ": PHY register value %d out of range", val);
                return -1;
        }

        spin_lock_irqsave(&lynx->phy_reg_lock, flags);

        reg_write(lynx, LINK_PHY, LINK_PHY_WRITE | LINK_PHY_ADDR(addr)
                  | LINK_PHY_WDATA(val));

        spin_unlock_irqrestore(&lynx->phy_reg_lock, flags);

        return 0;
}

static int sel_phy_reg_page(struct ti_lynx *lynx, int page)
{
        int reg;

        if (page > 7) {
                PRINT(KERN_ERR, lynx->id, __FUNCTION__
                      ": PHY page %d out of range", page);
                return -1;
        }

        reg = get_phy_reg(lynx, 7);
        if (reg != -1) {
                reg &= 0x1f;
                reg |= (page << 5);
                set_phy_reg(lynx, 7, reg);
                return 0;
        } else {
                return -1;
        }
}

#if 0 /* not needed at this time */
static int sel_phy_reg_port(struct ti_lynx *lynx, int port)
{
        int reg;

        if (port > 15) {
                PRINT(KERN_ERR, lynx->id, __FUNCTION__
                      ": PHY port %d out of range", port);
                return -1;
        }

        reg = get_phy_reg(lynx, 7);
        if (reg != -1) {
                reg &= 0xf0;
                reg |= port;
                set_phy_reg(lynx, 7, reg);
                return 0;
        } else {
                return -1;
        }
}
#endif

static u32 get_phy_vendorid(struct ti_lynx *lynx)
{
        u32 pvid = 0;
        sel_phy_reg_page(lynx, 1);
        pvid |= (get_phy_reg(lynx, 10) << 16);
        pvid |= (get_phy_reg(lynx, 11) << 8);
        pvid |= get_phy_reg(lynx, 12);
        PRINT(KERN_INFO, lynx->id, "PHY vendor id 0x%06x", pvid);
        return pvid;
}

static u32 get_phy_productid(struct ti_lynx *lynx)
{
        u32 id = 0;
        sel_phy_reg_page(lynx, 1);
        id |= (get_phy_reg(lynx, 13) << 16);
        id |= (get_phy_reg(lynx, 14) << 8);
        id |= get_phy_reg(lynx, 15);
        PRINT(KERN_INFO, lynx->id, "PHY product id 0x%06x", id);
        return id;
}

static quadlet_t generate_own_selfid(struct ti_lynx *lynx,
                                     struct hpsb_host *host)
{
        quadlet_t lsid;
        char phyreg[7];
        int i;

        phyreg[0] = lynx->phy_reg0;
        for (i = 1; i < 7; i++) {
                phyreg[i] = get_phy_reg(lynx, i);
        }

        /* FIXME? We assume a TSB21LV03A phy here.  This code doesn't support
           more than 3 ports on the PHY anyway. */

        lsid = 0x80400000 | ((phyreg[0] & 0xfc) << 22);
        lsid |= (phyreg[1] & 0x3f) << 16; /* gap count */
        lsid |= (phyreg[2] & 0xc0) << 8; /* max speed */
        lsid |= (phyreg[6] & 0x01) << 11; /* contender (phy dependent) */
        /* lsid |= 1 << 11; *//* set contender (hack) */
        lsid |= (phyreg[6] & 0x10) >> 3; /* initiated reset */

        for (i = 0; i < (phyreg[2] & 0xf); i++) { /* ports */
                if (phyreg[3 + i] & 0x4) {
                        lsid |= (((phyreg[3 + i] & 0x8) | 0x10) >> 3)
                                << (6 - i*2);
                } else {
                        lsid |= 1 << (6 - i*2);
                }
        }

        cpu_to_be32s(&lsid);
        PRINT(KERN_DEBUG, lynx->id, "generated own selfid 0x%x", lsid);
        return lsid;
}

static void handle_selfid(struct ti_lynx *lynx, struct hpsb_host *host)
{
        quadlet_t *q = lynx->rcv_page;
        int phyid, isroot, size;
        quadlet_t lsid = 0;
        int i;

        if (lynx->phy_reg0 == -1 || lynx->selfid_size == -1) return;

        size = lynx->selfid_size;
        phyid = lynx->phy_reg0;

        i = (size > 16 ? 16 : size) / 4 - 1;
        while (i >= 0) {
                cpu_to_be32s(&q[i]);
                i--;
        }
        
        if (!lynx->phyic.reg_1394a) {
                lsid = generate_own_selfid(lynx, host);
        }

        isroot = (phyid & 2) != 0;
        phyid >>= 2;
        PRINT(KERN_INFO, lynx->id, "SelfID process finished (phyid %d, %s)",
              phyid, (isroot ? "root" : "not root"));
        reg_write(lynx, LINK_ID, (0xffc0 | phyid) << 16);

        if (!lynx->phyic.reg_1394a && !size) {
                hpsb_selfid_received(host, lsid);
        }

        while (size > 0) {
                struct selfid *sid = (struct selfid *)q;

                if (!lynx->phyic.reg_1394a && !sid->extended 
                    && (sid->phy_id == (phyid + 1))) {
                        hpsb_selfid_received(host, lsid);
                }

                if (q[0] == ~q[1]) {
                        PRINT(KERN_DEBUG, lynx->id, "SelfID packet 0x%x rcvd",
                              q[0]);
                        hpsb_selfid_received(host, q[0]);
                } else {
                        PRINT(KERN_INFO, lynx->id,
                              "inconsistent selfid 0x%x/0x%x", q[0], q[1]);
                }
                q += 2;
                size -= 8;
        }

        if (!lynx->phyic.reg_1394a && isroot && phyid != 0) {
                hpsb_selfid_received(host, lsid);
        }

        hpsb_selfid_complete(host, phyid, isroot);

        if (host->in_bus_reset) return;

        if (isroot) reg_set_bits(lynx, LINK_CONTROL, LINK_CONTROL_CYCMASTER);
        reg_set_bits(lynx, LINK_CONTROL,
                     LINK_CONTROL_RCV_CMP_VALID | LINK_CONTROL_TX_ASYNC_EN
                     | LINK_CONTROL_RX_ASYNC_EN | LINK_CONTROL_CYCTIMEREN);
}



/* This must be called with the respective queue_lock held. */
static void send_next(struct ti_lynx *lynx, int what)
{
        struct ti_pcl pcl;
        struct lynx_send_data *d;
        struct hpsb_packet *packet;

        d = (what == hpsb_iso ? &lynx->iso_send : &lynx->async);
        packet = d->queue;

        d->header_dma = pci_map_single(lynx->dev, packet->header,
                                       packet->header_size, PCI_DMA_TODEVICE);
        if (packet->data_size) {
                d->data_dma = pci_map_single(lynx->dev, packet->data,
                                             packet->data_size,
                                             PCI_DMA_TODEVICE);
        } else {
                d->data_dma = 0;
        }

        pcl.next = PCL_NEXT_INVALID;
        pcl.async_error_next = PCL_NEXT_INVALID;
#ifdef __BIG_ENDIAN
        pcl.buffer[0].control = packet->speed_code << 14 | packet->header_size;
#else
        pcl.buffer[0].control = packet->speed_code << 14 | packet->header_size 
                | PCL_BIGENDIAN;
#endif
        pcl.buffer[0].pointer = d->header_dma;
        pcl.buffer[1].control = PCL_LAST_BUFF | packet->data_size;
        pcl.buffer[1].pointer = d->data_dma;

        switch (packet->type) {
        case hpsb_async:
                pcl.buffer[0].control |= PCL_CMD_XMT;
                break;
        case hpsb_iso:
                pcl.buffer[0].control |= PCL_CMD_XMT | PCL_ISOMODE;
                break;
        case hpsb_raw:
                pcl.buffer[0].control |= PCL_CMD_UNFXMT;
                break;
        }                

        if (!packet->data_be) {
                pcl.buffer[1].control |= PCL_BIGENDIAN;
        }

        put_pcl(lynx, d->pcl, &pcl);
        run_pcl(lynx, d->pcl_start, d->channel);
}


#if 0
static int lynx_detect(struct hpsb_host_template *tmpl)
{
        struct hpsb_host *host;
        int i;

        init_driver();

        for (i = 0; i < num_of_cards; i++) {
                host = hpsb_get_host(tmpl, 0);
                if (host == NULL) {
                        /* simply don't init more after out of mem */
                        return i;
                }
                host->hostdata = &cards[i];
                cards[i].host = host;
        }

        return num_of_cards;
}
#endif

static int lynx_initialize(struct hpsb_host *host)
{
        struct ti_lynx *lynx = host->hostdata;
        struct ti_pcl pcl;
        int i;
        u32 *pcli;

        lynx->selfid_size = -1;
        lynx->phy_reg0 = -1;

        lynx->async.queue = NULL;
        
        pcl.next = pcl_bus(lynx, lynx->rcv_pcl);
        put_pcl(lynx, lynx->rcv_pcl_start, &pcl);
        
        pcl.next = PCL_NEXT_INVALID;
        pcl.async_error_next = PCL_NEXT_INVALID;
#ifdef __BIG_ENDIAN
        pcl.buffer[0].control = PCL_CMD_RCV | 16;
        pcl.buffer[1].control = PCL_LAST_BUFF | 4080;
#else
        pcl.buffer[0].control = PCL_CMD_RCV | PCL_BIGENDIAN | 16;
        pcl.buffer[1].control = PCL_LAST_BUFF | 4080;
#endif
        pcl.buffer[0].pointer = lynx->rcv_page_dma;
        pcl.buffer[1].pointer = lynx->rcv_page_dma + 16;
        put_pcl(lynx, lynx->rcv_pcl, &pcl);
        
        pcl.next = pcl_bus(lynx, lynx->async.pcl);
        pcl.async_error_next = pcl_bus(lynx, lynx->async.pcl);
        put_pcl(lynx, lynx->async.pcl_start, &pcl);

        pcl.next = pcl_bus(lynx, lynx->iso_send.pcl);
        pcl.async_error_next = PCL_NEXT_INVALID;
        put_pcl(lynx, lynx->iso_send.pcl_start, &pcl);

        pcl.next = PCL_NEXT_INVALID;
        pcl.async_error_next = PCL_NEXT_INVALID;
        pcl.buffer[0].control = PCL_CMD_RCV | 4;
#ifndef __BIG_ENDIAN
        pcl.buffer[0].control |= PCL_BIGENDIAN;
#endif
        pcl.buffer[1].control = PCL_LAST_BUFF | 2044;

        for (i = 0; i < NUM_ISORCV_PCL; i++) {
                int page = i / ISORCV_PER_PAGE;
                int sec = i % ISORCV_PER_PAGE;

                pcl.buffer[0].pointer = lynx->iso_rcv.page_dma[page] 
                        + sec * MAX_ISORCV_SIZE;
                pcl.buffer[1].pointer = pcl.buffer[0].pointer + 4;
                put_pcl(lynx, lynx->iso_rcv.pcl[i], &pcl);
        }

        pcli = (u32 *)&pcl;
        for (i = 0; i < NUM_ISORCV_PCL; i++) {
                pcli[i] = pcl_bus(lynx, lynx->iso_rcv.pcl[i]);
        }
        put_pcl(lynx, lynx->iso_rcv.pcl_start, &pcl);

        /* FIFO sizes from left to right: ITF=48 ATF=48 GRF=160 */
        reg_write(lynx, FIFO_SIZES, 0x003030a0);
        /* 20 byte threshold before triggering PCI transfer */
        reg_write(lynx, DMA_GLOBAL_REGISTER, 0x2<<24);
        /* threshold on both send FIFOs before transmitting:
           FIFO size - cache line size - 1 */
        i = reg_read(lynx, PCI_LATENCY_CACHELINE) & 0xff;
        i = 0x30 - i - 1;
        reg_write(lynx, FIFO_XMIT_THRESHOLD, (i << 8) | i);
        
        reg_set_bits(lynx, PCI_INT_ENABLE, PCI_INT_1394);

        reg_write(lynx, LINK_INT_ENABLE, LINK_INT_PHY_TIMEOUT
                  | LINK_INT_PHY_REG_RCVD  | LINK_INT_PHY_BUSRESET
                  | LINK_INT_ISO_STUCK     | LINK_INT_ASYNC_STUCK 
                  | LINK_INT_SENT_REJECT   | LINK_INT_TX_INVALID_TC
                  | LINK_INT_GRF_OVERFLOW  | LINK_INT_ITF_UNDERFLOW
                  | LINK_INT_ATF_UNDERFLOW);
        
        reg_write(lynx, DMA_WORD0_CMP_VALUE(CHANNEL_ASYNC_RCV), 0);
        reg_write(lynx, DMA_WORD0_CMP_ENABLE(CHANNEL_ASYNC_RCV), 0xa<<4);
        reg_write(lynx, DMA_WORD1_CMP_VALUE(CHANNEL_ASYNC_RCV), 0);
        reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ASYNC_RCV),
                  DMA_WORD1_CMP_MATCH_LOCAL_NODE | DMA_WORD1_CMP_MATCH_BROADCAST
                  | DMA_WORD1_CMP_MATCH_EXACT    | DMA_WORD1_CMP_MATCH_BUS_BCAST
                  | DMA_WORD1_CMP_ENABLE_SELF_ID | DMA_WORD1_CMP_ENABLE_MASTER);

        run_pcl(lynx, lynx->rcv_pcl_start, CHANNEL_ASYNC_RCV);

        reg_write(lynx, DMA_WORD0_CMP_VALUE(CHANNEL_ISO_RCV), 0);
        reg_write(lynx, DMA_WORD0_CMP_ENABLE(CHANNEL_ISO_RCV), 0x9<<4);
        reg_write(lynx, DMA_WORD1_CMP_VALUE(CHANNEL_ISO_RCV), 0);
        reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ISO_RCV), 0);

        run_sub_pcl(lynx, lynx->iso_rcv.pcl_start, 0, CHANNEL_ISO_RCV);

        reg_write(lynx, LINK_CONTROL, LINK_CONTROL_RCV_CMP_VALID
                  | LINK_CONTROL_TX_ISO_EN   | LINK_CONTROL_RX_ISO_EN
                  | LINK_CONTROL_TX_ASYNC_EN | LINK_CONTROL_RX_ASYNC_EN
                  | LINK_CONTROL_RESET_TX    | LINK_CONTROL_RESET_RX);

        if (!lynx->phyic.reg_1394a) {
                /* attempt to enable contender bit -FIXME- would this work
                 * elsewhere? */
                reg_set_bits(lynx, GPIO_CTRL_A, 0x1);
                reg_write(lynx, GPIO_DATA_BASE + 0x3c, 0x1); 
        } else {
                /* set the contender bit in the extended PHY register
                 * set. (Should check that bis 0,1,2 (=0xE0) is set
                 * in register 2?)
                 */
                i = get_phy_reg(lynx, 4);
                if (i != -1) set_phy_reg(lynx, 4, i | 0x40);
        }

        return 1;
}

static void lynx_release(struct hpsb_host *host)
{
        struct ti_lynx *lynx;
        
        if (host != NULL) {
                lynx = host->hostdata;
                remove_card(lynx->dev);
        } else {
#ifdef CONFIG_IEEE1394_PCILYNX_PORTS
                unregister_chrdev(PCILYNX_MAJOR, PCILYNX_DRIVER_NAME);
#endif
        }
}

static int lynx_transmit(struct hpsb_host *host, struct hpsb_packet *packet)
{
        struct ti_lynx *lynx = host->hostdata;
        struct lynx_send_data *d;
        unsigned long flags;

        if (packet->data_size >= 4096) {
                PRINT(KERN_ERR, lynx->id, "transmit packet data too big (%Zd)",
                      packet->data_size);
                return 0;
        }

        switch (packet->type) {
        case hpsb_async:
        case hpsb_raw:
                d = &lynx->async;
                break;
        case hpsb_iso:
                d = &lynx->iso_send;
                break;
        default:
                PRINT(KERN_ERR, lynx->id, "invalid packet type %d",
                      packet->type);
                return 0;
        }

        packet->xnext = NULL;
        if (packet->tcode == TCODE_WRITEQ
            || packet->tcode == TCODE_READQ_RESPONSE) {
                cpu_to_be32s(&packet->header[3]);
        }

        spin_lock_irqsave(&d->queue_lock, flags);

        if (d->queue == NULL) {
                d->queue = packet;
                d->queue_last = packet;
                send_next(lynx, packet->type);
        } else {
                d->queue_last->xnext = packet;
                d->queue_last = packet;
        }

        spin_unlock_irqrestore(&d->queue_lock, flags);

        return 1;
}

static int lynx_devctl(struct hpsb_host *host, enum devctl_cmd cmd, int arg)
{
        struct ti_lynx *lynx = host->hostdata;
        int retval = 0;
        struct hpsb_packet *packet, *lastpacket;
        unsigned long flags;

        switch (cmd) {
        case RESET_BUS:
                if (reg_read(lynx, LINK_INT_STATUS) & LINK_INT_PHY_BUSRESET) {
                        retval = 0;
                        break;
                }

                if (arg) {
                        arg = 3 << 6;
                } else {
                        arg = 1 << 6;
                }

                retval = get_phy_reg(lynx, 1);
                arg |= (retval == -1 ? 63 : retval);
                retval = 0;

                PRINT(KERN_INFO, lynx->id, "resetting bus on request%s",
                      (host->attempt_root ? " and attempting to become root"
                       : ""));

                lynx->selfid_size = -1;
                lynx->phy_reg0 = -1;
                set_phy_reg(lynx, 1, arg);
                break;

        case GET_CYCLE_COUNTER:
                retval = reg_read(lynx, CYCLE_TIMER);
                break;
                
        case SET_CYCLE_COUNTER:
                reg_write(lynx, CYCLE_TIMER, arg);
                break;

        case SET_BUS_ID:
                reg_write(lynx, LINK_ID, 
                          (arg << 22) | (reg_read(lynx, LINK_ID) & 0x003f0000));
                break;
                
        case ACT_CYCLE_MASTER:
                if (arg) {
                        reg_set_bits(lynx, LINK_CONTROL,
                                     LINK_CONTROL_CYCMASTER);
                } else {
                        reg_clear_bits(lynx, LINK_CONTROL,
                                       LINK_CONTROL_CYCMASTER);
                }
                break;

        case CANCEL_REQUESTS:
                spin_lock_irqsave(&lynx->async.queue_lock, flags);

                reg_write(lynx, DMA_CHAN_CTRL(CHANNEL_ASYNC_SEND), 0);
                packet = lynx->async.queue;
                lynx->async.queue = NULL;

                spin_unlock_irqrestore(&lynx->async.queue_lock, flags);

                while (packet != NULL) {
                        lastpacket = packet;
                        packet = packet->xnext;
                        hpsb_packet_sent(host, lastpacket, ACKX_ABORTED);
                }

                break;

        case MODIFY_USAGE:
                if (arg) {
                        MOD_INC_USE_COUNT;
                } else {
                        MOD_DEC_USE_COUNT;
                }
                break;

        case ISO_LISTEN_CHANNEL:
                spin_lock_irqsave(&lynx->iso_rcv.lock, flags);
                
                if (lynx->iso_rcv.chan_count++ == 0) {
                        reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ISO_RCV),
                                  DMA_WORD1_CMP_ENABLE_MASTER);
                }

                spin_unlock_irqrestore(&lynx->iso_rcv.lock, flags);
                break;

        case ISO_UNLISTEN_CHANNEL:
                spin_lock_irqsave(&lynx->iso_rcv.lock, flags);

                if (--lynx->iso_rcv.chan_count == 0) {
                        reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ISO_RCV),
                                  0);
                }

                spin_unlock_irqrestore(&lynx->iso_rcv.lock, flags);
                break;

        default:
                PRINT(KERN_ERR, lynx->id, "unknown devctl command %d", cmd);
                retval = -1;
        }

        return retval;
}


/***************************************
 * IEEE-1394 functionality section END *
 ***************************************/

#ifdef CONFIG_IEEE1394_PCILYNX_PORTS
/* VFS functions for local bus / aux device access.  Access to those
 * is implemented as a character device instead of block devices
 * because buffers are not wanted for this.  Therefore llseek (from
 * VFS) can be used for these char devices with obvious effects.
 */
static int mem_open(struct inode*, struct file*);
static int mem_release(struct inode*, struct file*);
static unsigned int aux_poll(struct file*, struct poll_table_struct*);
static loff_t mem_llseek(struct file*, loff_t, int);
static ssize_t mem_read (struct file*, char*, size_t, loff_t*);
static ssize_t mem_write(struct file*, const char*, size_t, loff_t*);


static struct file_operations aux_ops = {
        OWNER_THIS_MODULE
        read:           mem_read,
        write:          mem_write,
        poll:           aux_poll,
        llseek:         mem_llseek,
        open:           mem_open,
        release:        mem_release,
};


static void aux_setup_pcls(struct ti_lynx *lynx)
{
        struct ti_pcl pcl;

        pcl.next = PCL_NEXT_INVALID;
        pcl.user_data = pcl_bus(lynx, lynx->dmem_pcl);
        put_pcl(lynx, lynx->dmem_pcl, &pcl);
}

static int mem_open(struct inode *inode, struct file *file)
{
        int cid = MINOR(inode->i_rdev);
        enum { t_rom, t_aux, t_ram } type;
        struct memdata *md;
        
        V22_COMPAT_MOD_INC_USE_COUNT;

        if (cid < PCILYNX_MINOR_AUX_START) {
                /* just for completeness */
                V22_COMPAT_MOD_DEC_USE_COUNT;
                return -ENXIO;
        } else if (cid < PCILYNX_MINOR_ROM_START) {
                cid -= PCILYNX_MINOR_AUX_START;
                if (cid >= num_of_cards || !cards[cid].aux_port) {
                        V22_COMPAT_MOD_DEC_USE_COUNT;
                        return -ENXIO;
                }
                type = t_aux;
        } else if (cid < PCILYNX_MINOR_RAM_START) {
                cid -= PCILYNX_MINOR_ROM_START;
                if (cid >= num_of_cards || !cards[cid].local_rom) {
                        V22_COMPAT_MOD_DEC_USE_COUNT;
                        return -ENXIO;
                }
                type = t_rom;
        } else {
                /* WARNING: Know what you are doing when opening RAM.
                 * It is currently used inside the driver! */
                cid -= PCILYNX_MINOR_RAM_START;
                if (cid >= num_of_cards || !cards[cid].local_ram) {
                        V22_COMPAT_MOD_DEC_USE_COUNT;
                        return -ENXIO;
                }
                type = t_ram;
        }

        md = (struct memdata *)kmalloc(sizeof(struct memdata), SLAB_KERNEL);
        if (md == NULL) {
                V22_COMPAT_MOD_DEC_USE_COUNT;
                return -ENOMEM;
        }

        md->lynx = &cards[cid];
        md->cid = cid;

        switch (type) {
        case t_rom:
                md->type = rom;
                break;
        case t_ram:
                md->type = ram;
                break;
        case t_aux:
                atomic_set(&md->aux_intr_last_seen,
                           atomic_read(&cards[cid].aux_intr_seen));
                md->type = aux;
                break;
        }

        file->private_data = md;

        return 0;
}

static int mem_release(struct inode *inode, struct file *file)
{
        struct memdata *md = (struct memdata *)file->private_data;

        kfree(md);

        V22_COMPAT_MOD_DEC_USE_COUNT;
        return 0;
}

static unsigned int aux_poll(struct file *file, poll_table *pt)
{
        struct memdata *md = (struct memdata *)file->private_data;
        int cid = md->cid;
        unsigned int mask;

        /* reading and writing is always allowed */
        mask = POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM;

        if (md->type == aux) {
                poll_wait(file, &cards[cid].aux_intr_wait, pt);

                if (atomic_read(&md->aux_intr_last_seen)
                    != atomic_read(&cards[cid].aux_intr_seen)) {
                        mask |= POLLPRI;
                        atomic_inc(&md->aux_intr_last_seen);
                }
        }

        return mask;
}

loff_t mem_llseek(struct file *file, loff_t offs, int orig)
{
        loff_t newoffs;

        switch (orig) {
        case 0:
                newoffs = offs;
                break;
        case 1:
                newoffs = offs + file->f_pos;
                break;
        case 2:
                newoffs = PCILYNX_MAX_MEMORY + 1 + offs;
                break;
        default:
                return -EINVAL;
        }

        if (newoffs < 0 || newoffs > PCILYNX_MAX_MEMORY + 1) return -EINVAL;

        file->f_pos = newoffs;
        return newoffs;
}

/* 
 * do not DMA if count is too small because this will have a serious impact 
 * on performance - the value 2400 was found by experiment and may not work
 * everywhere as good as here - use mem_mindma option for modules to change 
 */
short mem_mindma = 2400;
MODULE_PARM(mem_mindma, "h");

static ssize_t mem_dmaread(struct memdata *md, u32 physbuf, ssize_t count,
                           int offset)
{
        pcltmp_t pcltmp;
        struct ti_pcl *pcl;
        size_t retval;
        int i;
        DECLARE_WAITQUEUE(wait, current);

        count &= ~3;
        count = MIN(count, 53196);
        retval = count;

        if (reg_read(md->lynx, DMA_CHAN_CTRL(CHANNEL_LOCALBUS))
            & DMA_CHAN_CTRL_BUSY) {
                PRINT(KERN_WARNING, md->lynx->id, "DMA ALREADY ACTIVE!");
        }

        reg_write(md->lynx, LBUS_ADDR, md->type | offset);

        pcl = edit_pcl(md->lynx, md->lynx->dmem_pcl, &pcltmp);
        pcl->buffer[0].control = PCL_CMD_LBUS_TO_PCI | MIN(count, 4092);
        pcl->buffer[0].pointer = physbuf;
        count -= 4092;

        i = 0;
        while (count > 0) {
                i++;
                pcl->buffer[i].control = MIN(count, 4092);
                pcl->buffer[i].pointer = physbuf + i * 4092;
                count -= 4092;
        }
        pcl->buffer[i].control |= PCL_LAST_BUFF;
        commit_pcl(md->lynx, md->lynx->dmem_pcl, &pcltmp);

        set_current_state(TASK_INTERRUPTIBLE);
        add_wait_queue(&md->lynx->mem_dma_intr_wait, &wait);
        run_sub_pcl(md->lynx, md->lynx->dmem_pcl, 2, CHANNEL_LOCALBUS);

        schedule();
        while (reg_read(md->lynx, DMA_CHAN_CTRL(CHANNEL_LOCALBUS))
               & DMA_CHAN_CTRL_BUSY) {
                if (signal_pending(current)) {
                        retval = -EINTR;
                        break;
                }
                schedule();
        }

        reg_write(md->lynx, DMA_CHAN_CTRL(CHANNEL_LOCALBUS), 0);
        remove_wait_queue(&md->lynx->mem_dma_intr_wait, &wait);

        if (reg_read(md->lynx, DMA_CHAN_CTRL(CHANNEL_LOCALBUS))
            & DMA_CHAN_CTRL_BUSY) {
                PRINT(KERN_ERR, md->lynx->id, "DMA STILL ACTIVE!");
        }

        return retval;
}

static ssize_t mem_read(struct file *file, char *buffer, size_t count,
                        loff_t *offset)
{
        struct memdata *md = (struct memdata *)file->private_data;
        ssize_t bcount;
        size_t alignfix;
        int off = (int)*offset; /* avoid useless 64bit-arithmetic */
        ssize_t retval;
        void *membase;

        if ((off + count) > PCILYNX_MAX_MEMORY + 1) {
                count = PCILYNX_MAX_MEMORY + 1 - off;
        }
        if (count == 0) {
                return 0;
        }


        switch (md->type) {
        case rom:
                membase = md->lynx->local_rom;
                break;
        case ram:
                membase = md->lynx->local_ram;
                break;
        case aux:
                membase = md->lynx->aux_port;
                break;
        default:
                panic("pcilynx%d: unsupported md->type %d in " __FUNCTION__,
                      md->lynx->id, md->type);
        }

        down(&md->lynx->mem_dma_mutex);

        if (count < mem_mindma) {
                memcpy_fromio(md->lynx->mem_dma_buffer, membase+off, count);
                goto out;
        }

        bcount = count;
        alignfix = 4 - (off % 4);
        if (alignfix != 4) {
                if (bcount < alignfix) {
                        alignfix = bcount;
                }
                memcpy_fromio(md->lynx->mem_dma_buffer, membase+off,
                              alignfix);
                if (bcount == alignfix) {
                        goto out;
                }
                bcount -= alignfix;
                off += alignfix;
        }

        while (bcount >= 4) {
                retval = mem_dmaread(md, md->lynx->mem_dma_buffer_dma
                                     + count - bcount, bcount, off);
                if (retval < 0) return retval;

                bcount -= retval;
                off += retval;
        }

        if (bcount) {
                memcpy_fromio(md->lynx->mem_dma_buffer + count - bcount,
                              membase+off, bcount);
        }

 out:
        retval = copy_to_user(buffer, md->lynx->mem_dma_buffer, count);
        up(&md->lynx->mem_dma_mutex);

        if (retval < 0) return retval;
        *offset += count;
        return count;
}


static ssize_t mem_write(struct file *file, const char *buffer, size_t count, 
                         loff_t *offset)
{
        struct memdata *md = (struct memdata *)file->private_data;

        if (((*offset) + count) > PCILYNX_MAX_MEMORY+1) {
                count = PCILYNX_MAX_MEMORY+1 - *offset;
        }
        if (count == 0 || *offset > PCILYNX_MAX_MEMORY) {
                return -ENOSPC;
        }

        /* FIXME: dereferencing pointers to PCI mem doesn't work everywhere */
        switch (md->type) {
        case aux:
                copy_from_user(md->lynx->aux_port+(*offset), buffer, count);
                break;
        case ram:
                copy_from_user(md->lynx->local_ram+(*offset), buffer, count);
                break;
        case rom:
                /* the ROM may be writeable */
                copy_from_user(md->lynx->local_rom+(*offset), buffer, count);
                break;
        }

        file->f_pos += count;
        return count;
}
#endif /* CONFIG_IEEE1394_PCILYNX_PORTS */


/********************************************************
 * Global stuff (interrupt handler, init/shutdown code) *
 ********************************************************/


static void lynx_irq_handler(int irq, void *dev_id,
                             struct pt_regs *regs_are_unused)
{
        struct ti_lynx *lynx = (struct ti_lynx *)dev_id;
        struct hpsb_host *host = lynx->host;
        u32 intmask;
        u32 linkint;

        linkint = reg_read(lynx, LINK_INT_STATUS);
        intmask = reg_read(lynx, PCI_INT_STATUS);

        PRINTD(KERN_DEBUG, lynx->id, "interrupt: 0x%08x / 0x%08x", intmask,
               linkint);

        if (!(intmask & PCI_INT_INT_PEND)) return;

        reg_write(lynx, LINK_INT_STATUS, linkint);
        reg_write(lynx, PCI_INT_STATUS, intmask);

#ifdef CONFIG_IEEE1394_PCILYNX_PORTS
        if (intmask & PCI_INT_AUX_INT) {
                atomic_inc(&lynx->aux_intr_seen);
                wake_up_interruptible(&lynx->aux_intr_wait);
        }

        if (intmask & PCI_INT_DMA_HLT(CHANNEL_LOCALBUS)) {
                wake_up_interruptible(&lynx->mem_dma_intr_wait);
        }
#endif


        if (intmask & PCI_INT_1394) {
                if (linkint & LINK_INT_PHY_TIMEOUT) {
                        PRINT(KERN_INFO, lynx->id, "PHY timeout occurred");
                }
                if (linkint & LINK_INT_PHY_BUSRESET) {
                        PRINT(KERN_INFO, lynx->id, "bus reset interrupt");
                        lynx->selfid_size = -1;
                        lynx->phy_reg0 = -1;
                        if (!host->in_bus_reset)
                                hpsb_bus_reset(host);
                }
                if (linkint & LINK_INT_PHY_REG_RCVD) {
                        u32 reg;

                        spin_lock(&lynx->phy_reg_lock);
                        reg = reg_read(lynx, LINK_PHY);
                        spin_unlock(&lynx->phy_reg_lock);

                        if (!host->in_bus_reset) {
                                PRINT(KERN_INFO, lynx->id,
                                      "phy reg received without reset");
                        } else if (reg & 0xf00) {
                                PRINT(KERN_INFO, lynx->id,
                                      "unsolicited phy reg %d received",
                                      (reg >> 8) & 0xf);
                        } else {
                                lynx->phy_reg0 = reg & 0xff;
                                handle_selfid(lynx, host);
                        }
                }
                if (linkint & LINK_INT_ISO_STUCK) {
                        PRINT(KERN_INFO, lynx->id, "isochronous transmitter stuck");
                }
                if (linkint & LINK_INT_ASYNC_STUCK) {
                        PRINT(KERN_INFO, lynx->id, "asynchronous transmitter stuck");
                }
                if (linkint & LINK_INT_SENT_REJECT) {
                        PRINT(KERN_INFO, lynx->id, "sent reject");
                }
                if (linkint & LINK_INT_TX_INVALID_TC) {
                        PRINT(KERN_INFO, lynx->id, "invalid transaction code");
                }
                if (linkint & LINK_INT_GRF_OVERFLOW) {
                        /* flush FIFO if overflow happens during reset */
                        if (host->in_bus_reset)
                                reg_write(lynx, FIFO_CONTROL,
                                          FIFO_CONTROL_GRF_FLUSH);
                        PRINT(KERN_INFO, lynx->id, "GRF overflow");
                }
                if (linkint & LINK_INT_ITF_UNDERFLOW) {
                        PRINT(KERN_INFO, lynx->id, "ITF underflow");
                }
                if (linkint & LINK_INT_ATF_UNDERFLOW) {
                        PRINT(KERN_INFO, lynx->id, "ATF underflow");
                }
        }

        if (intmask & PCI_INT_DMA_HLT(CHANNEL_ISO_RCV)) {
                PRINTD(KERN_DEBUG, lynx->id, "iso receive");

                spin_lock(&lynx->iso_rcv.lock);

                lynx->iso_rcv.stat[lynx->iso_rcv.next] =
                        reg_read(lynx, DMA_CHAN_STAT(CHANNEL_ISO_RCV));

                lynx->iso_rcv.used++;
                lynx->iso_rcv.next = (lynx->iso_rcv.next + 1) % NUM_ISORCV_PCL;

                if ((lynx->iso_rcv.next == lynx->iso_rcv.last)
                    || !lynx->iso_rcv.chan_count) {
                        PRINTD(KERN_DEBUG, lynx->id, "stopped");
                        reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ISO_RCV), 0);
                }

                run_sub_pcl(lynx, lynx->iso_rcv.pcl_start, lynx->iso_rcv.next,
                            CHANNEL_ISO_RCV);

                spin_unlock(&lynx->iso_rcv.lock);

		tasklet_schedule(&lynx->iso_rcv.tq);
        }

        if (intmask & PCI_INT_DMA_HLT(CHANNEL_ASYNC_SEND)) {
                u32 ack;
                struct hpsb_packet *packet;
                
                spin_lock(&lynx->async.queue_lock);

                ack = reg_read(lynx, DMA_CHAN_STAT(CHANNEL_ASYNC_SEND));
                packet = lynx->async.queue;
                lynx->async.queue = packet->xnext;

                pci_unmap_single(lynx->dev, lynx->async.header_dma,
                                 packet->header_size, PCI_DMA_TODEVICE);
                if (packet->data_size) {
                        pci_unmap_single(lynx->dev, lynx->async.data_dma,
                                         packet->data_size, PCI_DMA_TODEVICE);
                }

                if (lynx->async.queue != NULL) {
                        send_next(lynx, hpsb_async);
                }

                spin_unlock(&lynx->async.queue_lock);

                if (ack & DMA_CHAN_STAT_SPECIALACK) {
                        ack = (ack >> 15) & 0xf;
                        PRINTD(KERN_INFO, lynx->id, "special ack %d", ack);
                        ack = (ack == 1 ? ACKX_TIMEOUT : ACKX_SEND_ERROR);
                } else {
                        ack = (ack >> 15) & 0xf;
                }
                
                hpsb_packet_sent(host, packet, ack);
        }

        if (intmask & PCI_INT_DMA_HLT(CHANNEL_ISO_SEND)) {
                struct hpsb_packet *packet;

                spin_lock(&lynx->iso_send.queue_lock);

                packet = lynx->iso_send.queue;
                lynx->iso_send.queue = packet->xnext;

                pci_unmap_single(lynx->dev, lynx->iso_send.header_dma,
                                 packet->header_size, PCI_DMA_TODEVICE);
                if (packet->data_size) {
                        pci_unmap_single(lynx->dev, lynx->iso_send.data_dma,
                                         packet->data_size, PCI_DMA_TODEVICE);
                }

                if (lynx->iso_send.queue != NULL) {
                        send_next(lynx, hpsb_iso);
                }

                spin_unlock(&lynx->iso_send.queue_lock);

                hpsb_packet_sent(host, packet, ACK_COMPLETE);
        }

        if (intmask & PCI_INT_DMA_HLT(CHANNEL_ASYNC_RCV)) {
                /* general receive DMA completed */
                int stat = reg_read(lynx, DMA_CHAN_STAT(CHANNEL_ASYNC_RCV));

                PRINTD(KERN_DEBUG, lynx->id, "received packet size %d",
                       stat & 0x1fff); 

                if (stat & DMA_CHAN_STAT_SELFID) {
                        lynx->selfid_size = stat & 0x1fff;
                        handle_selfid(lynx, host);
                } else {
                        quadlet_t *q_data = lynx->rcv_page;
                        if ((*q_data >> 4 & 0xf) == TCODE_READQ_RESPONSE
                            || (*q_data >> 4 & 0xf) == TCODE_WRITEQ) {
                                cpu_to_be32s(q_data + 3);
                        }
                        hpsb_packet_received(host, q_data, stat & 0x1fff, 0);
                }

                run_pcl(lynx, lynx->rcv_pcl_start, CHANNEL_ASYNC_RCV);
        }
}

static void iso_rcv_bh(struct ti_lynx *lynx)
{
        unsigned int idx;
        quadlet_t *data;
        unsigned long flags;

        spin_lock_irqsave(&lynx->iso_rcv.lock, flags);

        while (lynx->iso_rcv.used) {
                idx = lynx->iso_rcv.last;
                spin_unlock_irqrestore(&lynx->iso_rcv.lock, flags);

                data = lynx->iso_rcv.page[idx / ISORCV_PER_PAGE]
                        + (idx % ISORCV_PER_PAGE) * MAX_ISORCV_SIZE;

                if ((*data >> 16) + 4 != (lynx->iso_rcv.stat[idx] & 0x1fff)) {
                        PRINT(KERN_ERR, lynx->id,
                              "iso length mismatch 0x%08x/0x%08x", *data,
                              lynx->iso_rcv.stat[idx]);
                }

                if (lynx->iso_rcv.stat[idx] 
                    & (DMA_CHAN_STAT_PCIERR | DMA_CHAN_STAT_PKTERR)) {
                        PRINT(KERN_INFO, lynx->id,
                              "iso receive error on %d to 0x%p", idx, data);
                } else {
                        hpsb_packet_received(lynx->host, data,
                                             lynx->iso_rcv.stat[idx] & 0x1fff,
                                             0);
                }

                spin_lock_irqsave(&lynx->iso_rcv.lock, flags);
                lynx->iso_rcv.last = (idx + 1) % NUM_ISORCV_PCL;
                lynx->iso_rcv.used--;
        }

        if (lynx->iso_rcv.chan_count) {
                reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ISO_RCV),
                          DMA_WORD1_CMP_ENABLE_MASTER);
        }
        spin_unlock_irqrestore(&lynx->iso_rcv.lock, flags);
}


static int __devinit add_card(struct pci_dev *dev,
                              const struct pci_device_id *devid)
{
#define FAIL(fmt, args...) do { \
        PRINT_G(KERN_ERR, fmt , ## args); \
        num_of_cards--; \
        remove_card(dev); \
        return -1; \
        } while (0)

        struct ti_lynx *lynx; /* shortcut to currently handled device */
        unsigned int i;

        if (num_of_cards == MAX_PCILYNX_CARDS) {
                PRINT_G(KERN_WARNING, "cannot handle more than %d cards.  "
                        "Adjust MAX_PCILYNX_CARDS in pcilynx.h.",
                        MAX_PCILYNX_CARDS);
                return -1;
        }

        lynx = &cards[num_of_cards++];

        if (pci_set_dma_mask(dev, 0xffffffff))
                FAIL("DMA address limits not supported for PCILynx hardware %d",
                     lynx->id);
        if (pci_enable_device(dev))
                FAIL("failed to enable PCILynx hardware %d", lynx->id);
        pci_set_master(dev);

        lynx->host = hpsb_get_host(&lynx_template, 0);
        if (!lynx->host)
                FAIL("failed to allocate host structure");

        lynx->state = have_host_struct;
	lynx->host->hostdata = lynx;
        lynx->id = num_of_cards-1;
        lynx->dev = dev;
	lynx->host->pdev = dev;

        lynx->lock = SPIN_LOCK_UNLOCKED;
        lynx->phy_reg_lock = SPIN_LOCK_UNLOCKED;

#ifndef CONFIG_IEEE1394_PCILYNX_LOCALRAM
        lynx->pcl_mem = pci_alloc_consistent(dev, LOCALRAM_SIZE,
                                             &lynx->pcl_mem_dma);

        if (lynx->pcl_mem != NULL) {
                lynx->state = have_pcl_mem;
                PRINT(KERN_INFO, lynx->id, 
                      "allocated PCL memory %d Bytes @ 0x%p", LOCALRAM_SIZE,
                      lynx->pcl_mem);
        } else {
                FAIL("failed to allocate PCL memory area");
        }
#endif

#ifdef CONFIG_IEEE1394_PCILYNX_PORTS
        lynx->mem_dma_buffer = pci_alloc_consistent(dev, 65536,
                                                    &lynx->mem_dma_buffer_dma);
        if (lynx->mem_dma_buffer == NULL) {
                FAIL("failed to allocate DMA buffer for aux");
        }
        lynx->state = have_aux_buf;
#endif

        lynx->rcv_page = pci_alloc_consistent(dev, PAGE_SIZE,
                                              &lynx->rcv_page_dma);
        if (lynx->rcv_page == NULL) {
                FAIL("failed to allocate receive buffer");
        }
        lynx->state = have_1394_buffers;

        for (i = 0; i < ISORCV_PAGES; i++) {
                lynx->iso_rcv.page[i] =
                        pci_alloc_consistent(dev, PAGE_SIZE,
                                             &lynx->iso_rcv.page_dma[i]);
                if (lynx->iso_rcv.page[i] == NULL) {
                        FAIL("failed to allocate iso receive buffers");
                }
        }

        lynx->registers = ioremap_nocache(pci_resource_start(dev,0),
                                          PCILYNX_MAX_REGISTER);
        lynx->local_ram = ioremap(pci_resource_start(dev,1), PCILYNX_MAX_MEMORY);
        lynx->aux_port  = ioremap(pci_resource_start(dev,2), PCILYNX_MAX_MEMORY);
        lynx->local_rom = ioremap(pci_resource_start(dev,PCI_ROM_RESOURCE),
                                  PCILYNX_MAX_MEMORY);
        lynx->state = have_iomappings;

        if (lynx->registers == NULL) {
                FAIL("failed to remap registers - card not accessible");
        }

#ifdef CONFIG_IEEE1394_PCILYNX_LOCALRAM
        if (lynx->local_ram == NULL) {
                FAIL("failed to remap local RAM which is required for "
                     "operation");
        }
#endif

        reg_write(lynx, MISC_CONTROL, MISC_CONTROL_SWRESET);

        if (!request_irq(dev->irq, lynx_irq_handler, SA_SHIRQ,
                         PCILYNX_DRIVER_NAME, lynx)) {
                PRINT(KERN_INFO, lynx->id, "allocated interrupt %d", dev->irq);
                lynx->state = have_intr;
        } else {
                FAIL("failed to allocate shared interrupt %d", dev->irq);
        }

        /* alloc_pcl return values are not checked, it is expected that the
         * provided PCL space is sufficient for the initial allocations */
#ifdef CONFIG_IEEE1394_PCILYNX_PORTS
        if (lynx->aux_port != NULL) {
                lynx->dmem_pcl = alloc_pcl(lynx);
                aux_setup_pcls(lynx);
                sema_init(&lynx->mem_dma_mutex, 1);
        }
#endif
        lynx->rcv_pcl = alloc_pcl(lynx);
        lynx->rcv_pcl_start = alloc_pcl(lynx);
        lynx->async.pcl = alloc_pcl(lynx);
        lynx->async.pcl_start = alloc_pcl(lynx);
        lynx->iso_send.pcl = alloc_pcl(lynx);
        lynx->iso_send.pcl_start = alloc_pcl(lynx);

        for (i = 0; i < NUM_ISORCV_PCL; i++) {
                lynx->iso_rcv.pcl[i] = alloc_pcl(lynx);
        }
        lynx->iso_rcv.pcl_start = alloc_pcl(lynx);

        /* all allocations successful - simple init stuff follows */

        reg_write(lynx, PCI_INT_ENABLE, PCI_INT_DMA_ALL);

#ifdef CONFIG_IEEE1394_PCILYNX_PORTS
        reg_set_bits(lynx, PCI_INT_ENABLE, PCI_INT_AUX_INT);
        init_waitqueue_head(&lynx->mem_dma_intr_wait);
        init_waitqueue_head(&lynx->aux_intr_wait);
#endif

	tasklet_init(&lynx->iso_rcv.tq, (void (*)(unsigned long))iso_rcv_bh,
		     (unsigned long)lynx);

        lynx->iso_rcv.lock = SPIN_LOCK_UNLOCKED;

        lynx->async.queue_lock = SPIN_LOCK_UNLOCKED;
        lynx->async.channel = CHANNEL_ASYNC_SEND;
        lynx->iso_send.queue_lock = SPIN_LOCK_UNLOCKED;
        lynx->iso_send.channel = CHANNEL_ISO_SEND;
        
        PRINT(KERN_INFO, lynx->id, "remapped memory spaces reg 0x%p, rom 0x%p, "
              "ram 0x%p, aux 0x%p", lynx->registers, lynx->local_rom,
              lynx->local_ram, lynx->aux_port);

        /* now, looking for PHY register set */
        if ((get_phy_reg(lynx, 2) & 0xe0) == 0xe0) {
                lynx->phyic.reg_1394a = 1;
                PRINT(KERN_INFO, lynx->id,
                      "found 1394a conform PHY (using extended register set)");
                lynx->phyic.vendor = get_phy_vendorid(lynx);
                lynx->phyic.product = get_phy_productid(lynx);
        } else {
                lynx->phyic.reg_1394a = 0;
                PRINT(KERN_INFO, lynx->id, "found old 1394 PHY");
        }

	/* Tell the highlevel this host is ready */
	highlevel_add_one_host (lynx->host);

        return 0;
#undef FAIL
}

static void remove_card(struct pci_dev *dev)
{
        struct ti_lynx *lynx;
        int i;

        lynx = cards;
        while (lynx->dev != dev) lynx++;

        switch (lynx->state) {
        case have_intr:
                reg_write(lynx, PCI_INT_ENABLE, 0);
                free_irq(lynx->dev->irq, lynx);
        case have_iomappings:
                reg_write(lynx, MISC_CONTROL, MISC_CONTROL_SWRESET);
                iounmap(lynx->registers);
                iounmap(lynx->local_rom);
                iounmap(lynx->local_ram);
                iounmap(lynx->aux_port);
        case have_1394_buffers:
                for (i = 0; i < ISORCV_PAGES; i++) {
                        if (lynx->iso_rcv.page[i]) {
                                pci_free_consistent(lynx->dev, PAGE_SIZE,
                                                    lynx->iso_rcv.page[i],
                                                    lynx->iso_rcv.page_dma[i]);
                        }
                }
                pci_free_consistent(lynx->dev, PAGE_SIZE, lynx->rcv_page,
                                    lynx->rcv_page_dma);
        case have_aux_buf:
#ifdef CONFIG_IEEE1394_PCILYNX_PORTS
                pci_free_consistent(lynx->dev, 65536, lynx->mem_dma_buffer,
                                    lynx->mem_dma_buffer_dma);
#endif
        case have_pcl_mem:
#ifndef CONFIG_IEEE1394_PCILYNX_LOCALRAM
                pci_free_consistent(lynx->dev, LOCALRAM_SIZE, lynx->pcl_mem,
                                    lynx->pcl_mem_dma);
#endif
        case have_host_struct:
                /* FIXME - verify host freeing */
        case clear:;
                /* do nothing - already freed */
        }

	tasklet_kill(&lynx->iso_rcv.tq);

        lynx->state = clear;
}

#if 0
static int init_driver()
{
        struct pci_dev *dev = NULL;
        int success = 0;

        if (num_of_cards) {
                PRINT_G(KERN_DEBUG, __PRETTY_FUNCTION__ " called again");
                return 0;
        }

        PRINT_G(KERN_INFO, "looking for PCILynx cards");

        while ((dev = pci_find_device(PCI_VENDOR_ID_TI,
                                      PCI_DEVICE_ID_TI_PCILYNX, dev)) 
               != NULL) {
                if (add_card(dev) == 0) {
                        success = 1;
                }
        }

        if (success == 0) {
                PRINT_G(KERN_WARNING, "no operable PCILynx cards found");
                return -ENXIO;
        }

#ifdef CONFIG_IEEE1394_PCILYNX_PORTS
        if (register_chrdev(PCILYNX_MAJOR, PCILYNX_DRIVER_NAME, &aux_ops)) {
                PRINT_G(KERN_ERR, "allocation of char major number %d failed",
                        PCILYNX_MAJOR);
                return -EBUSY;
        }
#endif

        return 0;
}
#endif


static size_t get_lynx_rom(struct hpsb_host *host, const quadlet_t **ptr)
{
        *ptr = lynx_csr_rom;
        return sizeof(lynx_csr_rom);
}

static struct hpsb_host_template lynx_template = {
	name:             PCILYNX_DRIVER_NAME,
	initialize_host:  lynx_initialize,
	release_host:     lynx_release,
	get_rom:          get_lynx_rom,
	transmit_packet:  lynx_transmit,
	devctl:           lynx_devctl
};

static struct pci_device_id pci_table[] __devinitdata = {
	{
                vendor:     PCI_VENDOR_ID_TI,
                device:     PCI_DEVICE_ID_TI_PCILYNX,
                subvendor:  PCI_ANY_ID,
                subdevice:  PCI_ANY_ID,
	},
	{ }			/* Terminating entry */
};

static struct pci_driver lynx_pcidriver = {
        name:      PCILYNX_DRIVER_NAME,
        id_table:  pci_table,
        probe:     add_card,
        remove:    remove_card,
};

MODULE_AUTHOR("Andreas E. Bombe <andreas.bombe@munich.netsurf.de>");
MODULE_DESCRIPTION("driver for Texas Instruments PCI Lynx IEEE-1394 controller");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("pcilynx");
MODULE_DEVICE_TABLE(pci, pci_table);

static void __exit pcilynx_cleanup(void)
{
        hpsb_unregister_lowlevel(&lynx_template);
	pci_unregister_driver(&lynx_pcidriver);
        PRINT_G(KERN_INFO, "removed " PCILYNX_DRIVER_NAME " module");
}

static int __init pcilynx_init(void)
{
        int ret;

        if (hpsb_register_lowlevel(&lynx_template)) {
                PRINT_G(KERN_ERR, "registering failed");
                return -ENXIO;
        }

        ret = pci_module_init(&lynx_pcidriver);
        if (ret < 0) {
                PRINT_G(KERN_ERR, "PCI module init failed");
                hpsb_unregister_lowlevel(&lynx_template);
        }

        return ret;
}

module_init(pcilynx_init);
module_exit(pcilynx_cleanup);
