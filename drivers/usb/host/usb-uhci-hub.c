/*  
    UHCI HCD (Host Controller Driver) for USB, UHCI Root Hub
    
    (c) 1999-2002 
    Georg Acher      +    Deti Fliegl    +    Thomas Sailer
    georg@acher.org      deti@fliegl.de   sailer@ife.ee.ethz.ch
   
    with the help of
    David Brownell, david-b@pacbell.net
    Adam Richter, adam@yggdrasil.com
    Roman Weissgaerber, weissg@vienna.at
    
    HW-initalization based on material of
    Randy Dunlap + Johannes Erdfelt + Gregory P. Smith + Linus Torvalds 

    $Id: usb-uhci-hub.c,v 1.2 2002/05/21 21:40:16 acher Exp $
*/

#define CLR_RH_PORTSTAT(x) \
		status = inw(io_addr+USBPORTSC1+2*(wIndex-1)); \
		status = (status & 0xfff5) & ~(x); \
		outw(status, io_addr+USBPORTSC1+2*(wIndex-1))

#define SET_RH_PORTSTAT(x) \
		status = inw(io_addr+USBPORTSC1+2*(wIndex-1)); \
		status = (status & 0xfff5) | (x); \
		outw(status, io_addr+USBPORTSC1+2*(wIndex-1))

static int oldval=-1;

/* build "status change" packet (one or two bytes) from HC registers 
   Since uhci_hub_status_data is called by a SW timer, it is also used
   for monitoring HC health */
static int uhci_hub_status_data (struct usb_hcd *hcd, char *buf)
{
	struct uhci_hcd *uhci = hcd_to_uhci (hcd);
	unsigned long io_addr = (unsigned long)uhci->hcd.regs;
	int i, len=0, data = 0, portstate;
	int changed=0;

	for (i = 0; i < uhci->maxports; i++) {
		portstate=inw (io_addr + USBPORTSC1 + i * 2);
#if 0
		if (i==0 && (portstate&0xf) != (oldval&0xf))
			err("Port %i: %x", i+1, portstate);
#endif
		if (i==0)
			oldval=portstate;
		if ((portstate & 0xa) > 0) {
			changed=1;
		}
		data |= ((portstate & 0xa) > 0 ? (1 << (i + 1)) : 0);
		len = (i + 1) / 8 + 1;
	}


	*(__u16 *)buf = cpu_to_le16 (data);

	// Watchdog

	if (uhci->running && time_after(jiffies, uhci->last_hcd_irq + WATCHDOG_TIMEOUT)) {
		if (uhci->reanimations > MAX_REANIMATIONS) {
			err("He's dead, Jim. Giving up reanimating the UHCI host controller.\n"
			    "Maybe a real module reload helps...");
			uhci->running = 0;
		}
		else {	
			uhci->running = 0;
			uhci->need_init=1; // init done in the next submit_urb
		}
	}
	return changed?len:0;
}
/*-------------------------------------------------------------------------*/
static void uhci_hub_descriptor (struct uhci_hcd *uhci, struct usb_hub_descriptor *desc) 
{
	int		ports = uhci->maxports;
	u16		temp;
	desc->bDescriptorType = 0x29;
	desc->bPwrOn2PwrGood = 1;
	desc->bHubContrCurrent = 0;
	desc->bNbrPorts = ports;
	temp = 1 + (ports / 8);
	desc->bDescLength = 7 + 2 * temp;
	desc->wHubCharacteristics = 0;	
	desc-> bitmap[0] = 0;
	desc-> bitmap[1] = 0xff;
}
/*-------------------------------------------------------------------------*/
static int uhci_hub_control (
        struct usb_hcd  *hcd,
        u16             typeReq,
        u16             wValue,
        u16             wIndex,
        char            *buf,
        u16             wLength) 
{
	struct uhci_hcd *uhci = hcd_to_uhci (hcd);
	int status = 0;
	int stat = 0;
	int cstatus;
	unsigned long io_addr = (unsigned long)uhci->hcd.regs;
	int		ports = uhci->maxports;

	switch (typeReq) {	

	case ClearHubFeature:
		break;

	case ClearPortFeature:
		if (!wIndex || wIndex > ports)
                        goto error;

		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			CLR_RH_PORTSTAT (USBPORTSC_PE);
			break;
		case USB_PORT_FEAT_C_ENABLE:
			SET_RH_PORTSTAT (USBPORTSC_PEC);
			break;

		case USB_PORT_FEAT_SUSPEND:
			CLR_RH_PORTSTAT (USBPORTSC_SUSP);
			break;
		case USB_PORT_FEAT_C_SUSPEND:
			/*** WR_RH_PORTSTAT(RH_PS_PSSC); */
			break;

		case USB_PORT_FEAT_POWER:
		       	break;/* port power ** */

		case USB_PORT_FEAT_C_CONNECTION:
			SET_RH_PORTSTAT (USBPORTSC_CSC);
			break;
		
		case USB_PORT_FEAT_C_OVER_CURRENT:
			break;	/* port power over current ** */

		case USB_PORT_FEAT_C_RESET:
			break;
		default:
			goto error;
		}
		break;

	case GetHubDescriptor:
                uhci_hub_descriptor (uhci, (struct usb_hub_descriptor *) buf);
                break;

	case GetHubStatus:
		*(u32 *) buf = cpu_to_le32 (0);
		break;

	case GetPortStatus:
		if (!wIndex || wIndex > ports)
			goto error;

		status = inw (io_addr + USBPORTSC1 + 2 * (wIndex - 1));
		cstatus = ((status & USBPORTSC_CSC) >> (1 - 0)) |
			((status & USBPORTSC_PEC) >> (3 - 1));

		status = (status & USBPORTSC_CCS) |
			((status & USBPORTSC_PE) >> (2 - 1)) |
			((status & USBPORTSC_SUSP) >> (12 - 2)) |
			((status & USBPORTSC_PR) >> (9 - 4)) |
			(1 << 8) |	/* power on ** */
			((status & USBPORTSC_LSDA) << (-8 + 9));

		*(__u16 *) buf = cpu_to_le16 (status);
		*(__u16 *) (buf + 2) = cpu_to_le16 (cstatus);
		break;

	case SetHubFeature:
		// FIXME
		break;

	case SetPortFeature: 
		if (!wIndex || wIndex > ports)
			goto error;
			
		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			SET_RH_PORTSTAT (USBPORTSC_SUSP);
			break;
		case USB_PORT_FEAT_RESET:
			SET_RH_PORTSTAT (USBPORTSC_PR);
			uhci_wait_ms (10);
			CLR_RH_PORTSTAT (USBPORTSC_PR);
			udelay (10);
			SET_RH_PORTSTAT (USBPORTSC_PE);
			uhci_wait_ms (10);
			SET_RH_PORTSTAT (0xa);
			break;
		case USB_PORT_FEAT_POWER:
			break;	/* port power ** */
		case  USB_PORT_FEAT_ENABLE:
			SET_RH_PORTSTAT (USBPORTSC_PE);
			break;
		default:
			goto error;
		}
		break;
				
	default:
error:		
		stat = -EPIPE;
	}

	dbg("Root-Hub stat port1: %x port2: %x",
	     inw (io_addr + USBPORTSC1), inw (io_addr + USBPORTSC2));

	return stat;
}
