/*
 * usbmidi.c - ALSA USB MIDI driver
 *
 * Copyright (c) 2002 Clemens Ladisch
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed and/or modified under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sound/driver.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/asequencer.h>
#include <sound/seq_device.h>
#include <sound/seq_kernel.h>
#include <sound/seq_virmidi.h>
#include <sound/seq_midi_event.h>
#include <sound/initval.h>
#include "usbaudio.h"

MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_DESCRIPTION("USB MIDI");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_CLASSES("{sound}");

/* size of the per-endpoint output buffer, must be a multiple of 4 */
#define OUTPUT_BUFFER_SIZE 0x400

/* max. size of incoming sysex messages */
#define INPUT_BUFFER_SIZE 0x200

typedef struct usb_driver usb_driver_t;
typedef struct usb_device usb_device_t;
typedef struct usb_device_id usb_device_id_t;
typedef struct usb_interface usb_interface_t;
typedef struct usb_interface_descriptor usb_interface_descriptor_t;
typedef struct usb_ms_header_descriptor usb_ms_header_descriptor_t;
typedef struct usb_endpoint_descriptor usb_endpoint_descriptor_t;
typedef struct usb_ms_endpoint_descriptor usb_ms_endpoint_descriptor_t;

struct usb_ms_header_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubtype;
	__u8  bcdMSC[2];
	__u16 wTotalLength;
} __attribute__ ((packed));

struct usb_ms_endpoint_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubtype;
	__u8  bNumEmbMIDIJack;
	__u8  baAssocJackID[0];
} __attribute__ ((packed));

typedef struct usbmidi_out_port usbmidi_out_port_t;
typedef struct usbmidi_in_port usbmidi_in_port_t;

struct snd_usb_midi_out_endpoint {
	snd_usb_midi_t* umidi;
	struct urb* urb;
	int max_transfer;		/* size of urb buffer */
	struct tasklet_struct tasklet;

	uint8_t buffer[OUTPUT_BUFFER_SIZE]; /* ring buffer */
	int data_begin;
	int data_size;
	spinlock_t buffer_lock;

	struct usbmidi_out_port {
		snd_usb_midi_out_endpoint_t* ep;
		uint8_t cable;		/* cable number << 4 */
		uint8_t sysex_len;
		uint8_t sysex[2];
	} ports[0x10];
};

struct snd_usb_midi_in_endpoint {
	snd_usb_midi_t* umidi;
	snd_usb_midi_endpoint_t* ep;
	struct urb* urb;
	struct usbmidi_in_port {
		int seq_port;
		snd_midi_event_t* midi_event;
	} ports[0x10];
};

static void snd_usbmidi_do_output(snd_usb_midi_out_endpoint_t* ep);

/*
 * Submits the URB, with error handling.
 */
static int snd_usbmidi_submit_urb(struct urb* urb, int flags)
{
	int err = usb_submit_urb(urb, flags);
	if (err < 0 && err != -ENODEV)
		printk(KERN_ERR "snd-usb-midi: usb_submit_urb: %d\n", err);
	return err;
}

/*
 * Error handling for URB completion functions.
 */
static int snd_usbmidi_urb_error(int status)
{
	if (status == -ENOENT)
		return status; /* killed */
	if (status == -ENODEV ||
	    status == -EILSEQ ||
	    status == -ETIMEDOUT)
		return -ENODEV; /* device removed */
	printk(KERN_ERR "snd-usb-midi: urb status %d\n", status);
	return 0; /* continue */
}

/*
 * Converts a USB MIDI packet into an ALSA sequencer event.
 */
static void snd_usbmidi_input_packet(snd_usb_midi_in_endpoint_t* ep,
				     uint8_t packet[4])
{
	static const uint8_t cin_length[] = {
		0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1
	};
	int cable = packet[0] >> 4;
	usbmidi_in_port_t* port = &ep->ports[cable];
	snd_seq_event_t ev;

	if (!port->midi_event)
		return;
	memset(&ev, 0, sizeof(ev));
	if (snd_midi_event_encode(port->midi_event, &packet[1],
				  cin_length[packet[0] & 0x0f], &ev) > 0
	    && ev.type != SNDRV_SEQ_EVENT_NONE) {
		ev.source.port = port->seq_port;
		ev.dest.client = SNDRV_SEQ_ADDRESS_SUBSCRIBERS;
		snd_seq_kernel_client_dispatch(ep->umidi->seq_client,
					       &ev, 1, 0);
		if (ep->ep->rmidi[cable])
			snd_virmidi_receive(ep->ep->rmidi[cable], &ev);
	}
}

/*
 * Processes the data read from the device.
 */
static void snd_usbmidi_in_urb_complete(struct urb* urb)
{
	snd_usb_midi_in_endpoint_t* ep = snd_magic_cast(snd_usb_midi_in_endpoint_t, urb->context, return);

	if (urb->status == 0) {
		uint8_t* buffer = (uint8_t*)ep->urb->transfer_buffer;
		int i;

		for (i = 0; i + 4 <= urb->actual_length; i += 4)
			if (buffer[i] != 0)
				snd_usbmidi_input_packet(ep, &buffer[i]);
	} else {
		if (snd_usbmidi_urb_error(urb->status) < 0)
			return;
	}

	if (!usb_pipeint(urb->pipe)) {
		urb->dev = ep->umidi->chip->dev;
		snd_usbmidi_submit_urb(urb, GFP_ATOMIC);
	}
}

static void snd_usbmidi_out_urb_complete(struct urb* urb)
{
	snd_usb_midi_out_endpoint_t* ep = snd_magic_cast(snd_usb_midi_out_endpoint_t, urb->context, return);
	unsigned long flags;

	if (urb->status < 0) {
		if (snd_usbmidi_urb_error(urb->status) < 0)
			return;
	}
	spin_lock_irqsave(&ep->buffer_lock, flags);
	snd_usbmidi_do_output(ep);
	spin_unlock_irqrestore(&ep->buffer_lock, flags);
}

/*
 * This is called when some data should be transferred to the device
 * (after the reception of one or more sequencer events, or after completion
 * of the previous transfer). ep->buffer_lock must be held.
 */
static void snd_usbmidi_do_output(snd_usb_midi_out_endpoint_t* ep)
{
	int len;
	uint8_t* buffer;

	if (ep->urb->status == -EINPROGRESS ||
	    ep->data_size == 0)
		return;
	buffer = (uint8_t*)ep->urb->transfer_buffer;

	/* first chunk, up to the end of the buffer */
	len = OUTPUT_BUFFER_SIZE - ep->data_begin;
	if (len > ep->data_size)
		len = ep->data_size;
	if (len > ep->max_transfer)
		len = ep->max_transfer;
	if (len > 0) {
		memcpy(buffer, ep->buffer + ep->data_begin, len);
		ep->data_begin = (ep->data_begin + len) % OUTPUT_BUFFER_SIZE;
		ep->data_size -= len;
		buffer += len;
		ep->urb->transfer_buffer_length = len;
	}

	/* second chunk (after wraparound) */
	if (ep->data_begin == 0 && ep->data_size > 0 &&
	    len < ep->max_transfer) {
		len = ep->max_transfer - len;
		if (len > ep->data_size)
			len = ep->data_size;
		memcpy(buffer, ep->buffer, len);
		ep->data_begin = len;
		ep->data_size -= len;
		ep->urb->transfer_buffer_length += len;
	}

	if (len > 0) {
		ep->urb->dev = ep->umidi->chip->dev;
		snd_usbmidi_submit_urb(ep->urb, GFP_ATOMIC);
	}
}

static void snd_usbmidi_out_tasklet(unsigned long data)
{
	snd_usb_midi_out_endpoint_t* ep = snd_magic_cast(snd_usb_midi_out_endpoint_t, (void*)data, return);
	unsigned long flags;
	
	spin_lock_irqsave(&ep->buffer_lock, flags);
	snd_usbmidi_do_output(ep);
	spin_unlock_irqrestore(&ep->buffer_lock, flags);
}

/*
 * Adds one USB MIDI packet to the output buffer.
 */
static void output_packet(usbmidi_out_port_t* port,
			  uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3)
{
	snd_usb_midi_out_endpoint_t* ep = port->ep;
	unsigned long flags;

	spin_lock_irqsave(&ep->buffer_lock, flags);
	if (ep->data_size < OUTPUT_BUFFER_SIZE) {
		uint8_t* buf = ep->buffer + (ep->data_begin + ep->data_size) % OUTPUT_BUFFER_SIZE;
		buf[0] = p0;
		buf[1] = p1;
		buf[2] = p2;
		buf[3] = p3;
		ep->data_size += 4;
		if (ep->data_size == ep->max_transfer)
			snd_usbmidi_do_output(ep);
	}
	spin_unlock_irqrestore(&ep->buffer_lock, flags);
}

/*
 * Callback for snd_seq_dump_var_event.
 */
static int snd_usbmidi_sysex_dump(void* ptr, void* buf, int count)
{
	usbmidi_out_port_t* port = (usbmidi_out_port_t*)ptr;
	const uint8_t* dump = (const uint8_t*)buf;

	for (; count; --count) {
		uint8_t byte = *dump++;

		if (byte == 0xf0 && port->sysex_len > 0) {
			/*
			 * The previous SysEx wasn't terminated correctly.
			 * Send the last bytes anyway, and hope that the
			 * receiving device won't be too upset about the
			 * missing F7.
			 */
			output_packet(port,
				      port->cable | (0x04 + port->sysex_len),
				      port->sysex[0],
				      port->sysex_len >= 2 ? port->sysex[1] : 0,
				      0);
			port->sysex_len = 0;
		}
		if (byte != 0xf7) {
			if (port->sysex_len >= 2) {
				output_packet(port,
					      port->cable | 0x04,
					      port->sysex[0],
					      port->sysex[1],
					      byte);
				port->sysex_len = 0;
			} else {
				port->sysex[port->sysex_len++] = byte;
			}
		} else {
			uint8_t cin, data[3];
			int i;

			for (i = 0; i < port->sysex_len; ++i)
				data[i] = port->sysex[i];
			data[i++] = 0xf7;
			cin = port->cable | (0x04 + i);
			for (; i < 3; ++i)
				data[i] = 0;
			/*
			 * cin,data[] is x5,{F7 00 00}
			 *            or x6,{xx F7 00}
			 *            or x7,{xx xx F7}
			 */
			output_packet(port, cin, data[0], data[1], data[2]);
			port->sysex_len = 0;
		}
	}
	return 0;
}

/*
 * Converts an ALSA sequencer event into USB MIDI packets.
 */
static int snd_usbmidi_event_input(snd_seq_event_t* ev, int direct,
				   void* private_data, int atomic, int hop)
{
	usbmidi_out_port_t* port = (usbmidi_out_port_t*)private_data;
	int err;
	uint8_t p0, p1;

	p0 = port->cable;
	p1 = ev->data.note.channel & 0xf;

	switch (ev->type) {
	case SNDRV_SEQ_EVENT_NOTEON:
		output_packet(port, p0 | 0x09, p1 | 0x90,
			      ev->data.note.note & 0x7f,
			      ev->data.note.velocity & 0x7f);
		break;
	case SNDRV_SEQ_EVENT_NOTEOFF:
		output_packet(port, p0 | 0x08, p1 | 0x80,
			      ev->data.note.note & 0x7f,
			      ev->data.note.velocity & 0x7f);
		break;
	case SNDRV_SEQ_EVENT_KEYPRESS:
		output_packet(port, p0 | 0x0a, p1 | 0xa0,
			      ev->data.note.note & 0x7f,
			      ev->data.note.velocity & 0x7f);
		break;
	case SNDRV_SEQ_EVENT_CONTROLLER:
		output_packet(port, p0 | 0x0b, p1 | 0xb0,
			      ev->data.control.param & 0x7f,
			      ev->data.control.value & 0x7f);
		break;
	case SNDRV_SEQ_EVENT_PGMCHANGE:
		output_packet(port, p0 | 0x0c, p1 | 0xc0,
			      ev->data.control.value & 0x7f, 0);
		break;
	case SNDRV_SEQ_EVENT_CHANPRESS:
		output_packet(port, p0 | 0x0d, p1 | 0xd0,
			      ev->data.control.value & 0x7f, 0);
		break;
	case SNDRV_SEQ_EVENT_PITCHBEND:
		output_packet(port, p0 | 0x0e, p1 | 0xe0,
			      (ev->data.control.value + 0x2000) & 0x7f,
			      ((ev->data.control.value + 0x2000) >> 7) & 0x7f);
		break;
	case SNDRV_SEQ_EVENT_CONTROL14:
		if (ev->data.control.param < 0x20) {
			output_packet(port, p0 | 0x0b, p1 | 0xb0,
				      ev->data.control.param,
				      (ev->data.control.value >> 7) & 0x7f);
			output_packet(port, p0 | 0x0b, p1 | 0xb0,
				      ev->data.control.param + 0x20,
				      ev->data.control.value & 0x7f);
		} else {
			output_packet(port, p0 | 0x0b, p1 | 0xb0,
				      ev->data.control.param & 0x7f,
				      ev->data.control.value & 0x7f);
		}
		break;
	case SNDRV_SEQ_EVENT_SONGPOS:
		output_packet(port, p0 | 0x03, 0xf2,
			      ev->data.control.value & 0x7f,
			      (ev->data.control.value >> 7) & 0x7f);
		break;
	case SNDRV_SEQ_EVENT_SONGSEL:
		output_packet(port, p0 | 0x02, 0xf3,
			      ev->data.control.value & 0x7f, 0);
		break;
	case SNDRV_SEQ_EVENT_QFRAME:
		output_packet(port, p0 | 0x02, 0xf1,
			      ev->data.control.value & 0x7f, 0);
		break;
	case SNDRV_SEQ_EVENT_START:
		output_packet(port, p0 | 0x0f, 0xfa, 0, 0);
		break;
	case SNDRV_SEQ_EVENT_CONTINUE:
		output_packet(port, p0 | 0x0f, 0xfb, 0, 0);
		break;
	case SNDRV_SEQ_EVENT_STOP:
		output_packet(port, p0 | 0x0f, 0xfc, 0, 0);
		break;
	case SNDRV_SEQ_EVENT_CLOCK:
		output_packet(port, p0 | 0x0f, 0xf8, 0, 0);
		break;
	case SNDRV_SEQ_EVENT_TUNE_REQUEST:
		output_packet(port, p0 | 0x05, 0xf6, 0, 0);
		break;
	case SNDRV_SEQ_EVENT_RESET:
		output_packet(port, p0 | 0x0f, 0xff, 0, 0);
		break;
	case SNDRV_SEQ_EVENT_SENSING:
		output_packet(port, p0 | 0x0f, 0xfe, 0, 0);
		break;
	case SNDRV_SEQ_EVENT_SYSEX:
		err = snd_seq_dump_var_event(ev, snd_usbmidi_sysex_dump, port);
		if (err < 0)
			return err;
		break;
	default:
		return 0;
	}
	tasklet_hi_schedule(&port->ep->tasklet);
	return 0;
}

/*
 * Frees an input endpoint.
 * May be called when ep hasn't been initialized completely.
 */
static void snd_usbmidi_in_endpoint_delete(snd_usb_midi_in_endpoint_t* ep)
{
	int i;

	if (ep->urb) {
		if (ep->urb->transfer_buffer) {
			usb_unlink_urb(ep->urb);
			kfree(ep->urb->transfer_buffer);
		}
		usb_free_urb(ep->urb);
	}
	for (i = 0; i < 0x10; ++i)
		if (ep->ports[i].midi_event)
			snd_midi_event_free(ep->ports[i].midi_event);
	snd_magic_kfree(ep);
}

#ifndef OLD_USB
/* this code is not exported from USB core anymore */
struct usb_interface *local_usb_ifnum_to_if(struct usb_device *dev, unsigned ifnum)
{
	int i;
        
	for (i = 0; i < dev->actconfig->bNumInterfaces; i++)
		if (dev->actconfig->interface[i].altsetting[0].bInterfaceNumber == ifnum)
			return &dev->actconfig->interface[i];
                                                        
	return NULL;
}
#else
#define local_usb_ifnum_to_if usb_ifnum_to_if
#endif

/*
 * For Roland devices, use the alternate setting which uses interrupt
 * transfers for input.
 */
static usb_endpoint_descriptor_t* snd_usbmidi_get_int_epd(snd_usb_midi_t* umidi,
							  uint8_t epnum)
{
	usb_interface_t* intf;
	usb_interface_descriptor_t* intfd;

	if (umidi->chip->dev->descriptor.idVendor != 0x0582)
		return NULL;
	intf = local_usb_ifnum_to_if(umidi->chip->dev, umidi->ifnum);
	if (!intf || intf->num_altsetting != 2)
		return NULL;

	intfd = &intf->altsetting[0];
	if (intfd->bNumEndpoints != 2 ||
	    (intfd->endpoint[0].bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_BULK ||
	    (intfd->endpoint[1].bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_BULK)
		return NULL;

	intfd = &intf->altsetting[1];
	if (intfd->bNumEndpoints != 2 ||
	    (intfd->endpoint[0].bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_BULK ||
	    (intfd->endpoint[1].bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_INT)
		return NULL;

	usb_set_interface(umidi->chip->dev, intfd->bInterfaceNumber,
			  intfd->bAlternateSetting);
	return &intfd->endpoint[1];
}

/*
 * Creates an input endpoint, and initalizes input ports.
 * ALSA ports are created later.
 */
static int snd_usbmidi_in_endpoint_create(snd_usb_midi_t* umidi,
					  snd_usb_midi_endpoint_info_t* ep_info,
					  snd_usb_midi_endpoint_t* rep)
{
	snd_usb_midi_in_endpoint_t* ep;
	usb_endpoint_descriptor_t* int_epd;
	void* buffer;
	unsigned int pipe;
	int length, i, err;

	rep->in = NULL;
	ep = snd_magic_kcalloc(snd_usb_midi_in_endpoint_t, 0, GFP_KERNEL);
	if (!ep)
		return -ENOMEM;
	ep->umidi = umidi;
	ep->ep = rep;
	for (i = 0; i < 0x10; ++i)
		ep->ports[i].seq_port = -1;

	int_epd = snd_usbmidi_get_int_epd(umidi, ep_info->epnum);

	ep->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ep->urb) {
		snd_usbmidi_in_endpoint_delete(ep);
		return -ENOMEM;
	}
	if (int_epd)
		pipe = usb_rcvintpipe(umidi->chip->dev, ep_info->epnum);
	else
		pipe = usb_rcvbulkpipe(umidi->chip->dev, ep_info->epnum);
	length = usb_maxpacket(umidi->chip->dev, pipe, 0);
	buffer = kmalloc(length, GFP_KERNEL);
	if (!buffer) {
		snd_usbmidi_in_endpoint_delete(ep);
		return -ENOMEM;
	}
	if (int_epd)
		FILL_INT_URB(ep->urb, umidi->chip->dev, pipe, buffer, length,
			     snd_usbmidi_in_urb_complete, ep, int_epd->bInterval);
	else
		FILL_BULK_URB(ep->urb, umidi->chip->dev, pipe, buffer, length,
			      snd_usbmidi_in_urb_complete, ep);

	for (i = 0; i < 0x10; ++i)
		if (ep_info->in_cables & (1 << i)) {
			err = snd_midi_event_new(INPUT_BUFFER_SIZE,
				 		 &ep->ports[i].midi_event);
			if (err < 0) {
				snd_usbmidi_in_endpoint_delete(ep);
				return -ENOMEM;
			}
		}

	rep->in = ep;
	return 0;
}

static int snd_usbmidi_count_bits(uint16_t x)
{
	int i, bits = 0;

	for (i = 0; i < 16; ++i)
		bits += (x & (1 << i)) != 0;
	return bits;
}

/*
 * Frees an output endpoint.
 * May be called when ep hasn't been initialized completely.
 */
static void snd_usbmidi_out_endpoint_delete(snd_usb_midi_out_endpoint_t* ep)
{
	if (ep->tasklet.func)
		tasklet_kill(&ep->tasklet);
	if (ep->urb) {
		if (ep->urb->transfer_buffer) {
			usb_unlink_urb(ep->urb);
			kfree(ep->urb->transfer_buffer);
		}
		usb_free_urb(ep->urb);
	}
	snd_magic_kfree(ep);
}

/*
 * Creates an output endpoint, and initializes output ports.
 * ALSA ports are created later.
 */
static int snd_usbmidi_out_endpoint_create(snd_usb_midi_t* umidi,
					   snd_usb_midi_endpoint_info_t* ep_info,
			 		   snd_usb_midi_endpoint_t* rep)
{
	snd_usb_midi_out_endpoint_t* ep;
	int i;
	unsigned int pipe;
	void* buffer;

	rep->out = NULL;
	ep = snd_magic_kcalloc(snd_usb_midi_out_endpoint_t, 0, GFP_KERNEL);
	if (!ep)
		return -ENOMEM;
	ep->umidi = umidi;

	ep->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ep->urb) {
		snd_usbmidi_out_endpoint_delete(ep);
		return -ENOMEM;
	}
	pipe = usb_sndbulkpipe(umidi->chip->dev, ep_info->epnum);
	ep->max_transfer = usb_maxpacket(umidi->chip->dev, pipe, 1) & ~3;
	buffer = kmalloc(ep->max_transfer, GFP_KERNEL);
	if (!buffer) {
		snd_usbmidi_out_endpoint_delete(ep);
		return -ENOMEM;
	}
	FILL_BULK_URB(ep->urb, umidi->chip->dev, pipe, buffer,
		      ep->max_transfer, snd_usbmidi_out_urb_complete, ep);

	spin_lock_init(&ep->buffer_lock);
	tasklet_init(&ep->tasklet, snd_usbmidi_out_tasklet, (unsigned long)ep);

	for (i = 0; i < 0x10; ++i)
		if (ep_info->out_cables & (1 << i)) {
			ep->ports[i].ep = ep;
			ep->ports[i].cable = i << 4;
		}

	rep->out = ep;
	return 0;
}

/*
 * Frees the sequencer client, endpoints and ports.
 */
static int snd_usbmidi_seq_device_delete(snd_seq_device_t* seq_device)
{
	snd_usb_midi_t* umidi;
	int i, j;

	umidi = (snd_usb_midi_t*)SNDRV_SEQ_DEVICE_ARGPTR(seq_device);

	if (umidi->seq_client >= 0) {
		snd_seq_delete_kernel_client(umidi->seq_client);
		umidi->seq_client = -1;
	}
	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		snd_usb_midi_endpoint_t* ep = &umidi->endpoints[i];
		if (ep->out) {
			snd_usbmidi_out_endpoint_delete(ep->out);
			ep->out = NULL;
		}
		if (ep->in) {
			snd_usbmidi_in_endpoint_delete(ep->in);
			ep->in = NULL;
		}
		for (j = 0; j < 0x10; ++j)
			if (ep->rmidi[j]) {
				snd_device_free(umidi->chip->card, ep->rmidi[j]);
				ep->rmidi[j] = NULL;
			}
	}
	return 0;
}

/*
 * After input and output endpoints have been initialized, create
 * the ALSA port for each input/output port pair in the endpoint.
 * *port_idx is the port number, which must be unique over all endpoints.
 */
static int snd_usbmidi_create_endpoint_ports(snd_usb_midi_t* umidi, int ep, int* port_idx,
					     snd_usb_midi_endpoint_info_t* ep_info)
{
	int c, err;
	int cap, type, port;
	int out, in;
	snd_seq_port_callback_t port_callback;
	char port_name[48];

	for (c = 0; c < 0x10; ++c) {
		out = ep_info->out_cables & (1 << c);
		in = ep_info->in_cables & (1 << c);
		if (!(in || out))
			continue;
		cap = 0;
		memset(&port_callback, 0, sizeof(port_callback));
		port_callback.owner = THIS_MODULE;
		if (out) {
			port_callback.event_input = snd_usbmidi_event_input;
			port_callback.private_data = &umidi->endpoints[ep].out->ports[c];
			cap |= SNDRV_SEQ_PORT_CAP_WRITE |
				SNDRV_SEQ_PORT_CAP_SUBS_WRITE;	
		}
		if (in) {
			cap |= SNDRV_SEQ_PORT_CAP_READ |
				SNDRV_SEQ_PORT_CAP_SUBS_READ;
		}
		if (out && in) {
			cap |= SNDRV_SEQ_PORT_CAP_DUPLEX;
		}
		/* TODO: read type bits from element descriptor */
		type = SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC;
		/* TODO: read port name from jack descriptor */
		snprintf(port_name, sizeof(port_name), "%s Port %d",
			 umidi->chip->card->shortname, *port_idx);
		port = snd_seq_event_port_attach(umidi->seq_client,
						 &port_callback,
						 cap, type, port_name);
		if (port < 0) {
			snd_printk(KERN_ERR "cannot create port (error code %d)\n", port);
			return port;
		}
		if (in)
			umidi->endpoints[ep].in->ports[c].seq_port = port;

		if (*port_idx < SNDRV_MINOR_RAWMIDIS) {
			snd_rawmidi_t *rmidi;
			snd_virmidi_dev_t *rdev;
			err = snd_virmidi_new(umidi->chip->card, *port_idx, &rmidi);
			if (err < 0)
				return err;
			rdev = snd_magic_cast(snd_virmidi_dev_t, rmidi->private_data, return -ENXIO);
			strcpy(rmidi->name, port_name);
			rdev->seq_mode = SNDRV_VIRMIDI_SEQ_ATTACH;
			rdev->client = umidi->seq_client;
			rdev->port = port;
			err = snd_device_register(umidi->chip->card, rmidi);
			if (err < 0) {
				snd_device_free(umidi->chip->card, rmidi);
				return err;
			}
			umidi->endpoints[ep].rmidi[c] = rmidi;
		}
		++*port_idx;
	}
	return 0;
}

/*
 * Creates the endpoints and their ports.
 */
static int snd_usbmidi_create_endpoints(snd_usb_midi_t* umidi,
					snd_usb_midi_endpoint_info_t* endpoints)
{
	int i, err, port_idx = 0;

	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i) {
		if (!endpoints[i].epnum)
			continue;
		if (endpoints[i].out_cables) {
			err = snd_usbmidi_out_endpoint_create(umidi, &endpoints[i],
							      &umidi->endpoints[i]);
			if (err < 0)
				return err;
		}
		if (endpoints[i].in_cables) {
			err = snd_usbmidi_in_endpoint_create(umidi, &endpoints[i],
							     &umidi->endpoints[i]);
			if (err < 0)
				return err;
		}
		err = snd_usbmidi_create_endpoint_ports(umidi, i, &port_idx,
							&endpoints[i]);
		if (err < 0)
			return err;
		printk(KERN_INFO "snd-usb-midi: endpoint %d: created %d output and %d input ports\n",
		       endpoints[i].epnum,
		       snd_usbmidi_count_bits(endpoints[i].out_cables),
		       snd_usbmidi_count_bits(endpoints[i].in_cables));
	}
	return 0;
}

/*
 * Returns MIDIStreaming device capabilities.
 */
static int snd_usbmidi_get_ms_info(snd_usb_midi_t* umidi,
			   	   snd_usb_midi_endpoint_info_t* endpoints)
{
	usb_interface_t* intf;
	usb_interface_descriptor_t* intfd;
	usb_ms_header_descriptor_t* ms_header;
	usb_endpoint_descriptor_t* ep;
	usb_ms_endpoint_descriptor_t* ms_ep;
	int i, epidx;

	memset(endpoints, 0, sizeof(*endpoints) * MIDI_MAX_ENDPOINTS);

	intf = local_usb_ifnum_to_if(umidi->chip->dev, umidi->ifnum);
	if (!intf)
		return -ENXIO;
	intfd = &intf->altsetting[0];
	ms_header = (usb_ms_header_descriptor_t*)intfd->extra;
	if (intfd->extralen >= 7 &&
	    ms_header->bLength >= 7 &&
	    ms_header->bDescriptorType == USB_DT_CS_INTERFACE &&
	    ms_header->bDescriptorSubtype == HEADER)
		printk(KERN_INFO "snd-usb-midi: MIDIStreaming version %02x.%02x\n",
		       ms_header->bcdMSC[1], ms_header->bcdMSC[0]);
	else
		printk(KERN_WARNING "snd-usb-midi: MIDIStreaming interface descriptor not found\n");

	epidx = 0;
	for (i = 0; i < intfd->bNumEndpoints; ++i) {
		ep = &intfd->endpoint[i];
		if ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) != USB_ENDPOINT_XFER_BULK)
			continue;
		ms_ep = (usb_ms_endpoint_descriptor_t*)ep->extra;
		if (ep->extralen < 4 ||
		    ms_ep->bLength < 4 ||
		    ms_ep->bDescriptorType != USB_DT_CS_ENDPOINT ||
		    ms_ep->bDescriptorSubtype != MS_GENERAL)
			continue;
		if (endpoints[epidx].epnum != 0 &&
		    endpoints[epidx].epnum != (ep->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) {
			++epidx;
			if (epidx >= MIDI_MAX_ENDPOINTS) {
				printk(KERN_WARNING "snd-usb-midi: too many endpoints\n");
				break;
			}
		}
		endpoints[epidx].epnum = ep->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
		if (ep->bEndpointAddress & USB_DIR_IN) {
			endpoints[epidx].in_cables = (1 << ms_ep->bNumEmbMIDIJack) - 1;
		} else {
			endpoints[epidx].out_cables = (1 << ms_ep->bNumEmbMIDIJack) - 1;
		}
		printk(KERN_INFO "snd-usb-midi: detected %d %s jack(s) on endpoint %d\n",
		       ms_ep->bNumEmbMIDIJack,
		       ep->bEndpointAddress & USB_DIR_IN ? "input" : "output",
		       endpoints[epidx].epnum);
	}
	return 0;
}

/*
 * If the first endpoint isn't specified, use the first endpoint in the
 * first alternate setting of the interface.
 */
static int snd_usbmidi_detect_endpoint(snd_usb_midi_t* umidi, 
			       	       snd_usb_midi_endpoint_info_t* endpoint)
{
	usb_interface_t* intf;
	usb_interface_descriptor_t* intfd;
	usb_endpoint_descriptor_t* epd;

	if (endpoint->epnum == -1) {
		intf = local_usb_ifnum_to_if(umidi->chip->dev, umidi->ifnum);
		if (!intf || intf->num_altsetting < 1)
			return -ENOENT;
		intfd = intf->altsetting;
		if (intfd->bNumEndpoints < 1)
			return -ENOENT;
		epd = intfd->endpoint;
		endpoint->epnum = epd->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
	}
	return 0;
}

/*
 * Initialize the sequencer device.
 */
static int snd_usbmidi_seq_device_new(snd_seq_device_t* seq_device)
{
	snd_usb_midi_t* umidi;
	usb_device_t* dev;
	snd_seq_client_callback_t client_callback;
	snd_seq_client_info_t client_info;
	snd_usb_midi_endpoint_info_t endpoints[MIDI_MAX_ENDPOINTS];
	int i, err;

	umidi = (snd_usb_midi_t*)SNDRV_SEQ_DEVICE_ARGPTR(seq_device);

	memset(&client_callback, 0, sizeof(client_callback));
	client_callback.allow_output = 1;
	client_callback.allow_input = 1;
	umidi->seq_client = snd_seq_create_kernel_client(umidi->chip->card, 0,
							 &client_callback);
	if (umidi->seq_client < 0)
		return umidi->seq_client;

	memset(&client_info, 0, sizeof(client_info));
	client_info.client = umidi->seq_client;
	client_info.type = KERNEL_CLIENT;
	dev = umidi->chip->dev;
	if (dev->descriptor.iProduct)
		err = usb_string(dev, dev->descriptor.iProduct,
				 client_info.name, sizeof(client_info.name));
	else
		err = 0;
	if (err <= 0) {
		if (umidi->quirk && umidi->quirk->product_name) {
			strncpy(client_info.name, umidi->quirk->product_name,
				sizeof(client_info.name) - 1);
			client_info.name[sizeof(client_info.name) - 1] = '\0';
		} else {
			sprintf(client_info.name, "USB Device %#04x:%#04x",
				dev->descriptor.idVendor, dev->descriptor.idProduct);
		}
	}
	snd_seq_kernel_client_ctl(umidi->seq_client,
				  SNDRV_SEQ_IOCTL_SET_CLIENT_INFO,
				  &client_info);

	if (umidi->quirk) {
		memcpy(endpoints, umidi->quirk->endpoints, sizeof(endpoints));
		err = snd_usbmidi_detect_endpoint(umidi, &endpoints[0]);
	} else {
		err = snd_usbmidi_get_ms_info(umidi, endpoints);
	}
	if (err < 0) {
		snd_usbmidi_seq_device_delete(seq_device);
		return err;
	}
	err = snd_usbmidi_create_endpoints(umidi, endpoints);
	if (err < 0) {
		snd_usbmidi_seq_device_delete(seq_device);
		return err;
	}

	for (i = 0; i < MIDI_MAX_ENDPOINTS; ++i)
		if (umidi->endpoints[i].in)
			snd_usbmidi_submit_urb(umidi->endpoints[i].in->urb,
					       GFP_KERNEL);
	return 0;
}

static int __init snd_usbmidi_module_init(void)
{
	static snd_seq_dev_ops_t ops = {
		snd_usbmidi_seq_device_new,
		snd_usbmidi_seq_device_delete
	};

	return snd_seq_device_register_driver(SNDRV_SEQ_DEV_ID_USBMIDI, &ops,
	 				      sizeof(snd_usb_midi_t));
}

static void __exit snd_usbmidi_module_exit(void)
{
	snd_seq_device_unregister_driver(SNDRV_SEQ_DEV_ID_USBMIDI);
}

module_init(snd_usbmidi_module_init)
module_exit(snd_usbmidi_module_exit)
