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

/*
 * Use this for devices where other interfaces are standard compliant,
 * to prevent the quirk being applied to those interfaces. (To work with
 * hotplugging, bDeviceClass must be set to USB_CLASS_PER_INTERFACE.)
 */
#define USB_DEVICE_VENDOR_SPEC(vend, prod) \
	.match_flags = USB_DEVICE_ID_MATCH_VENDOR | \
		       USB_DEVICE_ID_MATCH_PRODUCT | \
		       USB_DEVICE_ID_MATCH_INT_CLASS, \
	.idVendor = vend, \
	.idProduct = prod, \
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC

/* Yamaha devices */
{
	USB_DEVICE(0x0499, 0x1000),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "UX256",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x1001),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "MU1000",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x1002),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "MU2000",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x1003),
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
	USB_DEVICE(0x0499, 0x1005),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "MOTIF6",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x1006),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "MOTIF7",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x1007),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "MOTIF8",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x1008),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "UX96",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x1009),
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
	USB_DEVICE(0x0499, 0x100e),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "S08",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x100f),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "CLP-150",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x1010),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "CLP-170",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x1011),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "P-250",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x1012),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "TYROS",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x1013),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "PF-500",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x1014),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "S90",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x5002),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "DME32",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x5003),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "DM2000",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},
{
	USB_DEVICE(0x0499, 0x5004),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Yamaha",
		.product_name = "02R96",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_YAMAHA
	}
},

/*
 * Roland/RolandED/Edirol devices
 *
 * The USB MIDI Specification has been written by Roland,
 * but a 100% conforming Roland device has yet to be found.
 */
{
	USB_DEVICE(0x0582, 0x0000),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "UA-100",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_COMPOSITE,
		.data = & (const snd_usb_audio_quirk_t[]) {
			{
				.ifnum = 0,
				.type = QUIRK_AUDIO_FIXED_ENDPOINT,
				.data = & (const struct audioformat) {
					.format = SNDRV_PCM_FORMAT_S16_LE,
					.channels = 4,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0,
					.endpoint = 0x01,
					.ep_attr = 0x09,
					.rates = SNDRV_PCM_RATE_CONTINUOUS,
					.rate_min = 44100,
					.rate_max = 44100,
				}
			},
			{
				.ifnum = 1,
				.type = QUIRK_AUDIO_FIXED_ENDPOINT,
				.data = & (const struct audioformat) {
					.format = SNDRV_PCM_FORMAT_S16_LE,
					.channels = 2,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = EP_CS_ATTR_FILL_MAX,
					.endpoint = 0x81,
					.ep_attr = 0x05,
					.rates = SNDRV_PCM_RATE_CONTINUOUS,
					.rate_min = 44100,
					.rate_max = 44100,
				}
			},
			{
				.ifnum = 2,
				.type = QUIRK_MIDI_FIXED_ENDPOINT,
				.data = & (const snd_usb_midi_endpoint_info_t) {
					.out_cables = 0x0007,
					.in_cables  = 0x0007
				}
			},
			{
				.ifnum = -1
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
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x000f,
			.in_cables  = 0x000f
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0003),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "SC-8850",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x003f,
			.in_cables  = 0x003f
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0004),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "U-8",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0003,
			.in_cables  = 0x0003
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0005),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UM-2",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0003,
			.in_cables  = 0x0003
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0007),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "SC-8820",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0013,
			.in_cables  = 0x0013
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0008),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "PC-300",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0009),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UM-1",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	USB_DEVICE(0x0582, 0x000b),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "SK-500",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0013,
			.in_cables  = 0x0013
		}
	}
},
{
	/* thanks to Emiliano Grilli <emillo@libero.it> for helping researching this data */
	USB_DEVICE(0x0582, 0x000c),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "SC-D70",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_COMPOSITE,
		.data = & (const snd_usb_audio_quirk_t[]) {
			{
				.ifnum = 0,
				.type = QUIRK_AUDIO_FIXED_ENDPOINT,
				.data = & (const struct audioformat) {
					.format = SNDRV_PCM_FORMAT_S24_3LE,
					.channels = 2,
					.iface = 0,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0,
					.endpoint = 0x01,
					.ep_attr = 0x01,
					.rates = SNDRV_PCM_RATE_CONTINUOUS,
					.rate_min = 44100,
					.rate_max = 44100,
				}
			},
			{
				.ifnum = 1,
				.type = QUIRK_AUDIO_FIXED_ENDPOINT,
				.data = & (const struct audioformat) {
					.format = SNDRV_PCM_FORMAT_S24_3LE,
					.channels = 2,
					.iface = 1,
					.altsetting = 1,
					.altset_idx = 1,
					.attributes = 0,
					.endpoint = 0x81,
					.ep_attr = 0x01,
					.rates = SNDRV_PCM_RATE_CONTINUOUS,
					.rate_min = 44100,
					.rate_max = 44100,
				}
			},
			{
				.ifnum = 2,
				.type = QUIRK_MIDI_FIXED_ENDPOINT,
				.data = & (const snd_usb_midi_endpoint_info_t) {
					.out_cables = 0x0007,
					.in_cables  = 0x0007
				}
			},
			{
				.ifnum = -1
			}
		}
	}
},
{	/*
	 * This quirk is for the "Advanced Driver" mode of the Edirol UA-5.
	 * If the advanced mode switch at the back of the unit is off, the
	 * UA-5 has ID 0x0582/0x0011 and is standard compliant (no quirks),
	 * but offers only 16-bit PCM.
	 * In advanced mode, the UA-5 will output S24_3LE samples (two
	 * channels) at the rate indicated on the front switch, including
	 * the 96kHz sample rate.
	 */
	USB_DEVICE(0x0582, 0x0010),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UA-5",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_COMPOSITE,
		.data = & (const snd_usb_audio_quirk_t[]) {
			{
				.ifnum = 1,
				.type = QUIRK_AUDIO_STANDARD_INTERFACE
			},
			{
				.ifnum = 2,
				.type = QUIRK_AUDIO_STANDARD_INTERFACE
			},
			{
				.ifnum = -1
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
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0014),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UM-880",
		.ifnum = 0,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x01ff,
			.in_cables  = 0x01ff
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0016),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "SD-90",
		.ifnum = 2,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x000f,
			.in_cables  = 0x000f
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0023),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UM-550",
		.ifnum = 0,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x003f,
			.in_cables  = 0x003f
		}
	}
},
{
	/*
	 * This quirk is for the "Advanced Driver" mode. If off, the UA-20
	 * has ID 0x0026 and is standard compliant, but has only 16-bit PCM
	 * and no MIDI.
	 */
	USB_DEVICE(0x0582, 0x0025),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "UA-20",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_COMPOSITE,
		.data = & (const snd_usb_audio_quirk_t[]) {
			{
				.ifnum = 1,
				.type = QUIRK_AUDIO_STANDARD_INTERFACE
			},
			{
				.ifnum = 2,
				.type = QUIRK_AUDIO_STANDARD_INTERFACE
			},
			{
				.ifnum = 3,
				.type = QUIRK_MIDI_STANDARD_INTERFACE
			},
			{
				.ifnum = -1
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
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0003,
			.in_cables  = 0x0007
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0029),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "SD-80",
		.ifnum = 0,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
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
			.out_cables = 0x0003,
			.in_cables  = 0x0003
		}
	}
},
{
	USB_DEVICE(0x0582, 0x002d),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "Roland",
		.product_name = "XV-2020",
		.ifnum = 0,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	USB_DEVICE(0x0582, 0x0033),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "EDIROL",
		.product_name = "PCR",
		.ifnum = 0,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0003,
			.in_cables  = 0x0007
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
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0003,
			.in_cables  = 0x0003
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1011),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "MidiSport 1x1",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1015),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "Keystation",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1021),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "MidiSport 4x4",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x000f,
			.in_cables  = 0x000f
		}
	}
},
{
	/*
	 * For hardware revision 1.05; in the later revisions (1.10 and
	 * 1.21), 0x1031 is the ID for the device without firmware.
	 * Thanks to Olaf Giesbrecht <Olaf_Giesbrecht@yahoo.de>
	 */
	USB_DEVICE_VER(0x0763, 0x1031, 0x0100, 0x0109),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "MidiSport 8x8",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x01ff,
			.in_cables  = 0x01ff
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1033),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "MidiSport 8x8",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x01ff,
			.in_cables  = 0x01ff
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x1041),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "MidiSport 2x4",
		.ifnum = QUIRK_ANY_INTERFACE,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x000f,
			.in_cables  = 0x0003
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x2001),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "Quattro",
		.ifnum = 9,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x2003),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "AudioPhile",
		.ifnum = 6,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},
{
	USB_DEVICE_VENDOR_SPEC(0x0763, 0x2008),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "M-Audio",
		.product_name = "Ozone",
		.ifnum = 3,
		.type = QUIRK_MIDI_MIDIMAN,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0001,
			.in_cables  = 0x0001
		}
	}
},

/* Mark of the Unicorn devices */
{
	/* thanks to Woodley Packard <sweaglesw@thibs.menloschool.org> */
	USB_DEVICE(0x07fd, 0x0001),
	.driver_info = (unsigned long) & (const snd_usb_audio_quirk_t) {
		.vendor_name = "MOTU",
		.product_name = "Fastlane",
		.ifnum = 1,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = & (const snd_usb_midi_endpoint_info_t) {
			.out_cables = 0x0003,
			.in_cables  = 0x0003
		}
	}
},

#undef USB_DEVICE_VENDOR_SPEC
