/*
 * UHCI-specific debugging code. Invaluable when something
 * goes wrong, but don't get in my face.
 *
 * Kernel visible pointers are surrounded in []'s and bus
 * visible pointers are surrounded in ()'s
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999 Johannes Erdfelt
 */

#include <linux/kernel.h>
#include <asm/io.h>

#include "uhci.h"

void uhci_show_td(struct uhci_td *td)
{
	char *spid;

	printk("%08x ", td->link);
	printk("e%d %s%s%s%s%s%s%s%s%s%sLength=%x ",
		((td->status >> 27) & 3),
		(td->status & TD_CTRL_SPD) ?      "SPD " : "",
		(td->status & TD_CTRL_LS) ?       "LS " : "",
		(td->status & TD_CTRL_IOC) ?      "IOC " : "",
		(td->status & TD_CTRL_ACTIVE) ?   "Active " : "",
		(td->status & TD_CTRL_STALLED) ?  "Stalled " : "",
		(td->status & TD_CTRL_DBUFERR) ?  "DataBufErr " : "",
		(td->status & TD_CTRL_BABBLE) ?   "Babble " : "",
		(td->status & TD_CTRL_NAK) ?      "NAK " : "",
		(td->status & TD_CTRL_CRCTIMEO) ? "CRC/Timeo " : "",
		(td->status & TD_CTRL_BITSTUFF) ? "BitStuff " : "",
		td->status & 0x7ff);

	switch (td->info & 0xff) {
		case USB_PID_SETUP:
			spid = "SETUP";
			break;
		case USB_PID_OUT:
			spid = "OUT";
			break;
		case USB_PID_IN:
			spid = "IN";
			break;
		default:
			spid = "?";
			break;
	}

	printk("MaxLen=%x DT%d EndPt=%x Dev=%x, PID=%x(%s) ",
		td->info >> 21,
		((td->info >> 19) & 1),
		(td->info >> 15) & 15,
		(td->info >> 8) & 127,
		(td->info & 0xff),
		spid);
	printk("(buf=%08x)\n", td->buffer);
}

static void uhci_show_sc(int port, unsigned short status)
{
	printk("  stat%d     =     %04x   %s%s%s%s%s%s%s%s\n",
		port,
		status,
		(status & USBPORTSC_SUSP) ? "PortSuspend " : "",
		(status & USBPORTSC_PR) ?   "PortReset " : "",
		(status & USBPORTSC_LSDA) ? "LowSpeed " : "",
		(status & USBPORTSC_RD) ?   "ResumeDetect " : "",
		(status & USBPORTSC_PEC) ?  "EnableChange " : "",
		(status & USBPORTSC_PE) ?   "PortEnabled " : "",
		(status & USBPORTSC_CSC) ?  "ConnectChange " : "",
		(status & USBPORTSC_CCS) ?  "PortConnected " : "");
}

void uhci_show_status(struct uhci *uhci)
{
	unsigned int io_addr = uhci->io_addr;
	unsigned short usbcmd, usbstat, usbint, usbfrnum;
	unsigned int flbaseadd;
	unsigned char sof;
	unsigned short portsc1, portsc2;

	usbcmd    = inw(io_addr + 0);
	usbstat   = inw(io_addr + 2);
	usbint    = inw(io_addr + 4);
	usbfrnum  = inw(io_addr + 6);
	flbaseadd = inl(io_addr + 8);
	sof       = inb(io_addr + 12);
	portsc1   = inw(io_addr + 16);
	portsc2   = inw(io_addr + 18);

	printk("  usbcmd    =     %04x   %s%s%s%s%s%s%s%s\n",
		usbcmd,
		(usbcmd & USBCMD_MAXP) ?    "Maxp64 " : "Maxp32 ",
		(usbcmd & USBCMD_CF) ?      "CF " : "",
		(usbcmd & USBCMD_SWDBG) ?   "SWDBG " : "",
		(usbcmd & USBCMD_FGR) ?     "FGR " : "",
		(usbcmd & USBCMD_EGSM) ?    "EGSM " : "",
		(usbcmd & USBCMD_GRESET) ?  "GRESET " : "",
		(usbcmd & USBCMD_HCRESET) ? "HCRESET " : "",
		(usbcmd & USBCMD_RS) ?      "RS " : "");

	printk("  usbstat   =     %04x   %s%s%s%s%s%s\n",
		usbstat,
		(usbstat & USBSTS_HCH) ?    "HCHalted " : "",
		(usbstat & USBSTS_HCPE) ?   "HostControllerProcessError " : "",
		(usbstat & USBSTS_HSE) ?    "HostSystemError " : "",
		(usbstat & USBSTS_RD) ?     "ResumeDetect " : "",
		(usbstat & USBSTS_ERROR) ?  "USBError " : "",
		(usbstat & USBSTS_USBINT) ? "USBINT " : "");

	printk("  usbint    =     %04x\n", usbint);
	printk("  usbfrnum  =   (%d)%03x\n", (usbfrnum >> 10) & 1,
		0xfff & (4*(unsigned int)usbfrnum));
	printk("  flbaseadd = %08x\n", flbaseadd);
	printk("  sof       =       %02x\n", sof);
	uhci_show_sc(1, portsc1);
	uhci_show_sc(2, portsc2);
}

#define uhci_link_to_qh(x) ((struct uhci_qh *) uhci_link_to_td(x))

struct uhci_td *uhci_link_to_td(unsigned int link)
{
	if (link & UHCI_PTR_TERM)
		return NULL;

	return bus_to_virt(link & ~UHCI_PTR_BITS);
}

void uhci_show_urb_queue(struct urb *urb)
{
	struct urb_priv *urbp = urb->hcpriv;
	struct list_head *head, *tmp;
	int i, checked = 0, prevactive = 0;

	printk("  URB [%p] urbp [%p]\n", urb, urbp);

	if (urbp->qh)
		printk("    QH [%p]\n", urbp->qh);
	else
		printk("    QH [%p] element (%08x) link (%08x)\n", urbp->qh,
			urbp->qh->element, urbp->qh->link);

	i = 0;

	head = &urbp->list;
	tmp = head->next;
	while (tmp != head) {
		struct uhci_td *td = list_entry(tmp, struct uhci_td, list);

		tmp = tmp->next;

		printk("      td %d: [%p]\n", i++, td);
		printk("      ");
		uhci_show_td(td);

		if (i > 10 && !checked && prevactive && tmp != head) {
			struct list_head *ntmp = tmp;
			struct uhci_td *ntd = td;
			int active = 1, ni = i;

			checked = 1;

			while (ntmp != head && ntmp->next != head && active) {
				ntd = list_entry(ntmp, struct uhci_td, list);

				ntmp = ntmp->next;

				active = ntd->status & TD_CTRL_ACTIVE;

				ni++;
			}

			if (active && ni > i) {
				printk("      [skipped %d active TD's]\n", ni - i);
				tmp = ntmp;
				td = ntd;
				i = ni;
			}
		}

		prevactive = td->status & TD_CTRL_ACTIVE;
	}
}

void uhci_show_queue(struct uhci_qh *qh)
{
	struct uhci_td *td, *first;
	int i = 0, count = 1000;

	if (qh->element & UHCI_PTR_QH)
		printk("      Element points to QH (bug?)\n");

	if (qh->element & UHCI_PTR_DEPTH)
		printk("      Depth traverse\n");

	if (qh->element & UHCI_PTR_TERM)
		printk("      Terminate\n");

	if (!(qh->element & ~UHCI_PTR_BITS)) {
		printk("      td 0: [NULL]\n");
		return;
	}

	first = uhci_link_to_td(qh->element);

	/* Make sure it doesn't runaway */
	for (td = first; td && count > 0; 
	     td = uhci_link_to_td(td->link), --count) {
		printk("      td %d: [%p]\n", i++, td);
		printk("      ");
		uhci_show_td(td);

		if (td == uhci_link_to_td(td->link)) {
			printk(KERN_ERR "td links to itself!\n");
			break;
		}
	}
}

static int uhci_is_skeleton_td(struct uhci *uhci, struct uhci_td *td)
{
	int j;

	for (j = 0; j < UHCI_NUM_SKELTD; j++)
		if (td == uhci->skeltd + j)
			return 1;

	return 0;
}

static int uhci_is_skeleton_qh(struct uhci *uhci, struct uhci_qh *qh)
{
	int j;

	for (j = 0; j < UHCI_NUM_SKELQH; j++)
		if (qh == uhci->skelqh + j)
			return 1;

	return 0;
}

static const char *td_names[] = {"interrupt1", "interrupt2", "interrupt4",
				 "interrupt8", "interrupt16", "interrupt32",
				 "interrupt64", "interrupt128", "interrupt256" };
static const char *qh_names[] = { "control", "bulk" };

void uhci_show_queues(struct uhci *uhci)
{
	int i, isqh = 0;
	struct uhci_qh *qh;
	struct uhci_td *td;

	for (i = 0; i < UHCI_NUMFRAMES; ++i) {
		int shown = 0;

		td = uhci_link_to_td(uhci->fl->frame[i]);
		if (td)
			isqh = uhci->fl->frame[i] & UHCI_PTR_QH;
		while (td && !isqh) {
			if (uhci_is_skeleton_td(uhci, td))
				break;

			if (!shown) {
				printk("   Frame %d\n", i);
				shown = 1;
			}

			printk("[%p] ", td);

			uhci_show_td(td);
			td = uhci_link_to_td(td->link);
			if (td)
				isqh = td->link & UHCI_PTR_QH;
		}
	}
	for (i = 0; i < UHCI_NUM_SKELTD; ++i) {
		printk("  %s: [%p] (%08x)\n", td_names[i],
			&uhci->skeltd[i],
			uhci->skeltd[i].link);

		td = uhci_link_to_td(uhci->skeltd[i].link);
		if (td)
			isqh = uhci->skeltd[i].link & UHCI_PTR_QH;
		while (td && !isqh) {
			if (uhci_is_skeleton_td(uhci, td))
				break;

			printk("[%p] ", td);

			uhci_show_td(td);
			td = uhci_link_to_td(td->link);
			if (td)
				isqh = td->link & UHCI_PTR_QH;
		}
	}
	for (i = 0; i < UHCI_NUM_SKELQH; ++i) {
		printk("  %s: [%p] (%08x) (%08x)\n", qh_names[i],
			&uhci->skelqh[i],
			uhci->skelqh[i].link, uhci->skelqh[i].element);

		qh = uhci_link_to_qh(uhci->skelqh[i].link);
		for (; qh; qh = uhci_link_to_qh(qh->link)) {
			if (uhci_is_skeleton_qh(uhci, qh))
				break;

			printk("    [%p] (%08x) (%08x)\n",
				qh, qh->link, qh->element);

			uhci_show_queue(qh);
		}
	}
}

