/*
 * usbfront-hub.c
 *
 * Xen USB Virtual Host Controller - Root Hub Emulations
 *
 * Copyright (C) 2009, FUJITSU LABORATORIES LTD.
 * Author: Noboru Iwamatsu <n_iwamatsu@jp.fujitsu.com>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * or, by your choice,
 *
 * When distributed separately from the Linux kernel or incorporated into
 * other software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * set virtual port connection status
 */
void set_connect_state(struct usbfront_info *info, int portnum)
{
	int port;

	port = portnum - 1;
	if (info->ports[port].status & USB_PORT_STAT_POWER) {
		switch (info->devices[port].speed) {
		case USB_SPEED_UNKNOWN:
			info->ports[port].status &=
				~(USB_PORT_STAT_CONNECTION |
					USB_PORT_STAT_ENABLE |
					USB_PORT_STAT_LOW_SPEED |
					USB_PORT_STAT_HIGH_SPEED |
					USB_PORT_STAT_SUSPEND);
			break;
		case USB_SPEED_LOW:
			info->ports[port].status |= USB_PORT_STAT_CONNECTION;
			info->ports[port].status |= USB_PORT_STAT_LOW_SPEED;
			break;
		case USB_SPEED_FULL:
			info->ports[port].status |= USB_PORT_STAT_CONNECTION;
			break;
		case USB_SPEED_HIGH:
			info->ports[port].status |= USB_PORT_STAT_CONNECTION;
			info->ports[port].status |= USB_PORT_STAT_HIGH_SPEED;
			break;
		default: /* error */
			return;
		}
		info->ports[port].status |= (USB_PORT_STAT_C_CONNECTION << 16);
	}
}

/*
 * set virtual device connection status
 */
void rhport_connect(struct usbfront_info *info,
				int portnum, enum usb_device_speed speed)
{
	int port;

	if (portnum < 1 || portnum > info->rh_numports)
		return; /* invalid port number */

	port = portnum - 1;
	if (info->devices[port].speed != speed) {
		switch (speed) {
		case USB_SPEED_UNKNOWN: /* disconnect */
			info->devices[port].status = USB_STATE_NOTATTACHED;
			break;
		case USB_SPEED_LOW:
		case USB_SPEED_FULL:
		case USB_SPEED_HIGH:
			info->devices[port].status = USB_STATE_ATTACHED;
			break;
		default: /* error */
			return;
		}
		info->devices[port].speed = speed;
		info->ports[port].c_connection = 1;

		set_connect_state(info, portnum);
	}
}

/*
 * SetPortFeature(PORT_SUSPENDED)
 */
void rhport_suspend(struct usbfront_info *info, int portnum)
{
	int port;

	port = portnum - 1;
	info->ports[port].status |= USB_PORT_STAT_SUSPEND;
	info->devices[port].status = USB_STATE_SUSPENDED;
}

/*
 * ClearPortFeature(PORT_SUSPENDED)
 */
void rhport_resume(struct usbfront_info *info, int portnum)
{
	int port;

	port = portnum - 1;
	if (info->ports[port].status & USB_PORT_STAT_SUSPEND) {
		info->ports[port].resuming = 1;
		info->ports[port].timeout = jiffies + msecs_to_jiffies(20);
	}
}

/*
 * SetPortFeature(PORT_POWER)
 */
void rhport_power_on(struct usbfront_info *info, int portnum)
{
	int port;

	port = portnum - 1;
	if ((info->ports[port].status & USB_PORT_STAT_POWER) == 0) {
		info->ports[port].status |= USB_PORT_STAT_POWER;
		if (info->devices[port].status != USB_STATE_NOTATTACHED)
			info->devices[port].status = USB_STATE_POWERED;
		if (info->ports[port].c_connection)
			set_connect_state(info, portnum);
	}
}

/*
 * ClearPortFeature(PORT_POWER)
 * SetConfiguration(non-zero)
 * Power_Source_Off
 * Over-current
 */
void rhport_power_off(struct usbfront_info *info, int portnum)
{
	int port;

	port = portnum - 1;
	if (info->ports[port].status & USB_PORT_STAT_POWER) {
		info->ports[port].status = 0;
		if (info->devices[port].status != USB_STATE_NOTATTACHED)
			info->devices[port].status = USB_STATE_ATTACHED;
	}
}

/*
 * ClearPortFeature(PORT_ENABLE)
 */
void rhport_disable(struct usbfront_info *info, int portnum)
{
	int port;

	port = portnum - 1;
	info->ports[port].status &= ~USB_PORT_STAT_ENABLE;
	info->ports[port].status &= ~USB_PORT_STAT_SUSPEND;
	info->ports[port].resuming = 0;
	if (info->devices[port].status != USB_STATE_NOTATTACHED)
		info->devices[port].status = USB_STATE_POWERED;
}

/*
 * SetPortFeature(PORT_RESET)
 */
void rhport_reset(struct usbfront_info *info, int portnum)
{
	int port;

	port = portnum - 1;
	info->ports[port].status &= ~(USB_PORT_STAT_ENABLE
					| USB_PORT_STAT_LOW_SPEED
					| USB_PORT_STAT_HIGH_SPEED);
	info->ports[port].status |= USB_PORT_STAT_RESET;

	if (info->devices[port].status != USB_STATE_NOTATTACHED)
		info->devices[port].status = USB_STATE_ATTACHED;

	/* 10msec reset signaling */
	info->ports[port].timeout = jiffies + msecs_to_jiffies(10);
}

#ifdef XENHCD_PM
#ifdef CONFIG_PM
static int xenhcd_bus_suspend(struct usb_hcd *hcd)
{
	struct usbfront_info *info = hcd_to_info(hcd);
	int ret = 0;
	int i, ports;

	ports = info->rh_numports;

	spin_lock_irq(&info->lock);
	if (!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags))
		ret = -ESHUTDOWN;
	else {
		/* suspend any active ports*/
		for (i = 1; i <= ports; i++)
			rhport_suspend(info, i);
	}
	spin_unlock_irq(&info->lock);

	del_timer_sync(&info->watchdog);

	return ret;
}

static int xenhcd_bus_resume(struct usb_hcd *hcd)
{
	struct usbfront_info *info = hcd_to_info(hcd);
	int ret = 0;
	int i, ports;

	ports = info->rh_numports;

	spin_lock_irq(&info->lock);
	if (!test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags))
		ret = -ESHUTDOWN;
	else {
		/* resume any suspended ports*/
		for (i = 1; i <= ports; i++)
			rhport_resume(info, i);
	}
	spin_unlock_irq(&info->lock);

	return ret;
}
#endif
#endif

static void xenhcd_hub_descriptor(struct usbfront_info *info,
				  struct usb_hub_descriptor *desc)
{
	u16 temp;
	int ports = info->rh_numports;

	desc->bDescriptorType = 0x29;
	desc->bPwrOn2PwrGood = 10; /* EHCI says 20ms max */
	desc->bHubContrCurrent = 0;
	desc->bNbrPorts = ports;

	/* size of DeviceRemovable and PortPwrCtrlMask fields*/
	temp = 1 + (ports / 8);
	desc->bDescLength = 7 + 2 * temp;

	/* bitmaps for DeviceRemovable and PortPwrCtrlMask */
	memset(&desc->bitmap[0], 0, temp);
	memset(&desc->bitmap[temp], 0xff, temp);

	/* per-port over current reporting and no power switching */
	temp = 0x000a;
	desc->wHubCharacteristics = cpu_to_le16(temp);
}

/* port status change mask for hub_status_data */
#define PORT_C_MASK \
	((USB_PORT_STAT_C_CONNECTION \
	| USB_PORT_STAT_C_ENABLE \
	| USB_PORT_STAT_C_SUSPEND \
	| USB_PORT_STAT_C_OVERCURRENT \
	| USB_PORT_STAT_C_RESET) << 16)

/*
 * See USB 2.0 Spec, 11.12.4 Hub and Port Status Change Bitmap.
 * If port status changed, writes the bitmap to buf and return
 * that length(number of bytes).
 * If Nothing changed, return 0.
 */
static int xenhcd_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct usbfront_info *info = hcd_to_info(hcd);

	int ports;
	int i;
	int length;

	unsigned long flags;
	int ret = 0;

	int changed = 0;

	if (!HC_IS_RUNNING(hcd->state))
		return 0;

	/* initialize the status to no-changes */
	ports = info->rh_numports;
	length = 1 + (ports / 8);
	for (i = 0; i < length; i++) {
		buf[i] = 0;
		ret++;
	}

	spin_lock_irqsave(&info->lock, flags);

	for (i = 0; i < ports; i++) {
		/* check status for each port */
		if (info->ports[i].status & PORT_C_MASK) {
			if (i < 7)
				buf[0] |= 1 << (i + 1);
			else if (i < 15)
				buf[1] |= 1 << (i - 7);
			else if (i < 23)
				buf[2] |= 1 << (i - 15);
			else
				buf[3] |= 1 << (i - 23);
			changed = 1;
		}
	}

	if (!changed)
		ret = 0;

	spin_unlock_irqrestore(&info->lock, flags);

	return ret;
}

static int xenhcd_hub_control(struct usb_hcd *hcd,
			       u16 typeReq,
			       u16 wValue,
			       u16 wIndex,
			       char *buf,
			       u16 wLength)
{
	struct usbfront_info *info = hcd_to_info(hcd);
	int ports = info->rh_numports;
	unsigned long flags;
	int ret = 0;
	int i;
	int changed = 0;

	spin_lock_irqsave(&info->lock, flags);
	switch (typeReq) {
	case ClearHubFeature:
		/* ignore this request */
		break;
	case ClearPortFeature:
		if (!wIndex || wIndex > ports)
			goto error;

		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			rhport_resume(info, wIndex);
			break;
		case USB_PORT_FEAT_POWER:
			rhport_power_off(info, wIndex);
			break;
		case USB_PORT_FEAT_ENABLE:
			rhport_disable(info, wIndex);
			break;
		case USB_PORT_FEAT_C_CONNECTION:
			info->ports[wIndex-1].c_connection = 0;
			/* falling through */
		default:
			info->ports[wIndex-1].status &= ~(1 << wValue);
			break;
		}
		break;
	case GetHubDescriptor:
		xenhcd_hub_descriptor(info,
				      (struct usb_hub_descriptor *) buf);
		break;
	case GetHubStatus:
		/* always local power supply good and no over-current exists. */
		*(__le32 *)buf = cpu_to_le32(0);
		break;
	case GetPortStatus:
		if (!wIndex || wIndex > ports)
			goto error;

		wIndex--;

		/* resume completion */
		if (info->ports[wIndex].resuming &&
			time_after_eq(jiffies, info->ports[wIndex].timeout)) {
			info->ports[wIndex].status |= (USB_PORT_STAT_C_SUSPEND << 16);
			info->ports[wIndex].status &= ~USB_PORT_STAT_SUSPEND;
		}

		/* reset completion */
		if ((info->ports[wIndex].status & USB_PORT_STAT_RESET) != 0 &&
			time_after_eq(jiffies, info->ports[wIndex].timeout)) {
			info->ports[wIndex].status |= (USB_PORT_STAT_C_RESET << 16);
			info->ports[wIndex].status &= ~USB_PORT_STAT_RESET;

			if (info->devices[wIndex].status != USB_STATE_NOTATTACHED) {
				info->ports[wIndex].status |= USB_PORT_STAT_ENABLE;
				info->devices[wIndex].status = USB_STATE_DEFAULT;
			}

			switch (info->devices[wIndex].speed) {
			case USB_SPEED_LOW:
				info->ports[wIndex].status |= USB_PORT_STAT_LOW_SPEED;
				break;
			case USB_SPEED_HIGH:
				info->ports[wIndex].status |= USB_PORT_STAT_HIGH_SPEED;
				break;
			default:
				break;
			}
		}

		((u16 *) buf)[0] = cpu_to_le16 (info->ports[wIndex].status);
		((u16 *) buf)[1] = cpu_to_le16 (info->ports[wIndex].status >> 16);
		break;
	case SetHubFeature:
		/* not supported */
		goto error;
	case SetPortFeature:
		if (!wIndex || wIndex > ports)
			goto error;

		switch (wValue) {
		case USB_PORT_FEAT_POWER:
			rhport_power_on(info, wIndex);
			break;
		case USB_PORT_FEAT_RESET:
			rhport_reset(info, wIndex);
			break;
		case USB_PORT_FEAT_SUSPEND:
			rhport_suspend(info, wIndex);
			break;
		default:
			if ((info->ports[wIndex-1].status & USB_PORT_STAT_POWER) != 0)
				info->ports[wIndex-1].status |= (1 << wValue);
		}
		break;

	default:
error:
		ret = -EPIPE;
	}
	spin_unlock_irqrestore(&info->lock, flags);

	/* check status for each port */
	for (i = 0; i < ports; i++) {
		if (info->ports[i].status & PORT_C_MASK)
			changed = 1;
	}
	if (changed)
		usb_hcd_poll_rh_status(hcd);

	return ret;
}
