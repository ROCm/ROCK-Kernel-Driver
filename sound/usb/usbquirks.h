/*
 * ALSA USB Audio Driver
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>,
 *                       Clemens Ladisch <clemens@ladisch.de>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

/*
 * The contents of this file are part of the driver's id_table.
 *
 * In a perfect world, this file would be empty.
 */

#define USB_DEVICE_VENDOR_SPEC(vend, prod) \
	.match_flags = USB_DEVICE_ID_MATCH_VENDOR | \
		       USB_DEVICE_ID_MATCH_PRODUCT | \
		       USB_DEVICE_ID_MATCH_INT_CLASS, \
	.idVendor = vend, \
	.idProduct = prod, \
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC

#if defined(CONFIG_SND_SEQUENCER) || defined(CONFIG_SND_SEQUENCER_MODULE)

/* Yamaha devices */
{
	USB_DEVICE_VENDOR_SPEC(0x0499, 0x1000),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "UX256",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0499, 0x1001),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "MU1000",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0499, 0x1002),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "MU2000",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0499, 0x1003),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "MU500",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0499, 0x1004),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "UW500",
		.ifnum = 3,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0499, 0x1005),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "MOTIF6",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0499, 0x1006),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "MOTIF7",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0499, 0x1007),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "MOTIF8",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0499, 0x1008),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "UX96",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0499, 0x1009),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "UX16",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0499, 0x100a),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "EOS BX",
		.ifnum = 3,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0499, 0x100e),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "S08",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0499, 0x100f),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "CLP-150",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0499, 0x1010),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "CLP-170",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},

/*
 * Once upon a time people thought, "Wouldn't it be nice if there was a
 * standard for USB MIDI devices, so that device drivers would not be forced
 * to know about the quirks of specific devices?"  So Roland went ahead and
 * wrote the USB Device Class Definition for MIDI Devices, and the USB-IF
 * endorsed it, and now everybody designing USB MIDI devices does so in
 * agreement with this standard (or at least tries to).
 *
 * And if you prefer a happy end, you can imagine that Roland devices set a
 * good example. Instead of being completely fucked up due to the lack of
 * class-specific descriptors.
 */
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0000),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "UA-100",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x0007,
			.in_cables  = 0x0007
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0002),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UM-4",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x000f,
			.in_cables  = 0x000f
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0003),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "SC-8850",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x003f,
			.in_cables  = 0x003f
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0004),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "U-8",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x0003,
			.in_cables  = 0x0003
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0005),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UM-2",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x0003,
			.in_cables  = 0x0003
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0007),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "SC-8820",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x0013,
			.in_cables  = 0x0013
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0008),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "PC-300",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0009),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UM-1",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x000b),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "SK-500",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x0013,
			.in_cables  = 0x0013
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x000c),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "SC-D70",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x0007,
			.in_cables  = 0x0007
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0012),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "XV-5050",
		.ifnum = 0,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0014),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UM-880",
		.ifnum = 0,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x01ff,
			.in_cables  = 0x01ff
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0016),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "SD-90",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x000f,
			.in_cables  = 0x000f
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0023),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UM-550",
		.ifnum = 0,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x003f,
			.in_cables  = 0x003f
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0027),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "SD-20",
		.ifnum = 0,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x0003,
			.in_cables  = 0x0007
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x0029),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "SD-80",
		.ifnum = 0,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x000f,
			.in_cables  = 0x000f
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0582, 0x002b),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UA-700",
		.ifnum = 3,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.epnum = -1,
			.out_cables = 0x0003,
			.in_cables  = 0x0003
		}
	}
},

/* Midiman/M-Audio devices */
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1002),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "MidiSport 2x2",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = (void*) 2
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1011),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "MidiSport 1x1",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = (void*) 1
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1015),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "Keystation",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = (void*) 1
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1021),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "MidiSport 4x4",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = (void*) 4
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1033),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "MidiSport 8x8",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = (void*) 9
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x2001),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "Quattro",
		.ifnum = 9,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = (void*) 1
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x2003),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "AudioPhile",
		.ifnum = 9,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = (void*) 1
	}
},

#endif /* CONFIG_SND_SEQUENCER(_MODULE) */

#undef USB_DEVICE_VENDOR_SPEC
