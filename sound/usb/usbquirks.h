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

#if defined(CONFIG_SND_SEQUENCER) || defined(CONFIG_SND_SEQUENCER_MODULE)

{
	/* from NetBSD's umidi driver */
	USB_DEVICE(0x0499, 0x1000), /* Yamaha UX256 */
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.ifnum = 0,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0xffff,
				.in_cables  = 0x00ff
			}
		}
	}
},
{
	/* from Nagano Daisuke's usb-midi driver */
	USB_DEVICE(0x0499, 0x1001), /* Yamaha MU1000 */
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.ifnum = 0,
		.endpoints = {
			{
				.epnum = 1,
				.out_cables = 0x000f,
				.in_cables  = 0x0001
			}
		}
	}
},
/*
 * I don't know whether the following Yamaha devices need entries or not:
 * 0x1002 MU2000   0x1008 UX96
 * 0x1003 MU500    0x1009 UX16
 * 0x1004 UW500    0x100e S08
 * 0x1005 MOTIF6   0x100f CLP-150
 * 0x1006 MOTIF7   0x1010 CLP-170
 * 0x1007 MOTIF8
 */

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
	USB_DEVICE(0x0582, 0x0000),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "UA-100",
		.ifnum = 2,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x0007,
				.in_cables  = 0x0007
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0002),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UM-4",
		.ifnum = 2,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x000f,
				.in_cables  = 0x000f
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0003),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "SC-8850",
		.ifnum = 2,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x003f,
				.in_cables  = 0x003f
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0004),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "U-8",
		.ifnum = 2,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x0003,
				.in_cables  = 0x0003
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0005),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UM-2",
		.ifnum = 2,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x0003,
				.in_cables  = 0x0003
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0007),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "SC-8820",
		.ifnum = 2,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x0013,
				.in_cables  = 0x0013
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0008),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "PC-300",
		.ifnum = 2,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x0001,
				.in_cables  = 0x0001
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0009),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UM-1",
		.ifnum = 2,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x0001,
				.in_cables  = 0x0001
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x000b),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "SK-500",
		.ifnum = 2,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x0013,
				.in_cables  = 0x0013
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x000c),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "SC-D70",
		.ifnum = 2,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x0007,
				.in_cables  = 0x0007
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0012),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "XV-5050",
		.ifnum = 0,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x0001,
				.in_cables  = 0x0001
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0014),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UM-880",
		.ifnum = 0,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x01ff,
				.in_cables  = 0x01ff
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0016),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "SD-90",
		.ifnum = 2,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x000f,
				.in_cables  = 0x000f
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0023),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UM-550",
		.ifnum = 0,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x003f,
				.in_cables  = 0x003f
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0027),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "SD-20",
		.ifnum = 0,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x0003,
				.in_cables  = 0x0007
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0029),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "SD-80",
		.ifnum = 0,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x000f,
				.in_cables  = 0x000f
			}
		}
	}
},
{
	USB_DEVICE(0x0582, 0x002b),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UA-700",
		.ifnum = 3,
		.endpoints = {
			{
				.epnum = -1,
				.out_cables = 0x0003,
				.in_cables  = 0x0003
			}
		}
	}
},

#endif /* CONFIG_SND_SEQUENCER(_MODULE) */
