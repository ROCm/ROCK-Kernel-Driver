/*
 * WL3501 Wireless LAN PCMCIA Card Driver for Linux
 * Written originally for Linux 2.0.30 by Fox Chen, mhchen@golf.ccl.itri.org.tw
 * Ported to 2.2, 2.4 & 2.5 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 * Wireless extensions in 2.4 by Gustavo Niemeyer <niemeyer@conectiva.com>
 *
 * References:
 *
 *   1. WL24xx packet drivers (tooasm.asm)
 *   2. Access Point Firmware Interface Specification for IEEE 802.11 SUTRO
 *   3. IEEE 802.11
 *   4. Linux network driver (/usr/src/linux/drivers/net)
 *   5. ISA card driver - wl24.c
 *   6. Linux PCMCIA skeleton driver - skeleton.c
 *   7. Linux PCMCIA 3c589 network driver - 3c589_cs.c
 *
 * Tested with WL2400 firmware 1.2, Linux 2.0.30, and pcmcia-cs-2.9.12
 *   1. Performance: about 165 Kbytes/sec in TCP/IP with Ad-Hoc mode.
 *      rsh 192.168.1.3 "dd if=/dev/zero bs=1k count=1000" > /dev/null
 *      (Specification 2M bits/sec. is about 250 Kbytes/sec., but we must deduct
 *       ETHER/IP/UDP/TCP header, and acknowledgement overhead)
 *
 * Tested with Planet AP in 2.4.17, 184 KiB/s in UDP in Infrastructure mode,
 * 173 KiB/s in TCP.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/bitops.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/wireless.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include "wl3501.h"

/*
 * All the PCMCIA modules use PCMCIA_DEBUG to control debugging.  If you do not
 * define PCMCIA_DEBUG at all, all the debug code will be left out.  If you
 * compile with PCMCIA_DEBUG=0, the debug code will be present but disabled --
 * but it can then be enabled for specific modules at load time with a
 * 'pc_debug=#' option to insmod.
 */
#define PCMCIA_DEBUG 0
#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
MODULE_PARM(pc_debug, "i");
#define dprintk(n, format, args...) \
	{ if (pc_debug > (n)) \
		printk(KERN_INFO "%s: " format "\n", __FUNCTION__, ##args); }
#else
#define dprintk(n, format, args...)
#endif

static u8 wl3501_fpage[] = {
	[0] = WL3501_BSS_FPAGE0,
	[1] = WL3501_BSS_FPAGE1,
	[2] = WL3501_BSS_FPAGE2,
	[3] = WL3501_BSS_FPAGE3,
};

#define wl3501_outb(a, b) { outb(a, b); WL3501_SLOW_DOWN_IO; }
#define wl3501_outb_p(a, b) { outb_p(a, b); WL3501_SLOW_DOWN_IO; }
#define wl3501_outsb(a, b, c) { outsb(a, b, c); WL3501_SLOW_DOWN_IO; }

#define WL3501_RELEASE_TIMEOUT (25 * HZ)
#define WL3501_MAX_ADHOC_TRIES 16

/* Parameters that can be set with 'insmod' */
/* Bit map of interrupts to choose from */
/* This means pick from 15, 14, 12, 11, 10, 9, 7, 5, 4, and 3 */
static unsigned long wl3501_irq_mask = 0xdeb8;
static int wl3501_irq_list[4] = { -1 };

MODULE_PARM(wl3501_irq_mask, "i");
MODULE_PARM(wl3501_irq_list, "1-4i");
MODULE_AUTHOR("Fox Chen <mhchen@golf.ccl.itri.org.tw>, "
	      "Arnaldo Carvalho de Melo <acme@conectiva.com.br>,"
	      "Gustavo Niemeyer <niemeyer@conectiva.com>");
MODULE_DESCRIPTION("Planet wl3501 wireless driver");
MODULE_LICENSE("GPL");

/*
 * The event() function is this driver's Card Services event handler.  It will
 * be called by Card Services when an appropriate card status event is
 * received. The config() and release() entry points are used to configure or
 * release a socket, in response to card insertion and ejection events.  They
 * are invoked from the wl24 event handler.
 */
static void wl3501_config(dev_link_t *link);
static void wl3501_release(unsigned long arg);
static int wl3501_event(event_t event, int pri, event_callback_args_t *args);

/*
 * The dev_info variable is the "key" that is used to match up this
 * device driver with appropriate cards, through the card configuration
 * database.
 */
static dev_info_t wl3501_dev_info = "wl3501_cs";

/*
 * A linked list of "instances" of the wl24 device.  Each actual PCMCIA card
 * corresponds to one device instance, and is described by one dev_link_t
 * structure (defined in ds.h).
 *
 * You may not want to use a linked list for this -- for example, the memory
 * card driver uses an array of dev_link_t pointers, where minor device numbers
 * are used to derive the corresponding array index.
 */
static dev_link_t *wl3501_dev_list;

static __inline__ void wl3501_switch_page(struct wl3501_card *this, u8 page)
{
	wl3501_outb(page, this->base_addr + WL3501_NIC_BSS);
}

/*
 * Hold SUTRO. (i.e. make SUTRO stop)
 * Return: 1 if SUTRO is originally running
 */
static int wl3501_hold_sutro(struct wl3501_card *this)
{
	u8 old = inb(this->base_addr + WL3501_NIC_GCR);
	u8 new = (old & ~(WL3501_GCR_ECINT | WL3501_GCR_INT2EC)) |
		  WL3501_GCR_ECWAIT;

	wl3501_outb(new, this->base_addr + WL3501_NIC_GCR);
	return !(old & WL3501_GCR_ECWAIT);
}

/*
 * UnHold SUTRO. (i.e. make SUTRO running)
 * Return: 1 if SUTRO is originally running
 */
static int wl3501_unhold_sutro(struct wl3501_card *this)
{
	u8 old = inb(this->base_addr + WL3501_NIC_GCR);
	u8 new = old & (~(WL3501_GCR_ECINT | WL3501_GCR_INT2EC |
			WL3501_GCR_ECWAIT));

	wl3501_outb(new, this->base_addr + WL3501_NIC_GCR);
	return !(old & WL3501_GCR_ECWAIT);
}

/*
 * Get Ethernet MAC addresss.
 *
 * WARNING: We switch to FPAGE0 and switc back again.
 *          Making sure there is no other WL function beening called by ISR.
 */
static int wl3501_get_flash_mac_addr(struct wl3501_card *this)
{
	int base_addr = this->base_addr;

	/* get MAC addr */
	wl3501_outb(WL3501_BSS_FPAGE3, base_addr + WL3501_NIC_BSS); /* BSS */
	wl3501_outb(0x00, base_addr + WL3501_NIC_LMAL);	/* LMAL */
	wl3501_outb(0x40, base_addr + WL3501_NIC_LMAH);	/* LMAH */

	/* wait for reading EEPROM */
	WL3501_NOPLOOP(100);
	this->mac_addr.b0 = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->mac_addr.b1 = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->mac_addr.b2 = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->mac_addr.b3 = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->mac_addr.b4 = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->mac_addr.b5 = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->freq_domain = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	wl3501_outb(WL3501_BSS_FPAGE0, base_addr + WL3501_NIC_BSS);
	wl3501_outb(0x04, base_addr + WL3501_NIC_LMAL);
	wl3501_outb(0x40, base_addr + WL3501_NIC_LMAH);
	WL3501_NOPLOOP(100);
	this->version[0] = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->version[1] = inb(base_addr + WL3501_NIC_IODPA);
	/* switch to SRAM Page 0 (for safety) */
	wl3501_switch_page(this, WL3501_BSS_SPAGE0);

	/* The MAC addr should be 00:60:... */
	return this->mac_addr.b0 == 0x00 && this->mac_addr.b1 == 0x60;
}

static void wl3501_flash_outb(struct wl3501_card *this, u16 page, u16 addr,
			      u8 data)
{
	/* switch to Flash RAM Page page */
	wl3501_outb(wl3501_fpage[page], this->base_addr + WL3501_NIC_BSS);

	/* set LMAL and LMAH */
	wl3501_outb(addr & 0xff, this->base_addr + WL3501_NIC_LMAL);
	wl3501_outb(addr >> 8, this->base_addr + WL3501_NIC_LMAH);

	/* out data to Port A */
	wl3501_outb(data, this->base_addr + WL3501_NIC_IODPA);
}

static u8 wl3501_flash_inb(struct wl3501_card *this, u16 page, u16 addr)
{
	/* switch to Flash RAM Page page */
	wl3501_outb(wl3501_fpage[page], this->base_addr + WL3501_NIC_BSS);

	/* set LMAL and LMAH */
	wl3501_outb(addr & 0xff, this->base_addr + WL3501_NIC_LMAL);
	wl3501_outb(addr >> 8, this->base_addr + WL3501_NIC_LMAH);

	/* out data to Port A */
	return inb(this->base_addr + WL3501_NIC_IODPA);
}

/*
 * When calling this function, must hold SUTRO first.
 */
static u16 wl3501_get_flash_id(struct wl3501_card *this)
{
	u8 byte0, byte1;
	u16 id;

	/* Autoselect command */
	wl3501_flash_outb(this, 0, 0x5555, 0xaa);
	wl3501_flash_outb(this, 0, 0x2aaa, 0x55);
	wl3501_flash_outb(this, 0, 0x5555, 0x90);
	WL3501_NOPLOOP(10000);

	byte0 = wl3501_flash_inb(this, 0, 0x0);
	byte1 = wl3501_flash_inb(this, 0, 0x1);

	id = (byte0 << 8) | byte1;

	printk(KERN_INFO "Flash ROM ID = 0x%x\n", id);
	return id;
}

/*
 * Polling if Erase/Programming command is completed
 * Note: IF a == b THEN XOR(a,b) = 0
 *
 * When calling this function, must hold SUTRO first.
 */
static int wl3501_flash_write_ok(struct wl3501_card *this)
{
	u8 byte0, byte1;

	/* Check 'Toggle Bit' (DQ6) to see if completed */
	do {
		byte0 = wl3501_flash_inb(this, 0, 0x0);
		byte1 = wl3501_flash_inb(this, 0, 0x0);

		/* Test if exceeded Time Limits (DQ5) */
		if (byte1 & 0x20) {
			/* Must test DQ6 again before return 0 */
			byte0 = wl3501_flash_inb(this, 0, 0x0);
			byte1 = wl3501_flash_inb(this, 0, 0x0);

			return !((byte0 ^ byte1) & 0x40);
		}
	} while ((byte0 ^ byte1) & 0x40);

	return 1;
}

/*
 * When calling this function, must hold SUTRO first.
 */
static int wl3501_flash_erase_sector(struct wl3501_card *this, u16 sector)
{
	u16 page = sector / 2;
	u16 addr = (sector & 1) ? 0x4000 : 0x0;

	/* Sector Erase command (6 commands must within 100 uS) */
	wl3501_flash_outb(this, 0, 0x5555, 0xaa);
	wl3501_flash_outb(this, 0, 0x2aaa, 0x55);
	wl3501_flash_outb(this, 0, 0x5555, 0x80);
	wl3501_flash_outb(this, 0, 0x5555, 0xaa);
	wl3501_flash_outb(this, 0, 0x2aaa, 0x55);
	wl3501_flash_outb(this, page, addr, 0x30);

	return wl3501_flash_write_ok(this);
}

/*
 * When calling this function, must hold SUTRO first.
 */
int wl3501_flash_writeb(struct wl3501_card *this, u16 page, u16 addr,
			u8 data)
{
	/* Autoselect command */
	wl3501_flash_outb(this, 0, 0x5555, 0xaa);
	wl3501_flash_outb(this, 0, 0x2aaa, 0x55);
	wl3501_flash_outb(this, 0, 0x5555, 0xa0);
	wl3501_flash_outb(this, page, addr, data);
	return wl3501_flash_write_ok(this);
}

/**
 * wl3501_write_flash -  Write mibExtra into flash ROM
 * @this - card
 * @bf - buffer to write
 * @len = buffer length
 *
 * In fact, only first 29 bytes are used. (not all of extra MIB)
 * To prevent alter other data will be used by future versions, we preserve
 * 256 bytes.
 */
static int wl3501_write_flash(struct wl3501_card *this, unsigned char *bf,
			      int len)
{
	int i;
	u32 bf_addr = *(u32 *)bf;
	int running = wl3501_hold_sutro(this);
	u16 flash_id = wl3501_get_flash_id(this);

	bf += 4;
	len -= 4;

	if (flash_id == 0x0120) {
		/* It's AMD AM29F010, must be erase before programming */
		/* Erase 1st 16 Kbytes within Page 0 */
		if (!(bf_addr & 0x3fff)) {
			if (!wl3501_flash_erase_sector(this, bf_addr >> 14)) {
				printk(KERN_WARNING
				       "wl3501_flash_erase_sector(0) failed\n");
				if (running)
					wl3501_unhold_sutro(this);
				return 0;
			}
		}
	} else if (flash_id != 0xf51d) {
		/* It's not AT29C010A */
		printk(KERN_WARNING "Flash ROM type (0x%x) is unknown!\n",
		       flash_id);
		if (running)
			wl3501_unhold_sutro(this);
		return 0;
	}

	/* Programming flash ROM byte by byte */
	for (i = 0; i < len; i++) {
		if (!wl3501_flash_writeb(this, bf_addr >> 15, i, *bf)) {
			printk(KERN_WARNING
			       "wl3501_flash_writeb(buf[%d]) failed!\n", i);
			if (running)
				wl3501_unhold_sutro(this);
			return 0;
		}
		bf++;
	}
#if 0
	/* Reset command */
	wl3501_outb(this, 0, 0x5555, 0xaa);
	wl3501_outb(this, 0, 0x2aaa, 0x55);
	wl3501_outb(this, 0, 0x5555, 0xf0);
	WL3501_NOPLOOP(10000);

	if (running)
		wl3501_unhold_sutro(this);
#endif
	return 1;
}

/**
 * wl3501_set_to_wla - Move 'size' bytes from PC to card
 * @dest: Card addressing space
 * @src: PC addressing space
 * @size: Bytes to move
 *
 * Move 'size' bytes from PC to card. (Shouldn't be interrupted)
 */
void wl3501_set_to_wla(struct wl3501_card *this, u16 dest, void *src, int size)
{
	/* switch to SRAM Page 0 */
	wl3501_switch_page(this, (dest & 0x8000) ? WL3501_BSS_SPAGE1 :
						   WL3501_BSS_SPAGE0);
	/* wl3501_outb(((dest>> 11) & 0x18), this->base_addr +
	  				     WL3501_NIC_BSS); */
	/* set LMAL and LMAH */
	wl3501_outb(dest & 0xff, this->base_addr + WL3501_NIC_LMAL);
	wl3501_outb(((dest >> 8) & 0x7f), this->base_addr + WL3501_NIC_LMAH);

	/* rep out to Port A */
	wl3501_outsb(this->base_addr + WL3501_NIC_IODPA, src, size);
}

/**
 * wl3501_get_from_wla - Move 'size' bytes from card to PC
 * @src: Card addressing space
 * @dest: PC addressing space
 * @size: Bytes to move
 *
 * Move 'size' bytes from card to PC. (Shouldn't be interrupted)
 */
void wl3501_get_from_wla(struct wl3501_card *this, u16 src, void *dest,
			 int size)
{
	/* switch to SRAM Page 0 */
	wl3501_switch_page(this, (src & 0x8000) ? WL3501_BSS_SPAGE1 :
						  WL3501_BSS_SPAGE0);
	/* wl3501_outb(((src>> 11) & 0x18), this->base_addr +
	  				    WL3501_NIC_BSS); */

	/* set LMAL and LMAH */
	wl3501_outb(src & 0xff, this->base_addr + WL3501_NIC_LMAL);
	wl3501_outb((src >> 8) & 0x7f, this->base_addr + WL3501_NIC_LMAH);

	/* rep get from Port A */
	insb(this->base_addr + WL3501_NIC_IODPA, dest, size);
}

/*
 * Get/Allocate a free Tx Data Buffer
 *
 *  *--------------*-----------------*----------------------------------*
 *  |    PLCP      |    MAC Header   |  DST  SRC         Data ...       |
 *  |  (24 bytes)  |    (30 bytes)   |  (6)  (6)  (Ethernet Row Data)   |
 *  *--------------*-----------------*----------------------------------*
 *  \               \- IEEE 802.11 -/ \------------ uDataLen -----------/
 *   \-struct wl3501_80211_tx_hdr--/   \-------- Ethernet Frame -------/
 *
 * Return = Postion in Card
 */
static u16 wl3501_get_tx_buffer(struct wl3501_card *this, u16 len)
{
	u16 next, blk_cnt = 0, zero = 0;
	u16 full_len = sizeof(struct wl3501_80211_tx_hdr) + len;
	u16 ret = 0;

	if (full_len > this->tx_buffer_cnt * 254)
		goto out;
	ret = this->tx_buffer_head;
	while (full_len) {
		if (full_len < 254)
			full_len = 0;
		else
			full_len -= 254;
		wl3501_get_from_wla(this, this->tx_buffer_head, &next,
				    sizeof(next));
		if (!full_len)
			wl3501_set_to_wla(this, this->tx_buffer_head, &zero,
					  sizeof(zero));
		this->tx_buffer_head = next;
		blk_cnt++;
		/* if buffer is not enough */
		if (!next && full_len) {
			this->tx_buffer_head = ret;
			ret = 0;
			goto out;
		}
	}
	this->tx_buffer_cnt -= blk_cnt;
out:
	return ret;
}

/*
 * Free an allocated Tx Buffer. ptr must be correct position.
 */
static void wl3501_free_tx_buffer(struct wl3501_card *this, u16 ptr)
{
	/* check if all space is not free */
	if (!this->tx_buffer_head)
		this->tx_buffer_head = ptr;
	else
		wl3501_set_to_wla(this, this->tx_buffer_tail,
				  &ptr, sizeof(ptr));
	while (ptr) {
		u16 next;

		this->tx_buffer_cnt++;
		wl3501_get_from_wla(this, ptr, &next, sizeof(next));
		this->tx_buffer_tail = ptr;
		ptr = next;
	}
}

static int wl3501_esbq_req_test(struct wl3501_card *this)
{
	u8 tmp;

	wl3501_get_from_wla(this, this->esbq_req_head + 3, &tmp, sizeof(tmp));
	return tmp & 0x80;
}

static void wl3501_esbq_req(struct wl3501_card *this, u16 * ptr)
{
	u16 tmp = 0;

	wl3501_set_to_wla(this, this->esbq_req_head, ptr, 2);
	wl3501_set_to_wla(this, this->esbq_req_head + 2, &tmp, sizeof(tmp));
	this->esbq_req_head += 4;
	if (this->esbq_req_head >= this->esbq_req_end)
		this->esbq_req_head = this->esbq_req_start;
}

/**
 * wl3501_send_pkt - Send a packet.
 * @this - card
 *
 * Send a packet.
 *
 * data = Ethernet raw frame.  (e.g. data[0] - data[5] is Dest MAC Addr,
 *                                    data[6] - data[11] is Src MAC Addr)
 * Ref: IEEE 802.11
 */
static int wl3501_send_pkt(struct wl3501_card *this, u8 *data, u16 len)
{
	u16 bf, sig_bf, next, tmplen, pktlen, tmp;
	struct wl3501_md_req sig;
	u8 *pdata = (char *)data;

	if (wl3501_esbq_req_test(this)) {
		sig_bf = wl3501_get_tx_buffer(this, sizeof(sig));
		if (!sig_bf)	/* No free buffer available */
			return -ENOMEM;
		bf = wl3501_get_tx_buffer(this, len + 26 + 24);
		if (!bf) {
			/* No free buffer available */
			wl3501_free_tx_buffer(this, sig_bf);
			return -ENOMEM;
		}
		memcpy((char *)&(sig.daddr[0]), pdata, 12);
		pktlen = len - 12;
		pdata += 12;
		sig.next_blk = 0;
		sig.sig_id = WL3501_SIG_MD_REQ;
		sig.data = bf;
		if (this->llc_type == 1) {
			if (((*pdata) * 256 + (*(pdata + 1))) > 1500) {
				sig.size = pktlen + 24 + 4 + 6;
				tmp = 0xaaaa;
				wl3501_set_to_wla(this, bf + 2 +
						  sizeof(struct wl3501_tx_hdr) -
						  6, &tmp, sizeof(tmp));
				tmp = 0x03;
				wl3501_set_to_wla(this, bf + 2 +
						  sizeof(struct wl3501_tx_hdr) -
						  4, &tmp, sizeof(tmp));
				tmp = 0x0;
				wl3501_set_to_wla(this, bf + 2 +
						  sizeof(struct wl3501_tx_hdr) -
						  2, &tmp, sizeof(tmp));
				if (pktlen >
				    (254 - sizeof(struct wl3501_tx_hdr))) {
					tmplen =
					    254 - sizeof(struct wl3501_tx_hdr);
					pktlen -= tmplen;
				} else {
					tmplen = pktlen;
					pktlen = 0;
				}
				wl3501_set_to_wla(this, bf + 2 +
						  sizeof(struct wl3501_tx_hdr),
						  pdata, tmplen);
				pdata += tmplen;
				wl3501_get_from_wla(this, bf, &next,
						    sizeof(next));
				bf = next;
			} else {
				sig.size = pktlen + 24 + 4 - 2;
				pdata += 2;
				pktlen -= 2;
				if (pktlen >
				    (254 - sizeof(struct wl3501_tx_hdr) + 6)) {
					tmplen = 254 -
					    sizeof(struct wl3501_tx_hdr) + 6;
					pktlen -= tmplen;
				} else {
					tmplen = pktlen;
					pktlen = 0;
				}
				wl3501_set_to_wla(this, bf + 2 +
						  sizeof(struct wl3501_tx_hdr) -
						  6, pdata, tmplen);
				pdata += tmplen;
				wl3501_get_from_wla(this, bf, &next,
						    sizeof(next));
				bf = next;
			}
		} else {
			pktlen += 12;
			sig.size = pktlen + 24 + 4;
			pdata -= 12;
			if (pktlen > (254 - sizeof(struct wl3501_tx_hdr) + 6)) {
				tmplen = 254 - sizeof(struct wl3501_tx_hdr) + 6;
				pktlen -= tmplen;
			} else {
				tmplen = pktlen;
				pktlen = 0;
			}
			wl3501_set_to_wla(this, bf + 2 +
					  sizeof(struct wl3501_tx_hdr) - 6,
					  pdata, tmplen);
			pdata += tmplen;
			wl3501_get_from_wla(this, bf, &next, sizeof(next));
			bf = next;
		}
		while (pktlen > 0) {
			if (pktlen > 254) {
				tmplen = 254;
				pktlen -= 254;
			} else {
				tmplen = pktlen;
				pktlen = 0;
			}
			wl3501_set_to_wla(this, bf + 2, pdata, tmplen);
			pdata += tmplen;
			wl3501_get_from_wla(this, bf, &next, sizeof(next));
			bf = next;
		}
		wl3501_set_to_wla(this, sig_bf, &sig, sizeof(sig));
		wl3501_esbq_req(this, &sig_bf);
	}
	return 0;
}

static int wl3501_mgmt_resync(struct wl3501_card *this)
{
	struct wl3501_resync_req signal;
	int rc = 1;

	signal.next_blk = 0;
	signal.sig_id = WL3501_SIG_RESYNC_REQ;

	if (wl3501_esbq_req_test(this)) {
		u16 ptr = wl3501_get_tx_buffer(this, sizeof(signal));
		if (ptr) {
			wl3501_set_to_wla(this, ptr, &signal, sizeof(signal));
			wl3501_esbq_req(this, &ptr);
			rc = 0;
		}
	}
	return rc;
}

static int wl3501_mgmt_scan(struct wl3501_card *this, u16 chan_time)
{
	struct wl3501_scan_req signal;
	int rc = 1;

	signal.next_blk = 0;
	signal.sig_id = WL3501_SIG_SCAN_REQ;
	signal.ssid[0] = 0;
	signal.ssid[1] = 0;
	signal.scan_type = WL3501_SCAN_TYPE_ACTIVE;
	signal.probe_delay = 0x10;
	signal.min_chan_time = chan_time;
	signal.max_chan_time = chan_time;

	if (this->net_type == WL3501_NET_TYPE_INFRASTRUCTURE)
		signal.bss_type = WL3501_NET_TYPE_INFRASTRUCTURE;
	else
		signal.bss_type = WL3501_NET_TYPE_INDEPENDENT;

	this->bss_cnt = this->join_sta_bss = 0;

	if (wl3501_esbq_req_test(this)) {
		u16 ptr = wl3501_get_tx_buffer(this, sizeof(signal));
		if (ptr) {
			wl3501_set_to_wla(this, ptr, &signal, sizeof(signal));
			wl3501_esbq_req(this, &ptr);
			rc = 0;
		}
	}
	return rc;
}

static int wl3501_mgmt_join(struct wl3501_card *this, u16 stas)
{
	struct wl3501_join_req signal;

	signal.next_blk = 0;
	signal.sig_id = WL3501_SIG_JOIN_REQ;
	signal.timeout = 10;
	memcpy((char *)&(signal.beacon_period),
	       (char *)&(this->bss_set[stas].beacon_period), 72);
	this->cap_info = signal.cap_info;
	this->chan = signal.phy_pset[2];

	if (wl3501_esbq_req_test(this)) {
		u16 ptr = wl3501_get_tx_buffer(this, sizeof(signal));
		if (ptr) {
			wl3501_set_to_wla(this, ptr, &signal, sizeof(signal));
			wl3501_esbq_req(this, &ptr);
			return 0;
		}
	}
	return 1;
}

static int wl3501_mgmt_start(struct wl3501_card *this)
{
	struct wl3501_start_req signal;
	int rc = 1;

	signal.next_blk = 0;
	signal.sig_id = WL3501_SIG_START_REQ;
	memcpy((char *)signal.ssid, (char *)this->essid, 34);
	memcpy((char *)this->keep_essid, (char *)this->essid, 34);
	signal.bss_type = WL3501_NET_TYPE_INDEPENDENT;
	signal.beacon_period = 400;
	signal.dtim_period = 1;
	signal.phy_pset[0] = 3;
	signal.phy_pset[1] = 1;
	signal.phy_pset[2] = this->chan;
	signal.cap_info = 0x02;
	signal.bss_basic_rate_set[0] = 0x01;
	signal.bss_basic_rate_set[1] = 0x02;
	signal.bss_basic_rate_set[2] = 0x82;
	signal.bss_basic_rate_set[3] = 0x84;
	signal.operational_rate_set[0] = 0x01;
	signal.operational_rate_set[1] = 0x02;
	signal.operational_rate_set[2] = 0x82;
	signal.operational_rate_set[3] = 0x84;
	signal.ibss_pset[0] = 6;
	signal.ibss_pset[1] = 2;
	signal.ibss_pset[2] = 10;
	signal.ibss_pset[3] = 0;

	if (wl3501_esbq_req_test(this)) {
		u16 ptr = wl3501_get_tx_buffer(this, sizeof(signal));
		if (ptr) {
			wl3501_set_to_wla(this, ptr, &signal, sizeof(signal));
			wl3501_esbq_req(this, &ptr);
			rc = 0;
		}
	}
	return rc;
}

static void wl3501_mgmt_scan_confirm(struct wl3501_card *this, u16 addr)
{
	u16 i, j;
	int matchflag = 0;
	struct wl3501_scan_confirm signal;

	wl3501_get_from_wla(this, addr, &signal, sizeof(signal));
	if (signal.status == WL3501_STATUS_SUCCESS) {
		if (this->driver_state == WL3501_SIG_SCAN_REQ) {
			for (i = 0; i < this->bss_cnt; i++) {
				if (!memcmp((char *)this->bss_set[i].bssid,
					    (char *)signal.bssid, ETH_ALEN))
					break;
			}
			if ((i == this->bss_cnt) && i < 20) {
				memcpy((char *)
				       &(this->bss_set[i].beacon_period),
				       (char *)&(signal.beacon_period), 73);
				this->bss_cnt++;
			}
		} else if ((this->net_type == WL3501_NET_TYPE_INFRASTRUCTURE &&
			    (signal.cap_info & 0x01)) ||
			   (this->net_type == WL3501_NET_TYPE_INDEPENDENT &&
			    (signal.cap_info & 0x02)) ||
			   this->net_type == WL3501_NET_TYPE_ANY_BSS) {
			if (!this->essid[1])
				matchflag = 1;
			else if (this->essid[1] == 3 &&
				 !strncmp((char *)&this->essid[2], "ANY", 3))
				matchflag = 1;
			else if (this->essid[1] != signal.ssid[1])
				matchflag = 0;
			else if (memcmp((char *)&(this->essid[2]),
					(char *)&(signal.ssid[2]),
					this->essid[1]))
				matchflag = 0;
			else
				matchflag = 1;
			if (matchflag) {
				for (i = 0; i < this->bss_cnt; i++) {
					if (!memcmp
					    ((char *)this->bss_set[i].bssid,
					     (char *)signal.bssid, ETH_ALEN)) {
						matchflag = 0;
						break;
					}
				}
			}
			if (matchflag && (i < 20)) {
				memcpy((char *)
				       &(this->bss_set[i].beacon_period),
				       (char *)&(signal.beacon_period), 73);
				this->bss_cnt++;
			}
		}
	} else if (signal.status == WL3501_STATUS_TIMEOUT &&
		   this->driver_state != WL3501_SIG_SCAN_REQ) {
		this->join_sta_bss = 0;
		for (j = this->join_sta_bss; j < this->bss_cnt; j++)
			if (!wl3501_mgmt_join(this, j))
				break;
		this->join_sta_bss = j;
		if (this->join_sta_bss == this->bss_cnt) {
			if (this->net_type == WL3501_NET_TYPE_INFRASTRUCTURE)
				wl3501_mgmt_scan(this, 100);
			else {
				this->adhoc_times++;
				if (this->adhoc_times > WL3501_MAX_ADHOC_TRIES)
					wl3501_mgmt_start(this);
				else
					wl3501_mgmt_scan(this, 100);
			}

		}
	}
}

/**
 * wl3501_block_interrupt - Mask interrupt from SUTRO
 * @this - card
 *
 * Mask interrupt from SUTRO. (i.e. SUTRO cannot interrupt the HOST)
 * Return: 1 if interrupt is originally enabled
 */
static int wl3501_block_interrupt(struct wl3501_card *this)
{
	u8 old = inb(this->base_addr + WL3501_NIC_GCR);
	u8 new = old & (~(WL3501_GCR_ECINT | WL3501_GCR_INT2EC |
			WL3501_GCR_ENECINT));

	wl3501_outb(new, this->base_addr + WL3501_NIC_GCR);
	return old & WL3501_GCR_ENECINT;
}

/**
 * wl3501_unblock_interrupt - Enable interrupt from SUTRO
 * @this - card
 *
 * Enable interrupt from SUTRO. (i.e. SUTRO can interrupt the HOST)
 * Return: 1 if interrupt is originally enabled
 */
static int wl3501_unblock_interrupt(struct wl3501_card *this)
{
	u8 old = inb(this->base_addr + WL3501_NIC_GCR);
	u8 new = (old & ~(WL3501_GCR_ECINT | WL3501_GCR_INT2EC)) |
		  WL3501_GCR_ENECINT;

	wl3501_outb(new, this->base_addr + WL3501_NIC_GCR);
	return old & WL3501_GCR_ENECINT;
}

/**
 * wl3501_receive - Receive data from Receive Queue.
 *
 * Receive data from Receive Queue.
 *
 * @this: card
 * @bf: address of host
 * @size: size of buffer.
 */
static u16 wl3501_receive(struct wl3501_card *this, u8 *bf, u16 size)
{
	u16 next_addr, next_addr1;
	u8 *data = bf + 12;

	size -= 12;
	wl3501_get_from_wla(this, this->start_seg + 2,
			    &next_addr, sizeof(next_addr));
	if (this->llc_type == 1) {
		if (this->ether_type == WL3501_PKT_TYPE_ETHERII) {
			if (size >
			    WL3501_BLKSZ - sizeof(struct wl3501_rx_hdr)) {
				wl3501_get_from_wla(this, this->start_seg +
						    sizeof(struct
							   wl3501_rx_hdr), data,
						    WL3501_BLKSZ -
						    sizeof(struct
							   wl3501_rx_hdr));
				size -=
				    WL3501_BLKSZ - sizeof(struct wl3501_rx_hdr);
				data +=
				    WL3501_BLKSZ - sizeof(struct wl3501_rx_hdr);
			} else {
				wl3501_get_from_wla(this, this->start_seg +
						    sizeof(struct
							   wl3501_rx_hdr), data,
						    size);
				size = 0;
			}
		} else {
			size -= 2;
			*data = (size >> 8) & 0xff;
			*(data + 1) = size & 0xff;
			data += 2;
			if (size >
			    WL3501_BLKSZ - sizeof(struct wl3501_rx_hdr) + 6) {
				wl3501_get_from_wla(this, this->start_seg +
						    sizeof(struct wl3501_rx_hdr)
						    - 6, data,
						    WL3501_BLKSZ -
						    sizeof(struct wl3501_rx_hdr)
						    + 6);
				size =
				    size - (WL3501_BLKSZ -
					    sizeof(struct wl3501_rx_hdr)) + 6;
				data +=
				    WL3501_BLKSZ -
				    sizeof(struct wl3501_rx_hdr) + 6;
			} else {
				wl3501_get_from_wla(this,
						    this->start_seg +
						    sizeof(struct wl3501_rx_hdr)
						    - 6, data, size);
				size = 0;
			}
		}
	} else {
		if (size > WL3501_BLKSZ - sizeof(struct wl3501_rx_hdr) - 6) {
			wl3501_get_from_wla(this, this->start_seg +
					    sizeof(struct wl3501_rx_hdr) + 6,
					    data, WL3501_BLKSZ -
					    sizeof(struct wl3501_rx_hdr) - 6);
			size = size - (WL3501_BLKSZ -
				       sizeof(struct wl3501_rx_hdr)) - 6;
			data +=
			    (WL3501_BLKSZ - sizeof(struct wl3501_rx_hdr) - 6);
		} else {
			wl3501_get_from_wla(this, this->start_seg +
					    sizeof(struct wl3501_rx_hdr) + 6,
					    data, size);
			size = 0;
		}
	}
	while (size > 0) {
		if (size > WL3501_BLKSZ - 5) {
			wl3501_get_from_wla(this, next_addr + 5, data,
					    WL3501_BLKSZ - 5);
			size -= (WL3501_BLKSZ - 5);
			data += (WL3501_BLKSZ - 5);
			wl3501_get_from_wla(this, next_addr + 2, &next_addr1,
					    sizeof(next_addr1));
			next_addr = next_addr1;
		} else {
			wl3501_get_from_wla(this, next_addr + 5, data, size);
			size = 0;
		}
	}
	return 0;
}

static void wl3501_esbq_req_free(struct wl3501_card *this)
{
	u8 tmp;
	u16 addr;

	if (this->esbq_req_head == this->esbq_req_tail)
		goto out;
	wl3501_get_from_wla(this, this->esbq_req_tail + 3, &tmp, sizeof(tmp));
	if (!(tmp & 0x80))
		goto out;
	wl3501_get_from_wla(this, this->esbq_req_tail, &addr, sizeof(addr));
	wl3501_free_tx_buffer(this, addr);
	this->esbq_req_tail += 4;
	if (this->esbq_req_tail >= this->esbq_req_end)
		this->esbq_req_tail = this->esbq_req_start;
out:
	return;
}

static int wl3501_esbq_confirm(struct wl3501_card *this)
{
	u8 tmp;

	wl3501_get_from_wla(this, this->esbq_confirm + 3, &tmp, sizeof(tmp));
	return tmp & 0x80;
}

static void wl3501_online(struct wl3501_card *this)
{
	this->card_start = 1;
	printk(KERN_INFO "Wireless LAN online. BSSID: "
	       "%02X %02X %02X %02X %02X %02X\n",
	       this->bssid.b0, this->bssid.b1, this->bssid.b2,
	       this->bssid.b3, this->bssid.b4, this->bssid.b5);
}

static void wl3501_esbq_confirm_done(struct wl3501_card *this)
{
	u8 tmp = 0;

	wl3501_set_to_wla(this, this->esbq_confirm + 3, &tmp, sizeof(tmp));
	this->esbq_confirm += 4;
	if (this->esbq_confirm >= this->esbq_confirm_end)
		this->esbq_confirm = this->esbq_confirm_start;
}

static int wl3501_mgmt_auth(struct wl3501_card *this)
{
	struct wl3501_auth_req signal;
	u16 ptr;

	signal.next_blk = 0;
	signal.sig_id = WL3501_SIG_AUTH_REQ;
	signal.type = WL3501_SYS_TYPE_OPEN;
	signal.timeout = 1000;
	memcpy((char *)&(signal.mac_addr), (char *)&(this->bssid), ETH_ALEN);
	if (wl3501_esbq_req_test(this)) {
		ptr = wl3501_get_tx_buffer(this, sizeof(signal));
		if (ptr) {
			wl3501_set_to_wla(this, ptr, &signal, sizeof(signal));
			wl3501_esbq_req(this, &ptr);
			return 0;
		}
	}
	return 1;
}

static int wl3501_mgmt_association(struct wl3501_card *this)
{
	struct wl3501_assoc_req signal;
	u16 ptr;

	signal.next_blk		= 0;
	signal.sig_id		= WL3501_SIG_ASSOC_REQ;
	signal.timeout		= 1000;
	signal.listen_interval	= 5;
	signal.cap_info		= this->cap_info;
	memcpy((char *)&(signal.mac_addr), (char *)&(this->bssid), ETH_ALEN);
	if (wl3501_esbq_req_test(this)) {
		ptr = wl3501_get_tx_buffer(this, sizeof(signal));
		if (ptr) {
			wl3501_set_to_wla(this, ptr, &signal, sizeof(signal));
			wl3501_esbq_req(this, &ptr);
			return 0;
		}
	}
	return 1;
}

static void wl3501_mgmt_join_confirm(struct net_device *dev,
				     struct wl3501_card *this, u16 addr)
{
	u16 i, j;
	struct wl3501_join_confirm sig;

	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));
	if (sig.status == WL3501_STATUS_SUCCESS) {
		if (this->net_type == WL3501_NET_TYPE_INFRASTRUCTURE) {
			if (this->join_sta_bss < this->bss_cnt) {
				i = this->join_sta_bss;
				memcpy((char *)&(this->bssid),
				       (char *)&(this->bss_set[i].bssid),
				       ETH_ALEN);
				this->chan = this->bss_set[i].phy_pset[2];
				memcpy((char *)this->keep_essid,
				       (char *)this->bss_set[i].ssid, 34);
				wl3501_mgmt_auth(this);
			}
		} else {
			i = this->join_sta_bss;
			memcpy((char *)&(this->bssid),
			       (char *)&(this->bss_set[i].bssid), ETH_ALEN);
			this->chan = this->bss_set[i].phy_pset[2];
			memcpy((char *)this->keep_essid,
			       (char *)this->bss_set[i].ssid, 34);
			wl3501_online(this);
		}
	} else {
		this->join_sta_bss++;
		for (j = this->join_sta_bss; j < this->bss_cnt; j++)
			if (!wl3501_mgmt_join(this, j))
				break;
		this->join_sta_bss = j;
		if (this->join_sta_bss == this->bss_cnt) {
			if (this->net_type == WL3501_NET_TYPE_INFRASTRUCTURE)
				wl3501_mgmt_scan(this, 100);
			else {
				this->adhoc_times++;
				if (this->adhoc_times > WL3501_MAX_ADHOC_TRIES)
					wl3501_mgmt_start(this);
				else
					wl3501_mgmt_scan(this, 100);
			}
		}
	}

	if (this->card_start)
		netif_wake_queue(dev);
	else
		netif_stop_queue(dev);
}

static inline void wl3501_alarm_interrupt(struct net_device *dev,
					  struct wl3501_card *this)
{
	if (this->net_type == WL3501_NET_TYPE_INFRASTRUCTURE) {
		printk(KERN_INFO "Wireless LAN offline\n");
		netif_stop_queue(dev);
		wl3501_mgmt_resync(this);
	}
}

static inline void wl3501_md_confirm_interrupt(struct net_device *dev,
					       struct wl3501_card *this,
					       u16 addr)
{
	struct wl3501_md_confirm sig;

	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));
	wl3501_free_tx_buffer(this, sig.data);
	netif_wake_queue(dev);
}

static inline void wl3501_md_ind_interrupt(struct net_device *dev,
					   struct wl3501_card *this, u16 addr)
{
	struct wl3501_md_ind sig;
	struct sk_buff *skb;
	u16 pkt_len;

	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));
	this->start_seg = sig.data;

	if (this->llc_type == 1) {
		u16 tmp;

		wl3501_get_from_wla(this,
				    sig.data + sizeof(struct wl3501_rx_hdr) - 6,
				    &tmp, sizeof(tmp));
		if (tmp == 0xaaaa) {
			pkt_len = sig.size + 12 - 24 - 4 - 6;
			this->ether_type = WL3501_PKT_TYPE_ETHERII;
		} else if (tmp == 0xe0e0) {
			pkt_len = sig.size + 12 - 24 - 4 + 2;
			this->ether_type = WL3501_PKT_TYPE_ETHER802_3E;
		} else {
			pkt_len = sig.size + 12 - 24 - 4 + 2;
			this->ether_type = WL3501_PKT_TYPE_ETHER802_3F;
		}
	} else
		pkt_len = sig.size - 24 - 4;

	skb = dev_alloc_skb(pkt_len + 5);

	if (!skb) {
		printk(KERN_WARNING "%s: Can't alloc a sk_buff of size %d.\n",
		       dev->name, pkt_len);
		this->stats.rx_dropped++;
	} else {
		skb->dev = dev;
		skb_reserve(skb, 2);	/* IP headers on 16 bytes
					   boundaries */
		eth_copy_and_sum(skb, (unsigned char *)&sig.daddr, 12, 0);
		wl3501_receive(this, skb->data, pkt_len);
		skb_put(skb, pkt_len);
		skb->protocol	= eth_type_trans(skb, dev);
		dev->last_rx	= jiffies;
		this->stats.rx_packets++;
		this->stats.rx_bytes += skb->len;
		netif_rx(skb);
	}
}

static inline void wl3501_get_confirm_interrupt(struct wl3501_card *this,
						u16 addr)
{
	struct wl3501_get_confirm sig;

	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));
#if 0
	if (!sig.mib_status) {
		switch (sig.mib_attrib) {
		case wl3501_mib_mac_addr:
			break;
		case wl3501_mib_current_reg_domain:
			break;
		}
	}
#endif
}

static inline void wl3501_start_confirm_interrupt(struct net_device *dev,
						  struct wl3501_card *this,
						  u16 addr)
{
	struct wl3501_start_confirm sig;

	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));
	this->card_start = sig.status == WL3501_STATUS_SUCCESS;
	if (this->card_start)
		netif_wake_queue(dev);
	else
		netif_stop_queue(dev);
}

static inline void wl3501_assoc_confirm_interrupt(struct net_device *dev,
						  struct wl3501_card *this,
						  u16 addr)
{
	struct wl3501_assoc_confirm sig;

	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));

	if (sig.status == WL3501_STATUS_SUCCESS)
		wl3501_online(this);
	else
		this->card_start = 0;

	if (this->card_start)
		netif_wake_queue(dev);
	else
		netif_stop_queue(dev);
}

static inline void wl3501_auth_confirm_interrupt(struct wl3501_card *this,
						 u16 addr)
{
	struct wl3501_auth_confirm sig;

	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));

	if (sig.status == WL3501_STATUS_SUCCESS)
		wl3501_mgmt_association(this);
	else
		wl3501_mgmt_resync(this);
}

static inline void wl3501_rx_interrupt(struct net_device *dev)
{
	int morepkts;
	u16 addr;
	u8 sig_id;
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
loop:
	morepkts = 0;
	if (!wl3501_esbq_confirm(this))
		goto free;
	wl3501_get_from_wla(this, this->esbq_confirm, &addr, sizeof(addr));
	wl3501_get_from_wla(this, addr + 2, &sig_id, sizeof(sig_id));

	switch (sig_id) {
	case WL3501_SIG_DEAUTH_IND:
	case WL3501_SIG_DISASSOC_IND:
	case WL3501_SIG_ALARM:
		wl3501_alarm_interrupt(dev, this);
		break;
	case WL3501_SIG_MD_CONFIRM:
		wl3501_md_confirm_interrupt(dev, this, addr);
		break;
	case WL3501_SIG_MD_IND:
		wl3501_md_ind_interrupt(dev, this, addr);
		break;
	case WL3501_SIG_GET_CONFIRM:
		wl3501_get_confirm_interrupt(this, addr);
		break;
	case WL3501_SIG_START_CONFIRM:
		wl3501_start_confirm_interrupt(dev, this, addr);
		break;
	case WL3501_SIG_SCAN_CONFIRM:
		wl3501_mgmt_scan_confirm(this, addr);
		break;
	case WL3501_SIG_JOIN_CONFIRM:
		wl3501_mgmt_join_confirm(dev, this, addr);
		break;
	case WL3501_SIG_ASSOC_CONFIRM:
		wl3501_assoc_confirm_interrupt(dev, this, addr);
		break;
	case WL3501_SIG_AUTH_CONFIRM:
		wl3501_auth_confirm_interrupt(this, addr);
		break;
	case WL3501_SIG_RESYNC_CONFIRM:
		wl3501_mgmt_resync(this); /* FIXME: should be resync_confirm */
		break;
	}
	wl3501_esbq_confirm_done(this);
	morepkts = 1;
	/* free request if necessary */
free:
	wl3501_esbq_req_free(this);
	if (morepkts)
		goto loop;
}

static inline void wl3501_ack_interrupt(struct wl3501_card *this)
{
	wl3501_outb(WL3501_GCR_ECINT, this->base_addr + WL3501_NIC_GCR);
}

/**
 * wl3501_interrupt - Hardware interrupt from card.
 * @irq - Interrupt number
 * @dev_id - net_device
 * @regs - registers
 *
 * We must acknowledge the interrupt as soon as possible, and block the
 * interrupt from the same card immediately to prevent re-entry.
 *
 * Before accessing the Control_Status_Block, we must lock SUTRO first.
 * On the other hand, to prevent SUTRO from malfunctioning, we must
 * unlock the SUTRO as soon as possible.
 */
static irqreturn_t wl3501_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct wl3501_card *this;
	int handled = 1;

	if (!dev)
		goto unknown;
	this = dev->priv;
	spin_lock(&this->lock);
	wl3501_ack_interrupt(this);
	wl3501_block_interrupt(this);
	wl3501_rx_interrupt(dev);
	wl3501_unblock_interrupt(this);
	spin_unlock(&this->lock);
out:
	return IRQ_RETVAL(handled);
unknown:
	handled = 0;
	printk(KERN_ERR "%s: irq %d for unknown device.\n", __FUNCTION__, irq);
	goto out;
}

static int wl3501_reset_board(struct wl3501_card *this)
{
	u8 tmp = 0;
	int i, rc = 0;

	/* Coreset */
	wl3501_outb_p(WL3501_GCR_CORESET, this->base_addr + WL3501_NIC_GCR);
	wl3501_outb_p(0, this->base_addr + WL3501_NIC_GCR);
	wl3501_outb_p(WL3501_GCR_CORESET, this->base_addr + WL3501_NIC_GCR);

	/* Reset SRAM 0x480 to zero */
	wl3501_set_to_wla(this, 0x480, &tmp, sizeof(tmp));

	/* Start up */
	wl3501_outb_p(0, this->base_addr + WL3501_NIC_GCR);

	WL3501_NOPLOOP(1024 * 50);

	wl3501_unblock_interrupt(this);	/* acme: was commented */

	/* Polling Self_Test_Status */
	for (i = 0; i < 10000; i++) {
		wl3501_get_from_wla(this, 0x480, &tmp, sizeof(tmp));

		if (tmp == 'W') {
			/* firmware complete all test successfully */
			tmp = 'A';
			wl3501_set_to_wla(this, 0x480, &tmp, sizeof(tmp));
			goto out;
		}
		WL3501_NOPLOOP(10);
	}
	printk(KERN_WARNING "%s: failed to reset the board!\n", __FUNCTION__);
	rc = -ENODEV;
out:
	return rc;
}

static int wl3501_init_firmware(struct wl3501_card *this)
{
	u16 ptr, next;
	int rc = wl3501_reset_board(this);

	if (rc)
		goto fail;
	/* Switch to SRAM Page 0 */
	wl3501_switch_page(this, WL3501_BSS_SPAGE0);
	/* Read parameter from card */
	wl3501_get_from_wla(this, 0x482, &this->esbq_req_start, 2);
	wl3501_get_from_wla(this, 0x486, &this->esbq_req_end, 2);
	wl3501_get_from_wla(this, 0x488, &this->esbq_confirm_start, 2);
	wl3501_get_from_wla(this, 0x48c, &this->esbq_confirm_end, 2);
	wl3501_get_from_wla(this, 0x48e, &this->tx_buffer_head, 2);
	wl3501_get_from_wla(this, 0x492, &this->tx_buffer_size, 2);
	this->esbq_req_tail	= this->esbq_req_head = this->esbq_req_start;
	this->esbq_req_end     += this->esbq_req_start;
	this->esbq_confirm	= this->esbq_confirm_start;
	this->esbq_confirm_end += this->esbq_confirm_start;
	/* Initial Tx Buffer */
	this->tx_buffer_cnt = 1;
	ptr = this->tx_buffer_head;
	next = ptr + WL3501_BLKSZ;
	while ((next - this->tx_buffer_head) < this->tx_buffer_size) {
		this->tx_buffer_cnt++;
		wl3501_set_to_wla(this, ptr, &next, sizeof(next));
		ptr = next;
		next = ptr + WL3501_BLKSZ;
	}
	rc = 0;
	next = 0;
	wl3501_set_to_wla(this, ptr, &next, sizeof(next));
	this->tx_buffer_tail = ptr;
out:
	return rc;
fail:
	printk(KERN_WARNING "%s: failed!\n", __FUNCTION__);
	goto out;
}

static int wl3501_close(struct net_device *dev)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	int rc = -ENODEV;
	unsigned long flags;
	dev_link_t *link;

	spin_lock_irqsave(&this->lock, flags);
	/* Check if the device is in wl3501_dev_list */
	for (link = wl3501_dev_list; link; link = link->next)
		if (link->priv == dev)
			break;
	if (!link)
		goto out;
	link->open--;

	/* Stop wl3501_hard_start_xmit() from now on */
	netif_stop_queue(dev);
	this->card_start = 0;
	wl3501_ack_interrupt(this);

	/* Mask interrupts from the SUTRO */
	wl3501_block_interrupt(this);

	if (link->state & DEV_STALE_CONFIG) {
		link->release.expires = jiffies + WL3501_RELEASE_TIMEOUT;
		link->state |= DEV_RELEASE_PENDING;
		add_timer(&link->release);
	}
	rc = 0;
	printk(KERN_INFO "%s: WL3501 closed\n", dev->name);
out:
	spin_unlock_irqrestore(&this->lock, flags);
	return rc;
}

/**
 * wl3501_reset - Reset the SUTRO.
 * @dev - network device
 *
 * It is almost the same as wl3501_open(). In fact, we may just wl3501_close()
 * and wl3501_open() again, but I wouldn't like to free_irq() when the driver
 * is running. It seems to be dangerous.
 */
static int wl3501_reset(struct net_device *dev)
{
	unsigned long flags;
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	int rc = -ENODEV;

	spin_lock_irqsave(&this->lock, flags);
	/* Stop processing interrupt from the card */
	wl3501_block_interrupt(this);

	/* Initial WL3501 firmware */
	printk(KERN_INFO "%s: Initialize WL3501 firmware...\n", dev->name);

	if (wl3501_init_firmware(this)) {
		printk(KERN_WARNING "%s: Can't initialize Firmware!\n",
		       dev->name);
		/* Free IRQ, and mark IRQ as unused */
		free_irq(dev->irq, dev);
		goto out;
	}

	/* Initial device variables */
	this->card_start = 0;
	/* queue has to be started only when the Card is Started */
	netif_stop_queue(dev);
	this->adhoc_times = 0;
	wl3501_ack_interrupt(this);

	/* Enable interrupt from card */
	wl3501_unblock_interrupt(this);
	wl3501_mgmt_scan(this, 100);
	printk(KERN_INFO "%s: device reset\n", dev->name);
	rc = 0;
out:
	spin_unlock_irqrestore(&this->lock, flags);
	return rc;
}

static void wl3501_tx_timeout(struct net_device *dev)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	struct net_device_stats *stats = &this->stats;
	int rc;

	stats->tx_errors++;
	rc = wl3501_reset(dev);
	if (rc)
		printk(KERN_ERR "%s: Error %d resetting card on Tx timeout!\n",
		       dev->name, rc);
	else {
		dev->trans_start = jiffies;
		netif_wake_queue(dev);
	}
}

/*
 * Return : 0 - OK
 *	    1 - Could not transmit (dev_queue_xmit will queue it)
 *		and try to sent it later
 */
static int wl3501_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int enabled, fail_send;
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;

	if (!netif_running(dev) || !this->card_start) {
		//printk(KERN_ERR "%s: Tx on stopped device!\n", dev->name);
		return 1;
	}
	if (netif_queue_stopped(dev)) {
		printk(KERN_ERR "%s: Tx while transmitter busy!\n", dev->name);
		return 1;
	}
	/* Avoid re-entry. Block a timer-based transmit from overlapping. */
	netif_stop_queue(dev);

	/*
	 * Good! This packet owns the transmitter, block interrupt immediately.
	 * wl3501_interrupt() has no chance to start the queue if it gets
	 * ISR_Tx.
	 */

	/*
	 * Mask interrupts from the SUTRO!
	 * We must mask interrupt from the same card, to prevent interrupt
	 * routine from accessing data structure and I/O port while
	 * wl3501_send_pkt is running. It's very important!
	 */

	enabled = wl3501_block_interrupt(this);

	/* Record transmitt start time */
	dev->trans_start = jiffies;

	/* Send the packet with default speed */
	fail_send = wl3501_send_pkt(this, skb->data, skb->len);

	/* Turn SUTRO interrupt back on only if it is originally enabled */
	if (enabled)
		wl3501_unblock_interrupt(this);

	/* If sent successfully, start queue. Otherwise, buffer is enqueued
	 * and will be restarted again when the SUTRO interrupts us and
	 * returns ISR_Tx */
	if (!fail_send) {
		dev_kfree_skb(skb);
		netif_start_queue(dev);	/* Let others own the transmitter */
		return 0;
	}
	return 1;		/* Try next time */
}

static int wl3501_open(struct net_device *dev)
{
	int rc = -ENODEV;
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	unsigned long flags;
	dev_link_t *link;

	spin_lock_irqsave(&this->lock, flags);
	/* Check if the device is in wl3501_dev_list */
	for (link = wl3501_dev_list; link; link = link->next)
		if (link->priv == dev)
			break;
	if (!DEV_OK(link))
		goto out;
	netif_device_attach(dev);
	link->open++;

	/* Initial WL3501 firmware */
	printk(KERN_INFO "%s: Initialize WL3501 firmware...\n", dev->name);
	if (wl3501_init_firmware(this))
		goto fail;
	/* Initial device variables */
	netif_start_queue(dev);
	this->card_start = 0;
	this->adhoc_times = 0;
	/* Acknowledge Interrupt, for cleaning last state */
	wl3501_ack_interrupt(this);

	/* Enable interrupt from card after all */
	wl3501_unblock_interrupt(this);
	wl3501_mgmt_scan(this, 100);
	rc = 0;
	printk(KERN_INFO "%s: WL3501 opened\n", dev->name);
out:
	spin_unlock_irqrestore(&this->lock, flags);
	return rc;
fail:
	printk(KERN_WARNING "%s: Can't initialize firmware!\n", dev->name);
	goto out;
}

/**
 * wl3501_init - "initialize" board
 * @dev - network device
 *
 * We never need to do anything when a wl3501 device is "initialized" by the net
 * software, because we only register already-found cards.
 */
static int wl3501_init(struct net_device *dev)
{
	return 0;
}

struct net_device_stats *wl3501_get_stats(struct net_device *dev)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;

	return &this->stats;
}

struct iw_statistics *wl3501_get_wireless_stats(struct net_device *dev)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	struct iw_statistics *wstats = &this->wstats;

	wstats->status = this->card_start;
	wstats->qual.qual    = 0;
	wstats->qual.level   = 0;
	wstats->qual.noise   = 0;
	wstats->discard.nwid = 0;
	wstats->discard.code = 0;
	wstats->discard.misc = 0;

	return wstats;
}

/**
 * wl3501_set_multicast_list - Set or clear the multicast filter
 * @dev - network device
 *
 * Set or clear the multicast filter for this card.
 *
 * CAUTION: To prevent interrupted by WL_Interrupt() and timer-based
 * wl3501_hard_start_xmit() from other interrupts, this should be run
 * single-threaded.
 */
static void wl3501_set_multicast_list(struct net_device *dev)
{
#if 0
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	unsigned long flags;
	u8 filter = RMR_UNICAST | RMR_BROADCAST;	/* Normal mode */

	if (dev->flags & IFF_PROMISC)	/* Promiscuous mode */
		filter |= RMR_PROMISCUOUS | RMR_ALL_MULTICAST;
	else if (dev->mc_count || (dev->flags & IFF_ALLMULTI))
		/* Allow multicast */
		filter |= RMR_ALL_MULTICAST;

	spin_lock_irqsave(&this->lock, flags);
	/* Must not be interrupted */
	wl3501_set_mib_value(this, TYPE_EXTRA_MIB, IDX_RECEIVEMODE,
			     &filter, sizeof(filter));
	spin_unlock_irqrestore(&this->lock, flags);
#endif
}

/**
 * wl3501_ioctl - Perform IOCTL call functions
 * @dev - network device
 * @ifreq - request
 * @cmd - command
 *
 * Perform IOCTL call functions here. Some are privileged operations and the
 * effective uid is checked in those cases.
 *
 * This part is optional. Needed only if you want to run wlu (unix version).
 *
 * CAUTION: To prevent interrupted by wl3501_interrupt() and timer-based
 * wl3501_hard_start_xmit() from other interrupts, this should be run
 * single-threaded. This function is expected to be a rare operation, and it's
 * simpler to just use cli() to disable ALL interrupts.
 */
static int wl3501_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	int rc = 0;
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	struct wl3501_ioctl_parm parm;
	struct wl3501_ioctl_blk *blk = (struct wl3501_ioctl_blk *)
	    &rq->ifr_data;
	unsigned char bf[1028];

	switch (blk->cmd) {
	case WL3501_IOCTL_CMD_SET_RESET: /* Reset drv - needed after set */
		rc = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;
		rc = wl3501_reset(dev);
		break;
	case WL3501_IOCTL_CMD_WRITE_FLASH: /* Write firmware into Flash */
		rc = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;
		rc = -EFAULT;
		if (copy_from_user(bf, blk->data, blk->len))
			break;
		rc = wl3501_write_flash(this, bf, blk->len) ? 0 : -EIO;
		break;
	case WL3501_IOCTL_CMD_GET_PARAMETER:
		parm.def_chan = this->def_chan;
		parm.chan = this->chan;
		parm.net_type = this->net_type;
		parm.version[0] = this->version[0];
		parm.version[1] = this->version[1];
		parm.freq_domain = this->freq_domain;
		memcpy((char *)&(parm.keep_essid[0]),
		       (char *)&(this->keep_essid[0]), 34);
		memcpy((char *)&(parm.essid[0]), (char *)&(this->essid[0]), 34);
		blk->len = sizeof(parm);
		rc = -EFAULT;
		if (copy_to_user(blk->data, &parm, blk->len))
			break;
		rc = 0;
		break;
	case WL3501_IOCTL_CMD_SET_PARAMETER:
		rc = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;
		rc = -EFAULT;
		if (copy_from_user(&parm, blk->data, sizeof(parm)))
			break;
		this->def_chan = parm.def_chan;
		this->net_type = parm.net_type;
		memcpy((char *)&(this->essid[0]), (char *)&(parm.essid[0]), 34);
		rc = wl3501_reset(dev);
		break;
	default:
		rc = -EOPNOTSUPP;
	}
	return rc;
}

/**
 * wl3501_detach - deletes a driver "instance"
 * @link - FILL_IN
 *
 * This deletes a driver "instance". The device is de-registered with Card
 * Services. If it has been released, all local data structures are freed.
 * Otherwise, the structures will be freed when the device is released.
 */
static void wl3501_detach(dev_link_t *link)
{
	dev_link_t **linkp;

	/* Locate device structure */
	for (linkp = &wl3501_dev_list; *linkp; linkp = &(*linkp)->next)
		if (*linkp == link)
			break;
	if (!*linkp)
		goto out;

	/* If the device is currently configured and active, we won't actually
	 * delete it yet.  Instead, it is marked so that when the release()
	 * function is called, that will trigger a proper detach(). */

	if (link->state & DEV_CONFIG) {
#ifdef PCMCIA_DEBUG
		printk(KERN_DEBUG "wl3501_cs: detach postponed, '%s' "
		       "still locked\n", link->dev->dev_name);
#endif
		link->state |= DEV_STALE_LINK;
		goto out;
	}

	/* Break the link with Card Services */
	if (link->handle)
		CardServices(DeregisterClient, link->handle);

	/* Unlink device structure, free pieces */
	*linkp = link->next;

	if (link->priv)
		kfree(link->priv);
	kfree(link);
out:
	return;
}

/**
 * wl3501_flush_stale_links - Remove zombie instances
 *
 * Remove zombie instances (card removed, detach pending)
 */
static void wl3501_flush_stale_links(void)
{
	dev_link_t *link, *next;

	for (link = wl3501_dev_list; link; link = next) {
		next = link->next;
		if (link->state & DEV_STALE_LINK)
			wl3501_detach(link);
	}
}

/**
 * wl3501_attach - creates an "instance" of the driver
 *
 * Creates an "instance" of the driver, allocating local data structures for
 * one device.  The device is registered with Card Services.
 *
 * The dev_link structure is initialized, but we don't actually configure the
 * card at this point -- we wait until we receive a card insertion event.
 */
static dev_link_t *wl3501_attach(void)
{
	client_reg_t client_reg;
	dev_link_t *link;
	struct net_device *dev;
	int ret, i;

	wl3501_flush_stale_links();

	/* Initialize the dev_link_t structure */
	link = kmalloc(sizeof(*link), GFP_KERNEL);
	if (!link)
		goto out;
	memset(link, 0, sizeof(struct dev_link_t));
	link->release.function = wl3501_release;
	link->release.data = (unsigned long)link;

	/* The io structure describes IO port mapping */
	link->io.NumPorts1 = 16;
	link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
	link->io.IOAddrLines = 5;

	/* Interrupt setup */
	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
	link->irq.IRQInfo1 = IRQ_INFO2_VALID | IRQ_LEVEL_ID;
	link->irq.IRQInfo2 = wl3501_irq_mask;
	if (wl3501_irq_list[0] == -1)
		link->irq.IRQInfo2 = wl3501_irq_mask;
	else
		for (i = 0; i < 4; i++)
			link->irq.IRQInfo2 |= 1 << wl3501_irq_list[i];
	link->irq.Handler = wl3501_interrupt;

	/* General socket configuration */
	link->conf.Attributes = CONF_ENABLE_IRQ;
	link->conf.Vcc = 50;
	link->conf.IntType = INT_MEMORY_AND_IO;
	link->conf.ConfigIndex = 1;
	link->conf.Present = PRESENT_OPTION;

	/* Allocate space for private device-specific data */
	dev = kmalloc(sizeof(*dev) + sizeof(struct wl3501_card), GFP_KERNEL);
	if (!dev)
		goto out_link;
	memset(dev, 0, sizeof(*dev) + sizeof(struct wl3501_card));
	ether_setup(dev);
	dev->priv		= dev + 1;
	dev->init		= wl3501_init;
	dev->open		= wl3501_open;
	dev->stop		= wl3501_close;
	dev->hard_start_xmit	= wl3501_hard_start_xmit;
	dev->tx_timeout		= wl3501_tx_timeout;
	dev->watchdog_timeo	= 10 * HZ;
	dev->get_stats		= wl3501_get_stats;
	dev->get_wireless_stats = wl3501_get_wireless_stats;
	dev->set_multicast_list = wl3501_set_multicast_list;
	dev->do_ioctl		= wl3501_ioctl;
	netif_stop_queue(dev);
	link->priv = link->irq.Instance = dev;

	/* Register with Card Services */
	link->next = wl3501_dev_list;
	wl3501_dev_list = link;
	client_reg.dev_info = &wl3501_dev_info;
	client_reg.Attributes = INFO_IO_CLIENT | INFO_CARD_SHARE;
	client_reg.EventMask = CS_EVENT_CARD_INSERTION |
	    CS_EVENT_RESET_PHYSICAL |
	    CS_EVENT_CARD_RESET | CS_EVENT_CARD_REMOVAL |
	    CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
	client_reg.event_handler = wl3501_event;
	client_reg.Version = 0x0210;
	client_reg.event_callback_args.client_data = link;
	ret = CardServices(RegisterClient, &link->handle, &client_reg);
	if (ret) {
		cs_error(link->handle, RegisterClient, ret);
		wl3501_detach(link);
		link = NULL;
	}
out:
	return link;
out_link:
	kfree(link);
	link = NULL;
	goto out;
}

#define CS_CHECK(fn, args...) \
while ((last_ret = CardServices(last_fn = (fn), args)) != 0) goto cs_failed

/**
 * wl3501_config - configure the PCMCIA socket and make eth device available
 * @link - FILL_IN
 *
 * wl3501_config() is scheduled to run after a CARD_INSERTION event is
 * received, to configure the PCMCIA socket, and to make the ethernet device
 * available to the system.
 */
static void wl3501_config(dev_link_t *link)
{
	tuple_t tuple;
	cisparse_t parse;
	client_handle_t handle = link->handle;
	struct net_device *dev = link->priv;
	int i = 0, j, last_fn, last_ret;
	unsigned char buf[64];
	struct wl3501_card *this;

	/* This reads the card's CONFIG tuple to find its config registers. */
	tuple.Attributes = 0;
	tuple.DesiredTuple = CISTPL_CONFIG;
	CS_CHECK(GetFirstTuple, handle, &tuple);
	tuple.TupleData = buf;
	tuple.TupleDataMax = 64;
	tuple.TupleOffset = 0;
	CS_CHECK(GetTupleData, handle, &tuple);
	CS_CHECK(ParseTuple, handle, &tuple, &parse);
	link->conf.ConfigBase = parse.config.base;
	link->conf.Present = parse.config.rmask[0];

	/* Configure card */
	link->state |= DEV_CONFIG;

	/* Try allocating IO ports.  This tries a few fixed addresses.  If you
	 * want, you can also read the card's config table to pick addresses --
	 * see the serial driver for an example. */

	for (j = 0x280; j < 0x400; j += 0x20) {
		/* The '^0x300' is so that we probe 0x300-0x3ff first, then
		 * 0x200-0x2ff, and so on, because this seems safer */
		link->io.BasePort1 = j;
		link->io.BasePort2 = link->io.BasePort1 + 0x10;
		i = CardServices(RequestIO, link->handle, &link->io);
		if (i == CS_SUCCESS)
			break;
	}
	if (i != CS_SUCCESS) {
		cs_error(link->handle, RequestIO, i);
		goto failed;
	}

	/* Now allocate an interrupt line. Note that this does not actually
	 * assign a handler to the interrupt. */

	CS_CHECK(RequestIRQ, link->handle, &link->irq);

	/* This actually configures the PCMCIA socket -- setting up the I/O
	 * windows and the interrupt mapping.  */

	CS_CHECK(RequestConfiguration, link->handle, &link->conf);

	dev->irq = link->irq.AssignedIRQ;
	dev->base_addr = link->io.BasePort1;
	netif_start_queue(dev);
	if (register_netdev(dev)) {
		printk(KERN_NOTICE "wl3501_cs: register_netdev() failed\n");
		goto failed;
	}

	SET_MODULE_OWNER(dev);

	/* At this point, the dev_node_t structure(s) should be initialized and
	 * arranged in a linked list at link->dev. */

	link->dev = &((struct wl3501_card *)dev->priv)->node;
	link->state &= ~DEV_CONFIG_PENDING;

	this = (struct wl3501_card *)dev->priv;
	this->base_addr = dev->base_addr;

	if (!wl3501_get_flash_mac_addr(this)) {
		printk(KERN_WARNING "%s: Cant read MAC addr in flash ROM?\n",
		       dev->name);
		goto failed;
	}
	strcpy(this->node.dev_name, dev->name);

	/* print probe information */
	printk(KERN_INFO "%s: wl3501 @ 0x%3.3x, IRQ %d, MAC addr in flash ROM:",
	       dev->name, this->base_addr, (int)dev->irq);
	for (i = 0; i < 6; i++) {
		dev->dev_addr[i] = ((char *)&this->mac_addr)[i];
		printk("%c%02x", i ? ':' : ' ', dev->dev_addr[i]);
	}
	printk("\n");
	/* initialize card parameter - add by jss */
	this->net_type		= WL3501_NET_TYPE_INFRASTRUCTURE;
	this->llc_type		= 1;
	this->def_chan		= 1;
	this->bss_cnt		= 0;
	this->join_sta_bss	= 0;
	this->adhoc_times	= 0;
	this->driver_state	= 0;
	this->card_start	= 0;
	this->essid[0]		= 0;
	this->essid[1]		= 3;
	this->essid[2]		= 'A';
	this->essid[3]		= 'N';
	this->essid[4]		= 'Y';
	spin_lock_init(&this->lock);

	switch (this->freq_domain) {
	case 0x31:
	case 0x32:
		this->def_chan = 10;
		break;
	case 0x40:
		this->def_chan = 14;
		break;
	case 0x10:
	case 0x20:
	case 0x30:
	default:
		this->def_chan = 1;
		break;
	}
	goto out;
cs_failed:
	cs_error(link->handle, last_fn, last_ret);
failed:
	wl3501_release((unsigned long)link);
out:
	return;
}

/**
 * wl3501_release - unregister the net, release PCMCIA configuration
 * @arg - FILL_IN
 *
 * After a card is removed, wl3501_release() will unregister the net device,
 * and release the PCMCIA configuration.  If the device is still open, this
 * will be postponed until it is closed.
 */
static void wl3501_release(unsigned long arg)
{
	dev_link_t *link = (dev_link_t *) arg;
	struct net_device *dev = link->priv;

	/* If the device is currently in use, we won't release until it is
	 * actually closed. */
	if (link->open) {
		dprintk(1, "release postponed, '%s' still open",
			link->dev->dev_name);
		link->state |= DEV_STALE_CONFIG;
		goto out;
	}

	/* Unlink the device chain */
	if (link->dev) {
		unregister_netdev(dev);
		link->dev = NULL;
	}

	/* Don't bother checking to see if these succeed or not */
	CardServices(ReleaseConfiguration, link->handle);
	CardServices(ReleaseIO, link->handle, &link->io);
	CardServices(ReleaseIRQ, link->handle, &link->irq);
	link->state &= ~DEV_CONFIG;

	if (link->state & DEV_STALE_LINK)
		wl3501_detach(link);
out:
	return;
}

/**
 * wl3501_event - The card status event handler
 * @event - event
 * @pri - priority
 * @args - arguments for this event
 *
 * The card status event handler. Mostly, this schedules other stuff to run
 * after an event is received. A CARD_REMOVAL event also sets some flags to
 * discourage the net drivers from trying to talk to the card any more.
 *
 * When a CARD_REMOVAL event is received, we immediately set a flag to block
 * future accesses to this device. All the functions that actually access the
 * device should check this flag to make sure the card is still present.
 */
static int wl3501_event(event_t event, int pri, event_callback_args_t *args)
{
	dev_link_t *link = args->client_data;
	struct net_device *dev = link->priv;

	switch (event) {
	case CS_EVENT_CARD_REMOVAL:
		link->state &= ~DEV_PRESENT;
		if (link->state & DEV_CONFIG) {
			netif_stop_queue(dev);
			link->release.expires = jiffies +
						WL3501_RELEASE_TIMEOUT;
			add_timer(&link->release);
		}
		break;
	case CS_EVENT_CARD_INSERTION:
		link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
		wl3501_config(link);
		break;
	case CS_EVENT_PM_SUSPEND:
		link->state |= DEV_SUSPEND;
		/* Fall through... */
	case CS_EVENT_RESET_PHYSICAL:
		if (link->state & DEV_CONFIG) {
			if (link->open) {
				netif_stop_queue(dev);
			}
			CardServices(ReleaseConfiguration, link->handle);
		}
		break;
	case CS_EVENT_PM_RESUME:
		link->state &= ~DEV_SUSPEND;
		/* Fall through... */
	case CS_EVENT_CARD_RESET:
		if (link->state & DEV_CONFIG) {
			CardServices(RequestConfiguration, link->handle,
				     &link->conf);
			if (link->open)
				wl3501_reset(dev);
		}
		break;
	}
	return 0;
}

static struct pcmcia_driver wl3501_driver = {
	.owner          = THIS_MODULE,
	.drv            = {
		.name   = "wl3501_cs",
	},
	.attach         = wl3501_attach,
	.detach         = wl3501_detach,
};

static int __init wl3501_init_module(void)
{
	servinfo_t serv;

	dprintk(0, ": loading");
	CardServices(GetCardServicesInfo, &serv);
	if (serv.Revision != CS_RELEASE_CODE) {
		printk(KERN_NOTICE
		       "wl3501_cs: Card Services release does not match!\n"
		       "Compiled with 0x%x, but current is 0x%lx\n",
		       CS_RELEASE_CODE, (unsigned long)serv.Revision);
		/* return -1; */
	}
	pcmcia_register_driver(&wl3501_driver);
	return 0;
}

static void __exit wl3501_exit_module(void)
{
	dprintk(0, ": unloading");
	pcmcia_unregister_driver(&wl3501_driver);
	while (wl3501_dev_list) {
		if (wl3501_dev_list->state & DEV_CONFIG)
			wl3501_release((unsigned long)wl3501_dev_list);
		wl3501_detach(wl3501_dev_list);
	}
}

module_init(wl3501_init_module);
module_exit(wl3501_exit_module);
