/*
 * Universal Host Controller Interface driver for USB.
 *
 * Maintainer: Alan Stern <stern@rowland.harvard.edu>
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999-2002 Johannes Erdfelt, johannes@erdfelt.com
 * (C) Copyright 1999 Randy Dunlap
 * (C) Copyright 1999 Georg Acher, acher@in.tum.de
 * (C) Copyright 1999 Deti Fliegl, deti@fliegl.de
 * (C) Copyright 1999 Thomas Sailer, sailer@ife.ee.ethz.ch
 * (C) Copyright 2004 Alan Stern, stern@rowland.harvard.edu
 */

static __u8 root_hub_hub_des[] =
{
	0x09,			/*  __u8  bLength; */
	0x29,			/*  __u8  bDescriptorType; Hub-descriptor */
	0x02,			/*  __u8  bNbrPorts; */
	0x0a,			/* __u16  wHubCharacteristics; */
	0x00,			/*   (per-port OC, no power switching) */
	0x01,			/*  __u8  bPwrOn2pwrGood; 2ms */
	0x00,			/*  __u8  bHubContrCurrent; 0 mA */
	0x00,			/*  __u8  DeviceRemovable; *** 7 Ports max *** */
	0xff			/*  __u8  PortPwrCtrlMask; *** 7 ports max *** */
};

#define	UHCI_RH_MAXCHILD	7

/* must write as zeroes */
#define WZ_BITS		(USBPORTSC_RES2 | USBPORTSC_RES3 | USBPORTSC_RES4)

/* status change bits:  nonzero writes will clear */
#define RWC_BITS	(USBPORTSC_OCC | USBPORTSC_PEC | USBPORTSC_CSC)

static int uhci_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);
	unsigned long io_addr = uhci->io_addr;
	int i;

	*buf = 0;
	for (i = 0; i < uhci->rh_numports; i++) {
		if (inw(io_addr + USBPORTSC1 + i * 2) & RWC_BITS)
			*buf |= (1 << (i + 1));
	}
	return !!*buf;
}

#define OK(x)			len = (x); break

#define CLR_RH_PORTSTAT(x) \
	status = inw(port_addr); \
	status &= ~(RWC_BITS|WZ_BITS); \
	status &= ~(x); \
	status |= RWC_BITS & (x); \
	outw(status, port_addr)

#define SET_RH_PORTSTAT(x) \
	status = inw(port_addr); \
	status |= (x); \
	status &= ~(RWC_BITS|WZ_BITS); \
	outw(status, port_addr)


/* size of returned buffer is part of USB spec */
static int uhci_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
			u16 wIndex, char *buf, u16 wLength)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);
	int status, retval = 0, len = 0;
	unsigned long port_addr = uhci->io_addr + USBPORTSC1 + 2 * (wIndex-1);
	__u16 wPortChange, wPortStatus;

	switch (typeReq) {
		/* Request Destination:
		   without flags: Device,
		   RH_INTERFACE: interface,
		   RH_ENDPOINT: endpoint,
		   RH_CLASS means HUB here,
		   RH_OTHER | RH_CLASS  almost ever means HUB_PORT here
		*/

	case GetHubStatus:
		*(__u32 *)buf = cpu_to_le32(0);
		OK(4);		/* hub power */
	case GetPortStatus:
		if (!wIndex || wIndex > uhci->rh_numports)
			goto err;
		status = inw(port_addr);

		/* Intel controllers report the OverCurrent bit active on.
		 * VIA controllers report it active off, so we'll adjust the
		 * bit value.  (It's not standardized in the UHCI spec.)
		 */
		if (to_pci_dev(hcd->self.controller)->vendor ==
				PCI_VENDOR_ID_VIA)
			status ^= USBPORTSC_OC;

		/* UHCI doesn't support C_SUSPEND and C_RESET (always false) */
		wPortChange = 0;
		if (status & USBPORTSC_CSC)
			wPortChange |= 1 << (USB_PORT_FEAT_C_CONNECTION - 16);
		if (status & USBPORTSC_PEC)
			wPortChange |= 1 << (USB_PORT_FEAT_C_ENABLE - 16);
		if (status & USBPORTSC_OCC)
			wPortChange |= 1 << (USB_PORT_FEAT_C_OVER_CURRENT - 16);

		/* UHCI has no power switching (always on) */
		wPortStatus = 1 << USB_PORT_FEAT_POWER;
		if (status & USBPORTSC_CCS)
			wPortStatus |= 1 << USB_PORT_FEAT_CONNECTION;
		if (status & USBPORTSC_PE) {
			wPortStatus |= 1 << USB_PORT_FEAT_ENABLE;
			if (status & (USBPORTSC_SUSP | USBPORTSC_RD))
				wPortStatus |= 1 << USB_PORT_FEAT_SUSPEND;
		}
		if (status & USBPORTSC_OC)
			wPortStatus |= 1 << USB_PORT_FEAT_OVER_CURRENT;
		if (status & USBPORTSC_PR)
			wPortStatus |= 1 << USB_PORT_FEAT_RESET;
		if (status & USBPORTSC_LSDA)
			wPortStatus |= 1 << USB_PORT_FEAT_LOWSPEED;

		if (wPortChange)
			dev_dbg(uhci_dev(uhci), "port %d portsc %04x\n",
					wIndex, status);

		*(__u16 *)buf = cpu_to_le16(wPortStatus);
		*(__u16 *)(buf + 2) = cpu_to_le16(wPortChange);
		OK(4);
	case SetHubFeature:		/* We don't implement these */
	case ClearHubFeature:
		switch (wValue) {
		case C_HUB_OVER_CURRENT:
		case C_HUB_LOCAL_POWER:
			OK(0);
		default:
			goto err;
		}
		break;
	case SetPortFeature:
		if (!wIndex || wIndex > uhci->rh_numports)
			goto err;

		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			SET_RH_PORTSTAT(USBPORTSC_SUSP);
			OK(0);
		case USB_PORT_FEAT_RESET:
			SET_RH_PORTSTAT(USBPORTSC_PR);
			mdelay(50);	/* USB v1.1 7.1.7.3 */
			CLR_RH_PORTSTAT(USBPORTSC_PR);
			udelay(10);
			SET_RH_PORTSTAT(USBPORTSC_PE);
			mdelay(10);
			CLR_RH_PORTSTAT(USBPORTSC_PEC|USBPORTSC_CSC);
			OK(0);
		case USB_PORT_FEAT_POWER:
			/* UHCI has no power switching */
			OK(0);
		default:
			goto err;
		}
		break;
	case ClearPortFeature:
		if (!wIndex || wIndex > uhci->rh_numports)
			goto err;

		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			CLR_RH_PORTSTAT(USBPORTSC_PE);
			OK(0);
		case USB_PORT_FEAT_C_ENABLE:
			CLR_RH_PORTSTAT(USBPORTSC_PEC);
			OK(0);
		case USB_PORT_FEAT_SUSPEND:
			CLR_RH_PORTSTAT(USBPORTSC_SUSP);
			OK(0);
		case USB_PORT_FEAT_C_SUSPEND:
			/* this driver won't report these */
			OK(0);
		case USB_PORT_FEAT_POWER:
			/* UHCI has no power switching */
			goto err;
		case USB_PORT_FEAT_C_CONNECTION:
			CLR_RH_PORTSTAT(USBPORTSC_CSC);
			OK(0);
		case USB_PORT_FEAT_C_OVER_CURRENT:
			CLR_RH_PORTSTAT(USBPORTSC_OCC);
			OK(0);
		case USB_PORT_FEAT_C_RESET:
			/* this driver won't report these */
			OK(0);
		default:
			goto err;
		}
		break;
	case GetHubDescriptor:
		len = min_t(unsigned int, sizeof(root_hub_hub_des), wLength);
		memcpy(buf, root_hub_hub_des, len);
		if (len > 2)
			buf[2] = uhci->rh_numports;
		OK(len);
	default:
err:
		retval = -EPIPE;
	}

	return retval;
}
