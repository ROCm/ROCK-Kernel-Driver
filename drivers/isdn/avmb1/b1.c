/*
 * $Id: b1.c,v 1.20 2000/11/23 20:45:14 kai Exp $
 * 
 * Common module for AVM B1 cards.
 * 
 * (c) Copyright 1999 by Carsten Paeth (calle@calle.in-berlin.de)
 * 
 * $Log: b1.c,v $
 * Revision 1.20  2000/11/23 20:45:14  kai
 * fixed module_init/exit stuff
 * Note: compiled-in kernel doesn't work pre 2.2.18 anymore.
 *
 * Revision 1.19  2000/11/19 17:02:47  kai
 * compatibility cleanup - part 3
 *
 * Revision 1.18  2000/11/19 17:01:53  kai
 * compatibility cleanup - part 2
 *
 * Revision 1.17  2000/11/01 14:05:02  calle
 * - use module_init/module_exit from linux/init.h.
 * - all static struct variables are initialized with "membername:" now.
 * - avm_cs.c, let it work with newer pcmcia-cs.
 *
 * Revision 1.16  2000/08/04 15:36:31  calle
 * copied wrong from file to file :-(
 *
 * Revision 1.15  2000/08/04 12:20:08  calle
 * - Fix unsigned/signed warning in the right way ...
 *
 * Revision 1.14  2000/06/19 16:51:53  keil
 * don't free skb in irq context
 *
 * Revision 1.13  2000/01/25 14:33:38  calle
 * - Added Support AVM B1 PCI V4.0 (tested with prototype)
 *   - splitted up t1pci.c into b1dma.c for common function with b1pciv4
 *   - support for revision register
 *
 * Revision 1.12  1999/11/05 16:38:01  calle
 * Cleanups before kernel 2.4:
 * - Changed all messages to use card->name or driver->name instead of
 *   constant string.
 * - Moved some data from struct avmcard into new struct avmctrl_info.
 *   Changed all lowlevel capi driver to match the new structur.
 *
 * Revision 1.11  1999/10/11 22:04:12  keil
 * COMPAT_NEED_UACCESS (no include in isdn_compat.h)
 *
 * Revision 1.10  1999/09/15 08:16:03  calle
 * Implementation of 64Bit extention complete.
 *
 * Revision 1.9  1999/09/07 09:02:53  calle
 * SETDATA removed. Now inside the kernel the datapart of DATA_B3_REQ and
 * DATA_B3_IND is always directly after the CAPI message. The "Data" member
 * ist never used inside the kernel.
 *
 * Revision 1.8  1999/08/22 20:26:22  calle
 * backported changes from kernel 2.3.14:
 * - several #include "config.h" gone, others come.
 * - "struct device" changed to "struct net_device" in 2.3.14, added a
 *   define in isdn_compat.h for older kernel versions.
 *
 * Revision 1.7  1999/08/04 10:10:09  calle
 * Bugfix: corrected /proc functions, added structure for new AVM cards.
 *
 * Revision 1.6  1999/07/23 08:51:04  calle
 * small fix and typo in checkin before.
 *
 * Revision 1.5  1999/07/23 08:41:48  calle
 * prepared for new AVM cards.
 *
 * Revision 1.4  1999/07/09 15:05:38  keil
 * compat.h is now isdn_compat.h
 *
 * Revision 1.3  1999/07/06 07:41:59  calle
 * - changes in /proc interface
 * - check and changed calls to [dev_]kfree_skb and [dev_]alloc_skb.
 *
 * Revision 1.2  1999/07/05 15:09:47  calle
 * - renamed "appl_release" to "appl_released".
 * - version und profile data now cleared on controller reset
 * - extended /proc interface, to allow driver and controller specific
 *   informations to include by driver hackers.
 *
 * Revision 1.1  1999/07/01 15:26:23  calle
 * complete new version (I love it):
 * + new hardware independed "capi_driver" interface that will make it easy to:
 *   - support other controllers with CAPI-2.0 (i.e. USB Controller)
 *   - write a CAPI-2.0 for the passive cards
 *   - support serial link CAPI-2.0 boxes.
 * + wrote "capi_driver" for all supported cards.
 * + "capi_driver" (supported cards) now have to be configured with
 *   make menuconfig, in the past all supported cards where included
 *   at once.
 * + new and better informations in /proc/capi/
 * + new ioctl to switch trace of capi messages per controller
 *   using "avmcapictrl trace [contr] on|off|...."
 * + complete testcircle with all supported cards and also the
 *   PCMCIA cards (now patch for pcmcia-cs-3.0.13 needed) done.
 *
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/capi.h>
#include <asm/io.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/netdevice.h>
#include "capilli.h"
#include "avmcard.h"
#include "capicmd.h"
#include "capiutil.h"

static char *revision = "$Revision: 1.20 $";

/* ------------------------------------------------------------- */

MODULE_AUTHOR("Carsten Paeth <calle@calle.in-berlin.de>");

/* ------------------------------------------------------------- */

int b1_irq_table[16] =
{0,
 0,
 0,
 192,				/* irq 3 */
 32,				/* irq 4 */
 160,				/* irq 5 */
 96,				/* irq 6 */
 224,				/* irq 7 */
 0,
 64,				/* irq 9 */
 80,				/* irq 10 */
 208,				/* irq 11 */
 48,				/* irq 12 */
 0,
 0,
 112,				/* irq 15 */
};

/* ------------------------------------------------------------- */

int b1_detect(unsigned int base, enum avmcardtype cardtype)
{
	int onoff, i;

	/*
	 * Statusregister 0000 00xx 
	 */
	if ((inb(base + B1_INSTAT) & 0xfc)
	    || (inb(base + B1_OUTSTAT) & 0xfc))
		return 1;
	/*
	 * Statusregister 0000 001x 
	 */
	b1outp(base, B1_INSTAT, 0x2);	/* enable irq */
	/* b1outp(base, B1_OUTSTAT, 0x2); */
	if ((inb(base + B1_INSTAT) & 0xfe) != 0x2
	    /* || (inb(base + B1_OUTSTAT) & 0xfe) != 0x2 */)
		return 2;
	/*
	 * Statusregister 0000 000x 
	 */
	b1outp(base, B1_INSTAT, 0x0);	/* disable irq */
	b1outp(base, B1_OUTSTAT, 0x0);
	if ((inb(base + B1_INSTAT) & 0xfe)
	    || (inb(base + B1_OUTSTAT) & 0xfe))
		return 3;
        
	for (onoff = !0, i= 0; i < 10 ; i++) {
		b1_set_test_bit(base, cardtype, onoff);
		if (b1_get_test_bit(base, cardtype) != onoff)
		   return 4;
		onoff = !onoff;
	}

	if (cardtype == avm_m1)
	   return 0;

        if ((b1_rd_reg(base, B1_STAT1(cardtype)) & 0x0f) != 0x01)
	   return 5;

	return 0;
}

void b1_getrevision(avmcard *card)
{
    card->class = inb(card->port + B1_ANALYSE);
    card->revision = inb(card->port + B1_REVISION);
}

int b1_load_t4file(avmcard *card, capiloaddatapart * t4file)
{
	unsigned char buf[256];
	unsigned char *dp;
	int i, left, retval;
	unsigned int base = card->port;

	dp = t4file->data;
	left = t4file->len;
	while (left > sizeof(buf)) {
		if (t4file->user) {
			retval = copy_from_user(buf, dp, sizeof(buf));
			if (retval)
				return -EFAULT;
		} else {
			memcpy(buf, dp, sizeof(buf));
		}
		for (i = 0; i < sizeof(buf); i++)
			if (b1_save_put_byte(base, buf[i]) < 0) {
				printk(KERN_ERR "%s: corrupted firmware file ?\n",
						card->name);
				return -EIO;
			}
		left -= sizeof(buf);
		dp += sizeof(buf);
	}
	if (left) {
		if (t4file->user) {
			retval = copy_from_user(buf, dp, left);
			if (retval)
				return -EFAULT;
		} else {
			memcpy(buf, dp, left);
		}
		for (i = 0; i < left; i++)
			if (b1_save_put_byte(base, buf[i]) < 0) {
				printk(KERN_ERR "%s: corrupted firmware file ?\n",
						card->name);
				return -EIO;
			}
	}
	return 0;
}

int b1_load_config(avmcard *card, capiloaddatapart * config)
{
	unsigned char buf[256];
	unsigned char *dp;
	unsigned int base = card->port;
	int i, j, left, retval;

	dp = config->data;
	left = config->len;
	if (left) {
		b1_put_byte(base, SEND_CONFIG);
        	b1_put_word(base, 1);
		b1_put_byte(base, SEND_CONFIG);
        	b1_put_word(base, left);
	}
	while (left > sizeof(buf)) {
		if (config->user) {
			retval = copy_from_user(buf, dp, sizeof(buf));
			if (retval)
				return -EFAULT;
		} else {
			memcpy(buf, dp, sizeof(buf));
		}
		for (i = 0; i < sizeof(buf); ) {
			b1_put_byte(base, SEND_CONFIG);
			for (j=0; j < 4; j++) {
				b1_put_byte(base, buf[i++]);
			}
		}
		left -= sizeof(buf);
		dp += sizeof(buf);
	}
	if (left) {
		if (config->user) {
			retval = copy_from_user(buf, dp, left);
			if (retval)
				return -EFAULT;
		} else {
			memcpy(buf, dp, left);
		}
		for (i = 0; i < left; ) {
			b1_put_byte(base, SEND_CONFIG);
			for (j=0; j < 4; j++) {
				if (i < left)
					b1_put_byte(base, buf[i++]);
				else
					b1_put_byte(base, 0);
			}
		}
	}
	return 0;
}

int b1_loaded(avmcard *card)
{
	unsigned int base = card->port;
	unsigned long stop;
	unsigned char ans;
	unsigned long tout = 2;

	for (stop = jiffies + tout * HZ; time_before(jiffies, stop);) {
		if (b1_tx_empty(base))
			break;
	}
	if (!b1_tx_empty(base)) {
		printk(KERN_ERR "%s: b1_loaded: tx err, corrupted t4 file ?\n",
				card->name);
		return 0;
	}
	b1_put_byte(base, SEND_POLL);
	for (stop = jiffies + tout * HZ; time_before(jiffies, stop);) {
		if (b1_rx_full(base)) {
			if ((ans = b1_get_byte(base)) == RECEIVE_POLL) {
				return 1;
			}
			printk(KERN_ERR "%s: b1_loaded: got 0x%x, firmware not running\n",
					card->name, ans);
			return 0;
		}
	}
	printk(KERN_ERR "%s: b1_loaded: firmware not running\n", card->name);
	return 0;
}

/* ------------------------------------------------------------- */

int b1_load_firmware(struct capi_ctr *ctrl, capiloaddata *data)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;
	unsigned long flags;
	int retval;

	b1_reset(port);

	if ((retval = b1_load_t4file(card, &data->firmware))) {
		b1_reset(port);
		printk(KERN_ERR "%s: failed to load t4file!!\n",
					card->name);
		return retval;
	}

	b1_disable_irq(port);

	if (data->configuration.len > 0 && data->configuration.data) {
		if ((retval = b1_load_config(card, &data->configuration))) {
			b1_reset(port);
			printk(KERN_ERR "%s: failed to load config!!\n",
					card->name);
			return retval;
		}
	}

	if (!b1_loaded(card)) {
		printk(KERN_ERR "%s: failed to load t4file.\n", card->name);
		return -EIO;
	}

	save_flags(flags);
	cli();
	b1_setinterrupt(port, card->irq, card->cardtype);
	b1_put_byte(port, SEND_INIT);
	b1_put_word(port, AVM_NAPPS);
	b1_put_word(port, AVM_NCCI_PER_CHANNEL*2);
	b1_put_word(port, ctrl->cnr - 1);
	restore_flags(flags);

	return 0;
}

void b1_reset_ctr(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;

	b1_reset(port);
	b1_reset(port);

	memset(cinfo->version, 0, sizeof(cinfo->version));
	ctrl->reseted(ctrl);
}

void b1_register_appl(struct capi_ctr *ctrl,
				__u16 appl,
				capi_register_params *rp)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;
	unsigned long flags;
	int nconn, want = rp->level3cnt;

	if (want > 0) nconn = want;
	else nconn = ctrl->profile.nbchannel * -want;
	if (nconn == 0) nconn = ctrl->profile.nbchannel;

	save_flags(flags);
	cli();
	b1_put_byte(port, SEND_REGISTER);
	b1_put_word(port, appl);
	b1_put_word(port, 1024 * (nconn+1));
	b1_put_word(port, nconn);
	b1_put_word(port, rp->datablkcnt);
	b1_put_word(port, rp->datablklen);
	restore_flags(flags);

	ctrl->appl_registered(ctrl, appl);
}

void b1_release_appl(struct capi_ctr *ctrl, __u16 appl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;
	unsigned long flags;

	save_flags(flags);
	cli();
	b1_put_byte(port, SEND_RELEASE);
	b1_put_word(port, appl);
	restore_flags(flags);
}

void b1_send_message(struct capi_ctr *ctrl, struct sk_buff *skb)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;
	unsigned long flags;
	__u16 len = CAPIMSG_LEN(skb->data);
	__u8 cmd = CAPIMSG_COMMAND(skb->data);
	__u8 subcmd = CAPIMSG_SUBCOMMAND(skb->data);

	save_flags(flags);
	cli();
	if (CAPICMD(cmd, subcmd) == CAPI_DATA_B3_REQ) {
		__u16 dlen = CAPIMSG_DATALEN(skb->data);
		b1_put_byte(port, SEND_DATA_B3_REQ);
		b1_put_slice(port, skb->data, len);
		b1_put_slice(port, skb->data + len, dlen);
	} else {
		b1_put_byte(port, SEND_MESSAGE);
		b1_put_slice(port, skb->data, len);
	}
	restore_flags(flags);
	dev_kfree_skb_any(skb);
}

/* ------------------------------------------------------------- */

void b1_parse_version(avmctrl_info *cinfo)
{
	struct capi_ctr *ctrl = cinfo->capi_ctrl;
	avmcard *card = cinfo->card;
	capi_profile *profp;
	__u8 *dversion;
	__u8 flag;
	int i, j;

	for (j = 0; j < AVM_MAXVERSION; j++)
		cinfo->version[j] = "\0\0" + 1;
	for (i = 0, j = 0;
	     j < AVM_MAXVERSION && i < cinfo->versionlen;
	     j++, i += cinfo->versionbuf[i] + 1)
		cinfo->version[j] = &cinfo->versionbuf[i + 1];

	strncpy(ctrl->serial, cinfo->version[VER_SERIAL], CAPI_SERIAL_LEN);
	memcpy(&ctrl->profile, cinfo->version[VER_PROFILE],sizeof(capi_profile));
	strncpy(ctrl->manu, "AVM GmbH", CAPI_MANUFACTURER_LEN);
	dversion = cinfo->version[VER_DRIVER];
	ctrl->version.majorversion = 2;
	ctrl->version.minorversion = 0;
	ctrl->version.majormanuversion = (((dversion[0] - '0') & 0xf) << 4);
	ctrl->version.majormanuversion |= ((dversion[2] - '0') & 0xf);
	ctrl->version.minormanuversion = (dversion[3] - '0') << 4;
	ctrl->version.minormanuversion |=
			(dversion[5] - '0') * 10 + ((dversion[6] - '0') & 0xf);

	profp = &ctrl->profile;

	flag = ((__u8 *)(profp->manu))[1];
	switch (flag) {
	case 0: if (cinfo->version[VER_CARDTYPE])
	           strcpy(cinfo->cardname, cinfo->version[VER_CARDTYPE]);
	        else strcpy(cinfo->cardname, "B1");
		break;
	case 3: strcpy(cinfo->cardname,"PCMCIA B"); break;
	case 4: strcpy(cinfo->cardname,"PCMCIA M1"); break;
	case 5: strcpy(cinfo->cardname,"PCMCIA M2"); break;
	case 6: strcpy(cinfo->cardname,"B1 V3.0"); break;
	case 7: strcpy(cinfo->cardname,"B1 PCI"); break;
	default: sprintf(cinfo->cardname, "AVM?%u", (unsigned int)flag); break;
        }
        printk(KERN_NOTICE "%s: card %d \"%s\" ready.\n",
				card->name, ctrl->cnr, cinfo->cardname);

        flag = ((__u8 *)(profp->manu))[3];
        if (flag)
		printk(KERN_NOTICE "%s: card %d Protocol:%s%s%s%s%s%s%s\n",
			card->name,
			ctrl->cnr,
			(flag & 0x01) ? " DSS1" : "",
			(flag & 0x02) ? " CT1" : "",
			(flag & 0x04) ? " VN3" : "",
			(flag & 0x08) ? " NI1" : "",
			(flag & 0x10) ? " AUSTEL" : "",
			(flag & 0x20) ? " ESS" : "",
			(flag & 0x40) ? " 1TR6" : ""
			);

        flag = ((__u8 *)(profp->manu))[5];
	if (flag)
		printk(KERN_NOTICE "%s: card %d Linetype:%s%s%s%s\n",
			card->name,
			ctrl->cnr,
			(flag & 0x01) ? " point to point" : "",
			(flag & 0x02) ? " point to multipoint" : "",
			(flag & 0x08) ? " leased line without D-channel" : "",
			(flag & 0x04) ? " leased line with D-channel" : ""
			);
}

/* ------------------------------------------------------------- */

void b1_handle_interrupt(avmcard * card)
{
	avmctrl_info *cinfo = &card->ctrlinfo[0];
	struct capi_ctr *ctrl = cinfo->capi_ctrl;
	unsigned char b1cmd;
	struct sk_buff *skb;

	unsigned ApplId;
	unsigned MsgLen;
	unsigned DataB3Len;
	unsigned NCCI;
	unsigned WindowSize;

	if (!b1_rx_full(card->port))
	   return;

	b1cmd = b1_get_byte(card->port);

	switch (b1cmd) {

	case RECEIVE_DATA_B3_IND:

		ApplId = (unsigned) b1_get_word(card->port);
		MsgLen = b1_get_slice(card->port, card->msgbuf);
		DataB3Len = b1_get_slice(card->port, card->databuf);

		if (MsgLen < 30) { /* not CAPI 64Bit */
			memset(card->msgbuf+MsgLen, 0, 30-MsgLen);
			MsgLen = 30;
			CAPIMSG_SETLEN(card->msgbuf, 30);
		}
		if (!(skb = alloc_skb(DataB3Len + MsgLen, GFP_ATOMIC))) {
			printk(KERN_ERR "%s: incoming packet dropped\n",
					card->name);
		} else {
			memcpy(skb_put(skb, MsgLen), card->msgbuf, MsgLen);
			memcpy(skb_put(skb, DataB3Len), card->databuf, DataB3Len);
			ctrl->handle_capimsg(ctrl, ApplId, skb);
		}
		break;

	case RECEIVE_MESSAGE:

		ApplId = (unsigned) b1_get_word(card->port);
		MsgLen = b1_get_slice(card->port, card->msgbuf);
		if (!(skb = alloc_skb(MsgLen, GFP_ATOMIC))) {
			printk(KERN_ERR "%s: incoming packet dropped\n",
					card->name);
		} else {
			memcpy(skb_put(skb, MsgLen), card->msgbuf, MsgLen);
			ctrl->handle_capimsg(ctrl, ApplId, skb);
		}
		break;

	case RECEIVE_NEW_NCCI:

		ApplId = b1_get_word(card->port);
		NCCI = b1_get_word(card->port);
		WindowSize = b1_get_word(card->port);

		ctrl->new_ncci(ctrl, ApplId, NCCI, WindowSize);

		break;

	case RECEIVE_FREE_NCCI:

		ApplId = b1_get_word(card->port);
		NCCI = b1_get_word(card->port);

		if (NCCI != 0xffffffff)
			ctrl->free_ncci(ctrl, ApplId, NCCI);
		else ctrl->appl_released(ctrl, ApplId);
		break;

	case RECEIVE_START:
	   	/* b1_put_byte(card->port, SEND_POLLACK); */
		ctrl->resume_output(ctrl);
		break;

	case RECEIVE_STOP:
		ctrl->suspend_output(ctrl);
		break;

	case RECEIVE_INIT:

		cinfo->versionlen = b1_get_slice(card->port, cinfo->versionbuf);
		b1_parse_version(cinfo);
		printk(KERN_INFO "%s: %s-card (%s) now active\n",
		       card->name,
		       cinfo->version[VER_CARDTYPE],
		       cinfo->version[VER_DRIVER]);
		ctrl->ready(ctrl);
		break;

	case RECEIVE_TASK_READY:
		ApplId = (unsigned) b1_get_word(card->port);
		MsgLen = b1_get_slice(card->port, card->msgbuf);
		card->msgbuf[MsgLen] = 0;
		while (    MsgLen > 0
		       && (   card->msgbuf[MsgLen-1] == '\n'
			   || card->msgbuf[MsgLen-1] == '\r')) {
			card->msgbuf[MsgLen-1] = 0;
			MsgLen--;
		}
		printk(KERN_INFO "%s: task %d \"%s\" ready.\n",
				card->name, ApplId, card->msgbuf);
		break;

	case RECEIVE_DEBUGMSG:
		MsgLen = b1_get_slice(card->port, card->msgbuf);
		card->msgbuf[MsgLen] = 0;
		while (    MsgLen > 0
		       && (   card->msgbuf[MsgLen-1] == '\n'
			   || card->msgbuf[MsgLen-1] == '\r')) {
			card->msgbuf[MsgLen-1] = 0;
			MsgLen--;
		}
		printk(KERN_INFO "%s: DEBUG: %s\n", card->name, card->msgbuf);
		break;

	case 0xff:
		printk(KERN_ERR "%s: card removed ?\n", card->name);
		return;
	default:
		printk(KERN_ERR "%s: b1_interrupt: 0x%x ???\n",
				card->name, b1cmd);
		return;
	}
}

/* ------------------------------------------------------------- */
int b1ctl_read_proc(char *page, char **start, off_t off,
        		int count, int *eof, struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	__u8 flag;
	int len = 0;
	char *s;

	len += sprintf(page+len, "%-16s %s\n", "name", card->name);
	len += sprintf(page+len, "%-16s 0x%x\n", "io", card->port);
	len += sprintf(page+len, "%-16s %d\n", "irq", card->irq);
	switch (card->cardtype) {
	case avm_b1isa: s = "B1 ISA"; break;
	case avm_b1pci: s = "B1 PCI"; break;
	case avm_b1pcmcia: s = "B1 PCMCIA"; break;
	case avm_m1: s = "M1"; break;
	case avm_m2: s = "M2"; break;
	case avm_t1isa: s = "T1 ISA (HEMA)"; break;
	case avm_t1pci: s = "T1 PCI"; break;
	case avm_c4: s = "C4"; break;
	default: s = "???"; break;
	}
	len += sprintf(page+len, "%-16s %s\n", "type", s);
	if (card->cardtype == avm_t1isa)
	   len += sprintf(page+len, "%-16s %d\n", "cardnr", card->cardnr);
	if ((s = cinfo->version[VER_DRIVER]) != 0)
	   len += sprintf(page+len, "%-16s %s\n", "ver_driver", s);
	if ((s = cinfo->version[VER_CARDTYPE]) != 0)
	   len += sprintf(page+len, "%-16s %s\n", "ver_cardtype", s);
	if ((s = cinfo->version[VER_SERIAL]) != 0)
	   len += sprintf(page+len, "%-16s %s\n", "ver_serial", s);

	if (card->cardtype != avm_m1) {
        	flag = ((__u8 *)(ctrl->profile.manu))[3];
        	if (flag)
			len += sprintf(page+len, "%-16s%s%s%s%s%s%s%s\n",
			"protocol",
			(flag & 0x01) ? " DSS1" : "",
			(flag & 0x02) ? " CT1" : "",
			(flag & 0x04) ? " VN3" : "",
			(flag & 0x08) ? " NI1" : "",
			(flag & 0x10) ? " AUSTEL" : "",
			(flag & 0x20) ? " ESS" : "",
			(flag & 0x40) ? " 1TR6" : ""
			);
	}
	if (card->cardtype != avm_m1) {
        	flag = ((__u8 *)(ctrl->profile.manu))[5];
		if (flag)
			len += sprintf(page+len, "%-16s%s%s%s%s\n",
			"linetype",
			(flag & 0x01) ? " point to point" : "",
			(flag & 0x02) ? " point to multipoint" : "",
			(flag & 0x08) ? " leased line without D-channel" : "",
			(flag & 0x04) ? " leased line with D-channel" : ""
			);
	}
	len += sprintf(page+len, "%-16s %s\n", "cardname", cinfo->cardname);

	if (off+count >= len)
	   *eof = 1;
	if (len < off)
           return 0;
	*start = page + off;
	return ((count < len-off) ? count : len-off);
}

/* ------------------------------------------------------------- */

EXPORT_SYMBOL(b1_irq_table);

EXPORT_SYMBOL(b1_detect);
EXPORT_SYMBOL(b1_getrevision);
EXPORT_SYMBOL(b1_load_t4file);
EXPORT_SYMBOL(b1_load_config);
EXPORT_SYMBOL(b1_loaded);
EXPORT_SYMBOL(b1_load_firmware);
EXPORT_SYMBOL(b1_reset_ctr);
EXPORT_SYMBOL(b1_register_appl);
EXPORT_SYMBOL(b1_release_appl);
EXPORT_SYMBOL(b1_send_message);

EXPORT_SYMBOL(b1_parse_version);
EXPORT_SYMBOL(b1_handle_interrupt);

EXPORT_SYMBOL(b1ctl_read_proc);

static int __init b1_init(void)
{
	char *p;
	char rev[10];

	if ((p = strchr(revision, ':'))) {
		strncpy(rev, p + 1, sizeof(rev));
		p = strchr(rev, '$');
		*p = 0;
	} else
		strcpy(rev, "1.0");

	printk(KERN_INFO "b1: revision %s\n", rev);

	return 0;
}

static void __exit b1_exit(void)
{
}

module_init(b1_init);
module_exit(b1_exit);
