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
#include <asm/semaphore.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/asequencer.h>
#include <sound/seq_device.h>
#include <sound/seq_kernel.h>
#include <sound/seq_virmidi.h>
#include <sound/seq_midi_event.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_DESCRIPTION("USB MIDI");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Generic,USB MIDI},"
		"{Roland/EDIROL,PC-300},"
		"{Roland/EDIROL,SC-8820},"
		"{Roland/EDIROL,SC-8850},"
		"{Roland/EDIROL,SC-D70},"
		"{Roland/EDIROL,SD-90},"
		"{Roland/EDIROL,SK-500},"
		"{Roland/EDIROL,U-8},"
		"{Roland/EDIROL,UA-100(G)},"
		"{Roland/EDIROL,UM-1(S)},"
		"{Roland/EDIROL,UM-2(E)},"
		"{Roland/EDIROL,UM-4},"
		"{Roland/EDIROL,UM-550},"
		"{Roland/EDIROL,UM-880},"
		"{Roland/EDIROL,XV-5050},"
		"{Yamaha,MU1000},"
		"{Yamaha,UX256}}");

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX; /* Index 0-max */
static char* snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR; /* Id for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP; /* Enable this card */
static int snd_vid[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = -1 }; /* Vendor id of this card */
static int snd_pid[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = -1 }; /* Product id of this card */
static int snd_int_transfer[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = 0 }; /* Use interrupt transfers for this card */

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for USB MIDI.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for USB MIDI.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_enable, "Enable USB MIDI.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_vid, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_vid, "USB Vendor ID for USB MIDI.");
MODULE_PARM_SYNTAX(snd_vid, SNDRV_ENABLED ",allows:{{-1,0xffff}},base:16");
MODULE_PARM(snd_pid, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_pid, "USB Product ID for USB MIDI.");
MODULE_PARM_SYNTAX(snd_pid, SNDRV_ENABLED ",allows:{{-1,0xffff}},base:16");
MODULE_PARM(snd_int_transfer, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_int_transfer, "Use interrupt transfers for USB MIDI input.");
MODULE_PARM_SYNTAX(snd_int_transfer, SNDRV_ENABLED "," SNDRV_BOOLEAN_FALSE_DESC ",skill:advanced");

/* size of the per-endpoint output buffer, must be a multiple of 4 */
#define OUTPUT_BUFFER_SIZE 0x400

/* max. size of incoming sysex messages */
#define INPUT_BUFFER_SIZE 0x200

#define MAX_ENDPOINTS 2

#define SNDRV_SEQ_DEV_ID_USBMIDI "usb-midi"

#ifndef USB_SUBCLASS_MIDISTREAMING
#define USB_SUBCLASS_MIDISTREAMING 3
#endif

#ifndef USB_DT_CS_INTERFACE
#define USB_DT_CS_INTERFACE (USB_TYPE_CLASS | USB_DT_INTERFACE)
#define USB_DT_CS_ENDPOINT (USB_TYPE_CLASS | USB_DT_ENDPOINT)
#endif

#ifndef USB_DST_MS_HEADER
#define USB_DST_MS_HEADER 0x01
#define USB_DST_MS_GENERAL 0x01
#define USB_DST_MS_HEADER_SIZE 7
#define USB_DST_MS_GENERAL_SIZE 4
#endif

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

typedef struct usbmidi usbmidi_t;
typedef struct usbmidi_device_info usbmidi_device_info_t;
typedef struct usbmidi_endpoint_info usbmidi_endpoint_info_t;
typedef struct usbmidi_endpoint usbmidi_endpoint_t;
typedef struct usbmidi_out_endpoint usbmidi_out_endpoint_t;
typedef struct usbmidi_out_port usbmidi_out_port_t;
typedef struct usbmidi_in_endpoint usbmidi_in_endpoint_t;
typedef struct usbmidi_in_port usbmidi_in_port_t;

/*
 * Describes the capabilities of a USB MIDI device.
 * This structure is filled after parsing the USB descriptors,
 * or is supplied explicitly for broken devices.
 */
struct usbmidi_device_info {
	char vendor[32];		/* vendor name */
	char product[32];		/* device name */
	int16_t ifnum;			/* interface number */
	struct usbmidi_endpoint_info {
		int16_t epnum;		/* endpoint number,
					   -1: autodetect (first ep only) */
		uint16_t out_cables;	/* bitmask */
		uint16_t in_cables;	/* bitmask */
	} endpoints[MAX_ENDPOINTS];
};

struct usbmidi {
	snd_card_t* card;
	usb_device_t* usb_device;
	int dev;
	int seq_client;
	usbmidi_device_info_t device_info;
	struct usbmidi_endpoint {
		usbmidi_out_endpoint_t* out;
		usbmidi_in_endpoint_t* in;
		snd_rawmidi_t* rmidi[0x10];
	} endpoints[MAX_ENDPOINTS];
};

struct usbmidi_out_endpoint {
	usbmidi_t* umidi;
	urb_t* urb;
	int max_transfer;		/* size of urb buffer */
	struct tasklet_struct tasklet;

	uint8_t buffer[OUTPUT_BUFFER_SIZE]; /* ring buffer */
	int data_begin;
	int data_size;
	spinlock_t buffer_lock;

	struct usbmidi_out_port {
		usbmidi_out_endpoint_t* ep;
		uint8_t cable;		/* cable number << 4 */
		uint8_t sysex_len;
		uint8_t sysex[2];
	} ports[0x10];
};

struct usbmidi_in_endpoint {
	usbmidi_t* umidi;
	usbmidi_endpoint_t* ep;
	urb_t* urb;
	struct usbmidi_in_port {
		int seq_port;
		snd_midi_event_t* midi_event;
	} ports[0x10];
};

static int snd_usbmidi_card_used[SNDRV_CARDS];
static DECLARE_MUTEX(snd_usbmidi_open_mutex);

static void snd_usbmidi_do_output(usbmidi_out_endpoint_t* ep);

/*
 * Submits the URB, with error handling.
 */
static int snd_usbmidi_submit_urb(urb_t* urb, int flags)
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
static void snd_usbmidi_input_packet(usbmidi_in_endpoint_t* ep,
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
static void snd_usbmidi_in_urb_complete(urb_t* urb)
{
	usbmidi_in_endpoint_t* ep = snd_magic_cast(usbmidi_in_endpoint_t, urb->context, return);

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
		urb->dev = ep->umidi->usb_device;
		snd_usbmidi_submit_urb(urb, GFP_ATOMIC);
	}
}

static void snd_usbmidi_out_urb_complete(urb_t* urb)
{
	usbmidi_out_endpoint_t* ep = snd_magic_cast(usbmidi_out_endpoint_t, urb->context, return);
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
static void snd_usbmidi_do_output(usbmidi_out_endpoint_t* ep)
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
		ep->urb->dev = ep->umidi->usb_device;
		snd_usbmidi_submit_urb(ep->urb, GFP_ATOMIC);
	}
}

static void snd_usbmidi_out_tasklet(unsigned long data)
{
	usbmidi_out_endpoint_t* ep = snd_magic_cast(usbmidi_out_endpoint_t, (void*)data, return);
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
	usbmidi_out_endpoint_t* ep = port->ep;
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
static void snd_usbmidi_in_endpoint_delete(usbmidi_in_endpoint_t* ep)
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

/*
 * Searches for an alternate setting in which the endpoint uses interrupt
 * transfers for input.
 */
static int snd_usbmidi_get_int_ep(usbmidi_t* umidi, uint8_t epnum,
				  usb_endpoint_descriptor_t** descriptor)
{
	usb_interface_t* intf;
	int i, j;

	*descriptor = NULL;
	intf = usb_ifnum_to_if(umidi->usb_device, umidi->device_info.ifnum);
	if (!intf)
		return -ENXIO;
	for (i = 0; i < intf->num_altsetting; ++i) {
		usb_interface_descriptor_t* intfd = &intf->altsetting[i];
		for (j = 0; j < intfd->bNumEndpoints; ++j) {
			usb_endpoint_descriptor_t* epd = &intfd->endpoint[j];
			if ((epd->bEndpointAddress & (USB_ENDPOINT_NUMBER_MASK | USB_ENDPOINT_DIR_MASK)) == (epnum | USB_DIR_IN) &&
			    (epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT) {
				usb_set_interface(umidi->usb_device,
						  intfd->bInterfaceNumber,
						  intfd->bAlternateSetting);
				*descriptor = &intfd->endpoint[j];
				return 0;
			}
		}
	}
	return -ENXIO;
}

/*
 * Creates an input endpoint, and initalizes input ports.
 * ALSA ports are created later.
 */
static int snd_usbmidi_in_endpoint_create(usbmidi_t* umidi,
					  usbmidi_endpoint_info_t* ep_info,
					  usbmidi_endpoint_t* rep)
{
	usbmidi_in_endpoint_t* ep;
	int do_int_transfer;
	usb_endpoint_descriptor_t* epd;
	void* buffer;
	unsigned int pipe;
	int length, i, err;

	rep->in = NULL;
	ep = snd_magic_kcalloc(usbmidi_in_endpoint_t, 0, GFP_KERNEL);
	if (!ep)
		return -ENOMEM;
	ep->umidi = umidi;
	ep->ep = rep;
	for (i = 0; i < 0x10; ++i)
		ep->ports[i].seq_port = -1;

	do_int_transfer = snd_int_transfer[umidi->dev];
	if (do_int_transfer) {
		if (snd_usbmidi_get_int_ep(umidi, ep_info->epnum, &epd) < 0) {
			printk(KERN_WARNING "snd-usb-midi: interrupt endpoint not found\n");
			do_int_transfer = 0;
		}
	}

	ep->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ep->urb) {
		snd_usbmidi_in_endpoint_delete(ep);
		return -ENOMEM;
	}
	if (do_int_transfer)
		pipe = usb_rcvintpipe(umidi->usb_device, ep_info->epnum);
	else
		pipe = usb_rcvbulkpipe(umidi->usb_device, ep_info->epnum);
	length = usb_maxpacket(umidi->usb_device, pipe, 0);
	buffer = kmalloc(length, GFP_KERNEL);
	if (!buffer) {
		snd_usbmidi_in_endpoint_delete(ep);
		return -ENOMEM;
	}
	if (do_int_transfer)
		FILL_INT_URB(ep->urb, umidi->usb_device, pipe, buffer, length,
			     snd_usbmidi_in_urb_complete, ep, epd->bInterval);
	else
		FILL_BULK_URB(ep->urb, umidi->usb_device, pipe, buffer, length,
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
static void snd_usbmidi_out_endpoint_delete(usbmidi_out_endpoint_t* ep)
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
static int snd_usbmidi_out_endpoint_create(usbmidi_t* umidi,
					   usbmidi_endpoint_info_t* ep_info,
			 		   usbmidi_endpoint_t* rep)
{
	usbmidi_out_endpoint_t* ep;
	int i;
	unsigned int pipe;
	void* buffer;

	rep->out = NULL;
	ep = snd_magic_kcalloc(usbmidi_out_endpoint_t, 0, GFP_KERNEL);
	if (!ep)
		return -ENOMEM;
	ep->umidi = umidi;

	ep->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ep->urb) {
		snd_usbmidi_out_endpoint_delete(ep);
		return -ENOMEM;
	}
	pipe = usb_sndbulkpipe(umidi->usb_device, ep_info->epnum);
	ep->max_transfer = usb_maxpacket(umidi->usb_device, pipe, 1) & ~3;
	buffer = kmalloc(ep->max_transfer, GFP_KERNEL);
	if (!buffer) {
		snd_usbmidi_out_endpoint_delete(ep);
		return -ENOMEM;
	}
	FILL_BULK_URB(ep->urb, umidi->usb_device, pipe, buffer,
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
	usbmidi_t* umidi;
	int i, j;

	umidi = (usbmidi_t*)SNDRV_SEQ_DEVICE_ARGPTR(seq_device);

	if (umidi->seq_client >= 0) {
		snd_seq_delete_kernel_client(umidi->seq_client);
		umidi->seq_client = -1;
	}
	for (i = 0; i < MAX_ENDPOINTS; ++i) {
		usbmidi_endpoint_t* ep = &umidi->endpoints[i];
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
				snd_device_free(umidi->card, ep->rmidi[j]);
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
static int snd_usbmidi_create_endpoint_ports(usbmidi_t* umidi, int ep,
					     int* port_idx)
{
	usbmidi_endpoint_info_t* ep_info = &umidi->device_info.endpoints[ep];
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
		sprintf(port_name, "%s Port %d",
			umidi->device_info.product, *port_idx);
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
			err = snd_virmidi_new(umidi->card, *port_idx, &rmidi);
			if (err < 0)
				return err;
			rdev = snd_magic_cast(snd_virmidi_dev_t, rmidi->private_data, return -ENXIO);
			strcpy(rmidi->name, port_name);
			rdev->seq_mode = SNDRV_VIRMIDI_SEQ_ATTACH;
			rdev->client = umidi->seq_client;
			rdev->port = port;
			err = snd_device_register(umidi->card, rmidi);
			if (err < 0) {
				snd_device_free(umidi->card, rmidi);
				return err;
			}
			umidi->endpoints[ep].rmidi[c] = rmidi;
		}
		++*port_idx;
	}
	return 0;
}

/*
 * Create the endpoints and their ports.
 */
static int snd_usbmidi_create_endpoints(usbmidi_t* umidi)
{
	int i, err, port_idx = 0;

	for (i = 0; i < MAX_ENDPOINTS; ++i) {
		usbmidi_endpoint_info_t* ep_info = &umidi->device_info.endpoints[i];

		if (!ep_info->epnum)
			continue;
		if (ep_info->out_cables) {
			err = snd_usbmidi_out_endpoint_create(umidi, ep_info,
							      &umidi->endpoints[i]);
			if (err < 0)
				return err;
		}
		if (ep_info->in_cables) {
			err = snd_usbmidi_in_endpoint_create(umidi, ep_info,
							     &umidi->endpoints[i]);
			if (err < 0)
				return err;
		}
		err = snd_usbmidi_create_endpoint_ports(umidi, i, &port_idx);
		if (err < 0)
			return err;
		printk(KERN_INFO "snd-usb-midi: endpoint %d: created %d output and %d input ports\n",
		       ep_info->epnum,
		       snd_usbmidi_count_bits(ep_info->out_cables),
		       snd_usbmidi_count_bits(ep_info->in_cables));
	}
	return 0;
}

/*
 * Initialize the sequencer device.
 */
static int snd_usbmidi_seq_device_new(snd_seq_device_t* seq_device)
{
	usbmidi_t* umidi;
	snd_seq_client_callback_t client_callback;
	snd_seq_client_info_t client_info;
	int i, err;

	umidi = (usbmidi_t*)SNDRV_SEQ_DEVICE_ARGPTR(seq_device);

	memset(&client_callback, 0, sizeof(client_callback));
	client_callback.allow_output = 1;
	client_callback.allow_input = 1;
	umidi->seq_client = snd_seq_create_kernel_client(umidi->card, 0,
							 &client_callback);
	if (umidi->seq_client < 0)
		return umidi->seq_client;

	memset(&client_info, 0, sizeof(client_info));
	client_info.client = umidi->seq_client;
	client_info.type = KERNEL_CLIENT;
	sprintf(client_info.name, "%s %s",
		umidi->device_info.vendor, umidi->device_info.product);
	snd_seq_kernel_client_ctl(umidi->seq_client,
				  SNDRV_SEQ_IOCTL_SET_CLIENT_INFO,
				  &client_info);

	err = snd_usbmidi_create_endpoints(umidi);
	if (err < 0) {
		snd_usbmidi_seq_device_delete(seq_device);
		return err;
	}

	for (i = 0; i < MAX_ENDPOINTS; ++i)
		if (umidi->endpoints[i].in)
			snd_usbmidi_submit_urb(umidi->endpoints[i].in->urb,
					       GFP_KERNEL);
	return 0;
}

static int snd_usbmidi_card_create(usb_device_t* usb_device,
				   usbmidi_device_info_t* device_info,
				   snd_card_t** rcard)
{
	snd_card_t* card;
	snd_seq_device_t* seq_device;
	usbmidi_t* umidi;
	int dev, err;

	if (rcard)
		*rcard = NULL;

	down(&snd_usbmidi_open_mutex);

	for (dev = 0; dev < SNDRV_CARDS; ++dev) {
		if (snd_enable[dev] && !snd_usbmidi_card_used[dev] &&
		    (snd_vid[dev] == -1 || 
		     snd_vid[dev] == usb_device->descriptor.idVendor) &&
		    (snd_pid[dev] == -1 ||
		     snd_pid[dev] == usb_device->descriptor.idProduct))
			break;
	}
	if (dev >= SNDRV_CARDS) {
		up(&snd_usbmidi_open_mutex);
		return -ENOENT;
	}

	card = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE, 0);
	if (!card) {
		up(&snd_usbmidi_open_mutex);
		return -ENOMEM;
	}
	strcpy(card->driver, "USB MIDI");
	snprintf(card->shortname, sizeof(card->shortname), "%s %s",
		 device_info->vendor, device_info->product);
	snprintf(card->longname, sizeof(card->longname), "%s %s at %03d/%03d if %d",
		 device_info->vendor, device_info->product,
		 usb_device->bus->busnum, usb_device->devnum,
		 device_info->ifnum);
	card->private_data = (void*)dev;

	err = snd_seq_device_new(card, 0, SNDRV_SEQ_DEV_ID_USBMIDI,
				 sizeof(usbmidi_t), &seq_device);
	if (err < 0) {
		snd_card_free(card);
		up(&snd_usbmidi_open_mutex);
		return err;
	}
	strcpy(seq_device->name, card->shortname);
	umidi = (usbmidi_t*)SNDRV_SEQ_DEVICE_ARGPTR(seq_device);
	umidi->card = card;
	umidi->usb_device = usb_device;
	umidi->dev = dev;
	umidi->seq_client = -1;
	umidi->device_info = *device_info;

	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		up(&snd_usbmidi_open_mutex);
		return err;
	}
	snd_usbmidi_card_used[dev] = 1;
	up(&snd_usbmidi_open_mutex);
	if (rcard)
		*rcard = card;
	return 0;
}

/*
 * If the first endpoint isn't specified, use the first endpoint in the
 * first alternate setting of the interface.
 */
static int snd_usbmidi_detect_endpoint(usb_device_t* usb_device, 
			       	       usbmidi_device_info_t* device_info)
{
	usb_interface_t* intf;
	usb_interface_descriptor_t* intfd;
	usb_endpoint_descriptor_t* epd;

	if (device_info->endpoints[0].epnum == -1) {
		intf = usb_ifnum_to_if(usb_device, device_info->ifnum);
		if (!intf || intf->num_altsetting < 1)
			return -ENOENT;
		intfd = intf->altsetting;
		if (intfd->bNumEndpoints < 1)
			return -ENOENT;
		epd = intfd->endpoint;
		device_info->endpoints[0].epnum = epd->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
	}
	return 0;
}

/*
 * Searches for the alternate setting with the greatest number of bulk transfer
 * endpoints.
 */
static usb_interface_descriptor_t* snd_usbmidi_get_altsetting(usb_device_t* usb_device,
							      usb_interface_t* intf)
{
	int i, best = -1;
	int best_out = 0, best_in = 0;
	usb_interface_descriptor_t* intfd;

	if (intf->num_altsetting == 1)
		return &intf->altsetting[0];
	for (i = 0; i < intf->num_altsetting; ++i) {
		int out = 0, in = 0, j;
		for (j = 0; j < intf->altsetting[i].bNumEndpoints; ++j) {
			usb_endpoint_descriptor_t* ep = &intf->altsetting[i].endpoint[j];
			if ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK) {
				if (ep->bEndpointAddress & USB_DIR_IN)
					++in;
				else
					++out;
			}
		}
		if ((out >= best_out && in >= best_in) &&
		    (out > best_out || in > best_in)) {
			best_out = out;
			best_in = in;
			best = i;
		}
	}
	if (best < 0)
		return NULL;
	intfd = &intf->altsetting[best];
	usb_set_interface(usb_device, intfd->bInterfaceNumber, intfd->bAlternateSetting);
	return intfd;
}

/*
 * Returns MIDIStreaming device capabilities in device_info.
 */
static int snd_usbmidi_get_ms_info(usb_device_t* usb_device,
				   unsigned int ifnum,
				   usbmidi_device_info_t* device_info)
{
	usb_interface_t* intf;
	usb_interface_descriptor_t* intfd;
	usb_ms_header_descriptor_t* ms_header;
	usb_endpoint_descriptor_t* ep;
	usb_ms_endpoint_descriptor_t* ms_ep;
	int i, epidx;

	memset(device_info, 0, sizeof(*device_info));

	if (usb_device->descriptor.iManufacturer == 0 ||
	    usb_string(usb_device, usb_device->descriptor.iManufacturer,
		       device_info->vendor, sizeof(device_info->vendor)) < 0)
		sprintf(device_info->vendor, "Unknown Vendor %x", usb_device->descriptor.idVendor);
	if (usb_device->descriptor.iProduct == 0 ||
	    usb_string(usb_device, usb_device->descriptor.iProduct,
		       device_info->product, sizeof(device_info->product)) < 0)
		sprintf(device_info->product, "Unknown Device %x", usb_device->descriptor.idProduct);

	intf = usb_ifnum_to_if(usb_device, ifnum);
	if (!intf)
		return -ENXIO;
	device_info->ifnum = ifnum;
	printk(KERN_INFO "snd-usb-midi: using interface %d\n",
	       intf->altsetting[0].bInterfaceNumber);

	intfd = snd_usbmidi_get_altsetting(usb_device, intf);
	if (!intfd) {
		printk(KERN_ERR "snd-usb-midi: could not determine altsetting\n");
		return -ENXIO;
	}
	ms_header = (usb_ms_header_descriptor_t*)intfd->extra;
	if (intfd->extralen >= USB_DST_MS_HEADER_SIZE &&
	    ms_header->bLength >= USB_DST_MS_HEADER_SIZE &&
	    ms_header->bDescriptorType == USB_DT_CS_INTERFACE &&
	    ms_header->bDescriptorSubtype == USB_DST_MS_HEADER)
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
		if (ep->extralen < USB_DST_MS_GENERAL_SIZE ||
		    ms_ep->bLength < USB_DST_MS_GENERAL_SIZE ||
		    ms_ep->bDescriptorType != USB_DT_CS_ENDPOINT ||
		    ms_ep->bDescriptorSubtype != USB_DST_MS_GENERAL)
			continue;
		if (device_info->endpoints[epidx].epnum != 0 &&
		    device_info->endpoints[epidx].epnum != (ep->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK)) {
			++epidx;
			if (epidx >= MAX_ENDPOINTS) {
				printk(KERN_WARNING "snd-usb-midi: too many endpoints\n");
				break;
			}
		}
		device_info->endpoints[epidx].epnum = ep->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
		if (ep->bEndpointAddress & USB_DIR_IN) {
			device_info->endpoints[epidx].in_cables = (1 << ms_ep->bNumEmbMIDIJack) - 1;
		} else {
			device_info->endpoints[epidx].out_cables = (1 << ms_ep->bNumEmbMIDIJack) - 1;
		}
		printk(KERN_INFO "snd-usb-midi: detected %d %s jack(s) on endpoint %d\n",
		       ms_ep->bNumEmbMIDIJack,
		       ep->bEndpointAddress & USB_DIR_IN ? "input" : "output",
		       device_info->endpoints[epidx].epnum);
	}
	return 0;
}

/*
 * Returns device capabilities, either explicitly supplied or from the
 * class-specific descriptors.
 */
static int snd_usbmidi_get_device_info(usb_device_t* usb_device,
				       unsigned int ifnum,
				       const usb_device_id_t* usb_device_id,
				       usbmidi_device_info_t* device_info)
{
	if (usb_device_id->driver_info) {
		usbmidi_device_info_t* id_info = (usbmidi_device_info_t*)usb_device_id->driver_info;
		if (ifnum != id_info->ifnum)
			return -ENXIO;
		*device_info = *id_info;
		if (snd_usbmidi_detect_endpoint(usb_device, device_info) < 0)
			return -ENXIO;
	} else {
		if (snd_usbmidi_get_ms_info(usb_device, ifnum, device_info) < 0)
			return -ENXIO;
	}
	return 0;
}

/*
 * Probes for a supported device.
 */
static void* snd_usbmidi_usb_probe(usb_device_t* device,
				   unsigned int ifnum,
				   const usb_device_id_t* device_id)
{
	usbmidi_device_info_t device_info;
	snd_card_t* card = NULL;
	int err;

	if (snd_usbmidi_get_device_info(device, ifnum, device_id,
					&device_info) == 0) {
		printk(KERN_INFO "snd-usb-midi: detected %s %s\n",
		       device_info.vendor, device_info.product);
		err = snd_usbmidi_card_create(device, &device_info, &card);
		if (err < 0)
			snd_printk(KERN_ERR "cannot create card (error code %d)\n", err);
	}
	return card;
}

/*
 * Frees the device.
 */
static void snd_usbmidi_usb_disconnect(usb_device_t* usb_device, void* ptr)
{
	snd_card_t* card = (snd_card_t*)ptr;
	int dev = (int)card->private_data;

	snd_card_free(card);
	down(&snd_usbmidi_open_mutex);
	snd_usbmidi_card_used[dev] = 0;
	up(&snd_usbmidi_open_mutex);
}

/*
 * Information about devices with broken descriptors.
 */

static usbmidi_device_info_t snd_usbmidi_yamaha_ux256_info = {
	/* from NetBSD's umidi driver */
	.vendor = "Yamaha", .product = "UX256",
	.ifnum = 0,
	.endpoints = {{ -1, 0xffff, 0x00ff }}
};
static usbmidi_device_info_t snd_usbmidi_yamaha_mu1000_info = {
	/* from Nagano Daisuke's usb-midi driver */
	.vendor = "Yamaha", .product = "MU1000",
	.ifnum = 0,
	.endpoints = {{ 1, 0x000f, 0x0001 }}
};
/*
 * There ain't no such thing as a standard-compliant Roland device.
 * Apparently, Roland decided not to risk to have wrong entries in the USB
 * descriptors. The consequence is that class-specific descriptors are
 * conspicuous by their absence.
 *
 * And now you may guess which company was responsible for writing the
 * USB Device Class Definition for MIDI Devices.
 */
static usbmidi_device_info_t snd_usbmidi_roland_ua100_info = {
	.vendor = "Roland", .product = "UA-100",
	.ifnum = 2,
	.endpoints = {{ -1, 0x0007, 0x0007 }}
};
static usbmidi_device_info_t snd_usbmidi_roland_um4_info = {
	.vendor = "EDIROL", .product = "UM-4",
	.ifnum = 2,
	.endpoints = {{ -1, 0x000f, 0x000f }}
};
static usbmidi_device_info_t snd_usbmidi_roland_sc8850_info = {
	.vendor = "Roland", .product = "SC-8850",
	.ifnum = 2,
	.endpoints = {{ -1, 0x003f, 0x003f }}
};
static usbmidi_device_info_t snd_usbmidi_roland_u8_info = {
	.vendor = "Roland", .product = "U-8",
	.ifnum = 2,
	.endpoints = {{ -1, 0x0003, 0x0003 }}
};
static usbmidi_device_info_t snd_usbmidi_roland_um2_info = {
	.vendor = "EDIROL", .product = "UM-2",
	.ifnum = 2,
	.endpoints = {{ -1, 0x0003, 0x0003 }}
};
static usbmidi_device_info_t snd_usbmidi_roland_sc8820_info = {
	.vendor = "Roland", .product = "SC-8820",
	.ifnum = 2,
	.endpoints = {{ -1, 0x0013, 0x0013 }}
};
static usbmidi_device_info_t snd_usbmidi_roland_pc300_info = {
	.vendor = "Roland", .product = "PC-300",
	.ifnum = 2,
	.endpoints = {{ -1, 0x0001, 0x0001 }}
};
static usbmidi_device_info_t snd_usbmidi_roland_um1_info = {
	.vendor = "EDIROL", .product = "UM-1",
	.ifnum = 2,
	.endpoints = {{ -1, 0x0001, 0x0001 }}
};
static usbmidi_device_info_t snd_usbmidi_roland_sk500_info = {
	.vendor = "Roland", .product = "SK-500",
	.ifnum = 2,
	.endpoints = {{ -1, 0x0013, 0x0013 }}
};
static usbmidi_device_info_t snd_usbmidi_roland_scd70_info = {
	.vendor = "Roland", .product = "SC-D70",
	.ifnum = 2,
	.endpoints = {{ -1, 0x0007, 0x0007 }}
};
static usbmidi_device_info_t snd_usbmidi_roland_xv5050_info = {
	.vendor = "Roland", .product = "XV-5050",
	.ifnum = 0,
	.endpoints = {{ -1, 0x0001, 0x0001 }}
};
static usbmidi_device_info_t snd_usbmidi_roland_um880_info = {
	.vendor = "EDIROL", .product = "UM-880",
	.ifnum = 0,
	.endpoints = {{ -1, 0x01ff, 0x01ff }}
};
static usbmidi_device_info_t snd_usbmidi_roland_sd90_info = {
	.vendor = "EDIROL", .product = "SD-90",
	.ifnum = 2,
	.endpoints = {{ -1, 0x000f, 0x000f }}
};
static usbmidi_device_info_t snd_usbmidi_roland_um550_info = {
	.vendor = "EDIROL", .product = "UM-550",
	.ifnum = 0,
	.endpoints = {{ -1, 0x003f, 0x003f }}
};

#define USBMIDI_NONCOMPLIANT_DEVICE(vid, pid, name) \
		USB_DEVICE(vid, pid), \
		driver_info: (unsigned long)&snd_usbmidi_##name##_info
static usb_device_id_t snd_usbmidi_usb_id_table[] = {
	{ match_flags: USB_DEVICE_ID_MATCH_INT_CLASS |
		       USB_DEVICE_ID_MATCH_INT_SUBCLASS,
	  bInterfaceClass: USB_CLASS_AUDIO,
	  bInterfaceSubClass: USB_SUBCLASS_MIDISTREAMING },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0499, 0x1000, yamaha_ux256) },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0499, 0x1001, yamaha_mu1000) },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0582, 0x0000, roland_ua100) },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0582, 0x0002, roland_um4) },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0582, 0x0003, roland_sc8850) },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0582, 0x0004, roland_u8) },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0582, 0x0005, roland_um2) },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0582, 0x0007, roland_sc8820) },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0582, 0x0008, roland_pc300) },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0582, 0x0009, roland_um1) },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0582, 0x000b, roland_sk500) },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0582, 0x000c, roland_scd70) },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0582, 0x0012, roland_xv5050) },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0582, 0x0014, roland_um880) },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0582, 0x0016, roland_sd90) },
	{ USBMIDI_NONCOMPLIANT_DEVICE(0x0582, 0x0023, roland_um550) },
	{ /* terminator */ }
};

MODULE_DEVICE_TABLE(usb, snd_usbmidi_usb_id_table);

static usb_driver_t snd_usbmidi_usb_driver = {
	.name = "snd-usb-midi",
	.probe = snd_usbmidi_usb_probe,
	.disconnect = snd_usbmidi_usb_disconnect,
	.id_table = snd_usbmidi_usb_id_table,
	.driver_list = LIST_HEAD_INIT(snd_usbmidi_usb_driver.driver_list)
};

static int __init snd_usbmidi_module_init(void)
{
	static snd_seq_dev_ops_t ops = {
		snd_usbmidi_seq_device_new,
		snd_usbmidi_seq_device_delete
	};
	int err;

	err = snd_seq_device_register_driver(SNDRV_SEQ_DEV_ID_USBMIDI, &ops,
					     sizeof(usbmidi_t));
	if (err < 0)
		return err;
	err = usb_register(&snd_usbmidi_usb_driver);
	if (err < 0) {
		snd_seq_device_unregister_driver(SNDRV_SEQ_DEV_ID_USBMIDI);
		return err;
	}
	return 0;
}

static void __exit snd_usbmidi_module_exit(void)
{
	usb_deregister(&snd_usbmidi_usb_driver);
	snd_seq_device_unregister_driver(SNDRV_SEQ_DEV_ID_USBMIDI);
}

module_init(snd_usbmidi_module_init)
module_exit(snd_usbmidi_module_exit)

#ifndef MODULE

/*
 * format is snd-usb-midi=snd_enable,snd_index,snd_id,
 *                        snd_vid,snd_pid,snd_int_transfer
 */
static int __init snd_usbmidi_module_setup(char* str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str, &snd_enable[nr_dev]) == 2 &&
	       get_option(&str, &snd_index[nr_dev]) == 2 &&
	       get_id(&str, &snd_id[nr_dev]) == 2 &&
	       get_option(&str, &snd_vid[nr_dev]) == 2 &&
	       get_option(&str, &snd_pid[nr_dev]) == 2 &&
	       get_option(&str, &snd_int_transfer[nr_dev]) == 2);
	++nr_dev;
	return 1;
}

__setup("snd-usb-midi=", snd_usbmidi_module_setup);

#endif /* !MODULE */
