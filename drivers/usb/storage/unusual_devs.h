/* Driver for USB Mass Storage compliant devices
 * Ununsual Devices File
 *
 * $Id: unusual_devs.h,v 1.1 2000/12/05 05:38:31 mdharm Exp $
 *
 * Current development and maintenance by:
 *   (c) 2000 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Initial work by:
 *   (c) 2000 Adam J. Richter (adam@yggdrasil.com), Yggdrasil Computing, Inc.
 *
 * Please see http://www.one-eyed-alien.net/~mdharm/linux-usb for more
 * information about this driver.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* IMPORTANT NOTE: This file must be included in another file which does
 * the following thing for it to work:
 * The macro UNUSUAL_DEV() must be defined before this file is included
 */
#include <linux/config.h>

/* If you edit this file, please try to keep it sorted first by VendorID,
 * then by ProductID.
 */

UNUSUAL_DEV(  0x03ee, 0x0000, 0x0000, 0x0245, 
		"Mitsumi",
		"CD-R/RW Drive",
		US_SC_8020, US_PR_CBI, NULL, 0), 

UNUSUAL_DEV(  0x03f0, 0x0107, 0x0200, 0x0200, 
		"HP",
		"CD-Writer+",
		US_SC_8070, US_PR_CB, NULL, 0), 

#ifdef CONFIG_USB_STORAGE_HP8200e
UNUSUAL_DEV(  0x03f0, 0x0207, 0x0001, 0x0001, 
		"HP",
		"CD-Writer+ 8200e",
		US_SC_8070, US_PR_SCM_ATAPI, init_8200e, 0), 
#endif

UNUSUAL_DEV(  0x04e6, 0x0001, 0x0200, 0x0200, 
		"Matshita",
		"LS-120",
		US_SC_8020, US_PR_CB, NULL, 0),

UNUSUAL_DEV(  0x04e6, 0x0002, 0x0100, 0x0100, 
		"Shuttle",
		"eUSCSI Bridge",
		US_SC_SCSI, US_PR_BULK, usb_stor_euscsi_init, 
		US_FL_SCM_MULT_TARG ), 

#ifdef CONFIG_USB_STORAGE_SDDR09
UNUSUAL_DEV(  0x04e6, 0x0003, 0x0000, 0x9999, 
		"Sandisk",
		"ImageMate SDDR09",
		US_SC_SCSI, US_PR_EUSB_SDDR09, NULL,
		US_FL_SINGLE_LUN | US_FL_START_STOP ),
#endif

#ifdef CONFIG_USB_STORAGE_DPCM
UNUSUAL_DEV(  0x0436, 0x0005, 0x0100, 0x0100,
		"Microtech",
		"CameraMate (DPCM_USB)",
 		US_SC_SCSI, US_PR_DPCM_USB, NULL,
		US_FL_START_STOP ),
#endif

UNUSUAL_DEV(  0x04e6, 0x0006, 0x0100, 0x0200, 
		"Shuttle",
		"eUSB MMC Adapter",
		US_SC_SCSI, US_PR_CB, NULL, 
		US_FL_SINGLE_LUN), 

UNUSUAL_DEV(  0x04e6, 0x0007, 0x0100, 0x0200, 
		"Sony",
		"Hifd",
		US_SC_SCSI, US_PR_CB, NULL, 
		US_FL_SINGLE_LUN), 

UNUSUAL_DEV(  0x04e6, 0x0009, 0x0200, 0x0200, 
		"Shuttle",
		"eUSB ATA/ATAPI Adapter",
		US_SC_8020, US_PR_CB, NULL, 0),

UNUSUAL_DEV(  0x04e6, 0x000a, 0x0200, 0x0200, 
		"Shuttle",
		"eUSB CompactFlash Adapter",
		US_SC_8020, US_PR_CB, NULL, 0),

UNUSUAL_DEV(  0x04e6, 0x000B, 0x0100, 0x0100, 
		"Shuttle",
		"eUSCSI Bridge",
		US_SC_SCSI, US_PR_BULK, usb_stor_euscsi_init, 
		US_FL_SCM_MULT_TARG ), 

UNUSUAL_DEV(  0x04e6, 0x000C, 0x0100, 0x0100, 
		"Shuttle",
		"eUSCSI Bridge",
		US_SC_SCSI, US_PR_BULK, usb_stor_euscsi_init, 
		US_FL_SCM_MULT_TARG ), 

UNUSUAL_DEV(  0x04e6, 0x0101, 0x0200, 0x0200, 
		"Shuttle",
		"CD-RW Device",
		US_SC_8020, US_PR_CB, NULL, 0),

UNUSUAL_DEV(  0x054c, 0x0010, 0x0106, 0x0210, 
		"Sony",
		"DSC-S30/S70/505V/F505", 
		US_SC_SCSI, US_PR_CB, NULL,
		US_FL_SINGLE_LUN | US_FL_START_STOP | US_FL_MODE_XLATE ),

UNUSUAL_DEV(  0x054c, 0x002d, 0x0100, 0x0100, 
		"Sony",
		"Memorystick MSAC-US1",
		US_SC_UFI, US_PR_CB, NULL,
		US_FL_SINGLE_LUN | US_FL_START_STOP ),

UNUSUAL_DEV(  0x057b, 0x0000, 0x0000, 0x0299, 
		"Y-E Data",
		"Flashbuster-U",
		US_SC_UFI,  US_PR_CB, NULL,
		US_FL_SINGLE_LUN),

UNUSUAL_DEV(  0x057b, 0x0000, 0x0300, 0x9999, 
		"Y-E Data",
		"Flashbuster-U",
		US_SC_UFI,  US_PR_CBI, NULL,
		US_FL_SINGLE_LUN),

UNUSUAL_DEV(  0x059f, 0xa601, 0x0200, 0x0200, 
		"LaCie",
		"USB Hard Disk",
		US_SC_RBC, US_PR_CB, NULL, 0 ), 

UNUSUAL_DEV(  0x05ab, 0x0031, 0x0100, 0x0100, 
		"In-System",
		"USB/IDE Bridge (ATAPI ONLY!)",
		US_SC_8070, US_PR_BULK, NULL, 0 ), 

UNUSUAL_DEV(  0x0644, 0x0000, 0x0100, 0x0100, 
		"TEAC",
		"Floppy Drive",
		US_SC_UFI, US_PR_CB, NULL, 0 ), 

#ifdef CONFIG_USB_STORAGE_SDDR09
UNUSUAL_DEV(  0x066b, 0x0105, 0x0100, 0x0100, 
		"Olympus",
		"Camedia MAUSB-2",
		US_SC_SCSI, US_PR_EUSB_SDDR09, NULL,
		US_FL_SINGLE_LUN | US_FL_START_STOP ),
#endif

UNUSUAL_DEV(  0x0693, 0x0002, 0x0100, 0x0100, 
		"Hagiwara",
		"FlashGate SmartMedia",
		US_SC_SCSI, US_PR_BULK, NULL, 0 ),

UNUSUAL_DEV(  0x0693, 0x0005, 0x0100, 0x0100,
		"Hagiwara",
		"Flashgate",
		US_SC_SCSI, US_PR_BULK, NULL, 0 ), 

UNUSUAL_DEV(  0x0781, 0x0001, 0x0200, 0x0200, 
		"Sandisk",
		"ImageMate SDDR-05a",
		US_SC_SCSI, US_PR_CB, NULL,
		US_FL_SINGLE_LUN | US_FL_START_STOP),

UNUSUAL_DEV( 0x0781, 0x0100, 0x0100, 0x0100,
                "Sandisk",
                "ImageMate SDDR-12",
                US_SC_SCSI, US_PR_CB, NULL,
                US_FL_SINGLE_LUN ),

#ifdef CONFIG_USB_STORAGE_SDDR09
UNUSUAL_DEV(  0x0781, 0x0200, 0x0100, 0x0100, 
		"Sandisk",
		"ImageMate SDDR-09",
		US_SC_SCSI, US_PR_EUSB_SDDR09, NULL,
		US_FL_SINGLE_LUN | US_FL_START_STOP ),
#endif

UNUSUAL_DEV(  0x0781, 0x0002, 0x0009, 0x0009, 
		"Sandisk",
		"ImageMate SDDR-31",
		US_SC_SCSI, US_PR_BULK, NULL,
		US_FL_IGNORE_SER),

UNUSUAL_DEV(  0x07af, 0x0004, 0x0100, 0x0100, 
		"Microtech",
		"USB-SCSI-DB25",
		US_SC_SCSI, US_PR_BULK, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ), 

#ifdef CONFIG_USB_STORAGE_FREECOM
UNUSUAL_DEV( 0x07ab, 0xfc01, 0x0000, 0x9999,
                "Freecom",
                "USB-IDE",
                US_SC_QIC, US_PR_FREECOM, freecom_init, 0),
#endif

UNUSUAL_DEV(  0x07af, 0x0005, 0x0100, 0x0100, 
		"Microtech",
		"USB-SCSI-HD50",
		US_SC_SCSI, US_PR_BULK, usb_stor_euscsi_init,
		US_FL_SCM_MULT_TARG ), 

#ifdef CONFIG_USB_STORAGE_DPCM
UNUSUAL_DEV(  0x07af, 0x0006, 0x0100, 0x0100,
		"Microtech",
		"CameraMate (DPCM_USB)",
 		US_SC_SCSI, US_PR_DPCM_USB, NULL,
		US_FL_START_STOP ),
#endif

