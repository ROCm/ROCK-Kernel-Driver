/*
 * WL3501 Wireless LAN PCMCIA Card Driver for Linux
 * Written originally for Linux 2.0.30 by Fox Chen, mhchen@golf.ccl.itri.org.tw
 * Ported to 2.2, 2.4 & 2.5 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 * Wireless extensions in 2.4 by Gustavo Niemeyer <niemeyer@conectiva.com>
 *
 * References used by Fox Chen while writing the original driver for 2.0.30:
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
 * Tested with Planet AP in 2.4.17, 184 Kbytes/s in UDP in Infrastructure mode,
 * 173 Kbytes/s in TCP.
 *
 * Tested with Planet AP in 2.5.73-bk, 216 Kbytes/s in Infrastructure mode
 * with a SMP machine (dual pentium 100), using pktgen, 432 pps (pkt_size = 60)
 */
#undef REALLY_SLOW_IO	/* most systems can safely undef this */

#include <linux/config.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/ethtool.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fcntl.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/wireless.h>

#include <net/iw_handler.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

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

/*
 * Conversion from Channel (this->chan) to frequency, this information
 * was obtained from the Planet WAP 1000 Access Point web interface. -acme
 */
static int wl3501_chan2freq[] = {
	[0] = 2412, [1] = 2417, [2] = 2422, [3] = 2427, [4] = 2432,
	[5] = 2437, [6] = 2442, [7] = 2447, [8] = 2452, [9] = 2457,
	[10] = 2462,
};

#define wl3501_outb(a, b) { outb(a, b); slow_down_io(); }
#define wl3501_outb_p(a, b) { outb_p(a, b); slow_down_io(); }
#define wl3501_outsb(a, b, c) { outsb(a, b, c); slow_down_io(); }

#define WL3501_RELEASE_TIMEOUT (25 * HZ)
#define WL3501_MAX_ADHOC_TRIES 16

/* Parameters that can be set with 'insmod' */
/* Bit map of interrupts to choose from */
/* This means pick from 15, 14, 12, 11, 10, 9, 7, 5, 4, and 3 */
static unsigned long wl3501_irq_mask = 0xdeb8;
static int wl3501_irq_list[4] = { -1 };

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

static inline void wl3501_switch_page(struct wl3501_card *this, u8 page)
{
	wl3501_outb(page, this->base_addr + WL3501_NIC_BSS);
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
	this->mac_addr[0] = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->mac_addr[1] = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->mac_addr[2] = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->mac_addr[3] = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->mac_addr[4] = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->mac_addr[5] = inb(base_addr + WL3501_NIC_IODPA);
	WL3501_NOPLOOP(100);
	this->reg_domain = inb(base_addr + WL3501_NIC_IODPA);
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
	return this->mac_addr[0] == 0x00 && this->mac_addr[1] == 0x60;
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
 *  \               \- IEEE 802.11 -/ \-------------- len --------------/
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

static void wl3501_esbq_req(struct wl3501_card *this, u16 *ptr)
{
	u16 tmp = 0;

	wl3501_set_to_wla(this, this->esbq_req_head, ptr, 2);
	wl3501_set_to_wla(this, this->esbq_req_head + 2, &tmp, sizeof(tmp));
	this->esbq_req_head += 4;
	if (this->esbq_req_head >= this->esbq_req_end)
		this->esbq_req_head = this->esbq_req_start;
}

static int wl3501_esbq_exec(struct wl3501_card *this, void *sig, int sig_size)
{
	int rc = -EIO;

	if (wl3501_esbq_req_test(this)) {
		u16 ptr = wl3501_get_tx_buffer(this, sig_size);
		if (ptr) {
			wl3501_set_to_wla(this, ptr, sig, sig_size);
			wl3501_esbq_req(this, &ptr);
			rc = 0;
		}
	}
	return rc;
}

static int wl3501_get_mib_value(struct wl3501_card *this, u8 index,
				void *bf, int size)
{
	struct wl3501_get_req signal;
	unsigned long flags;
	int rc = -EIO;
    
	signal.next_blk	  = 0;
	signal.sig_id	  = WL3501_SIG_GET_REQ;
	signal.mib_attrib = index;

	spin_lock_irqsave(&this->lock, flags);
	if (wl3501_esbq_req_test(this)) {
		u16 ptr = wl3501_get_tx_buffer(this, sizeof(signal));
		if (ptr) {
			wl3501_set_to_wla(this, ptr, &signal, sizeof(signal));
			wl3501_esbq_req(this, &ptr);
			this->sig_get_confirm.mib_status = 255;
			spin_unlock_irqrestore(&this->lock, flags);
			rc = wait_event_interruptible(this->wait,
				this->sig_get_confirm.mib_status != 255);
			if (!rc)
				memcpy(bf, this->sig_get_confirm.mib_value,
				       size);
			goto out;
		}
	}
	spin_unlock_irqrestore(&this->lock, flags);
out:
	return rc;
}

/**
 * wl3501_send_pkt - Send a packet.
 * @this - card
 *
 * Send a packet.
 *
 * data = Ethernet raw frame.  (e.g. data[0] - data[5] is Dest MAC Addr,
 *                                   data[6] - data[11] is Src MAC Addr)
 * Ref: IEEE 802.11
 */
static int wl3501_send_pkt(struct wl3501_card *this, u8 *data, u16 len)
{
	u16 bf, sig_bf, next, tmplen, pktlen;
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
		memcpy(&sig.daddr[0], pdata, 12);
		pktlen = len - 12;
		pdata += 12;
		sig.next_blk = 0;
		sig.sig_id = WL3501_SIG_MD_REQ;
		sig.data = bf;
		if (((*pdata) * 256 + (*(pdata + 1))) > 1500) {
			unsigned char addr4[ETH_ALEN] = {
				[0] = 0xAA, [1] = 0xAA, [2] = 0x03, [4] = 0x00,
			};

			wl3501_set_to_wla(this, bf + 2 +
					  offsetof(struct wl3501_tx_hdr, addr4),
					  addr4, sizeof(addr4));
			sig.size = pktlen + 24 + 4 + 6;
			if (pktlen > (254 - sizeof(struct wl3501_tx_hdr))) {
				tmplen = 254 - sizeof(struct wl3501_tx_hdr);
				pktlen -= tmplen;
			} else {
				tmplen = pktlen;
				pktlen = 0;
			}
			wl3501_set_to_wla(this,
					  bf + 2 + sizeof(struct wl3501_tx_hdr),
					  pdata, tmplen);
			pdata += tmplen;
			wl3501_get_from_wla(this, bf, &next, sizeof(next));
			bf = next;
		} else {
			sig.size = pktlen + 24 + 4 - 2;
			pdata += 2;
			pktlen -= 2;
			if (pktlen > (254 - sizeof(struct wl3501_tx_hdr) + 6)) {
				tmplen = 254 - sizeof(struct wl3501_tx_hdr) + 6;
				pktlen -= tmplen;
			} else {
				tmplen = pktlen;
				pktlen = 0;
			}
			wl3501_set_to_wla(this, bf + 2 +
					  offsetof(struct wl3501_tx_hdr, addr4),
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
	struct wl3501_resync_req sig = {
		.sig_id = WL3501_SIG_RESYNC_REQ,
	};

	return wl3501_esbq_exec(this, &sig, sizeof(sig));
}

static inline int wl3501_fw_bss_type(struct wl3501_card *this)
{
	return this->net_type == IW_MODE_INFRA ? WL3501_NET_TYPE_INFRA :
						 WL3501_NET_TYPE_ADHOC;
}

static inline int wl3501_fw_cap_info(struct wl3501_card *this)
{
	return this->net_type == IW_MODE_INFRA ? WL3501_MGMT_CAPABILITY_ESS :
						 WL3501_MGMT_CAPABILITY_IBSS;
}

static int wl3501_mgmt_scan(struct wl3501_card *this, u16 chan_time)
{
	struct wl3501_scan_req sig = {
		.sig_id		= WL3501_SIG_SCAN_REQ,
		.scan_type	= WL3501_SCAN_TYPE_ACTIVE,
		.probe_delay	= 0x10,
		.min_chan_time	= chan_time,
		.max_chan_time	= chan_time,
		.bss_type	= wl3501_fw_bss_type(this),
	};

	this->bss_cnt = this->join_sta_bss = 0;
	return wl3501_esbq_exec(this, &sig, sizeof(sig));
}

static int wl3501_mgmt_join(struct wl3501_card *this, u16 stas)
{
	struct wl3501_join_req sig = {
		.sig_id	  = WL3501_SIG_JOIN_REQ,
		.timeout  = 10,
		.phy_pset = {
			[2] = this->def_chan,
		},
	};

	memcpy(&sig.beacon_period, &this->bss_set[stas].beacon_period, 72);
	return wl3501_esbq_exec(this, &sig, sizeof(sig));
}

static int wl3501_mgmt_start(struct wl3501_card *this)
{
	struct wl3501_start_req sig = {
		.sig_id			= WL3501_SIG_START_REQ,
		.beacon_period		= 400,
		.dtim_period		= 1,
		.phy_pset		= {
			[0] = 3, [1] = 1, [2] = this->chan,
		},
		.bss_basic_rate_set	= {
			[0] = 0x01, [1] = 0x02, [2] = 0x82, [3] = 0x84,
		},
		.operational_rate_set	= {
			[0] = 0x01, [1] = 0x02, [2] = 0x82, [3] = 0x84,
		},
		.ibss_pset		= {
			[0] = 6, [1] = 2, [2] = 10,
		},
		.bss_type		= wl3501_fw_bss_type(this),
		.cap_info		= wl3501_fw_cap_info(this),
	};

	memcpy(sig.ssid, this->essid, WL3501_ESSID_MAX_LEN);
	memcpy(this->keep_essid, this->essid, WL3501_ESSID_MAX_LEN);
	return wl3501_esbq_exec(this, &sig, sizeof(sig));
}

static void wl3501_mgmt_scan_confirm(struct wl3501_card *this, u16 addr)
{
	u16 i = 0;
	int matchflag = 0;
	struct wl3501_scan_confirm signal;

	dprintk(3, "entry");
	wl3501_get_from_wla(this, addr, &signal, sizeof(signal));
	if (signal.status == WL3501_STATUS_SUCCESS) {
		dprintk(3, "success");
		if ((this->net_type == IW_MODE_INFRA &&
		     (signal.cap_info & WL3501_MGMT_CAPABILITY_ESS)) ||
		    (this->net_type == IW_MODE_ADHOC &&
		     (signal.cap_info & WL3501_MGMT_CAPABILITY_IBSS)) ||
		    this->net_type == IW_MODE_AUTO) {
			if (!this->essid[1])
				matchflag = 1;
			else if (this->essid[1] == 3 &&
				 !strncmp((char *)&this->essid[2], "ANY", 3))
				matchflag = 1;
			else if (this->essid[1] != signal.ssid[1])
				matchflag = 0;
			else if (memcmp(&this->essid[2], &signal.ssid[2],
					this->essid[1]))
				matchflag = 0;
			else
				matchflag = 1;
			if (matchflag) {
				for (i = 0; i < this->bss_cnt; i++) {
					if (!memcmp(this->bss_set[i].bssid,
						    signal.bssid, ETH_ALEN)) {
						matchflag = 0;
						break;
					}
				}
			}
			if (matchflag && (i < 20)) {
				memcpy(&this->bss_set[i].beacon_period,
				       &signal.beacon_period, 73);
				this->bss_cnt++;
				this->rssi = signal.rssi;
			}
		}
	} else if (signal.status == WL3501_STATUS_TIMEOUT) {
		dprintk(3, "timeout");
		this->join_sta_bss = 0;
		for (i = this->join_sta_bss; i < this->bss_cnt; i++)
			if (!wl3501_mgmt_join(this, i))
				break;
		this->join_sta_bss = i;
		if (this->join_sta_bss == this->bss_cnt) {
			if (this->net_type == IW_MODE_INFRA)
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
	const int offset_addr4 = offsetof(struct wl3501_rx_hdr, addr4);
	u16 next_addr, next_addr1;
	u8 *data = bf + 12;

	size -= 12;
	wl3501_get_from_wla(this, this->start_seg + 2,
			    &next_addr, sizeof(next_addr));
	if (this->ether_type == ARPHRD_ETHER) {
		if (size > WL3501_BLKSZ - sizeof(struct wl3501_rx_hdr)) {
			wl3501_get_from_wla(this,
					    this->start_seg +
						sizeof(struct wl3501_rx_hdr),
					    data,
					    WL3501_BLKSZ -
						sizeof(struct wl3501_rx_hdr));
			size -= WL3501_BLKSZ - sizeof(struct wl3501_rx_hdr);
			data += WL3501_BLKSZ - sizeof(struct wl3501_rx_hdr);
		} else {
			wl3501_get_from_wla(this,
					    this->start_seg +
						sizeof(struct wl3501_rx_hdr),
					    data, size);
			size = 0;
		}
	} else {
		size -= 2;
		*data = (size >> 8) & 0xff;
		*(data + 1) = size & 0xff;
		data += 2;
		if (size > WL3501_BLKSZ - sizeof(struct wl3501_rx_hdr) + 6) {
			wl3501_get_from_wla(this,
					    this->start_seg + offset_addr4,
					    data,
					    WL3501_BLKSZ -
					      sizeof(struct wl3501_rx_hdr) + 6);
			size -= WL3501_BLKSZ - sizeof(struct wl3501_rx_hdr) + 6;
			data += WL3501_BLKSZ - sizeof(struct wl3501_rx_hdr) + 6;
		} else {
			wl3501_get_from_wla(this,
					    this->start_seg + offset_addr4,
					    data, size);
			size = 0;
		}
	}
	while (size > 0) {
		if (size > WL3501_BLKSZ - 5) {
			wl3501_get_from_wla(this, next_addr + 5, data,
					    WL3501_BLKSZ - 5);
			size -= WL3501_BLKSZ - 5;
			data += WL3501_BLKSZ - 5;
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

static void wl3501_online(struct net_device *dev)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;

	printk(KERN_INFO "%s: Wireless LAN online. BSSID: "
	       "%02X %02X %02X %02X %02X %02X\n", dev->name,
	       this->bssid[0], this->bssid[1], this->bssid[2],
	       this->bssid[3], this->bssid[4], this->bssid[5]);
	netif_wake_queue(dev);
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
	struct wl3501_auth_req sig = {
		.sig_id	 = WL3501_SIG_AUTH_REQ,
		.type	 = WL3501_SYS_TYPE_OPEN,
		.timeout = 1000,
	};

	dprintk(3, "entry");
	memcpy(sig.mac_addr, this->bssid, ETH_ALEN);
	return wl3501_esbq_exec(this, &sig, sizeof(sig));
}

static int wl3501_mgmt_association(struct wl3501_card *this)
{
	struct wl3501_assoc_req sig = {
		.sig_id		 = WL3501_SIG_ASSOC_REQ,
		.timeout	 = 1000,
		.listen_interval = 5,
		.cap_info	 = this->cap_info,
	};

	dprintk(3, "entry");
	memcpy(sig.mac_addr, this->bssid, ETH_ALEN);
	return wl3501_esbq_exec(this, &sig, sizeof(sig));
}

static void wl3501_mgmt_join_confirm(struct net_device *dev, u16 addr)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	struct wl3501_join_confirm sig;

	dprintk(3, "entry");
	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));
	if (sig.status == WL3501_STATUS_SUCCESS) {
		if (this->net_type == IW_MODE_INFRA) {
			if (this->join_sta_bss < this->bss_cnt) {
				const int i = this->join_sta_bss;
				memcpy(this->bssid,
				       this->bss_set[i].bssid, ETH_ALEN);
				this->chan = this->bss_set[i].phy_pset[2];
				memcpy(this->keep_essid, this->bss_set[i].ssid,
				       WL3501_ESSID_MAX_LEN);
				wl3501_mgmt_auth(this);
			}
		} else {
			const int i = this->join_sta_bss;
			memcpy(this->bssid, this->bss_set[i].bssid, ETH_ALEN);
			this->chan = this->bss_set[i].phy_pset[2];
			memcpy(this->keep_essid,
			       this->bss_set[i].ssid, WL3501_ESSID_MAX_LEN);
			wl3501_online(dev);
		}
	} else {
		int i;
		this->join_sta_bss++;
		for (i = this->join_sta_bss; i < this->bss_cnt; i++)
			if (!wl3501_mgmt_join(this, i))
				break;
		this->join_sta_bss = i;
		if (this->join_sta_bss == this->bss_cnt) {
			if (this->net_type == IW_MODE_INFRA)
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

static inline void wl3501_alarm_interrupt(struct net_device *dev,
					  struct wl3501_card *this)
{
	if (this->net_type == IW_MODE_INFRA) {
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

	dprintk(3, "entry");
	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));
	wl3501_free_tx_buffer(this, sig.data);
	if (netif_queue_stopped(dev))
		netif_wake_queue(dev);
}

static inline void wl3501_md_ind_interrupt(struct net_device *dev,
					   struct wl3501_card *this, u16 addr)
{
	struct wl3501_md_ind sig;
	struct sk_buff *skb;
	unsigned char rssi, addr4[ETH_ALEN];
	u16 pkt_len;

	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));
	this->start_seg = sig.data;
	wl3501_get_from_wla(this,
			    sig.data + offsetof(struct wl3501_rx_hdr, rssi),
			    &rssi, sizeof(rssi));
	this->rssi = rssi <= 63 ? (rssi * 100) / 64 : 255;

	wl3501_get_from_wla(this,
			    sig.data +
				offsetof(struct wl3501_rx_hdr, addr4),
			    &addr4, sizeof(addr4));
	if (addr4[0] == 0xAA && addr4[1] == 0xAA &&
	    addr4[2] == 0x03 && addr4[4] == 0x00) {
		pkt_len = sig.size + 12 - 24 - 4 - 6;
		this->ether_type = ARPHRD_ETHER;
	} else if (addr4[0] == 0xE0 && addr4[1] == 0xE0) {
		pkt_len = sig.size + 12 - 24 - 4 + 2;
		this->ether_type = ARPHRD_IEEE80211; /* FIXME */
	} else {
		pkt_len = sig.size + 12 - 24 - 4 + 2;
		this->ether_type = ARPHRD_IEEE80211; /* FIXME */
	}

	skb = dev_alloc_skb(pkt_len + 5);

	if (!skb) {
		printk(KERN_WARNING "%s: Can't alloc a sk_buff of size %d.\n",
		       dev->name, pkt_len);
		this->stats.rx_dropped++;
	} else {
		skb->dev = dev;
		skb_reserve(skb, 2); /* IP headers on 16 bytes boundaries */
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
	dprintk(3, "entry");
	wl3501_get_from_wla(this, addr, &this->sig_get_confirm,
			    sizeof(this->sig_get_confirm));
	wake_up(&this->wait);
}

static inline void wl3501_start_confirm_interrupt(struct net_device *dev,
						  struct wl3501_card *this,
						  u16 addr)
{
	struct wl3501_start_confirm sig;

	dprintk(3, "entry");
	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));
	if (sig.status == WL3501_STATUS_SUCCESS)
		netif_wake_queue(dev);
}

static inline void wl3501_assoc_confirm_interrupt(struct net_device *dev,
						  u16 addr)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	struct wl3501_assoc_confirm sig;

	dprintk(3, "entry");
	wl3501_get_from_wla(this, addr, &sig, sizeof(sig));

	if (sig.status == WL3501_STATUS_SUCCESS)
		wl3501_online(dev);
}

static inline void wl3501_auth_confirm_interrupt(struct wl3501_card *this,
						 u16 addr)
{
	struct wl3501_auth_confirm sig;

	dprintk(3, "entry");
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

	dprintk(3, "entry");
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
		wl3501_mgmt_join_confirm(dev, addr);
		break;
	case WL3501_SIG_ASSOC_CONFIRM:
		wl3501_assoc_confirm_interrupt(dev, addr);
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
	this->card_name[0] = '\0';
	wl3501_get_from_wla(this, 0x1a00,
			    this->card_name, sizeof(this->card_name));
	this->card_name[sizeof(this->card_name) - 1] = '\0';
	this->firmware_date[0] = '\0';
	wl3501_get_from_wla(this, 0x1a40,
			    this->firmware_date, sizeof(this->firmware_date));
	this->firmware_date[sizeof(this->firmware_date) - 1] = '\0';
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
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	int rc = -ENODEV;

	wl3501_block_interrupt(this);

	if (wl3501_init_firmware(this)) {
		printk(KERN_WARNING "%s: Can't initialize Firmware!\n",
		       dev->name);
		/* Free IRQ, and mark IRQ as unused */
		free_irq(dev->irq, dev);
		goto out;
	}

	/*
	 * Queue has to be started only when the Card is Started
	 */
	netif_stop_queue(dev);
	this->adhoc_times = 0;
	wl3501_ack_interrupt(this);
	wl3501_unblock_interrupt(this);
	wl3501_mgmt_scan(this, 100);
	dprintk(1, "%s: device reset", dev->name);
	rc = 0;
out:
	return rc;
}

static void wl3501_tx_timeout(struct net_device *dev)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	struct net_device_stats *stats = &this->stats;
	unsigned long flags;
	int rc;

	stats->tx_errors++;
	spin_lock_irqsave(&this->lock, flags);
	rc = wl3501_reset(dev);
	spin_unlock_irqrestore(&this->lock, flags);
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
	int enabled, rc;
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	unsigned long flags;

	spin_lock_irqsave(&this->lock, flags);
	enabled = wl3501_block_interrupt(this);
	dev->trans_start = jiffies;
	rc = wl3501_send_pkt(this, skb->data, skb->len);
	if (enabled)
		wl3501_unblock_interrupt(this);
	if (rc) {
		++this->stats.tx_dropped;
		netif_stop_queue(dev);
	} else {
		++this->stats.tx_packets;
		this->stats.tx_bytes += skb->len;
		kfree_skb(skb);

		if (this->tx_buffer_cnt < 2)
			netif_stop_queue(dev);
	}
	spin_unlock_irqrestore(&this->lock, flags);
	return rc;
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
	dprintk(1, "%s: Initialize WL3501 firmware...", dev->name);
	if (wl3501_init_firmware(this))
		goto fail;
	/* Initial device variables */
	this->adhoc_times = 0;
	/* Acknowledge Interrupt, for cleaning last state */
	wl3501_ack_interrupt(this);

	/* Enable interrupt from card after all */
	wl3501_unblock_interrupt(this);
	wl3501_mgmt_scan(this, 100);
	rc = 0;
	dprintk(1, "%s: WL3501 opened", dev->name);
	printk(KERN_INFO "%s: Card Name: %s\n"
			 "%s: Firmware Date: %s\n",
			 dev->name, this->card_name,
			 dev->name, this->firmware_date);
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
	u32 value; /* size checked: it is u32 */

	memset(wstats, 0, sizeof(*wstats));
	wstats->status = netif_running(dev);
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_WEP_ICV_ERROR_COUNT,
				  &value, sizeof(value)))
		wstats->discard.code += value;
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_WEP_UNDECRYPTABLE_COUNT,
				  &value, sizeof(value)))
		wstats->discard.code += value;
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_WEP_EXCLUDED_COUNT,
				  &value, sizeof(value)))
		wstats->discard.code += value;
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_RETRY_COUNT,
				  &value, sizeof(value)))
		wstats->discard.retries	= value;
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_FAILED_COUNT,
				  &value, sizeof(value)))
		wstats->discard.misc += value;
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_RTS_FAILURE_COUNT,
				  &value, sizeof(value)))
		wstats->discard.misc += value;
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_ACK_FAILURE_COUNT,
				  &value, sizeof(value)))
		wstats->discard.misc += value;
	if (!wl3501_get_mib_value(this, WL3501_MIB_ATTR_FRAME_DUPLICATE_COUNT,
				  &value, sizeof(value)))
		wstats->discard.misc += value;
	return wstats;
}

static inline int wl3501_ethtool_ioctl(struct net_device *dev, void *uaddr)
{
	u32 ethcmd;
	int rc = -EFAULT;

	if (copy_from_user(&ethcmd, uaddr, sizeof(ethcmd)))
		goto out;

	switch (ethcmd) {
	case ETHTOOL_GDRVINFO: {
		struct ethtool_drvinfo info = { .cmd = ETHTOOL_GDRVINFO, };

		strlcpy(info.driver, wl3501_dev_info, sizeof(info.driver));
		rc = copy_to_user(uaddr, &info, sizeof(info)) ? -EFAULT : 1;
	}
	default:
		rc = -EOPNOTSUPP;
		break;
	}
out:
	return rc;
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
 * single-threaded.
 */
static int wl3501_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	int rc = -ENODEV;

	if (netif_device_present(dev)) {
		rc = -EOPNOTSUPP;
		if (cmd == SIOCETHTOOL)
			rc = wl3501_ethtool_ioctl(dev, (void *)rq->ifr_data);
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

static int wl3501_get_name(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	strlcpy(wrqu->name, "IEEE 802.11-DS", sizeof(wrqu->name));
	return 0;
}

static int wl3501_set_freq(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	int channel = wrqu->freq.m;
	int rc = 0;

	if (channel > 1000 || wrqu->freq.e > 0)
		rc = -EOPNOTSUPP;
	else if (channel < 1 || channel > ARRAY_SIZE(wl3501_chan2freq))
		rc = -EINVAL;
	else {
		struct wl3501_card *this = (struct wl3501_card *)dev->priv;

		this->def_chan = channel;
		rc = wl3501_reset(dev);
	}

	return rc;
}

static int wl3501_get_freq(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;

	wrqu->freq.m = wl3501_chan2freq[this->chan - 1] * 100000;
	wrqu->freq.e = 1;
	return 0;
}

static int wl3501_set_mode(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	int rc = -EINVAL;

	if (wrqu->mode == IW_MODE_INFRA ||
	    wrqu->mode == IW_MODE_ADHOC ||
	    wrqu->mode == IW_MODE_AUTO) {
		struct wl3501_card *this = (struct wl3501_card *)dev->priv;

		this->net_type = wrqu->mode;
		rc = wl3501_reset(dev);
	}
	return rc;
}

static int wl3501_get_mode(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;

	wrqu->mode = this->net_type;
	return 0;
}

static int wl3501_get_sens(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;

	wrqu->sens.value = this->rssi;
	wrqu->sens.disabled = !wrqu->sens.value;
	wrqu->sens.fixed = 1;
	return 0;
}

static int wl3501_get_range(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;

	/* Set the length (very important for backward compatibility) */
	wrqu->data.length = sizeof(*range);

	/* Set all the info we don't care or don't know about to zero */
	memset(range, 0, sizeof(*range));

	/* Set the Wireless Extension versions */
	range->we_version_compiled	= WIRELESS_EXT;
	range->we_version_source	= 1;
	range->throughput		= 2 * 1000 * 1000;     /* ~2 Mb/s */
	/* FIXME: study the code to fill in more fields... */
	return 0;
}

static int wl3501_set_wap(struct net_device *dev, struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	static const unsigned char bcast[ETH_ALEN] =
					{ 255, 255, 255, 255, 255, 255 };
	int rc = -EINVAL;

	/* FIXME: we support other ARPHRDs...*/
	if (wrqu->ap_addr.sa_family != ARPHRD_ETHER)
		goto out;
	if (!memcmp(bcast, wrqu->ap_addr.sa_data, ETH_ALEN)) {
		/* FIXME: rescan? */
	} else
		memcpy(this->bssid, wrqu->ap_addr.sa_data, ETH_ALEN);
		/* FIXME: rescan? deassoc & scan? */
	rc = 0;
out:
	return rc;
}

static int wl3501_get_wap(struct net_device *dev, struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;

	wrqu->ap_addr.sa_family = this->ether_type;
	memcpy(wrqu->ap_addr.sa_data, this->bssid, ETH_ALEN);
	return 0;
}

static int wl3501_set_essid(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	int rc = 0;

	if (wrqu->data.flags) {
		strlcpy(this->essid + 2, extra, min_t(u16, wrqu->data.length,
						      IW_ESSID_MAX_SIZE));
		rc = wl3501_reset(dev);
	}
	return rc;
}

static int wl3501_get_essid(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	unsigned long flags;

	spin_lock_irqsave(&this->lock, flags);
	wrqu->essid.flags  = 1;
	wrqu->essid.length = IW_ESSID_MAX_SIZE;
	strlcpy(extra, this->essid + 2, IW_ESSID_MAX_SIZE);
	spin_unlock_irqrestore(&this->lock, flags);
	return 0;
}

static int wl3501_set_nick(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;

	if (wrqu->data.length > sizeof(this->nick))
		return -E2BIG;
	strlcpy(this->nick, extra, wrqu->data.length);
	return 0;
}

static int wl3501_get_nick(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;

	strlcpy(extra, this->nick, 32);
	wrqu->data.length = strlen(extra);
	return 0;
}

static int wl3501_get_rate(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	/*
	 * FIXME: have to see from where to get this info, perhaps this card
	 * works at 1 Mbit/s too... for now leave at 2 Mbit/s that is the most
	 * common with the Planet Access Points. -acme
	 */
	wrqu->bitrate.value = 2000000;
	wrqu->bitrate.fixed = 1;
	return 0;
}

static int wl3501_get_rts_threshold(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu, char *extra)
{
	u16 threshold; /* size checked: it is u16 */
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	int rc = wl3501_get_mib_value(this, WL3501_MIB_ATTR_RTS_THRESHOLD,
				      &threshold, sizeof(threshold));
	if (!rc) {
		wrqu->rts.value = threshold;
		wrqu->rts.disabled = threshold >= 2347;
		wrqu->rts.fixed = 1;
	}
	return rc;
}

static int wl3501_get_frag_threshold(struct net_device *dev,
				     struct iw_request_info *info,
				     union iwreq_data *wrqu, char *extra)
{
	u16 threshold; /* size checked: it is u16 */
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	int rc = wl3501_get_mib_value(this, WL3501_MIB_ATTR_FRAG_THRESHOLD,
				      &threshold, sizeof(threshold));
	if (!rc) {
		wrqu->frag.value = threshold;
		wrqu->frag.disabled = threshold >= 2346;
		wrqu->frag.fixed = 1;
	}
	return rc;
}

static int wl3501_get_txpow(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	u16 txpow;
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	int rc = wl3501_get_mib_value(this,
				      WL3501_MIB_ATTR_CURRENT_TX_PWR_LEVEL,
				      &txpow, sizeof(txpow));
	if (!rc) {
		wrqu->txpower.value = txpow;
		wrqu->txpower.disabled = 0;
		/*
		 * From the MIB values I think this can be configurable,
		 * as it lists several tx power levels -acme
		 */
		wrqu->txpower.fixed = 0;
		wrqu->txpower.flags = IW_TXPOW_MWATT;
	}
	return rc;
}

static int wl3501_get_retry(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	u8 retry; /* size checked: it is u8 */
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	int rc = wl3501_get_mib_value(this,
				      WL3501_MIB_ATTR_LONG_RETRY_LIMIT,
				      &retry, sizeof(retry));
	if (rc)
		goto out;
	if (wrqu->retry.flags & IW_RETRY_MAX) {
		wrqu->retry.flags = IW_RETRY_LIMIT | IW_RETRY_MAX;
		goto set_value;
	}
	rc = wl3501_get_mib_value(this, WL3501_MIB_ATTR_SHORT_RETRY_LIMIT,
				  &retry, sizeof(retry));
	if (rc)
		goto out;
	wrqu->retry.flags = IW_RETRY_LIMIT | IW_RETRY_MIN;
set_value:
	wrqu->retry.value = retry;
	wrqu->retry.disabled = 0;
out:
	return rc;
}

static int wl3501_get_encode(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	u8 implemented, restricted, keys[100], len_keys, tocopy;
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	int rc = wl3501_get_mib_value(this,
				      WL3501_MIB_ATTR_PRIV_OPT_IMPLEMENTED,
				      &implemented, sizeof(implemented));
	if (rc)
		goto out;
	if (!implemented) {
		wrqu->encoding.flags = IW_ENCODE_DISABLED;
		goto out;
	}
	rc = wl3501_get_mib_value(this, WL3501_MIB_ATTR_EXCLUDE_UNENCRYPTED,
				  &restricted, sizeof(restricted));
	if (rc)
		goto out;
	wrqu->encoding.flags = restricted ? IW_ENCODE_RESTRICTED :
					    IW_ENCODE_OPEN;
	rc = wl3501_get_mib_value(this, WL3501_MIB_ATTR_WEP_KEY_MAPPINGS_LEN,
				  &len_keys, sizeof(len_keys));
	if (rc)
		goto out;
	rc = wl3501_get_mib_value(this, WL3501_MIB_ATTR_WEP_KEY_MAPPINGS,
				  keys, len_keys);
	if (rc)
		goto out;
	tocopy = min_t(u8, len_keys, wrqu->encoding.length);
	tocopy = min_t(u8, tocopy, 100);
	wrqu->encoding.length = tocopy;
	memset(extra, 0, tocopy);
	memcpy(extra, keys, tocopy);
out:
	return rc;
}

static int wl3501_get_power(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	u8 pwr_state;
	struct wl3501_card *this = (struct wl3501_card *)dev->priv;
	int rc = wl3501_get_mib_value(this,
				      WL3501_MIB_ATTR_CURRENT_PWR_STATE,
				      &pwr_state, sizeof(pwr_state));
	if (rc)
		goto out;
	wrqu->power.disabled = !pwr_state;
	wrqu->power.flags = IW_POWER_ON;
out:
	return rc;
}

static const iw_handler	wl3501_handler[] = {
	[SIOCGIWNAME	- SIOCIWFIRST] = wl3501_get_name,
	[SIOCSIWFREQ	- SIOCIWFIRST] = wl3501_set_freq,
	[SIOCGIWFREQ	- SIOCIWFIRST] = wl3501_get_freq,
	[SIOCSIWMODE	- SIOCIWFIRST] = wl3501_set_mode,
	[SIOCGIWMODE	- SIOCIWFIRST] = wl3501_get_mode,
	[SIOCGIWSENS	- SIOCIWFIRST] = wl3501_get_sens,
	[SIOCGIWRANGE	- SIOCIWFIRST] = wl3501_get_range,
	[SIOCSIWSPY	- SIOCIWFIRST] = iw_handler_set_spy,
	[SIOCGIWSPY	- SIOCIWFIRST] = iw_handler_get_spy,
	[SIOCSIWTHRSPY	- SIOCIWFIRST] = iw_handler_set_thrspy,
	[SIOCGIWTHRSPY	- SIOCIWFIRST] = iw_handler_get_thrspy,
	[SIOCSIWAP	- SIOCIWFIRST] = wl3501_set_wap,
	[SIOCGIWAP	- SIOCIWFIRST] = wl3501_get_wap,
	[SIOCSIWESSID	- SIOCIWFIRST] = wl3501_set_essid,
	[SIOCGIWESSID	- SIOCIWFIRST] = wl3501_get_essid,
	[SIOCSIWNICKN	- SIOCIWFIRST] = wl3501_set_nick,
	[SIOCGIWNICKN	- SIOCIWFIRST] = wl3501_get_nick,
	[SIOCGIWRATE	- SIOCIWFIRST] = wl3501_get_rate,
	[SIOCGIWRTS	- SIOCIWFIRST] = wl3501_get_rts_threshold,
	[SIOCGIWFRAG	- SIOCIWFIRST] = wl3501_get_frag_threshold,
	[SIOCGIWTXPOW	- SIOCIWFIRST] = wl3501_get_txpow,
	[SIOCGIWRETRY	- SIOCIWFIRST] = wl3501_get_retry,
	[SIOCGIWENCODE	- SIOCIWFIRST] = wl3501_get_encode,
	[SIOCGIWPOWER	- SIOCIWFIRST] = wl3501_get_power,
};

static const struct iw_handler_def wl3501_handler_def = {
	.num_standard	= sizeof(wl3501_handler) / sizeof(iw_handler),
	.standard	= (iw_handler *)wl3501_handler,
	.spy_offset	= offsetof(struct wl3501_card, spy_data),
};

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
	init_timer(&link->release);
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
	if (wl3501_irq_list[0] != -1)
		for (i = 0; i < 4; i++)
			link->irq.IRQInfo2 |= 1 << wl3501_irq_list[i];
	link->irq.Handler = wl3501_interrupt;

	/* General socket configuration */
	link->conf.Attributes = CONF_ENABLE_IRQ;
	link->conf.Vcc = 50;
	link->conf.IntType = INT_MEMORY_AND_IO;
	link->conf.ConfigIndex = 1;
	link->conf.Present = PRESENT_OPTION;

	dev = alloc_etherdev(sizeof(struct wl3501_card));
	if (!dev)
		goto out_link;
	dev->init		= wl3501_init;
	dev->open		= wl3501_open;
	dev->stop		= wl3501_close;
	dev->hard_start_xmit	= wl3501_hard_start_xmit;
	dev->tx_timeout		= wl3501_tx_timeout;
	dev->watchdog_timeo	= 5 * HZ;
	dev->get_stats		= wl3501_get_stats;
	dev->get_wireless_stats = wl3501_get_wireless_stats;
	dev->do_ioctl		= wl3501_ioctl;
	dev->wireless_handlers	= (struct iw_handler_def *)&wl3501_handler_def;
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
	if (register_netdev(dev)) {
		printk(KERN_NOTICE "wl3501_cs: register_netdev() failed\n");
		goto failed;
	}

	SET_MODULE_OWNER(dev);

	/*
	 * At this point, the dev_node_t structure(s) should be initialized and
	 * arranged in a linked list at link->dev.
	 */
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
	/*
	 * Initialize card parameters - added by jss
	 */
	this->net_type		= IW_MODE_INFRA;
	this->bss_cnt		= 0;
	this->join_sta_bss	= 0;
	this->adhoc_times	= 0;
	this->essid[0]		= 0;
	this->essid[1]		= 3;
	this->essid[2]		= 'A';
	this->essid[3]		= 'N';
	this->essid[4]		= 'Y';
	this->card_name[0]	= '\0';
	this->firmware_date[0]	= '\0';
	this->rssi		= 255;
	strlcpy(this->nick, "Planet WL3501", sizeof(this->nick));
	spin_lock_init(&this->lock);
	init_waitqueue_head(&this->wait);

	switch (this->reg_domain) {
	case WL3501_REG_DOMAIN_SPAIN:
	case WL3501_REG_DOMAIN_FRANCE:
		this->def_chan = 10;
		break;
	case WL3501_REG_DOMAIN_MKK:
		this->def_chan = 14;
		break;
	case WL3501_REG_DOMAIN_FCC:
	case WL3501_REG_DOMAIN_IC:
	case WL3501_REG_DOMAIN_ETSI:
	default:
		this->def_chan = 1;
		break;
	}
	netif_start_queue(dev);
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
 * @arg - link
 *
 * After a card is removed, wl3501_release() will unregister the net device,
 * and release the PCMCIA configuration.  If the device is still open, this
 * will be postponed until it is closed.
 */
static void wl3501_release(unsigned long arg)
{
	dev_link_t *link = (dev_link_t *)arg;
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
			while (link->open > 0)
				wl3501_close(dev);
			netif_device_detach(dev);
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
			if (link->open)
				netif_device_detach(dev);
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
			if (link->open) {
				wl3501_reset(dev);
				netif_device_attach(dev);
			}
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
		del_timer(&wl3501_dev_list->release);
		/* Mark the device as non-existing to minimize calls to card */
		wl3501_dev_list->state &= ~DEV_PRESENT;
		if (wl3501_dev_list->state & DEV_CONFIG)
			wl3501_release((unsigned long)wl3501_dev_list);
		wl3501_detach(wl3501_dev_list);
	}
}

module_init(wl3501_init_module);
module_exit(wl3501_exit_module);

MODULE_PARM(wl3501_irq_mask, "i");
MODULE_PARM(wl3501_irq_list, "1-4i");
MODULE_AUTHOR("Fox Chen <mhchen@golf.ccl.itri.org.tw>, "
	      "Arnaldo Carvalho de Melo <acme@conectiva.com.br>,"
	      "Gustavo Niemeyer <niemeyer@conectiva.com>");
MODULE_DESCRIPTION("Planet wl3501 wireless driver");
MODULE_LICENSE("GPL");
