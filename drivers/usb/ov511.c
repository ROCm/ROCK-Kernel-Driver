/*
 * OmniVision OV511 Camera-to-USB Bridge Driver
 *
 * Copyright (c) 1999-2001 Mark W. McClelland
 * Original decompression code Copyright 1998-2000 OmniVision Technologies
 * Many improvements by Bret Wallach <bwallac1@san.rr.com>
 * Color fixes by by Orion Sky Lawlor <olawlor@acm.org> (2/26/2000)
 * Snapshot code by Kevin Moore
 * OV7620 fixes by Charl P. Botha <cpbotha@ieee.org>
 * Changes by Claudio Matsuoka <claudio@conectiva.com>
 * Original SAA7111A code by Dave Perks <dperks@ibm.net>
 * Kernel I2C interface adapted from nt1003 driver
 *
 * Based on the Linux CPiA driver written by Peter Pregler,
 * Scott J. Bertin and Johannes Erdfelt.
 * 
 * Please see the file: linux/Documentation/usb/ov511.txt 
 * and the website at:  http://alpha.dyndns.org/ov511
 * for more info.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/pagemap.h>
#include <asm/io.h>
#include <asm/semaphore.h>
#include <asm/processor.h>
#include <linux/wrapper.h>

#if defined (__i386__)
	#include <asm/cpufeature.h>
#endif

#include "ov511.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.48a for Linux 2.4"
#define EMAIL "mmcclell@bigfoot.com"
#define DRIVER_AUTHOR "Mark McClelland <mmcclell@bigfoot.com> & Bret Wallach \
	& Orion Sky Lawlor <olawlor@acm.org> & Kevin Moore & Charl P. Botha \
	<cpbotha@ieee.org> & Claudio Matsuoka <claudio@conectiva.com>"
#define DRIVER_DESC "OV511 USB Camera Driver"

#define OV511_I2C_RETRIES 3
#define ENABLE_Y_QUANTABLE 1
#define ENABLE_UV_QUANTABLE 1

/* Pixel count * 3 bytes for RGB */
#define MAX_FRAME_SIZE(w, h) ((w) * (h) * 3)
#define MAX_DATA_SIZE(w, h) (MAX_FRAME_SIZE(w, h) + sizeof(struct timeval))

/* Max size * bytes per YUV420 pixel (1.5) + one extra isoc frame for safety */
#define MAX_RAW_DATA_SIZE(w, h) ((w) * (h) * 3 / 2 + 1024)

#define FATAL_ERROR(rc) ((rc) < 0 && (rc) != -EPERM)

/* PARAMETER VARIABLES: */
/* (See ov511.txt for detailed descriptions of these.) */

/* Sensor automatically changes brightness */
static int autobright = 1;

/* Sensor automatically changes gain */
static int autogain = 1;

/* Sensor automatically changes exposure */
static int autoexp = 1;

/* 0=no debug messages
 * 1=init/detection/unload and other significant messages,
 * 2=some warning messages
 * 3=config/control function calls
 * 4=most function calls and data parsing messages
 * 5=highly repetitive mesgs
 * NOTE: This should be changed to 0, 1, or 2 for production kernels
 */
static int debug; /* = 0 */

/* Fix vertical misalignment of red and blue at 640x480 */
static int fix_rgb_offset; /* = 0 */

/* Snapshot mode enabled flag */
static int snapshot; /* = 0 */

/* Force image to be read in RGB instead of BGR. This option allow
 * programs that expect RGB data (e.g. gqcam) to work with this driver. */
static int force_rgb; /* = 0 */

/* Number of seconds before inactive buffers are deallocated */
static int buf_timeout = 5;

/* Number of cameras to stream from simultaneously */
static int cams = 1;

/* Enable compression. Needs a fast (>300 MHz) CPU. */
static int compress; /* = 0 */

/* Display test pattern - doesn't work yet either */
static int testpat; /* = 0 */

/* Setting this to 1 will make the sensor output GBR422 instead of YUV420. Only
 * affects RGB24 mode. */
static int sensor_gbr; /* = 0 */

/* Dump raw pixel data. */
static int dumppix; /* = 0 */

/* LED policy. Only works on some OV511+ cameras. 0=off, 1=on (default), 2=auto
 * (on when open) */
static int led = 1;

/* Set this to 1 to dump the bridge register contents after initialization */
static int dump_bridge; /* = 0 */

/* Set this to 1 to dump the sensor register contents after initialization */
static int dump_sensor; /* = 0 */

/* Temporary option for debugging "works, but no image" problem. Prints the
 * first 12 bytes of data (potentially a packet header) in each isochronous
 * data frame. */
static int printph; /* = 0 */

/* Compression parameters - I'm not exactly sure what these do yet */
static int phy = 0x1f;
static int phuv = 0x05;
static int pvy = 0x06;
static int pvuv = 0x06;
static int qhy = 0x14;
static int qhuv = 0x03;
static int qvy = 0x04;
static int qvuv = 0x04;

/* Light frequency. Set to 50 or 60 (Hz), or zero for default settings */
static int lightfreq; /* = 0 */

/* Set this to 1 to enable banding filter by default. Compensates for
 * alternating horizontal light/dark bands caused by (usually fluorescent)
 * lights */
static int bandingfilter; /* = 0 */

/* Pixel clock divisor */
static int clockdiv = -1;

/* Isoc packet size */
static int packetsize = -1;

/* Frame drop register (16h) */
static int framedrop = -1;

/* Allows picture settings (brightness, hue, etc...) to take effect immediately,
 * even in the middle of a frame. This reduces the time to change settings, but
 * can ruin frames during the change. Only affects OmniVision sensors. */
static int fastset; /* = 0 */

/* Forces the palette to a specific value. If an application requests a
 * different palette, it will be rejected. */
static int force_palette; /* = 0 */

/* Set tuner type, if not autodetected */
static int tuner = -1;

/* Allows proper exposure of objects that are illuminated from behind. Only
 * affects OmniVision sensors. */
static int backlight; /* = 0 */

/* If you change this, you must also change the MODULE_PARM definition */
#define OV511_MAX_UNIT_VIDEO 16

/* Allows specified minor numbers to be forced. They will be assigned in the
 * order that devices are detected. Note that you cannot specify 0 as a minor
 * number. If you do not specify any, the next available one will be used. This
 * requires kernel 2.4.5 or later. */
static int unit_video[OV511_MAX_UNIT_VIDEO];

/* Remove zero-padding from uncompressed incoming data. This will compensate for
 * the blocks of corruption that appear when the camera cannot keep up with the
 * speed of the USB bus (eg. at low frame resolutions) */
static int remove_zeros; /* = 0 */

MODULE_PARM(autobright, "i");
MODULE_PARM_DESC(autobright, "Sensor automatically changes brightness");
MODULE_PARM(autogain, "i");
MODULE_PARM_DESC(autogain, "Sensor automatically changes gain");
MODULE_PARM(autoexp, "i");
MODULE_PARM_DESC(autoexp, "Sensor automatically changes exposure");
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug,
  "Debug level: 0=none, 1=inits, 2=warning, 3=config, 4=functions, 5=max");
MODULE_PARM(fix_rgb_offset, "i");
MODULE_PARM_DESC(fix_rgb_offset,
  "Fix vertical misalignment of red and blue at 640x480");
MODULE_PARM(snapshot, "i");
MODULE_PARM_DESC(snapshot, "Enable snapshot mode");
MODULE_PARM(force_rgb, "i");
MODULE_PARM_DESC(force_rgb, "Read RGB instead of BGR");
MODULE_PARM(buf_timeout, "i");
MODULE_PARM_DESC(buf_timeout, "Number of seconds before buffer deallocation");
MODULE_PARM(cams, "i");
MODULE_PARM_DESC(cams, "Number of simultaneous cameras");
MODULE_PARM(compress, "i");
MODULE_PARM_DESC(compress, "Turn on compression (not reliable yet)");
MODULE_PARM(testpat, "i");
MODULE_PARM_DESC(testpat,
  "Replace image with vertical bar testpattern (only partially working)");

// Temporarily removed (needs to be rewritten for new format conversion code)
// MODULE_PARM(sensor_gbr, "i");
// MODULE_PARM_DESC(sensor_gbr, "Make sensor output GBR422 rather than YUV420");

MODULE_PARM(dumppix, "i");
MODULE_PARM_DESC(dumppix, "Dump raw pixel data");
MODULE_PARM(led, "i");
MODULE_PARM_DESC(led,
  "LED policy (OV511+ or later). 0=off, 1=on (default), 2=auto (on when open)");
MODULE_PARM(dump_bridge, "i");
MODULE_PARM_DESC(dump_bridge, "Dump the bridge registers");
MODULE_PARM(dump_sensor, "i");
MODULE_PARM_DESC(dump_sensor, "Dump the sensor registers");
MODULE_PARM(printph, "i");
MODULE_PARM_DESC(printph, "Print frame start/end headers");
MODULE_PARM(phy, "i");
MODULE_PARM_DESC(phy, "Prediction range (horiz. Y)");
MODULE_PARM(phuv, "i");
MODULE_PARM_DESC(phuv, "Prediction range (horiz. UV)");
MODULE_PARM(pvy, "i");
MODULE_PARM_DESC(pvy, "Prediction range (vert. Y)");
MODULE_PARM(pvuv, "i");
MODULE_PARM_DESC(pvuv, "Prediction range (vert. UV)");
MODULE_PARM(qhy, "i");
MODULE_PARM_DESC(qhy, "Quantization threshold (horiz. Y)");
MODULE_PARM(qhuv, "i");
MODULE_PARM_DESC(qhuv, "Quantization threshold (horiz. UV)");
MODULE_PARM(qvy, "i");
MODULE_PARM_DESC(qvy, "Quantization threshold (vert. Y)");
MODULE_PARM(qvuv, "i");
MODULE_PARM_DESC(qvuv, "Quantization threshold (vert. UV)");
MODULE_PARM(lightfreq, "i");
MODULE_PARM_DESC(lightfreq,
  "Light frequency. Set to 50 or 60 Hz, or zero for default settings");
MODULE_PARM(bandingfilter, "i");
MODULE_PARM_DESC(bandingfilter,
  "Enable banding filter (to reduce effects of fluorescent lighting)");
MODULE_PARM(clockdiv, "i");
MODULE_PARM_DESC(clockdiv, "Force pixel clock divisor to a specific value");
MODULE_PARM(packetsize, "i");
MODULE_PARM_DESC(packetsize, "Force a specific isoc packet size");
MODULE_PARM(framedrop, "i");
MODULE_PARM_DESC(framedrop, "Force a specific frame drop register setting");
MODULE_PARM(fastset, "i");
MODULE_PARM_DESC(fastset, "Allows picture settings to take effect immediately");
MODULE_PARM(force_palette, "i");
MODULE_PARM_DESC(force_palette, "Force the palette to a specific value");
MODULE_PARM(tuner, "i");
MODULE_PARM_DESC(tuner, "Set tuner type, if not autodetected");
MODULE_PARM(backlight, "i");
MODULE_PARM_DESC(backlight, "For objects that are lit from behind");
MODULE_PARM(unit_video, "0-16i");
MODULE_PARM_DESC(unit_video,
  "Force use of specific minor number(s). 0 is not allowed.");
MODULE_PARM(remove_zeros, "i");
MODULE_PARM_DESC(remove_zeros,
  "Remove zero-padding from uncompressed incoming data");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

static struct usb_driver ov511_driver;

static struct ov51x_decomp_ops *ov511_decomp_ops;
static struct ov51x_decomp_ops *ov511_mmx_decomp_ops;
static struct ov51x_decomp_ops *ov518_decomp_ops;
static struct ov51x_decomp_ops *ov518_mmx_decomp_ops;

/* Number of times to retry a failed I2C transaction. Increase this if you
 * are getting "Failed to read sensor ID..." */
static int i2c_detect_tries = 5;

/* MMX support is present in kernel and CPU. Checked upon decomp module load. */
static int ov51x_mmx_available;

/* Function prototypes */
static void ov51x_clear_snapshot(struct usb_ov511 *);
static int ov51x_check_snapshot(struct usb_ov511 *);
static inline int sensor_get_picture(struct usb_ov511 *, 
				     struct video_picture *);
static int sensor_get_exposure(struct usb_ov511 *, unsigned char *);
static int ov511_control_ioctl(struct inode *, struct file *, unsigned int,
			       unsigned long);

/**********************************************************************
 * List of known OV511-based cameras
 **********************************************************************/

static struct cam_list clist[] = {
	{   0, "Generic Camera (no ID)" },
	{   1, "Mustek WCam 3X" },
	{   3, "D-Link DSB-C300" },
	{   4, "Generic OV511/OV7610" },
	{   5, "Puretek PT-6007" },
	{   6, "Lifeview USB Life TV (NTSC)" },
	{  21, "Creative Labs WebCam 3" },
	{  36, "Koala-Cam" },
	{  38, "Lifeview USB Life TV" },
	{  41, "Samsung Anycam MPC-M10" },
	{  43, "Mtekvision Zeca MV402" },
	{  46, "Suma eON" },
	{ 100, "Lifeview RoboCam" },
	{ 102, "AverMedia InterCam Elite" },
	{ 112, "MediaForte MV300" },	/* or OV7110 evaluation kit */
	{  -1, NULL }
};

static __devinitdata struct usb_device_id device_table [] = {
	{ USB_DEVICE(VEND_OMNIVISION, PROD_OV511) },
	{ USB_DEVICE(VEND_OMNIVISION, PROD_OV511PLUS) },
	{ USB_DEVICE(VEND_OMNIVISION, PROD_OV518) },
	{ USB_DEVICE(VEND_OMNIVISION, PROD_OV518PLUS) },
	{ USB_DEVICE(VEND_MATTEL, PROD_ME2CAM) },
	{ }  /* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, device_table);

#if defined(CONFIG_PROC_FS) && defined(CONFIG_VIDEO_PROC_FS)
static struct palette_list plist[] = {
	{ VIDEO_PALETTE_GREY,	"GREY" },
	{ VIDEO_PALETTE_HI240,  "HI240" },
	{ VIDEO_PALETTE_RGB565, "RGB565" },
	{ VIDEO_PALETTE_RGB24,	"RGB24" },
	{ VIDEO_PALETTE_RGB32,	"RGB32" },
	{ VIDEO_PALETTE_RGB555, "RGB555" },
	{ VIDEO_PALETTE_YUV422, "YUV422" },
	{ VIDEO_PALETTE_YUYV,   "YUYV" },
	{ VIDEO_PALETTE_UYVY,   "UYVY" },
	{ VIDEO_PALETTE_YUV420, "YUV420" },
	{ VIDEO_PALETTE_YUV411, "YUV411" },
	{ VIDEO_PALETTE_RAW,    "RAW" },
	{ VIDEO_PALETTE_YUV422P,"YUV422P" },
	{ VIDEO_PALETTE_YUV411P,"YUV411P" },
	{ VIDEO_PALETTE_YUV420P,"YUV420P" },
	{ VIDEO_PALETTE_YUV410P,"YUV410P" },
	{ -1, NULL }
};
#endif

static unsigned char yQuanTable511[] = OV511_YQUANTABLE;
static unsigned char uvQuanTable511[] = OV511_UVQUANTABLE;
static unsigned char yQuanTable518[] = OV518_YQUANTABLE;
static unsigned char uvQuanTable518[] = OV518_UVQUANTABLE;

/**********************************************************************
 *
 * Memory management
 *
 * This is a shameless copy from the USB-cpia driver (linux kernel
 * version 2.3.29 or so, I have no idea what this code actually does ;).
 * Actually it seems to be a copy of a shameless copy of the bttv-driver.
 * Or that is a copy of a shameless copy of ... (To the powers: is there
 * no generic kernel-function to do this sort of stuff?)
 *
 * Yes, it was a shameless copy from the bttv-driver. IIRC, Alan says
 * there will be one, but apparentely not yet -jerdfelt
 *
 * So I copied it again for the OV511 driver -claudio
 **********************************************************************/

/* Given PGD from the address space's page table, return the kernel
 * virtual mapping of the physical memory mapped at ADR.
 */
static inline unsigned long 
uvirt_to_kva(pgd_t *pgd, unsigned long adr)
{
	unsigned long ret = 0UL;
	pmd_t *pmd;
	pte_t *ptep, pte;

	if (!pgd_none(*pgd)) {
		pmd = pmd_offset(pgd, adr);
		if (!pmd_none(*pmd)) {
			ptep = pte_offset(pmd, adr);
			pte = *ptep;
			if (pte_present(pte)) {
				ret = (unsigned long) 
				      page_address(pte_page(pte));
				ret |= (adr & (PAGE_SIZE - 1));
			}
		}
	}

	return ret;
}

/* Here we want the physical address of the memory.
 * This is used when initializing the contents of the
 * area and marking the pages as reserved.
 */
static inline unsigned long 
kvirt_to_pa(unsigned long adr)
{
	unsigned long va, kva, ret;

	va = VMALLOC_VMADDR(adr);
	kva = uvirt_to_kva(pgd_offset_k(va), va);
	ret = __pa(kva);
	return ret;
}

static void *
rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr, page;

	/* Round it off to PAGE_SIZE */
	size += (PAGE_SIZE - 1);
	size &= ~(PAGE_SIZE - 1);

	mem = vmalloc_32(size);
	if (!mem)
		return NULL;

	memset(mem, 0, size); /* Clear the ram out, no junk to the user */
	adr = (unsigned long) mem;
	while (size > 0) {
		page = kvirt_to_pa(adr);
		mem_map_reserve(virt_to_page(__va(page)));
		adr += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	return mem;
}

static void 
rvfree(void *mem, unsigned long size)
{
	unsigned long adr, page;

	if (!mem)
		return;

	size += (PAGE_SIZE - 1);
	size &= ~(PAGE_SIZE - 1);

	adr=(unsigned long) mem;
	while (size > 0) {
		page = kvirt_to_pa(adr);
		mem_map_unreserve(virt_to_page(__va(page)));
		adr += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}
	vfree(mem);
}

/**********************************************************************
 * /proc interface
 * Based on the CPiA driver version 0.7.4 -claudio
 **********************************************************************/

#if defined(CONFIG_PROC_FS) && defined(CONFIG_VIDEO_PROC_FS)

static struct proc_dir_entry *ov511_proc_entry = NULL;
extern struct proc_dir_entry *video_proc_entry;

static struct file_operations ov511_control_fops = {
	ioctl:		ov511_control_ioctl,
};

#define YES_NO(x) ((x) ? "yes" : "no")

/* /proc/video/ov511/<minor#>/info */
static int 
ov511_read_proc_info(char *page, char **start, off_t off, int count, int *eof,
		     void *data)
{
	char *out = page;
	int i, j, len;
	struct usb_ov511 *ov511 = data;
	struct video_picture p;
	unsigned char exp;

	if (!ov511 || !ov511->dev)
		return -ENODEV;

	sensor_get_picture(ov511, &p);
	sensor_get_exposure(ov511, &exp);

	/* IMPORTANT: This output MUST be kept under PAGE_SIZE
	 *            or we need to get more sophisticated. */

	out += sprintf(out, "driver_version  : %s\n", DRIVER_VERSION);
	out += sprintf(out, "custom_id       : %d\n", ov511->customid);
	out += sprintf(out, "model           : %s\n", ov511->desc ?
		       clist[ov511->desc].description : "unknown");
	out += sprintf(out, "streaming       : %s\n", YES_NO(ov511->streaming));
	out += sprintf(out, "grabbing        : %s\n", YES_NO(ov511->grabbing));
	out += sprintf(out, "compress        : %s\n", YES_NO(ov511->compress));
	out += sprintf(out, "subcapture      : %s\n", YES_NO(ov511->sub_flag));
	out += sprintf(out, "sub_size        : %d %d %d %d\n",
		       ov511->subx, ov511->suby, ov511->subw, ov511->subh);
	out += sprintf(out, "data_format     : %s\n",
		       force_rgb ? "RGB" : "BGR");
	out += sprintf(out, "brightness      : %d\n", p.brightness >> 8);
	out += sprintf(out, "colour          : %d\n", p.colour >> 8);
	out += sprintf(out, "contrast        : %d\n", p.contrast >> 8);
	out += sprintf(out, "hue             : %d\n", p.hue >> 8);
	out += sprintf(out, "exposure        : %d\n", exp);
	out += sprintf(out, "num_frames      : %d\n", OV511_NUMFRAMES);
	for (i = 0; i < OV511_NUMFRAMES; i++) {
		out += sprintf(out, "frame           : %d\n", i);
		out += sprintf(out, "  depth         : %d\n",
			       ov511->frame[i].depth);
		out += sprintf(out, "  size          : %d %d\n",
			       ov511->frame[i].width, ov511->frame[i].height);
		out += sprintf(out, "  format        : ");
		for (j = 0; plist[j].num >= 0; j++) {
			if (plist[j].num == ov511->frame[i].format) {
				out += sprintf(out, "%s\n", plist[j].name);
				break;
			}
		}
		if (plist[j].num < 0)
			out += sprintf(out, "unknown\n");
		out += sprintf(out, "  data_buffer   : 0x%p\n",
			       ov511->frame[i].data);
	}
	out += sprintf(out, "snap_enabled    : %s\n",
		       YES_NO(ov511->snap_enabled));
	out += sprintf(out, "bridge          : %s\n",
		       ov511->bridge == BRG_OV511 ? "OV511" :
			ov511->bridge == BRG_OV511PLUS ? "OV511+" :
			ov511->bridge == BRG_OV518 ? "OV518" :
			ov511->bridge == BRG_OV518PLUS ? "OV518+" :
			"unknown");
	out += sprintf(out, "sensor          : %s\n",
		       ov511->sensor == SEN_OV6620 ? "OV6620" :
			ov511->sensor == SEN_OV6630 ? "OV6630" :
			ov511->sensor == SEN_OV7610 ? "OV7610" :
			ov511->sensor == SEN_OV7620 ? "OV7620" :
			ov511->sensor == SEN_OV7620AE ? "OV7620AE" :
			ov511->sensor == SEN_OV8600 ? "OV8600" :
			ov511->sensor == SEN_KS0127 ? "KS0127" :
			ov511->sensor == SEN_KS0127B ? "KS0127B" :
			ov511->sensor == SEN_SAA7111A ? "SAA7111A" :
			"unknown");
	out += sprintf(out, "packet_size     : %d\n", ov511->packet_size);
	out += sprintf(out, "framebuffer     : 0x%p\n", ov511->fbuf);

	len = out - page;
	len -= off;
	if (len < count) {
		*eof = 1;
		if (len <= 0)
			return 0;
	} else
		len = count;

	*start = page + off;

	return len;
}

/* /proc/video/ov511/<minor#>/button
 *
 * When the camera's button is pressed, the output of this will change from a
 * 0 to a 1 (ASCII). It will retain this value until it is read, after which
 * it will reset to zero.
 * 
 * SECURITY NOTE: Since reading this file can change the state of the snapshot
 * status, it is important for applications that open it to keep it locked
 * against access by other processes, using flock() or a similar mechanism. No
 * locking is provided by this driver.
 */
static int 
ov511_read_proc_button(char *page, char **start, off_t off, int count, int *eof,
		       void *data)
{
	char *out = page;
	int len, status;
	struct usb_ov511 *ov511 = data;

	if (!ov511 || !ov511->dev)
		return -ENODEV;

	status = ov51x_check_snapshot(ov511);
	out += sprintf(out, "%d", status);

	if (status)
		ov51x_clear_snapshot(ov511);

	len = out - page;
	len -= off;
	if (len < count) {
		*eof = 1;
		if (len <= 0)
			return 0;
	} else {
		len = count;
	}

	*start = page + off;

	return len;
}

static void 
create_proc_ov511_cam(struct usb_ov511 *ov511)
{
	char dirname[4];

	if (!ov511_proc_entry || !ov511)
		return;

	/* Create per-device directory */
	sprintf(dirname, "%d", ov511->vdev.minor);
	PDEBUG(4, "creating /proc/video/ov511/%s/", dirname);
	ov511->proc_devdir = create_proc_entry(dirname, S_IFDIR,
		ov511_proc_entry);
	if (!ov511->proc_devdir)
		return;

	/* Create "info" entry (human readable device information) */
	PDEBUG(4, "creating /proc/video/ov511/%s/info", dirname);
	ov511->proc_info = create_proc_read_entry("info",
		S_IFREG|S_IRUGO|S_IWUSR, ov511->proc_devdir,
		ov511_read_proc_info, ov511);
	if (!ov511->proc_info)
		return;

	/* Don't create it if old snapshot mode on (would cause race cond.) */
	if (!snapshot) {
		/* Create "button" entry (snapshot button status) */
		PDEBUG(4, "creating /proc/video/ov511/%s/button", dirname);
		ov511->proc_button = create_proc_read_entry("button",
			S_IFREG|S_IRUGO|S_IWUSR, ov511->proc_devdir,
			ov511_read_proc_button, ov511);
		if (!ov511->proc_button)
			return;
	}

	/* Create "control" entry (ioctl() interface) */
	PDEBUG(4, "creating /proc/video/ov511/%s/control", dirname);
	lock_kernel();
	ov511->proc_control = create_proc_entry("control",
		S_IFREG|S_IRUGO|S_IWUSR, ov511->proc_devdir);
	if (!ov511->proc_control) {
		unlock_kernel();
		return;
	}
	ov511->proc_control->data = ov511;
	ov511->proc_control->proc_fops = &ov511_control_fops;
	unlock_kernel();
}

static void 
destroy_proc_ov511_cam(struct usb_ov511 *ov511)
{
	char dirname[4];
	
	if (!ov511 || !ov511->proc_devdir)
		return;

	sprintf(dirname, "%d", ov511->vdev.minor);

	/* Destroy "control" entry */
	if (ov511->proc_control) {
		PDEBUG(4, "destroying /proc/video/ov511/%s/control", dirname);
		remove_proc_entry("control", ov511->proc_devdir);
		ov511->proc_control = NULL;
	}

	/* Destroy "button" entry */
	if (ov511->proc_button) {
		PDEBUG(4, "destroying /proc/video/ov511/%s/button", dirname);
		remove_proc_entry("button", ov511->proc_devdir);
		ov511->proc_button = NULL;
	}

	/* Destroy "info" entry */
	if (ov511->proc_info) {
		PDEBUG(4, "destroying /proc/video/ov511/%s/info", dirname);
		remove_proc_entry("info", ov511->proc_devdir);
		ov511->proc_info = NULL;
	}

	/* Destroy per-device directory */
	PDEBUG(4, "destroying /proc/video/ov511/%s/", dirname);
	remove_proc_entry(dirname, ov511_proc_entry);
	ov511->proc_devdir = NULL;
}

static void 
proc_ov511_create(void)
{
	/* No current standard here. Alan prefers /proc/video/ as it keeps
	 * /proc "less cluttered than /proc/randomcardifoundintheshed/"
	 * -claudio
	 */
	if (video_proc_entry == NULL) {
		err("Error: /proc/video/ does not exist");
		return;
	}

	ov511_proc_entry = create_proc_entry("ov511", S_IFDIR,
					     video_proc_entry);

	if (ov511_proc_entry)
		ov511_proc_entry->owner = THIS_MODULE;
	else
		err("Unable to create /proc/video/ov511");
}

static void 
proc_ov511_destroy(void)
{
	PDEBUG(3, "removing /proc/video/ov511");

	if (ov511_proc_entry == NULL)
		return;

	remove_proc_entry("ov511", video_proc_entry);
}
#endif /* CONFIG_PROC_FS && CONFIG_VIDEO_PROC_FS */

/**********************************************************************
 *
 * Register I/O
 *
 **********************************************************************/

static int 
ov511_reg_write(struct usb_device *dev, unsigned char reg, unsigned char value)
{
	int rc;

	PDEBUG(5, "0x%02X:0x%02X", reg, value);

	rc = usb_control_msg(dev,
			     usb_sndctrlpipe(dev, 0),
			     2 /* REG_IO */,
			     USB_TYPE_CLASS | USB_RECIP_DEVICE,
			     0, (__u16)reg, &value, 1, HZ);	

	if (rc < 0)
		err("reg write: error %d", rc);

	return rc;
}

/* returns: negative is error, pos or zero is data */
static int 
ov511_reg_read(struct usb_device *dev, unsigned char reg)
{
	int rc;
	unsigned char buffer[1];

	rc = usb_control_msg(dev,
			     usb_rcvctrlpipe(dev, 0),
			     2 /* REG_IO */,
			     USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_DEVICE,
			     0, (__u16)reg, buffer, 1, HZ);
                               
	PDEBUG(5, "0x%02X:0x%02X", reg, buffer[0]);
	
	if (rc < 0) {
		err("reg read: error %d", rc);
		return rc;
	} else {
		return buffer[0];	
	}
}

/*
 * Writes bits at positions specified by mask to a reg. Bits that are in
 * the same position as 1's in "mask" are cleared and set to "value". Bits
 * that are in the same position as 0's in "mask" are preserved, regardless 
 * of their respective state in "value".
 */
static int 
ov511_reg_write_mask(struct usb_device *dev,
		     unsigned char reg,
		     unsigned char value,
		     unsigned char mask)
{
	int ret;
	unsigned char oldval, newval;

	ret = ov511_reg_read(dev, reg);
	if (ret < 0)
		return ret;

	oldval = (unsigned char) ret;
	oldval &= (~mask);		/* Clear the masked bits */
	value &= mask;			/* Enforce mask on value */
	newval = oldval | value;	/* Set the desired bits */

	return (ov511_reg_write(dev, reg, newval));
}

/* Writes multiple (n) values to a single register. Only valid with certain
 * registers (0x30 and 0xc4 - 0xce). Used for writing 16 and 24-bit values. */
static int 
ov518_reg_write_multi(struct usb_device *dev,
		      unsigned char reg,
		      unsigned char *values,
		      int n)
{
	int rc;

	PDEBUG(5, "0x%02X:[multiple], n=%d", reg, n);  // FIXME

	if (values == NULL) {
		err("reg write multiple: NULL buffer");
		return -EINVAL;
	}

	rc = usb_control_msg(dev,
			     usb_sndctrlpipe(dev, 0),
			     2 /* REG_IO */,
			     USB_TYPE_CLASS | USB_RECIP_DEVICE,
			     0, (__u16)reg, values, n, HZ);	

	if (rc < 0)
		err("reg write multiple: error %d", rc);

	return rc;
}

static int 
ov511_upload_quan_tables(struct usb_device *dev)
{
	unsigned char *pYTable = yQuanTable511;
	unsigned char *pUVTable = uvQuanTable511;
	unsigned char val0, val1;
	int i, rc, reg = OV511_OMNICE_Y_LUT_BEGIN;

	PDEBUG(4, "Uploading quantization tables");

	for (i = 0; i < OV511_QUANTABLESIZE / 2; i++)
	{
		if (ENABLE_Y_QUANTABLE)
		{
			val0 = *pYTable++;
			val1 = *pYTable++;
			val0 &= 0x0f;
			val1 &= 0x0f;
			val0 |= val1 << 4;
			rc = ov511_reg_write(dev, reg, val0);
			if (rc < 0)
				return rc;
		}

		if (ENABLE_UV_QUANTABLE)
		{
			val0 = *pUVTable++;
			val1 = *pUVTable++;
			val0 &= 0x0f;
			val1 &= 0x0f;
			val0 |= val1 << 4;
			rc = ov511_reg_write(dev, reg + OV511_QUANTABLESIZE / 2,
				val0);
			if (rc < 0)
				return rc;
		}

		reg++;
	}

	return 0;
}

/* OV518 quantization tables are 8x4 (instead of 8x8) */
static int 
ov518_upload_quan_tables(struct usb_device *dev)
{
	unsigned char *pYTable = yQuanTable518;
	unsigned char *pUVTable = uvQuanTable518;
	unsigned char val0, val1;
	int i, rc, reg = OV511_OMNICE_Y_LUT_BEGIN;

	PDEBUG(4, "Uploading quantization tables");

	for (i = 0; i < OV518_QUANTABLESIZE / 2; i++)
	{
		if (ENABLE_Y_QUANTABLE)
		{
			val0 = *pYTable++;
			val1 = *pYTable++;
			val0 &= 0x0f;
			val1 &= 0x0f;
			val0 |= val1 << 4;
			rc = ov511_reg_write(dev, reg, val0);
			if (rc < 0)
				return rc;
		}

		if (ENABLE_UV_QUANTABLE)
		{
			val0 = *pUVTable++;
			val1 = *pUVTable++;
			val0 &= 0x0f;
			val1 &= 0x0f;
			val0 |= val1 << 4;
			rc = ov511_reg_write(dev, reg + OV518_QUANTABLESIZE / 2,
				val0);
			if (rc < 0)
				return rc;
		}

		reg++;
	}

	return 0;
}

/* NOTE: Do not call this function directly!
 * The OV518 I2C I/O procedure is different, hence, this function.
 * This is normally only called from ov51x_i2c_write(). Note that this function
 * always succeeds regardless of whether the sensor is present and working.
 */
static int 
ov518_i2c_write_internal(struct usb_device *dev,
			 unsigned char reg,
			 unsigned char value)
{
	int rc;

	PDEBUG(5, "0x%02X:0x%02X", reg, value);

	/* Select camera register */
	rc = ov511_reg_write(dev, OV511_REG_I2C_SUB_ADDRESS_3_BYTE, reg);
	if (rc < 0) goto error;

	/* Write "value" to I2C data port of OV511 */
	rc = ov511_reg_write(dev, OV511_REG_I2C_DATA_PORT, value);
	if (rc < 0) goto error;

	/* Initiate 3-byte write cycle */
	rc = ov511_reg_write(dev, OV518_REG_I2C_CONTROL, 0x01);
	if (rc < 0) goto error;

	return 0;

error:
	err("ov518 i2c write: error %d", rc);
	return rc;
}

/* NOTE: Do not call this function directly! */
static int 
ov511_i2c_write_internal(struct usb_device *dev,
			 unsigned char reg,
			 unsigned char value)
{
	int rc, retries;

	PDEBUG(5, "0x%02X:0x%02X", reg, value);

	/* Three byte write cycle */
	for (retries = OV511_I2C_RETRIES; ; ) {
		/* Select camera register */
		rc = ov511_reg_write(dev, OV511_REG_I2C_SUB_ADDRESS_3_BYTE,
				     reg);
		if (rc < 0) goto error;

		/* Write "value" to I2C data port of OV511 */
		rc = ov511_reg_write(dev, OV511_REG_I2C_DATA_PORT, value);	
		if (rc < 0) goto error;

		/* Initiate 3-byte write cycle */
		rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x01);
		if (rc < 0) goto error;

		do rc = ov511_reg_read(dev, OV511_REG_I2C_CONTROL);
		while (rc > 0 && ((rc&1) == 0)); /* Retry until idle */
		if (rc < 0) goto error;

		if ((rc&2) == 0) /* Ack? */
			break;
#if 0
		/* I2C abort */	
		ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x10);
#endif
		if (--retries < 0) {
			err("i2c write retries exhausted");
			rc = -1;
			goto error;
		}
	}

	return 0;

error:
	err("i2c write: error %d", rc);
	return rc;
}

/* NOTE: Do not call this function directly!
 * The OV518 I2C I/O procedure is different, hence, this function.
 * This is normally only called from ov51x_i2c_read(). Note that this function
 * always succeeds regardless of whether the sensor is present and working.
 */
static int 
ov518_i2c_read_internal(struct usb_device *dev, unsigned char reg)
{
	int rc, value;

	/* Select camera register */
	rc = ov511_reg_write(dev, OV511_REG_I2C_SUB_ADDRESS_2_BYTE, reg);
	if (rc < 0) goto error;

	/* Initiate 2-byte write cycle */
	rc = ov511_reg_write(dev, OV518_REG_I2C_CONTROL, 0x03);
	if (rc < 0) goto error;

	/* Initiate 2-byte read cycle */
	rc = ov511_reg_write(dev, OV518_REG_I2C_CONTROL, 0x05);
	if (rc < 0) goto error;

	value = ov511_reg_read(dev, OV511_REG_I2C_DATA_PORT);

	PDEBUG(5, "0x%02X:0x%02X", reg, value);

	return value;

error:
	err("ov518 i2c read: error %d", rc);
	return rc;
}

/* NOTE: Do not call this function directly!
 * returns: negative is error, pos or zero is data */
static int 
ov511_i2c_read_internal(struct usb_device *dev, unsigned char reg)
{
	int rc, value, retries;

	/* Two byte write cycle */
	for (retries = OV511_I2C_RETRIES; ; ) {
		/* Select camera register */
		rc = ov511_reg_write(dev, OV511_REG_I2C_SUB_ADDRESS_2_BYTE,
				     reg);
		if (rc < 0) goto error;

		/* Initiate 2-byte write cycle */
		rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x03);
		if (rc < 0) goto error;

		do rc = ov511_reg_read(dev, OV511_REG_I2C_CONTROL);
		while (rc > 0 && ((rc&1) == 0)); /* Retry until idle */
		if (rc < 0) goto error;

		if ((rc&2) == 0) /* Ack? */
			break;

		/* I2C abort */	
		ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x10);

		if (--retries < 0) {
			err("i2c write retries exhausted");
			rc = -1;
			goto error;
		}
	}

	/* Two byte read cycle */
	for (retries = OV511_I2C_RETRIES; ; ) {
		/* Initiate 2-byte read cycle */
		rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x05);
		if (rc < 0) goto error;

		do rc = ov511_reg_read(dev, OV511_REG_I2C_CONTROL);
		while (rc > 0 && ((rc&1) == 0)); /* Retry until idle */
		if (rc < 0) goto error;

		if ((rc&2) == 0) /* Ack? */
			break;

		/* I2C abort */	
		rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x10);
		if (rc < 0) goto error;

		if (--retries < 0) {
			err("i2c read retries exhausted");
			rc = -1;
			goto error;
		}
	}

	value = ov511_reg_read(dev, OV511_REG_I2C_DATA_PORT);

	PDEBUG(5, "0x%02X:0x%02X", reg, value);
		
	/* This is needed to make ov51x_i2c_write() work */
	rc = ov511_reg_write(dev, OV511_REG_I2C_CONTROL, 0x05);
	if (rc < 0)
		goto error;
	
	return value;

error:
	err("i2c read: error %d", rc);
	return rc;
}

/* returns: negative is error, pos or zero is data */
static int 
ov51x_i2c_read(struct usb_ov511 *ov511, unsigned char reg)
{
	int rc;
	struct usb_device *dev = ov511->dev;

	down(&ov511->i2c_lock);

	if (dev->descriptor.idProduct == PROD_OV518 ||
	    dev->descriptor.idProduct == PROD_OV518PLUS)
		rc = ov518_i2c_read_internal(dev, reg);
	else
		rc = ov511_i2c_read_internal(dev, reg);

	up(&ov511->i2c_lock);

	return rc;
}

static int 
ov51x_i2c_write(struct usb_ov511 *ov511,
		unsigned char reg,
		unsigned char value)
{
	int rc;
	struct usb_device *dev = ov511->dev;

	down(&ov511->i2c_lock);

	if (dev->descriptor.idProduct == PROD_OV518 ||
	    dev->descriptor.idProduct == PROD_OV518PLUS)
		rc = ov518_i2c_write_internal(dev, reg, value);
	else
		rc = ov511_i2c_write_internal(dev, reg, value);

	up(&ov511->i2c_lock);

	return rc;
}

/* Do not call this function directly! */
static int 
ov51x_i2c_write_mask_internal(struct usb_device *dev,
			      unsigned char reg,
			      unsigned char value,
			      unsigned char mask)
{
	int rc;
	unsigned char oldval, newval;

	if (mask == 0xff) {
		newval = value;
	} else {
		if (dev->descriptor.idProduct == PROD_OV518 ||
		    dev->descriptor.idProduct == PROD_OV518PLUS)
			rc = ov518_i2c_read_internal(dev, reg);
		else
			rc = ov511_i2c_read_internal(dev, reg);
		if (rc < 0)
			return rc;

		oldval = (unsigned char) rc;
		oldval &= (~mask);		/* Clear the masked bits */
		value &= mask;			/* Enforce mask on value */
		newval = oldval | value;	/* Set the desired bits */
	}

	if (dev->descriptor.idProduct == PROD_OV518 ||
	    dev->descriptor.idProduct == PROD_OV518PLUS)
		return (ov518_i2c_write_internal(dev, reg, newval));
	else
		return (ov511_i2c_write_internal(dev, reg, newval));
}

/* Writes bits at positions specified by mask to an I2C reg. Bits that are in
 * the same position as 1's in "mask" are cleared and set to "value". Bits
 * that are in the same position as 0's in "mask" are preserved, regardless 
 * of their respective state in "value".
 */
static int 
ov51x_i2c_write_mask(struct usb_ov511 *ov511,
		     unsigned char reg,
		     unsigned char value,
		     unsigned char mask)
{
	int rc;
	struct usb_device *dev = ov511->dev;

	down(&ov511->i2c_lock);
	rc = ov51x_i2c_write_mask_internal(dev, reg, value, mask);
	up(&ov511->i2c_lock);

	return rc;
}

/* Write to a specific I2C slave ID and register, using the specified mask */
static int 
ov51x_i2c_write_slave(struct usb_ov511 *ov511,
		      unsigned char slave,
		      unsigned char reg,
		      unsigned char value,
		      unsigned char mask)
{
	int rc = 0;
	struct usb_device *dev = ov511->dev;

	down(&ov511->i2c_lock);

	/* Set new slave IDs */
	if (ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_WRITE, slave) < 0) {
		rc = -EIO;
		goto out;
	}

	if (ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_READ, slave + 1) < 0) {
		rc = -EIO;
		goto out;
	}

	rc = ov51x_i2c_write_mask_internal(dev, reg, value, mask);
	/* Don't bail out yet if error; IDs must be restored */

	/* Restore primary IDs */
	slave = ov511->primary_i2c_slave;
	if (ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_WRITE, slave) < 0) {
		rc = -EIO;
		goto out;
	}

	if (ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_READ, slave + 1) < 0) {
		rc = -EIO;
		goto out;
	}

out:
	up(&ov511->i2c_lock);
	return rc;
}

/* Read from a specific I2C slave ID and register */
static int 
ov51x_i2c_read_slave(struct usb_ov511 *ov511,
		     unsigned char slave,
		     unsigned char reg)
{
	int rc;
	struct usb_device *dev = ov511->dev;

	down(&ov511->i2c_lock);

	/* Set new slave IDs */
	if (ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_WRITE, slave) < 0) {
		rc = -EIO;
		goto out;
	}

	if (ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_READ, slave + 1) < 0) {
		rc = -EIO;
		goto out;
	}

	if (dev->descriptor.idProduct == PROD_OV518 ||
	    dev->descriptor.idProduct == PROD_OV518PLUS)
		rc = ov518_i2c_read_internal(dev, reg);
	else
		rc = ov511_i2c_read_internal(dev, reg);
	/* Don't bail out yet if error; IDs must be restored */

	/* Restore primary IDs */
	slave = ov511->primary_i2c_slave;
	if (ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_WRITE, slave) < 0) {
		rc = -EIO;
		goto out;
	}

	if (ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_READ, slave + 1) < 0) {
		rc = -EIO;
		goto out;
	}

out:
	up(&ov511->i2c_lock);
	return rc;
}

static int 
ov511_write_regvals(struct usb_ov511 *ov511,
		    struct ov511_regvals * pRegvals)
{
	int rc;
	struct usb_device *dev = ov511->dev;

	while (pRegvals->bus != OV511_DONE_BUS) {
		if (pRegvals->bus == OV511_REG_BUS) {
			if ((rc = ov511_reg_write(dev, pRegvals->reg,
			                          pRegvals->val)) < 0)
				goto error;
		} else if (pRegvals->bus == OV511_I2C_BUS) {
			if ((rc = ov51x_i2c_write(ov511, pRegvals->reg, 
			                          pRegvals->val)) < 0)
				goto error;
		} else {
			err("Bad regval array");
			rc = -1;
			goto error;
		}
		pRegvals++;
	}
	return 0;

error:
	err("write regvals: error %d", rc);
	return rc;
}

#ifdef OV511_DEBUG 
static void 
ov511_dump_i2c_range(struct usb_ov511 *ov511, int reg1, int regn)
{
	int i;
	int rc;
	for (i = reg1; i <= regn; i++) {
		rc = ov51x_i2c_read(ov511, i);
		info("OV7610[0x%X] = 0x%X", i, rc);
	}
}

static void 
ov51x_dump_i2c_regs(struct usb_ov511 *ov511)
{
	info("I2C REGS");
	ov511_dump_i2c_range(ov511, 0x00, 0x7C);
}

static void 
ov511_dump_reg_range(struct usb_device *dev, int reg1, int regn)
{
	int i;
	int rc;
	for (i = reg1; i <= regn; i++) {
	  rc = ov511_reg_read(dev, i);
	  info("OV511[0x%X] = 0x%X", i, rc);
	}
}

static void 
ov511_dump_regs(struct usb_device *dev)
{
	info("CAMERA INTERFACE REGS");
	ov511_dump_reg_range(dev, 0x10, 0x1f);
	info("DRAM INTERFACE REGS");
	ov511_dump_reg_range(dev, 0x20, 0x23);
	info("ISO FIFO REGS");
	ov511_dump_reg_range(dev, 0x30, 0x31);
	info("PIO REGS");
	ov511_dump_reg_range(dev, 0x38, 0x39);
	ov511_dump_reg_range(dev, 0x3e, 0x3e);
	info("I2C REGS");
	ov511_dump_reg_range(dev, 0x40, 0x49);
	info("SYSTEM CONTROL REGS");
	ov511_dump_reg_range(dev, 0x50, 0x55);
	ov511_dump_reg_range(dev, 0x5e, 0x5f);
	info("OmniCE REGS");
	ov511_dump_reg_range(dev, 0x70, 0x79);
	/* NOTE: Quantization tables are not readable. You will get the value
	 * in reg. 0x79 for every table register */
	ov511_dump_reg_range(dev, 0x80, 0x9f);
	ov511_dump_reg_range(dev, 0xa0, 0xbf);

}
#endif

/**********************************************************************
 *
 * Kernel I2C Interface
 *
 **********************************************************************/

/* For as-yet unimplemented I2C interface */
static void 
call_i2c_clients(struct usb_ov511 *ov511, unsigned int cmd,
		 void *arg)
{
	/* Do nothing */
}

/*****************************************************************************/

static int 
ov511_reset(struct usb_ov511 *ov511, unsigned char reset_type)
{
	int rc;
		
	/* Setting bit 0 not allowed on 518/518Plus */
	if (ov511->bridge == BRG_OV518 ||
	    ov511->bridge == BRG_OV518PLUS)
		reset_type &= 0xfe;

	PDEBUG(4, "Reset: type=0x%X", reset_type);

	rc = ov511_reg_write(ov511->dev, OV511_REG_SYSTEM_RESET, reset_type);
	rc = ov511_reg_write(ov511->dev, OV511_REG_SYSTEM_RESET, 0);

	if (rc < 0)
		err("reset: command failed");

	return rc;
}

/* Temporarily stops OV511 from functioning. Must do this before changing
 * registers while the camera is streaming */
static inline int 
ov511_stop(struct usb_ov511 *ov511)
{
	PDEBUG(4, "stopping");
	ov511->stopped = 1;	
	if (ov511->bridge == BRG_OV518 ||
	    ov511->bridge == BRG_OV518PLUS)
		return (ov511_reg_write(ov511->dev, OV511_REG_SYSTEM_RESET,
					0x3a));
	else
		return (ov511_reg_write(ov511->dev, OV511_REG_SYSTEM_RESET,
					0x3d));
}

/* Restarts OV511 after ov511_stop() is called. Has no effect if it is not
 * actually stopped (for performance). */
static inline int 
ov511_restart(struct usb_ov511 *ov511)
{
	if (ov511->stopped) {
		PDEBUG(4, "restarting");
		ov511->stopped = 0;	

		/* Reinitialize the stream */
		if (ov511->bridge == BRG_OV518 ||
		    ov511->bridge == BRG_OV518PLUS)
			ov511_reg_write(ov511->dev, 0x2f, 0x80);

		return (ov511_reg_write(ov511->dev, OV511_REG_SYSTEM_RESET,
					0x00));
	}

	return 0;
}

/* Resets the hardware snapshot button */
static void 
ov51x_clear_snapshot(struct usb_ov511 *ov511)
{
	if (ov511->bridge == BRG_OV511 || ov511->bridge == BRG_OV511PLUS) {
		ov511_reg_write(ov511->dev, OV511_REG_SYSTEM_SNAPSHOT, 0x01);
		ov511_reg_write(ov511->dev, OV511_REG_SYSTEM_SNAPSHOT, 0x03);
		ov511_reg_write(ov511->dev, OV511_REG_SYSTEM_SNAPSHOT, 0x01);
	} else if (ov511->bridge == BRG_OV518 ||
		   ov511->bridge == BRG_OV518PLUS) {
		warn("snapshot reset not supported yet on OV518(+)");
	} else {
		err("clear snap: invalid bridge type");
	}
	
}

/* Checks the status of the snapshot button. Returns 1 if it was pressed since
 * it was last cleared, and zero in all other cases (including errors) */
static int 
ov51x_check_snapshot(struct usb_ov511 *ov511)
{
	int ret, status = 0;

	if (ov511->bridge == BRG_OV511 || ov511->bridge == BRG_OV511PLUS) {
		ret = ov511_reg_read(ov511->dev, OV511_REG_SYSTEM_SNAPSHOT);
		if (ret < 0) {
			err("Error checking snspshot status (%d)", ret);
		} else if (ret & 0x08) {
			status = 1;
		}
	} else if (ov511->bridge == BRG_OV518 ||
		   ov511->bridge == BRG_OV518PLUS) {
		warn("snapshot check not supported yet on OV518(+)");
	} else {
		err("check snap: invalid bridge type");
	}

	return status;
}

/* Sets I2C read and write slave IDs. Returns <0 for error */
static int 
ov51x_set_slave_ids(struct usb_ov511 *ov511,
		    unsigned char write_id,
		    unsigned char read_id)
{
	struct usb_device *dev = ov511->dev;

	if (ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_WRITE, write_id) < 0)
		return -EIO;

	if (ov511_reg_write(dev, OV511_REG_I2C_SLAVE_ID_READ, read_id) < 0)
		return -EIO;

	if (ov511_reset(ov511, OV511_RESET_NOREGS) < 0)
		return -EIO;

	return 0;
}

/* This does an initial reset of an OmniVision sensor and ensures that I2C
 * is synchronized. Returns <0 for failure.
 */
static int 
ov51x_init_ov_sensor(struct usb_ov511 *ov511)
{
	int i, success;

	/* Reset the sensor */ 
	if (ov51x_i2c_write(ov511, 0x12, 0x80) < 0) return -EIO;

	/* Wait for it to initialize */ 
	schedule_timeout (1 + 150 * HZ / 1000);

	for (i = 0, success = 0; i < i2c_detect_tries && !success; i++) {
		if ((ov51x_i2c_read(ov511, OV7610_REG_ID_HIGH) == 0x7F) &&
		    (ov51x_i2c_read(ov511, OV7610_REG_ID_LOW) == 0xA2)) {
			success = 1;
			continue;
		}

		/* Reset the sensor */ 
		if (ov51x_i2c_write(ov511, 0x12, 0x80) < 0) return -EIO;
		/* Wait for it to initialize */ 
		schedule_timeout(1 + 150 * HZ / 1000);
		/* Dummy read to sync I2C */
		if (ov51x_i2c_read(ov511, 0x00) < 0) return -EIO;
	}

	if (!success)
		return -EIO;
	
	PDEBUG(1, "I2C synced in %d attempt(s)", i);

	return 0;
}

static int 
ov511_set_packet_size(struct usb_ov511 *ov511, int size)
{
	int alt, mult;

	if (ov511_stop(ov511) < 0)
		return -EIO;

	mult = size >> 5;

	if (ov511->bridge == BRG_OV511) {
		if (size == 0) alt = OV511_ALT_SIZE_0;
		else if (size == 257) alt = OV511_ALT_SIZE_257;
		else if (size == 513) alt = OV511_ALT_SIZE_513;
		else if (size == 769) alt = OV511_ALT_SIZE_769;
		else if (size == 993) alt = OV511_ALT_SIZE_993;
		else {
			err("Set packet size: invalid size (%d)", size);
			return -EINVAL;
		}
	} else if (ov511->bridge == BRG_OV511PLUS) {
		if (size == 0) alt = OV511PLUS_ALT_SIZE_0;
		else if (size == 33) alt = OV511PLUS_ALT_SIZE_33;
		else if (size == 129) alt = OV511PLUS_ALT_SIZE_129;
		else if (size == 257) alt = OV511PLUS_ALT_SIZE_257;
		else if (size == 385) alt = OV511PLUS_ALT_SIZE_385;
		else if (size == 513) alt = OV511PLUS_ALT_SIZE_513;
		else if (size == 769) alt = OV511PLUS_ALT_SIZE_769;
		else if (size == 961) alt = OV511PLUS_ALT_SIZE_961;
		else {
			err("Set packet size: invalid size (%d)", size);
			return -EINVAL;
		}
	} else if (ov511->bridge == BRG_OV518 ||
		   ov511->bridge == BRG_OV518PLUS) {
		if (size == 0) alt = OV518_ALT_SIZE_0;
		else if (size == 128) alt = OV518_ALT_SIZE_128;
		else if (size == 256) alt = OV518_ALT_SIZE_256;
		else if (size == 384) alt = OV518_ALT_SIZE_384;
		else if (size == 512) alt = OV518_ALT_SIZE_512;
		else if (size == 640) alt = OV518_ALT_SIZE_640;
		else if (size == 768) alt = OV518_ALT_SIZE_768;
		else if (size == 896) alt = OV518_ALT_SIZE_896;
		else {
			err("Set packet size: invalid size (%d)", size);
			return -EINVAL;
		}
	} else {
		err("Set packet size: Invalid bridge type");
		return -EINVAL;
	}

	PDEBUG(3, "set packet size: %d, mult=%d, alt=%d", size, mult, alt);

	// FIXME: Don't know how to do this on OV518 yet
	if (ov511->bridge != BRG_OV518 &&
	    ov511->bridge != BRG_OV518PLUS) {
		if (ov511_reg_write(ov511->dev, OV511_REG_FIFO_PACKET_SIZE,
				    mult) < 0) {
			return -EIO;
		}
	}
	
	if (usb_set_interface(ov511->dev, ov511->iface, alt) < 0) {
		err("Set packet size: set interface error");
		return -EBUSY;
	}

	/* Initialize the stream */
	if (ov511->bridge == BRG_OV518 ||
	    ov511->bridge == BRG_OV518PLUS)
		if (ov511_reg_write(ov511->dev, 0x2f, 0x80) < 0)
			return -EIO;

	// FIXME - Should we only reset the FIFO?
	if (ov511_reset(ov511, OV511_RESET_NOREGS) < 0)
		return -EIO;

	ov511->packet_size = size;

	if (ov511_restart(ov511) < 0)
		return -EIO;

	return 0;
}

/* Upload compression params and quantization tables. Returns 0 for success. */
static int
ov511_init_compression(struct usb_ov511 *ov511)
{
	struct usb_device *dev = ov511->dev;
	int rc = 0;

	if (!ov511->compress_inited) {

		ov511_reg_write(dev, 0x70, phy);
		ov511_reg_write(dev, 0x71, phuv);
		ov511_reg_write(dev, 0x72, pvy);
		ov511_reg_write(dev, 0x73, pvuv);
		ov511_reg_write(dev, 0x74, qhy);
		ov511_reg_write(dev, 0x75, qhuv);
		ov511_reg_write(dev, 0x76, qvy);
		ov511_reg_write(dev, 0x77, qvuv);

		if (ov511_upload_quan_tables(dev) < 0) {
			err("Error uploading quantization tables");
			rc = -EIO;
			goto out;
		}
	}

	ov511->compress_inited = 1;
out:	
	return rc;
}

/* Upload compression params and quantization tables. Returns 0 for success. */
static int
ov518_init_compression(struct usb_ov511 *ov511)
{
	struct usb_device *dev = ov511->dev;
	int rc = 0;

	if (!ov511->compress_inited) {

		if (ov518_upload_quan_tables(dev) < 0) {
			err("Error uploading quantization tables");
			rc = -EIO;
			goto out;
		}
	}

	ov511->compress_inited = 1;
out:	
	return rc;
}

/* -------------------------------------------------------------------------- */

/* Sets sensor's contrast setting to "val" */
static int
sensor_set_contrast(struct usb_ov511 *ov511, unsigned short val)
{
	int rc;

	PDEBUG(3, "%d", val);

	if (ov511->stop_during_set)
		if (ov511_stop(ov511) < 0)
			return -EIO;

	switch (ov511->sensor) {
	case SEN_OV7610:
	case SEN_OV6620:
	case SEN_OV6630:
	{
		rc = ov51x_i2c_write(ov511, OV7610_REG_CNT, val >> 8);
		if (rc < 0)
			goto out;
		break;
	}
	case SEN_OV7620:
	{
		unsigned char ctab[] = {
			0x01, 0x05, 0x09, 0x11, 0x15, 0x35, 0x37, 0x57,
			0x5b, 0xa5, 0xa7, 0xc7, 0xc9, 0xcf, 0xef, 0xff
		};

		/* Use Y gamma control instead. Bit 0 enables it. */
		rc = ov51x_i2c_write(ov511, 0x64, ctab[val>>12]);
		if (rc < 0)
			goto out;
		break;
	}
	case SEN_SAA7111A:
	{
		rc = ov51x_i2c_write(ov511, 0x0b, val >> 9);
		if (rc < 0)
			goto out;
		break;
	}
	default:
	{
		PDEBUG(3, "Unsupported with this sensor");
		rc = -EPERM;
		goto out;
	}
	}

	rc = 0;		/* Success */
	ov511->contrast = val;
out:
	if (ov511_restart(ov511) < 0)
		return -EIO;

	return rc;
}

/* Gets sensor's contrast setting */
static int
sensor_get_contrast(struct usb_ov511 *ov511, unsigned short *val)
{
	int rc;

	switch (ov511->sensor) {
	case SEN_OV7610:
	case SEN_OV6620:
	case SEN_OV6630:
		rc = ov51x_i2c_read(ov511, OV7610_REG_CNT);
		if (rc < 0)
			return rc;
		else
			*val = rc << 8;
		break;
	case SEN_OV7620:
		/* Use Y gamma reg instead. Bit 0 is the enable bit. */
		rc = ov51x_i2c_read(ov511, 0x64);
		if (rc < 0)
			return rc;
		else
			*val = (rc & 0xfe) << 8;
		break;
	case SEN_SAA7111A:
		*val = ov511->contrast;
		break;
	default:
		PDEBUG(3, "Unsupported with this sensor");
		return -EPERM;
	}

	PDEBUG(3, "%d", *val);
	ov511->contrast = *val;

	return 0;
}

/* -------------------------------------------------------------------------- */

/* Sets sensor's brightness setting to "val" */
static int
sensor_set_brightness(struct usb_ov511 *ov511, unsigned short val)
{
	int rc;

	PDEBUG(4, "%d", val);

	if (ov511->stop_during_set)
		if (ov511_stop(ov511) < 0)
			return -EIO;

	switch (ov511->sensor) {
	case SEN_OV7610:
	case SEN_OV7620AE:
	case SEN_OV6620:
	case SEN_OV6630:
		rc = ov51x_i2c_write(ov511, OV7610_REG_BRT, val >> 8);
		if (rc < 0)
			goto out;
		break;
	case SEN_OV7620:
		/* 7620 doesn't like manual changes when in auto mode */
		if (!ov511->auto_brt) {
			rc = ov51x_i2c_write(ov511, OV7610_REG_BRT, val >> 8);
			if (rc < 0)
				goto out;
		}
		break;
	case SEN_SAA7111A:
		rc = ov51x_i2c_write(ov511, 0x0a, val >> 8);
		if (rc < 0)
			goto out;
		break;
	default:
		PDEBUG(3, "Unsupported with this sensor");
		rc = -EPERM;
		goto out;
	}

	rc = 0;		/* Success */
	ov511->brightness = val;
out:
	if (ov511_restart(ov511) < 0)
		return -EIO;

	return rc;
}

/* Gets sensor's brightness setting */
static int
sensor_get_brightness(struct usb_ov511 *ov511, unsigned short *val)
{
	int rc;

	switch (ov511->sensor) {
	case SEN_OV7610:
	case SEN_OV7620AE:
	case SEN_OV7620:
	case SEN_OV6620:
	case SEN_OV6630:
		rc = ov51x_i2c_read(ov511, OV7610_REG_BRT);
		if (rc < 0)
			return rc;
		else
			*val = rc << 8;
		break;
	case SEN_SAA7111A:
		*val = ov511->brightness;
		break;
	default:
		PDEBUG(3, "Unsupported with this sensor");
		return -EPERM;
	}

	PDEBUG(3, "%d", *val);
	ov511->brightness = *val;

	return 0;
}

/* -------------------------------------------------------------------------- */

/* Sets sensor's saturation (color intensity) setting to "val" */
static int
sensor_set_saturation(struct usb_ov511 *ov511, unsigned short val)
{
	int rc;

	PDEBUG(3, "%d", val);

	if (ov511->stop_during_set)
		if (ov511_stop(ov511) < 0)
			return -EIO;

	switch (ov511->sensor) {
	case SEN_OV7610:
	case SEN_OV7620AE:
	case SEN_OV6620:
	case SEN_OV6630:
		rc = ov51x_i2c_write(ov511, OV7610_REG_SAT, val >> 8);
		if (rc < 0)
			goto out;
		break;
	case SEN_OV7620:
//		/* Use UV gamma control instead. Bits 0 & 7 are reserved. */
//		rc = ov511_i2c_write(ov511->dev, 0x62, (val >> 9) & 0x7e);
//		if (rc < 0)
//			goto out;
		rc = ov51x_i2c_write(ov511, OV7610_REG_SAT, val >> 8);
		if (rc < 0)
			goto out;
		break;
	case SEN_SAA7111A:
		rc = ov51x_i2c_write(ov511, 0x0c, val >> 9);
		if (rc < 0)
			goto out;
		break;
	default:
		PDEBUG(3, "Unsupported with this sensor");
		rc = -EPERM;
		goto out;
	}

	rc = 0;		/* Success */
	ov511->colour = val;
out:
	if (ov511_restart(ov511) < 0)
		return -EIO;

	return rc;
}

/* Gets sensor's saturation (color intensity) setting */
static int
sensor_get_saturation(struct usb_ov511 *ov511, unsigned short *val)
{
	int rc;

	switch (ov511->sensor) {
	case SEN_OV7610:
	case SEN_OV7620AE:
	case SEN_OV6620:
	case SEN_OV6630:
		rc = ov51x_i2c_read(ov511, OV7610_REG_SAT);
		if (rc < 0)
			return rc;
		else
			*val = rc << 8;
		break;
	case SEN_OV7620:
//		/* Use UV gamma reg instead. Bits 0 & 7 are reserved. */
//		rc = ov51x_i2c_read(ov511, 0x62);
//		if (rc < 0)
//			return rc;
//		else
//			*val = (rc & 0x7e) << 9;
		rc = ov51x_i2c_read(ov511, OV7610_REG_SAT);
		if (rc < 0)
			return rc;
		else
			*val = rc << 8;
		break;
	case SEN_SAA7111A:
		*val = ov511->colour;
		break;
	default:
		PDEBUG(3, "Unsupported with this sensor");
		return -EPERM;
	}

	PDEBUG(3, "%d", *val);
	ov511->colour = *val;

	return 0;
}

/* -------------------------------------------------------------------------- */

/* Sets sensor's hue (red/blue balance) setting to "val" */
static int
sensor_set_hue(struct usb_ov511 *ov511, unsigned short val)
{
	int rc;

	PDEBUG(3, "%d", val);

	if (ov511->stop_during_set)
		if (ov511_stop(ov511) < 0)
			return -EIO;

	switch (ov511->sensor) {
	case SEN_OV7610:
	case SEN_OV6620:
	case SEN_OV6630:
		rc = ov51x_i2c_write(ov511, OV7610_REG_RED, 0xFF - (val >> 8));
		if (rc < 0)
			goto out;

		rc = ov51x_i2c_write(ov511, OV7610_REG_BLUE, val >> 8);
		if (rc < 0)
			goto out;
		break;
	case SEN_OV7620:
// Hue control is causing problems. I will enable it once it's fixed.
#if 0
		rc = ov51x_i2c_write(ov511, 0x7a,
				     (unsigned char)(val >> 8) + 0xb);
		if (rc < 0)
			goto out;

		rc = ov51x_i2c_write(ov511, 0x79, 
				     (unsigned char)(val >> 8) + 0xb);
		if (rc < 0)
			goto out;
#endif
		break;
	case SEN_SAA7111A:
		rc = ov51x_i2c_write(ov511, 0x0d, (val + 32768) >> 8);
		if (rc < 0)
			goto out;
		break;
	default:
		PDEBUG(3, "Unsupported with this sensor");
		rc = -EPERM;
		goto out;
	}

	rc = 0;		/* Success */
	ov511->hue = val;
out:
	if (ov511_restart(ov511) < 0)
		return -EIO;

	return rc;
}

/* Gets sensor's hue (red/blue balance) setting */
static int
sensor_get_hue(struct usb_ov511 *ov511, unsigned short *val)
{
	int rc;

	switch (ov511->sensor) {
	case SEN_OV7610:
	case SEN_OV6620:
	case SEN_OV6630:
		rc = ov51x_i2c_read(ov511, OV7610_REG_BLUE);
		if (rc < 0)
			return rc;
		else
			*val = rc << 8;
		break;
	case SEN_OV7620:
		rc = ov51x_i2c_read(ov511, 0x7a);
		if (rc < 0)
			return rc;
		else
			*val = rc << 8;
		break;
	case SEN_SAA7111A:
		*val = ov511->hue;
		break;
	default:
		PDEBUG(3, "Unsupported with this sensor");
		return -EPERM;
	}

	PDEBUG(3, "%d", *val);
	ov511->hue = *val;

	return 0;
}

/* -------------------------------------------------------------------------- */

static inline int
sensor_set_picture(struct usb_ov511 *ov511, struct video_picture *p)
{
	int rc;

	PDEBUG(4, "sensor_set_picture");

	ov511->whiteness = p->whiteness;

	/* Don't return error if a setting is unsupported, or rest of settings
         * will not be performed */

	rc = sensor_set_contrast(ov511, p->contrast);
	if (FATAL_ERROR(rc))
		return rc;

	rc = sensor_set_brightness(ov511, p->brightness);
	if (FATAL_ERROR(rc))
		return rc;

	rc = sensor_set_saturation(ov511, p->colour);
	if (FATAL_ERROR(rc))
		return rc;

	rc = sensor_set_hue(ov511, p->hue);
	if (FATAL_ERROR(rc))
		return rc;

	return 0;
}

static inline int
sensor_get_picture(struct usb_ov511 *ov511, struct video_picture *p)
{
	int rc;

	PDEBUG(4, "sensor_get_picture");

	/* Don't return error if a setting is unsupported, or rest of settings
         * will not be performed */

	rc = sensor_get_contrast(ov511, &(p->contrast));
	if (FATAL_ERROR(rc))
		return rc;

	rc = sensor_get_brightness(ov511, &(p->brightness));
	if (FATAL_ERROR(rc))
		return rc;

	rc = sensor_get_saturation(ov511, &(p->colour));
	if (FATAL_ERROR(rc))
		return rc;

	rc = sensor_get_hue(ov511, &(p->hue));
	if (FATAL_ERROR(rc))
		return rc;

	p->whiteness = 105 << 8;

	/* Can we get these from frame[0]? -claudio? */
	p->depth = ov511->frame[0].depth;
	p->palette = ov511->frame[0].format;

	return 0;
}

// FIXME: Exposure range is only 0x00-0x7f in interlace mode
/* Sets current exposure for sensor. This only has an effect if auto-exposure
 * is off */
static inline int
sensor_set_exposure(struct usb_ov511 *ov511, unsigned char val)
{
	int rc;

	PDEBUG(3, "%d", val);

	if (ov511->stop_during_set)
		if (ov511_stop(ov511) < 0)
			return -EIO;

	switch (ov511->sensor) {
	case SEN_OV6620:
	case SEN_OV6630:
	case SEN_OV7610:
	case SEN_OV7620:
	case SEN_OV7620AE:
	case SEN_OV8600:
		rc = ov51x_i2c_write(ov511, 0x10, val);
		if (rc < 0)
			goto out;

		break;
	case SEN_KS0127:
	case SEN_KS0127B:
	case SEN_SAA7111A:
		PDEBUG(3, "Unsupported with this sensor");
		return -EPERM;
	default:
		err("Sensor not supported for set_exposure");
		return -EINVAL;
	}

	rc = 0;		/* Success */
	ov511->exposure = val;
out:
	if (ov511_restart(ov511) < 0)
		return -EIO;

	return rc;
}

/* Gets current exposure level from sensor, regardless of whether it is under
 * manual control. */
static int
sensor_get_exposure(struct usb_ov511 *ov511, unsigned char *val)
{
	int rc;

	switch (ov511->sensor) {
	case SEN_OV7610:
	case SEN_OV6620:
	case SEN_OV6630:
	case SEN_OV7620:
	case SEN_OV7620AE:
	case SEN_OV8600:
		rc = ov51x_i2c_read(ov511, 0x10);
		if (rc < 0)
			return rc;
		else
			*val = rc;
		break;
	case SEN_KS0127:
	case SEN_KS0127B:
	case SEN_SAA7111A:
		val = 0;
		PDEBUG(3, "Unsupported with this sensor");
		return -EPERM;
	default:
		err("Sensor not supported for get_exposure");
		return -EINVAL;
	}

	PDEBUG(3, "%d", *val);
	ov511->exposure = *val;

	return 0;
}

/* Turns on or off the LED. Only has an effect with OV511+/OV518(+) */
static inline void 
ov51x_led_control(struct usb_ov511 *ov511, int enable)
{
	PDEBUG(4, " (%s)", enable ? "turn on" : "turn off");

	if (ov511->bridge == BRG_OV511PLUS)
		ov511_reg_write(ov511->dev, OV511_REG_SYSTEM_LED_CTL, 
			        enable ? 1 : 0);
	else if (ov511->bridge == BRG_OV518 ||
		 ov511->bridge == BRG_OV518PLUS)
		ov511_reg_write_mask(ov511->dev, OV518_REG_GPIO_OUT, 
				     enable ? 0x02 : 0x00, 0x02);
	return;
}

/* Matches the sensor's internal frame rate to the lighting frequency.
 * Valid frequencies are:
 *	50 - 50Hz, for European and Asian lighting
 *	60 - 60Hz, for American lighting
 *
 * Tested with: OV7610, OV7620, OV7620AE, OV6620
 * Unsupported: KS0127, KS0127B, SAA7111A
 * Returns: 0 for success
 */
static int
sensor_set_light_freq(struct usb_ov511 *ov511, int freq)
{
	int sixty;

	PDEBUG(4, "%d Hz", freq);

	if (freq == 60)
		sixty = 1;
	else if (freq == 50)
		sixty = 0;
	else {
		err("Invalid light freq (%d Hz)", freq);
		return -EINVAL;
	}

	switch (ov511->sensor) {
	case SEN_OV7610:
		ov51x_i2c_write_mask(ov511, 0x2a, sixty?0x00:0x80, 0x80);
		ov51x_i2c_write(ov511, 0x2b, sixty?0x00:0xac);
		ov51x_i2c_write_mask(ov511, 0x13, 0x10, 0x10);
		ov51x_i2c_write_mask(ov511, 0x13, 0x00, 0x10);
		break;
	case SEN_OV7620:
	case SEN_OV7620AE:
	case SEN_OV8600:
		ov51x_i2c_write_mask(ov511, 0x2a, sixty?0x00:0x80, 0x80);
		ov51x_i2c_write(ov511, 0x2b, sixty?0x00:0xac);
		ov51x_i2c_write_mask(ov511, 0x76, 0x01, 0x01);
		break;		
	case SEN_OV6620:
	case SEN_OV6630:
		ov51x_i2c_write(ov511, 0x2b, sixty?0xa8:0x28);
		ov51x_i2c_write(ov511, 0x2a, sixty?0x84:0xa4);
		break;
	case SEN_KS0127:
	case SEN_KS0127B:
	case SEN_SAA7111A:
		PDEBUG(5, "Unsupported with this sensor");
		return -EPERM;
	default:
		err("Sensor not supported for set_light_freq");
		return -EINVAL;
	}

	ov511->lightfreq = freq;

	return 0;
}

/* If enable is true, turn on the sensor's banding filter, otherwise turn it
 * off. This filter tries to reduce the pattern of horizontal light/dark bands
 * caused by some (usually fluorescent) lighting. The light frequency must be
 * set either before or after enabling it with ov51x_set_light_freq().
 *
 * Tested with: OV7610, OV7620, OV7620AE, OV6620.
 * Unsupported: KS0127, KS0127B, SAA7111A
 * Returns: 0 for success
 */
static inline int
sensor_set_banding_filter(struct usb_ov511 *ov511, int enable)
{
	int rc;

	PDEBUG(4, " (%s)", enable ? "turn on" : "turn off");

	if (ov511->sensor == SEN_KS0127 || ov511->sensor == SEN_KS0127B
		|| ov511->sensor == SEN_SAA7111A) {
		PDEBUG(5, "Unsupported with this sensor");
		return -EPERM;
	}

	rc = ov51x_i2c_write_mask(ov511, 0x2d, enable?0x04:0x00, 0x04);
	if (rc < 0)
		return rc;

	ov511->bandfilt = enable;

	return 0;
}

/* If enable is true, turn on the sensor's auto brightness control, otherwise
 * turn it off.
 *
 * Unsupported: KS0127, KS0127B, SAA7111A
 * Returns: 0 for success
 */
static inline int
sensor_set_auto_brightness(struct usb_ov511 *ov511, int enable)
{
	int rc;

	PDEBUG(4, " (%s)", enable ? "turn on" : "turn off");

	if (ov511->sensor == SEN_KS0127 || ov511->sensor == SEN_KS0127B
		|| ov511->sensor == SEN_SAA7111A) {
		PDEBUG(5, "Unsupported with this sensor");
		return -EPERM;
	}

	rc = ov51x_i2c_write_mask(ov511, 0x2d, enable?0x10:0x00, 0x10);
	if (rc < 0)
		return rc;

	ov511->auto_brt = enable;

	return 0;
}

/* If enable is true, turn on the sensor's auto exposure control, otherwise
 * turn it off.
 *
 * Unsupported: KS0127, KS0127B, SAA7111A
 * Returns: 0 for success
 */
static inline int
sensor_set_auto_exposure(struct usb_ov511 *ov511, int enable)
{	
	PDEBUG(4, " (%s)", enable ? "turn on" : "turn off");

	switch (ov511->sensor) {
	case SEN_OV7610:
		ov51x_i2c_write_mask(ov511, 0x29, enable?0x00:0x80, 0x80);
		break;
	case SEN_OV6620:
	case SEN_OV7620:
	case SEN_OV7620AE:
	case SEN_OV8600:
		ov51x_i2c_write_mask(ov511, 0x13, enable?0x01:0x00, 0x01);
		break;		
	case SEN_OV6630:
		ov51x_i2c_write_mask(ov511, 0x28, enable?0x00:0x10, 0x10);
		break;
	case SEN_KS0127:
	case SEN_KS0127B:
	case SEN_SAA7111A:
		PDEBUG(5, "Unsupported with this sensor");
		return -EPERM;
	default:
		err("Sensor not supported for set_auto_exposure");
		return -EINVAL;
	}

	ov511->auto_exp = enable;

	return 0;
}

/* Modifies the sensor's exposure algorithm to allow proper exposure of objects
 * that are illuminated from behind.
 *
 * Tested with: OV6620, OV7620
 * Unsupported: OV7610, OV7620AE, KS0127, KS0127B, SAA7111A
 * Returns: 0 for success
 */
static int
sensor_set_backlight(struct usb_ov511 *ov511, int enable)
{

	PDEBUG(4, " (%s)", enable ? "turn on" : "turn off");

	switch (ov511->sensor) {
	case SEN_OV7620:
	case SEN_OV8600:
		ov51x_i2c_write_mask(ov511, 0x68, enable?0xe0:0xc0, 0xe0);
		ov51x_i2c_write_mask(ov511, 0x29, enable?0x08:0x00, 0x08);
		ov51x_i2c_write_mask(ov511, 0x28, enable?0x02:0x00, 0x02);
		break;		
	case SEN_OV6620:
		ov51x_i2c_write_mask(ov511, 0x4e, enable?0xe0:0xc0, 0xe0);
		ov51x_i2c_write_mask(ov511, 0x29, enable?0x08:0x00, 0x08);
		ov51x_i2c_write_mask(ov511, 0x0e, enable?0x80:0x00, 0x80);
		break;
	case SEN_OV6630:
		ov51x_i2c_write_mask(ov511, 0x4e, enable?0x80:0x60, 0xe0);
		ov51x_i2c_write_mask(ov511, 0x29, enable?0x08:0x00, 0x08);
		ov51x_i2c_write_mask(ov511, 0x28, enable?0x02:0x00, 0x02);
		break;
	case SEN_OV7610:
	case SEN_OV7620AE:
	case SEN_KS0127:
	case SEN_KS0127B:
	case SEN_SAA7111A:
		PDEBUG(5, "Unsupported with this sensor");
		return -EPERM;
	default:
		err("Sensor not supported for set_backlight");
		return -EINVAL;
	}

	ov511->backlight = enable;

	return 0;
}

/* Returns number of bits per pixel (regardless of where they are located;
 * planar or not), or zero for unsupported format.
 */
static inline int 
ov511_get_depth(int palette)
{
	switch (palette) {
	case VIDEO_PALETTE_GREY:    return 8;
	case VIDEO_PALETTE_RGB565:  return 16;
	case VIDEO_PALETTE_RGB24:   return 24;  
	case VIDEO_PALETTE_YUV422:  return 16;
	case VIDEO_PALETTE_YUYV:    return 16;
	case VIDEO_PALETTE_YUV420:  return 12;
	case VIDEO_PALETTE_YUV422P: return 16; /* Planar */
	case VIDEO_PALETTE_YUV420P: return 12; /* Planar */
	default:		    return 0;  /* Invalid format */
	}
}

/* Bytes per frame. Used by read(). Return of 0 indicates error */
static inline long int 
get_frame_length(struct ov511_frame *frame)
{
	if (!frame)
		return 0;
	else
		return ((frame->width * frame->height
			 * ov511_get_depth(frame->format)) >> 3);
}

static int
mode_init_ov_sensor_regs(struct usb_ov511 *ov511, int width, int height,
			 int mode, int sub_flag, int qvga)
{
	int clock;

	/******** Mode (VGA/QVGA) and sensor specific regs ********/

	switch (ov511->sensor) {
	case SEN_OV7610:
		ov51x_i2c_write(ov511, 0x14, qvga?0x24:0x04);
// FIXME: Does this improve the image quality or frame rate?
#if 0
		ov51x_i2c_write_mask(ov511, 0x28, qvga?0x00:0x20, 0x20);
		ov51x_i2c_write(ov511, 0x24, 0x10);
		ov51x_i2c_write(ov511, 0x25, qvga?0x40:0x8a);
		ov51x_i2c_write(ov511, 0x2f, qvga?0x30:0xb0);
		ov51x_i2c_write(ov511, 0x35, qvga?0x1c:0x9c);
#endif
		break;
	case SEN_OV7620:
//		ov51x_i2c_write(ov511, 0x2b, 0x00);
		ov51x_i2c_write(ov511, 0x14, qvga?0xa4:0x84);
		ov51x_i2c_write_mask(ov511, 0x28, qvga?0x00:0x20, 0x20);
		ov51x_i2c_write(ov511, 0x24, qvga?0x20:0x3a);
		ov51x_i2c_write(ov511, 0x25, qvga?0x30:0x60);
		ov51x_i2c_write_mask(ov511, 0x2d, qvga?0x40:0x00, 0x40);
		ov51x_i2c_write_mask(ov511, 0x67, qvga?0xf0:0x90, 0xf0);
		ov51x_i2c_write_mask(ov511, 0x74, qvga?0x20:0x00, 0x20);
		break;
	case SEN_OV7620AE:
//		ov51x_i2c_write(ov511, 0x2b, 0x00);
		ov51x_i2c_write(ov511, 0x14, qvga?0xa4:0x84);
// FIXME: Enable this once 7620AE uses 7620 initial settings
#if 0
		ov51x_i2c_write_mask(ov511, 0x28, qvga?0x00:0x20, 0x20);
		ov51x_i2c_write(ov511, 0x24, qvga?0x20:0x3a);
		ov51x_i2c_write(ov511, 0x25, qvga?0x30:0x60);
		ov51x_i2c_write_mask(ov511, 0x2d, qvga?0x40:0x00, 0x40);
		ov51x_i2c_write_mask(ov511, 0x67, qvga?0xb0:0x90, 0xf0);
		ov51x_i2c_write_mask(ov511, 0x74, qvga?0x20:0x00, 0x20);
#endif
		break;
	case SEN_OV6620:
	case SEN_OV6630:
		ov51x_i2c_write(ov511, 0x14, qvga?0x24:0x04);
		/* No special settings yet */
		break;
	default:
		err("Invalid sensor");
		return -EINVAL;
	}

	/******** Palette-specific regs ********/

	if (mode == VIDEO_PALETTE_GREY) {
		if (ov511->sensor == SEN_OV7610
		    || ov511->sensor == SEN_OV7620AE) {
			/* these aren't valid on the OV6620/OV7620/6630? */
			ov51x_i2c_write_mask(ov511, 0x0e, 0x40, 0x40);
		}
		ov51x_i2c_write_mask(ov511, 0x13, 0x20, 0x20);
	} else {
		if (ov511->sensor == SEN_OV7610
		    || ov511->sensor == SEN_OV7620AE) {
			/* not valid on the OV6620/OV7620/6630? */
			ov51x_i2c_write_mask(ov511, 0x0e, 0x00, 0x40);
		}
		ov51x_i2c_write_mask(ov511, 0x13, 0x00, 0x20);
	}

	/******** Clock programming ********/

	// FIXME: Test this with OV6630

	/* The OV6620 needs special handling. This prevents the 
	 * severe banding that normally occurs */
	if (ov511->sensor == SEN_OV6620 || ov511->sensor == SEN_OV6630)
	{
		/* Clock down */

		ov51x_i2c_write(ov511, 0x2a, 0x04);

		if (ov511->compress) {
//			clock = 0;    /* This ensures the highest frame rate */
			clock = 3;
		} else if (clockdiv == -1) {   /* If user didn't override it */
			clock = 3;    /* Gives better exposure time */
		} else {
			clock = clockdiv;
		}

		PDEBUG(4, "Setting clock divisor to %d", clock);

		ov51x_i2c_write(ov511, 0x11, clock);

		ov51x_i2c_write(ov511, 0x2a, 0x84);
		/* This next setting is critical. It seems to improve
		 * the gain or the contrast. The "reserved" bits seem
		 * to have some effect in this case. */
		ov51x_i2c_write(ov511, 0x2d, 0x85);
	}
	else
	{
		if (ov511->compress) {
			clock = 1;    /* This ensures the highest frame rate */
		} else if (clockdiv == -1) {   /* If user didn't override it */
			/* Calculate and set the clock divisor */
			clock = ((sub_flag ? ov511->subw * ov511->subh
				  : width * height)
				 * (mode == VIDEO_PALETTE_GREY ? 2 : 3) / 2)
				 / 66000;
		} else {
			clock = clockdiv;
		}

		PDEBUG(4, "Setting clock divisor to %d", clock);

		ov51x_i2c_write(ov511, 0x11, clock);
	}

	/******** Special Features ********/

	if (framedrop >= 0)
		ov51x_i2c_write(ov511, 0x16, framedrop);

	/* We only have code to convert GBR -> RGB24 */
	if ((mode == VIDEO_PALETTE_RGB24) && sensor_gbr)
		ov51x_i2c_write_mask(ov511, 0x12, 0x08, 0x08);
	else
		ov51x_i2c_write_mask(ov511, 0x12, 0x00, 0x08);

	/* Test Pattern */
	ov51x_i2c_write_mask(ov511, 0x12, (testpat?0x02:0x00), 0x02);

	/* Auto white balance */
//	if (awb)
		ov51x_i2c_write_mask(ov511, 0x12, 0x04, 0x04);
//	else
//		ov51x_i2c_write_mask(ov511, 0x12, 0x00, 0x04);

	// This will go away as soon as ov511_mode_init_sensor_regs()
	// is fully tested.
	/* 7620/6620/6630? don't have register 0x35, so play it safe */
	if (ov511->sensor == SEN_OV7610 ||
	    ov511->sensor == SEN_OV7620AE) {
		if (width == 640 && height == 480)
			ov51x_i2c_write(ov511, 0x35, 0x9e);
		else
			ov51x_i2c_write(ov511, 0x35, 0x1e);
	}

	return 0;
}

static int
set_ov_sensor_window(struct usb_ov511 *ov511, int width, int height, int mode,
		     int sub_flag)
{
	int ret;
	int hwsbase, hwebase, vwsbase, vwebase, hwsize, vwsize; 
	int hoffset, voffset, hwscale = 0, vwscale = 0;

	/* The different sensor ICs handle setting up of window differently.
	 * IF YOU SET IT WRONG, YOU WILL GET ALL ZERO ISOC DATA FROM OV51x!!! */
	switch (ov511->sensor) {
	case SEN_OV7610:
	case SEN_OV7620AE:
		hwsbase = 0x38;
		hwebase = 0x3a;
		vwsbase = vwebase = 0x05;
		break;
	case SEN_OV6620:
	case SEN_OV6630:	// FIXME: Is this right?
		hwsbase = 0x38;
		hwebase = 0x3a;
		vwsbase = 0x05;
		vwebase = 0x06;
		break;
	case SEN_OV7620:
		hwsbase = 0x2f;		/* From 7620.SET (spec is wrong) */
		hwebase = 0x2f;
		vwsbase = vwebase = 0x05;
		break;
	default:
		err("Invalid sensor");
		return -EINVAL;
	}

	if (ov511->sensor == SEN_OV6620 || ov511->sensor == SEN_OV6630) {
		if (width > 176 && height > 144) {  /* CIF */
			ret = mode_init_ov_sensor_regs(ov511, width, height,
				mode, sub_flag, 0);
			if (ret < 0)
				return ret;
			hwscale = 1;
			vwscale = 1;  /* The datasheet says 0; it's wrong */
			hwsize = 352;
			vwsize = 288;
		} else if (width > 176 || height > 144) {
			err("Illegal dimensions");
			return -EINVAL;
		} else {			    /* QCIF */
			ret = mode_init_ov_sensor_regs(ov511, width, height,
				mode, sub_flag, 1);
			if (ret < 0)
				return ret;
			hwsize = 176;
			vwsize = 144;
		}
	} else {
		if (width > 320 && height > 240) {  /* VGA */
			ret = mode_init_ov_sensor_regs(ov511, width, height,
				mode, sub_flag, 0);
			if (ret < 0)
				return ret;
			hwscale = 2;
			vwscale = 1;
			hwsize = 640;
			vwsize = 480;
		} else if (width > 320 || height > 240) {
			err("Illegal dimensions");
			return -EINVAL;
		} else {			    /* QVGA */
			ret = mode_init_ov_sensor_regs(ov511, width, height,
				mode, sub_flag, 1);
			if (ret < 0)
				return ret;
			hwscale = 1;
			hwsize = 320;
			vwsize = 240;
		}
	}

	/* Center the window */
	hoffset = ((hwsize - width) / 2) >> hwscale;
	voffset = ((vwsize - height) / 2) >> vwscale;

	/* FIXME! - This needs to be changed to support 160x120 and 6620!!! */
	if (sub_flag) {
		ov51x_i2c_write(ov511, 0x17, hwsbase+(ov511->subx>>hwscale));
		ov51x_i2c_write(ov511, 0x18,
				hwebase+((ov511->subx+ov511->subw)>>hwscale));
		ov51x_i2c_write(ov511, 0x19, vwsbase+(ov511->suby>>vwscale));
		ov51x_i2c_write(ov511, 0x1a,
				vwebase+((ov511->suby+ov511->subh)>>vwscale));
	} else {
		ov51x_i2c_write(ov511, 0x17, hwsbase + hoffset);
		ov51x_i2c_write(ov511, 0x18,
				hwebase + hoffset + (hwsize>>hwscale));
		ov51x_i2c_write(ov511, 0x19, vwsbase + voffset);
		ov51x_i2c_write(ov511, 0x1a,
				vwebase + voffset + (vwsize>>vwscale));
	}

#ifdef OV511_DEBUG
	if (dump_sensor)
		ov51x_dump_i2c_regs(ov511);
#endif

	return 0;
}

/* Set up the OV511/OV511+ with the given image parameters.
 *
 * Do not put any sensor-specific code in here (including I2C I/O functions)
 */
static int
ov511_mode_init_regs(struct usb_ov511 *ov511,
		     int width, int height, int mode, int sub_flag)
{
	int lncnt, pxcnt, rc = 0;
	struct usb_device *dev = ov511->dev;

	if (!ov511 || !dev)
		return -EFAULT;

	if (sub_flag) {
		width = ov511->subw;
		height = ov511->subh;
	}

	PDEBUG(3, "width:%d, height:%d, mode:%d, sub:%d",
	       width, height, mode, sub_flag);

	// FIXME: This should be moved to a 7111a-specific function once
	// subcapture is dealt with properly
	if (ov511->sensor == SEN_SAA7111A) {
		if (width == 320 && height == 240) {
			/* No need to do anything special */
		} else if (width == 640 && height == 480) {
			/* Set the OV511 up as 320x480, but keep the V4L
			 * resolution as 640x480 */
			width = 320;
		} else {
			err("SAA7111A only supports 320x240 or 640x480");
			return -EINVAL;
		}
	}

	/* Make sure width and height are a multiple of 8 */
	if (width % 8 || height % 8) {
		err("Invalid size (%d, %d) (mode = %d)", width, height, mode);
		return -EINVAL;
	}

	if (width < ov511->minwidth || height < ov511->minheight) {
		err("Requested dimensions are too small");
		return -EINVAL;
	}

	if (ov511_stop(ov511) < 0)
		return -EIO;

	if (mode == VIDEO_PALETTE_GREY) {
		ov511_reg_write(dev, 0x16, 0x00);

		/* For snapshot */
		ov511_reg_write(dev, 0x1e, 0x00);
		ov511_reg_write(dev, 0x1f, 0x01);
	} else {
		ov511_reg_write(dev, 0x16, 0x01);

		/* For snapshot */
		ov511_reg_write(dev, 0x1e, 0x01);
		ov511_reg_write(dev, 0x1f, 0x03);
	}

	/* Here I'm assuming that snapshot size == image size.
	 * I hope that's always true. --claudio
	 */
	pxcnt = (width >> 3) - 1;
	lncnt = (height >> 3) - 1;

	ov511_reg_write(dev, 0x12, pxcnt);
	ov511_reg_write(dev, 0x13, lncnt);
	ov511_reg_write(dev, 0x14, 0x00);
	ov511_reg_write(dev, 0x15, 0x00);
	ov511_reg_write(dev, 0x18, 0x03);	/* YUV420, low pass filer on */

	/* Snapshot additions */
	ov511_reg_write(dev, 0x1a, pxcnt);
	ov511_reg_write(dev, 0x1b, lncnt);
        ov511_reg_write(dev, 0x1c, 0x00);
        ov511_reg_write(dev, 0x1d, 0x00);

	if (ov511->compress) {
		ov511_reg_write(dev, 0x78, 0x07); // Turn on Y & UV compression
		ov511_reg_write(dev, 0x79, 0x03); // Enable LUTs
		ov511_reset(ov511, OV511_RESET_OMNICE);
	}
//out:
	if (ov511_restart(ov511) < 0)
		return -EIO;

	return rc;
}

static struct mode_list_518 mlist518[] = {
	/* W    H   reg28 reg29 reg2a reg2c reg2e reg24 reg25 */
	{ 352, 288, 0x00, 0x16, 0x48, 0x00, 0x00, 0x9f, 0x90 },
	{ 320, 240, 0x00, 0x14, 0x3c, 0x10, 0x18, 0x9f, 0x90 },
	{ 176, 144, 0x05, 0x0b, 0x24, 0x00, 0x00, 0xff, 0xf0 },
	{ 160, 120, 0x05, 0x0a, 0x1e, 0x08, 0x0c, 0xff, 0xf0 },
	{ 0, 0 }
};

/* Sets up the OV518/OV518+ with the given image parameters
 *
 * OV518 needs a completely different approach, until we can figure out what
 * the individual registers do. Many register ops are commented out until we
 * can find out if they are still valid. Also, only 15 FPS is supported now.
 *
 * Do not put any sensor-specific code in here (including I2C I/O functions)
 */
static int
ov518_mode_init_regs(struct usb_ov511 *ov511,
		     int width, int height, int mode, int sub_flag)
{
	int i;
	struct usb_device *dev = ov511->dev;
	unsigned char b[3]; /* Multiple-value reg buffer */

	PDEBUG(3, "width:%d, height:%d, mode:%d, sub:%d",
	       width, height, mode, sub_flag);

	if (ov511_stop(ov511) < 0)
		return -EIO;

	for (i = 0; mlist518[i].width; i++) {
//		int lncnt, pxcnt;

		if (width != mlist518[i].width || height != mlist518[i].height)
			continue;

// FIXME: Subcapture won't be possible until we know what the registers do
// FIXME: We can't handle anything but YUV420 so far

//		/* Here I'm assuming that snapshot size == image size.
//		 * I hope that's always true. --claudio
//		 */
//		pxcnt = sub_flag ? (ov511->subw >> 3) - 1 : mlist[i].pxcnt;
//		lncnt = sub_flag ? (ov511->subh >> 3) - 1 : mlist[i].lncnt;
//
//		ov511_reg_write(dev, 0x12, pxcnt);
//		ov511_reg_write(dev, 0x13, lncnt);

		/******** Set the mode ********/		

		/* Mode independent regs */
		ov511_reg_write(dev, 0x2b, 0x00);
		ov511_reg_write(dev, 0x2d, 0x00);
		ov511_reg_write(dev, 0x3b, 0x00);
		ov511_reg_write(dev, 0x3d, 0x00);

		/* Mode dependent regs. Regs 38 - 3e are always the same as
		 * regs 28 - 2e */
		ov511_reg_write_mask(dev, 0x28, mlist518[i].reg28
			| (mode == VIDEO_PALETTE_GREY) ? 0x80:0x00, 0x8f);
		ov511_reg_write(dev, 0x29, mlist518[i].reg29);
		ov511_reg_write(dev, 0x2a, mlist518[i].reg2a);
		ov511_reg_write(dev, 0x2c, mlist518[i].reg2c);
		ov511_reg_write(dev, 0x2e, mlist518[i].reg2e);
		ov511_reg_write_mask(dev, 0x38, mlist518[i].reg28 
			| (mode == VIDEO_PALETTE_GREY) ? 0x80:0x00, 0x8f);
		ov511_reg_write(dev, 0x39, mlist518[i].reg29);
		ov511_reg_write(dev, 0x3a, mlist518[i].reg2a);
		ov511_reg_write(dev, 0x3c, mlist518[i].reg2c);
		ov511_reg_write(dev, 0x3e, mlist518[i].reg2e);
		ov511_reg_write(dev, 0x24, mlist518[i].reg24);
		ov511_reg_write(dev, 0x25, mlist518[i].reg25);

		/* Windows driver does this here; who knows why */
		ov511_reg_write(dev, 0x2f, 0x80);

		/******** Set the framerate (to 15 FPS) ********/		

		/* Mode independent, but framerate dependent, regs */
		/* These are for 15 FPS only */
		ov511_reg_write(dev, 0x51, 0x08);
		ov511_reg_write(dev, 0x22, 0x18);
		ov511_reg_write(dev, 0x23, 0xff);
		ov511_reg_write(dev, 0x71, 0x19);  /* Compression-related? */

		// FIXME: Sensor-specific
		/* Bit 5 is what matters here. Of course, it is "reserved" */
		ov51x_i2c_write(ov511, 0x54, 0x23);

		ov511_reg_write(dev, 0x2f, 0x80);

		/* Mode dependent regs */
		if ((width == 352 && height == 288) ||
		    (width == 320 && height == 240)) {
			b[0]=0x80; b[1]=0x02;
			ov518_reg_write_multi(dev, 0x30, b, 2);
			b[0]=0x90; b[1]=0x01;
			ov518_reg_write_multi(dev, 0xc4, b, 2);
			b[0]=0xf4; b[1]=0x01;
			ov518_reg_write_multi(dev, 0xc6, b, 2);
			b[0]=0xf4; b[1]=0x01;
			ov518_reg_write_multi(dev, 0xc7, b, 2);
			b[0]=0x8e; b[1]=0x00;
			ov518_reg_write_multi(dev, 0xc8, b, 2);
			b[0]=0x1a; b[1]=0x00; b[2]=0x02;
			ov518_reg_write_multi(dev, 0xca, b, 3);
			b[0]=0x14; b[1]=0x02;
			ov518_reg_write_multi(dev, 0xcb, b, 2);
			b[0]=0xd0; b[1]=0x07;
			ov518_reg_write_multi(dev, 0xcc, b, 2);
			b[0]=0x20; b[1]=0x00;
			ov518_reg_write_multi(dev, 0xcd, b, 2);
			b[0]=0x60; b[1]=0x02;
			ov518_reg_write_multi(dev, 0xce, b, 2);

		} else if ((width == 176 && height == 144) ||
			   (width == 160 && height == 120)) {
			b[0]=0x80; b[1]=0x01;
			ov518_reg_write_multi(dev, 0x30, b, 2);
			b[0]=0xc8; b[1]=0x00;
			ov518_reg_write_multi(dev, 0xc4, b, 2);
			b[0]=0x40; b[1]=0x01;
			ov518_reg_write_multi(dev, 0xc6, b, 2);
			b[0]=0x40; b[1]=0x01;
			ov518_reg_write_multi(dev, 0xc7, b, 2);
			b[0]=0x60; b[1]=0x00;
			ov518_reg_write_multi(dev, 0xc8, b, 2);
			b[0]=0x0f; b[1]=0x33; b[2]=0x01;
			ov518_reg_write_multi(dev, 0xca, b, 3);
			b[0]=0x40; b[1]=0x01;
			ov518_reg_write_multi(dev, 0xcb, b, 2);
			b[0]=0xec; b[1]=0x04;
			ov518_reg_write_multi(dev, 0xcc, b, 2);
			b[0]=0x13; b[1]=0x00;
			ov518_reg_write_multi(dev, 0xcd, b, 2);
			b[0]=0x6d; b[1]=0x01;
			ov518_reg_write_multi(dev, 0xce, b, 2);
		} else {
			/* Can't happen, since we already handled this case */
			err("ov518_mode_init_regs(): **** logic error ****");
		}

		ov511_reg_write(dev, 0x2f, 0x80);

		break;
	}

	if (ov511_restart(ov511) < 0)
		return -EIO;

	/* Reset it just for good measure */
	if (ov511_reset(ov511, OV511_RESET_NOREGS) < 0)
		return -EIO;

	if (mlist518[i].width == 0) {
		err("Unknown mode (%d, %d): %d", width, height, mode);
		return -EINVAL;
	}

	return 0;
}

/* This is a wrapper around the OV511, OV518, and sensor specific functions */
static int
mode_init_regs(struct usb_ov511 *ov511,
	       int width, int height, int mode, int sub_flag)
{
	int rc = 0;

	if (ov511->bridge == BRG_OV518 ||
	    ov511->bridge == BRG_OV518PLUS) {
		rc = ov518_mode_init_regs(ov511, width, height, mode, sub_flag);
	} else {
		rc = ov511_mode_init_regs(ov511, width, height, mode, sub_flag);
	}

	if (FATAL_ERROR(rc))
		return rc;

	switch (ov511->sensor) {
	case SEN_OV7610:
	case SEN_OV7620:
	case SEN_OV7620AE:
	case SEN_OV8600:
	case SEN_OV6620:
	case SEN_OV6630:
		rc = set_ov_sensor_window(ov511, width, height, mode, sub_flag);
		break;
	case SEN_KS0127:
	case SEN_KS0127B:
		err("KS0127-series decoders not supported yet");
		rc = -EINVAL;
		break;
	case SEN_SAA7111A:
//		rc = mode_init_saa_sensor_regs(ov511, width, height, mode, 
//					       sub_flag);

		PDEBUG(1, "SAA status = 0X%x", ov51x_i2c_read(ov511, 0x1f));
		break;
	default:
		err("Unknown sensor");
		rc = -EINVAL;
	}

	if (FATAL_ERROR(rc))
		return rc;

	/* Sensor-independent settings */
	rc = sensor_set_auto_brightness(ov511, ov511->auto_brt);
	if (FATAL_ERROR(rc))
		return rc;

	rc = sensor_set_auto_exposure(ov511, ov511->auto_exp);
	if (FATAL_ERROR(rc))
		return rc;

	rc = sensor_set_banding_filter(ov511, bandingfilter);
	if (FATAL_ERROR(rc))
		return rc;

	if (ov511->lightfreq) {
		rc = sensor_set_light_freq(ov511, lightfreq);
		if (FATAL_ERROR(rc))
			return rc;
	}

	rc = sensor_set_backlight(ov511, ov511->backlight);
	if (FATAL_ERROR(rc))
		return rc;

	return 0;
}

/* This sets the default image parameters (Size = max, RGB24). This is
 * useful for apps that use read() and do not set these.
 */
static int 
ov51x_set_default_params(struct usb_ov511 *ov511)
{
	int i;

	PDEBUG(3, "%dx%d, RGB24", ov511->maxwidth, ov511->maxheight);

	/* Set default sizes in case IOCTL (VIDIOCMCAPTURE) is not used
	 * (using read() instead). */
	for (i = 0; i < OV511_NUMFRAMES; i++) {
		ov511->frame[i].width = ov511->maxwidth;
		ov511->frame[i].height = ov511->maxheight;
		ov511->frame[i].bytes_read = 0;
		if (force_palette)
			ov511->frame[i].format = force_palette;
		else
			ov511->frame[i].format = VIDEO_PALETTE_RGB24;
		ov511->frame[i].depth = ov511_get_depth(ov511->frame[i].format);
	}

	/* Initialize to max width/height, RGB24 */
	if (mode_init_regs(ov511, ov511->maxwidth, ov511->maxheight,
			   ov511->frame[0].format, 0) < 0)
		return -EINVAL;

	return 0;
}

/**********************************************************************
 *
 * Video decoder stuff
 *
 **********************************************************************/

/* Set analog input port of decoder */
static int 
decoder_set_input(struct usb_ov511 *ov511, int input)
{
	PDEBUG(4, "port %d", input);

	switch (ov511->sensor) {
	case SEN_SAA7111A:
	{
		/* Select mode */
		ov51x_i2c_write_mask(ov511, 0x02, input, 0x07);
		/* Bypass chrominance trap for modes 4..7 */
		ov51x_i2c_write_mask(ov511, 0x09,
				     (input > 3) ? 0x80:0x00, 0x80);
		break;
	}
	default:
		return -EINVAL;
	}

	return 0;
}

/* Get ASCII name of video input */
static int 
decoder_get_input_name(struct usb_ov511 *ov511, int input, char *name)
{
	switch (ov511->sensor) {
	case SEN_SAA7111A:
	{
		if (input < 0 || input > 7)
			return -EINVAL;
		else if (input < 4)
			sprintf(name, "CVBS-%d", input);
		else // if (input < 8)
			sprintf(name, "S-Video-%d", input - 4);

		break;
	}
	default:
		sprintf(name, "%s", "Camera");
	}

	return 0;
}

/* Set norm (NTSC, PAL, SECAM, AUTO) */
static int 
decoder_set_norm(struct usb_ov511 *ov511, int norm)
{
	PDEBUG(4, "%d", norm);

	switch (ov511->sensor) {
	case SEN_SAA7111A:
	{
		int reg_8, reg_e;

		if (norm == VIDEO_MODE_NTSC) {
			reg_8 = 0x40;	/* 60 Hz */
			reg_e = 0x00;	/* NTSC M / PAL BGHI */
		} else if (norm == VIDEO_MODE_PAL) {
			reg_8 = 0x00;	/* 50 Hz */
			reg_e = 0x00;	/* NTSC M / PAL BGHI */	
		} else if (norm == VIDEO_MODE_AUTO) {
			reg_8 = 0x80;	/* Auto field detect */
			reg_e = 0x00;	/* NTSC M / PAL BGHI */
		} else if (norm == VIDEO_MODE_SECAM) {
			reg_8 = 0x00;	/* 50 Hz */
			reg_e = 0x50;	/* SECAM / PAL 4.43 */
		} else {
			return -EINVAL;
		}

		ov51x_i2c_write_mask(ov511, 0x08, reg_8, 0xc0);
		ov51x_i2c_write_mask(ov511, 0x0e, reg_e, 0x70);
		break;
	}
	default:
		return -EINVAL;
	}

	return 0;
}


/**********************************************************************
 *
 * Color correction functions
 *
 **********************************************************************/

/*
 * Turn a YUV4:2:0 block into an RGB block
 *
 * Video4Linux seems to use the blue, green, red channel
 * order convention-- rgb[0] is blue, rgb[1] is green, rgb[2] is red.
 *
 * Color space conversion coefficients taken from the excellent
 * http://www.inforamp.net/~poynton/ColorFAQ.html
 * In his terminology, this is a CCIR 601.1 YCbCr -> RGB.
 * Y values are given for all 4 pixels, but the U (Pb)
 * and V (Pr) are assumed constant over the 2x2 block.
 *
 * To avoid floating point arithmetic, the color conversion
 * coefficients are scaled into 16.16 fixed-point integers.
 * They were determined as follows:
 *
 *	double brightness = 1.0;  (0->black; 1->full scale) 
 *	double saturation = 1.0;  (0->greyscale; 1->full color)
 *	double fixScale = brightness * 256 * 256;
 *	int rvScale = (int)(1.402 * saturation * fixScale);
 *	int guScale = (int)(-0.344136 * saturation * fixScale);
 *	int gvScale = (int)(-0.714136 * saturation * fixScale);
 *	int buScale = (int)(1.772 * saturation * fixScale);
 *	int yScale = (int)(fixScale);	
 */

/* LIMIT: convert a 16.16 fixed-point value to a byte, with clipping. */
#define LIMIT(x) ((x)>0xffffff?0xff: ((x)<=0xffff?0:((x)>>16)))

static inline void
ov511_move_420_block(int yTL, int yTR, int yBL, int yBR, int u, int v, 
		     int rowPixels, unsigned char * rgb, int bits)
{
	const int rvScale = 91881;
	const int guScale = -22553;
	const int gvScale = -46801;
	const int buScale = 116129;
	const int yScale  = 65536;
	int r, g, b;

	g = guScale * u + gvScale * v;
	if (force_rgb) {
		r = buScale * u;
		b = rvScale * v;
	} else {
		r = rvScale * v;
		b = buScale * u;
	}

	yTL *= yScale; yTR *= yScale;
	yBL *= yScale; yBR *= yScale;

	if (bits == 24) {
		/* Write out top two pixels */
		rgb[0] = LIMIT(b+yTL); rgb[1] = LIMIT(g+yTL);
		rgb[2] = LIMIT(r+yTL);

		rgb[3] = LIMIT(b+yTR); rgb[4] = LIMIT(g+yTR);
		rgb[5] = LIMIT(r+yTR);

		/* Skip down to next line to write out bottom two pixels */
		rgb += 3 * rowPixels;
		rgb[0] = LIMIT(b+yBL); rgb[1] = LIMIT(g+yBL);
		rgb[2] = LIMIT(r+yBL);

		rgb[3] = LIMIT(b+yBR); rgb[4] = LIMIT(g+yBR);
		rgb[5] = LIMIT(r+yBR);
	} else if (bits == 16) {
		/* Write out top two pixels */
		rgb[0] = ((LIMIT(b+yTL) >> 3) & 0x1F) 
			| ((LIMIT(g+yTL) << 3) & 0xE0);
		rgb[1] = ((LIMIT(g+yTL) >> 5) & 0x07)
			| (LIMIT(r+yTL) & 0xF8);

		rgb[2] = ((LIMIT(b+yTR) >> 3) & 0x1F) 
			| ((LIMIT(g+yTR) << 3) & 0xE0);
		rgb[3] = ((LIMIT(g+yTR) >> 5) & 0x07) 
			| (LIMIT(r+yTR) & 0xF8);

		/* Skip down to next line to write out bottom two pixels */
		rgb += 2 * rowPixels;

		rgb[0] = ((LIMIT(b+yBL) >> 3) & 0x1F)
			| ((LIMIT(g+yBL) << 3) & 0xE0);
		rgb[1] = ((LIMIT(g+yBL) >> 5) & 0x07)
			| (LIMIT(r+yBL) & 0xF8);

		rgb[2] = ((LIMIT(b+yBR) >> 3) & 0x1F)
			| ((LIMIT(g+yBR) << 3) & 0xE0);
		rgb[3] = ((LIMIT(g+yBR) >> 5) & 0x07)
			| (LIMIT(r+yBR) & 0xF8);
	}
}

/**********************************************************************
 *
 * Raw data parsing
 *
 **********************************************************************/

/* Copies a 64-byte segment at pIn to an 8x8 block at pOut. The width of the
 * array at pOut is specified by w.
 */
static inline void 
ov511_make_8x8(unsigned char *pIn, unsigned char *pOut, int w)
{
	unsigned char *pOut1 = pOut;
	int x, y;

	for (y = 0; y < 8; y++) {
		pOut1 = pOut;
		for (x = 0; x < 8; x++) {
			*pOut1++ = *pIn++;
		}
		pOut += w;
	}
		
}

/*
 * For RAW BW (YUV400) images, data shows up in 256 byte segments.
 * The segments represent 4 squares of 8x8 pixels as follows:
 *
 *      0  1 ...  7    64  65 ...  71   ...  192 193 ... 199
 *      8  9 ... 15    72  73 ...  79        200 201 ... 207
 *           ...              ...                    ...
 *     56 57 ... 63   120 121 ... 127        248 249 ... 255
 *
 */ 
static void
yuv400raw_to_yuv400p(struct ov511_frame *frame,
		     unsigned char *pIn0, unsigned char *pOut0)
{
	int x, y;
	unsigned char *pIn, *pOut, *pOutLine;

	/* Copy Y */
	pIn = pIn0;
	pOutLine = pOut0;
	for (y = 0; y < frame->rawheight - 1; y += 8) {
		pOut = pOutLine;
		for (x = 0; x < frame->rawwidth - 1; x += 8) {
			ov511_make_8x8(pIn, pOut, frame->rawwidth);
			pIn += 64;
			pOut += 8;
		}
		pOutLine += 8 * frame->rawwidth;
	}
}

/*
 * For YUV4:2:0 images, the data shows up in 384 byte segments.
 * The first 64 bytes of each segment are U, the next 64 are V.  The U and
 * V are arranged as follows:
 *
 *      0  1 ...  7
 *      8  9 ... 15
 *           ...   
 *     56 57 ... 63
 *
 * U and V are shipped at half resolution (1 U,V sample -> one 2x2 block).
 *
 * The next 256 bytes are full resolution Y data and represent 4 squares
 * of 8x8 pixels as follows:
 *
 *      0  1 ...  7    64  65 ...  71   ...  192 193 ... 199
 *      8  9 ... 15    72  73 ...  79        200 201 ... 207
 *           ...              ...                    ...
 *     56 57 ... 63   120 121 ... 127   ...  248 249 ... 255
 *
 * Note that the U and V data in one segment represents a 16 x 16 pixel
 * area, but the Y data represents a 32 x 8 pixel area. If the width is not an
 * even multiple of 32, the extra 8x8 blocks within a 32x8 block belong to the
 * next horizontal stripe.
 *
 * If dumppix module param is set, _parse_data just dumps the incoming segments,
 * verbatim, in order, into the frame. When used with vidcat -f ppm -s 640x480
 * this puts the data on the standard output and can be analyzed with the
 * parseppm.c utility I wrote.  That's a much faster way for figuring out how
 * this data is scrambled.
 */

/* Converts from raw, uncompressed segments at pIn0 to a YUV420P frame at pOut0.
 *
 * FIXME: Currently only handles width and height that are multiples of 16
 */
static void
yuv420raw_to_yuv420p(struct ov511_frame *frame,
		     unsigned char *pIn0, unsigned char *pOut0)
{
	int k, x, y;
	unsigned char *pIn, *pOut, *pOutLine;
	const unsigned int a = frame->rawwidth * frame->rawheight;
	const unsigned int w = frame->rawwidth / 2;

	/* Copy U and V */
	pIn = pIn0;
	pOutLine = pOut0 + a;
	for (y = 0; y < frame->rawheight - 1; y += 16) {
		pOut = pOutLine;
		for (x = 0; x < frame->rawwidth - 1; x += 16) {
			ov511_make_8x8(pIn, pOut, w);
			ov511_make_8x8(pIn + 64, pOut + a/4, w);
			pIn += 384;
			pOut += 8;
		}
		pOutLine += 8 * w;
	}

	/* Copy Y */
	pIn = pIn0 + 128;
	pOutLine = pOut0;
	k = 0;
	for (y = 0; y < frame->rawheight - 1; y += 8) {
		pOut = pOutLine;
		for (x = 0; x < frame->rawwidth - 1; x += 8) {
			ov511_make_8x8(pIn, pOut, frame->rawwidth);
			pIn += 64;
			pOut += 8;
			if ((++k) > 3) {
				k = 0;
				pIn += 128;
			}
		}
		pOutLine += 8 * frame->rawwidth;
	}
}

/*
 * fixFrameRGBoffset--
 * My camera seems to return the red channel about 1 pixel
 * low, and the blue channel about 1 pixel high. After YUV->RGB
 * conversion, we can correct this easily. OSL 2/24/2000.
 */
static void 
fixFrameRGBoffset(struct ov511_frame *frame)
{
	int x, y;
	int rowBytes = frame->width*3, w = frame->width;
	unsigned char *rgb = frame->data;
	const int shift = 1;  /* Distance to shift pixels by, vertically */

	/* Don't bother with little images */
	if (frame->width < 400) 
		return;

	/* This only works with RGB24 */
	if (frame->format != VIDEO_PALETTE_RGB24)
		return;

	/* Shift red channel up */
	for (y = shift; y < frame->height; y++)	{
		int lp = (y-shift)*rowBytes;     /* Previous line offset */
		int lc = y*rowBytes;             /* Current line offset */
		for (x = 0; x < w; x++)
			rgb[lp+x*3+2] = rgb[lc+x*3+2]; /* Shift red up */
	}

	/* Shift blue channel down */
	for (y = frame->height-shift-1; y >= 0; y--) {
		int ln = (y + shift) * rowBytes;  /* Next line offset */
		int lc = y * rowBytes;            /* Current line offset */
		for (x = 0; x < w; x++)
			rgb[ln+x*3+0] = rgb[lc+x*3+0]; /* Shift blue down */
	}
}

/**********************************************************************
 *
 * Decompression
 *
 **********************************************************************/

/* Chooses a decompression module, locks it, and sets ov511->decomp_ops
 * accordingly. Returns -ENXIO if decompressor is not available, otherwise
 * returns 0 if no other error.
 */
static int 
ov51x_request_decompressor(struct usb_ov511 *ov511)
{
	if (!ov511)
		return -ENODEV;

	if (ov511->decomp_ops) {
		err("ERROR: Decompressor already requested!");
		return -EINVAL;
	}

	lock_kernel();

	/* Try to get MMX, and fall back on no-MMX if necessary */
	if (ov511->bridge == BRG_OV511 || ov511->bridge == BRG_OV511PLUS) {
		if (ov511_mmx_decomp_ops) {
			PDEBUG(3, "Using OV511 MMX decompressor");
			ov511->decomp_ops = ov511_mmx_decomp_ops;
		} else if (ov511_decomp_ops) {
			PDEBUG(3, "Using OV511 decompressor");
			ov511->decomp_ops = ov511_decomp_ops;
		} else {
			err("No decompressor available");
		}
	} else if (ov511->bridge == BRG_OV518 ||
		   ov511->bridge == BRG_OV518PLUS) {
		if (ov518_mmx_decomp_ops) {
			PDEBUG(3, "Using OV518 MMX decompressor");
			ov511->decomp_ops = ov518_mmx_decomp_ops;
		} else if (ov518_decomp_ops) {
			PDEBUG(3, "Using OV518 decompressor");
			ov511->decomp_ops = ov518_decomp_ops;
		} else {
			err("No decompressor available");
		}
	} else {
		err("Unknown bridge");
	}

	if (ov511->decomp_ops) {
		if (!ov511->decomp_ops->decomp_lock) {
			ov511->decomp_ops = NULL;
			unlock_kernel();
			return -ENOSYS;
		}
		ov511->decomp_ops->decomp_lock();
		unlock_kernel();
		return 0;
	} else {
		unlock_kernel();
		return -ENXIO;
	}
}

/* Unlocks decompression module and nulls ov511->decomp_ops. Safe to call even
 * if ov511->decomp_ops is NULL.
 */
static void 
ov51x_release_decompressor(struct usb_ov511 *ov511)
{
	int released = 0;	/* Did we actually do anything? */

	if (!ov511)
		return;

	lock_kernel();

	if (ov511->decomp_ops && ov511->decomp_ops->decomp_unlock) {
		ov511->decomp_ops->decomp_unlock();
		released = 1;
	}

	ov511->decomp_ops = NULL;
	
	unlock_kernel();

	if (released)
		PDEBUG(3, "Decompressor released");
}

static void 
ov51x_decompress(struct usb_ov511 *ov511, struct ov511_frame *frame,
		 unsigned char *pIn0, unsigned char *pOut0)
{
	if (!ov511->decomp_ops)
		if (ov51x_request_decompressor(ov511))
			return;

	PDEBUG(4, "Decompressing %d bytes", frame->bytes_recvd);

	if (frame->format == VIDEO_PALETTE_GREY 
	    && ov511->decomp_ops->decomp_400) {
		int ret = ov511->decomp_ops->decomp_400(
			pIn0,
			pOut0,
			frame->rawwidth,
			frame->rawheight,
			frame->bytes_recvd);
		PDEBUG(4, "DEBUG: decomp_400 returned %d", ret);
	} else if (ov511->decomp_ops->decomp_420) {
		int ret = ov511->decomp_ops->decomp_420(
			pIn0,
			pOut0,
			frame->rawwidth,
			frame->rawheight,
			frame->bytes_recvd);
		PDEBUG(4, "DEBUG: decomp_420 returned %d", ret);
	} else {
		err("Decompressor does not support this format");
	}
}

/**********************************************************************
 *
 * Format conversion
 *
 **********************************************************************/

/* Converts from planar YUV420 to RGB24. */
static void 
yuv420p_to_rgb(struct ov511_frame *frame,
	       unsigned char *pIn0, unsigned char *pOut0, int bits)
{
	const int numpix = frame->width * frame->height;
	const int bytes = bits >> 3;
	int i, j, y00, y01, y10, y11, u, v;
	unsigned char *pY = pIn0;
	unsigned char *pU = pY + numpix;
	unsigned char *pV = pU + numpix / 4;
	unsigned char *pOut = pOut0;

	for (j = 0; j <= frame->height - 2; j += 2) {
		for (i = 0; i <= frame->width - 2; i += 2) {
			y00 = *pY;
			y01 = *(pY + 1);
			y10 = *(pY + frame->width);
			y11 = *(pY + frame->width + 1);
			u = (*pU++) - 128;
			v = (*pV++) - 128;

			ov511_move_420_block(y00, y01, y10, y11, u, v,
					     frame->width, pOut, bits);
	
			pY += 2;
			pOut += 2 * bytes;

		}
		pY += frame->width;
		pOut += frame->width * bytes;
	}
}

/* Converts from planar YUV420 to YUV422 (YUYV). */
static void
yuv420p_to_yuv422(struct ov511_frame *frame,
		  unsigned char *pIn0, unsigned char *pOut0)
{
	const int numpix = frame->width * frame->height;
	int i, j;
	unsigned char *pY = pIn0;
	unsigned char *pU = pY + numpix;
	unsigned char *pV = pU + numpix / 4;
	unsigned char *pOut = pOut0;

	for (i = 0; i < numpix; i++) {
		*pOut = *(pY + i);
		pOut += 2;
	}

	pOut = pOut0 + 1;
	for (j = 0; j <= frame->height - 2 ; j += 2) {
		for (i = 0; i <= frame->width - 2; i += 2) {
			int u = *pU++;
			int v = *pV++;
			
			*pOut = u;
			*(pOut+2) = v;
			*(pOut+frame->width*2) = u;
			*(pOut+frame->width*2+2) = v;
			pOut += 4;
		}
		pOut += (frame->width * 2);
	}
}

/* Converts pData from planar YUV420 to planar YUV422 **in place**. */
static void
yuv420p_to_yuv422p(struct ov511_frame *frame, unsigned char *pData)
{
	const int numpix = frame->width * frame->height;
	const int w = frame->width;
	int j;
	unsigned char *pIn, *pOut;

	/* Clear U and V */
	memset(pData + numpix + numpix / 2, 127, numpix / 2);

	/* Convert V starting from beginning and working forward */
	pIn = pData + numpix + numpix / 4;
	pOut = pData + numpix +numpix / 2;
	for (j = 0; j <= frame->height - 2; j += 2) {
		memmove(pOut, pIn, w/2);
		memmove(pOut + w/2, pIn, w/2);
		pIn += w/2;
		pOut += w;
	}

	/* Convert U, starting from end and working backward */
	pIn = pData + numpix + numpix / 4;
	pOut = pData + numpix + numpix / 2;
	for (j = 0; j <= frame->height - 2; j += 2) {
		pIn -= w/2;
		pOut -= w;
		memmove(pOut, pIn, w/2);
		memmove(pOut + w/2, pIn, w/2);
	}
}

/* Fuses even and odd fields together, and doubles width.
 * INPUT: an odd field followed by an even field at pIn0, in YUV planar format
 * OUTPUT: a normal YUV planar image, with correct aspect ratio
 */
static void
deinterlace(struct ov511_frame *frame, int rawformat,
            unsigned char *pIn0, unsigned char *pOut0)
{
	const int fieldheight = frame->rawheight / 2;
	const int fieldpix = fieldheight * frame->rawwidth;
	const int w = frame->width;
	int x, y;
	unsigned char *pInEven, *pInOdd, *pOut;

	PDEBUG(5, "fieldheight=%d", fieldheight);

	if (frame->rawheight != frame->height) {
		err("invalid height");
		return;
	}

	if ((frame->rawwidth * 2) != frame->width) {
		err("invalid width");
		return;
	}

	/* Y */
	pInOdd = pIn0;
	pInEven = pInOdd + fieldpix;
	pOut = pOut0;
	for (y = 0; y < fieldheight; y++) {
		for (x = 0; x < frame->rawwidth; x++) {
			*pOut = *pInEven;
			*(pOut+1) = *pInEven++;
			*(pOut+w) = *pInOdd;
			*(pOut+w+1) = *pInOdd++;
			pOut += 2;
		}
		pOut += w;
	}

	if (rawformat == RAWFMT_YUV420) {
	/* U */
		pInOdd = pIn0 + fieldpix * 2;
		pInEven = pInOdd + fieldpix / 4;
		for (y = 0; y < fieldheight / 2; y++) {
			for (x = 0; x < frame->rawwidth / 2; x++) {
				*pOut = *pInEven;
				*(pOut+1) = *pInEven++;
				*(pOut+w/2) = *pInOdd;
				*(pOut+w/2+1) = *pInOdd++;
				pOut += 2;
			}
			pOut += w/2;
		}
	/* V */
		pInOdd = pIn0 + fieldpix * 2 + fieldpix / 2;
		pInEven = pInOdd + fieldpix / 4;
		for (y = 0; y < fieldheight / 2; y++) {
			for (x = 0; x < frame->rawwidth / 2; x++) {
				*pOut = *pInEven;
				*(pOut+1) = *pInEven++;
				*(pOut+w/2) = *pInOdd;
				*(pOut+w/2+1) = *pInOdd++;
				pOut += 2;
			}
			pOut += w/2;
		}
	}
}

/* Post-processes the specified frame. This consists of:
 * 	1. Decompress frame, if necessary
 *	2. Deinterlace frame and scale to proper size, if necessary
 * 	3. Convert from YUV planar to destination format, if necessary
 * 	4. Fix the RGB offset, if necessary
 */
static void 
ov511_postprocess(struct usb_ov511 *ov511, struct ov511_frame *frame)
{
	if (dumppix) {
		memset(frame->data, 0, 
			MAX_DATA_SIZE(ov511->maxwidth, ov511->maxheight));
		PDEBUG(4, "Dumping %d bytes", frame->bytes_recvd);
		memmove(frame->data, frame->rawdata, frame->bytes_recvd);
		return;
	}

	/* YUV400 must be handled separately */
	if (frame->format == VIDEO_PALETTE_GREY) {
		/* Deinterlace frame, if necessary */
		if (ov511->sensor == SEN_SAA7111A && frame->rawheight == 480) {
			if (frame->compressed)
				ov51x_decompress(ov511, frame, frame->rawdata,
						 frame->tempdata);
			else
				yuv400raw_to_yuv400p(frame, frame->rawdata,
						     frame->tempdata);

			deinterlace(frame, RAWFMT_YUV400, frame->tempdata,
			            frame->data);
		} else {
			if (frame->compressed)
				ov51x_decompress(ov511, frame, frame->rawdata,
						 frame->data);
			else
				yuv400raw_to_yuv400p(frame, frame->rawdata,
						     frame->data);
		}

		return;
	}

	/* Process frame->data to frame->rawdata */
	if (frame->compressed)
		ov51x_decompress(ov511, frame, frame->rawdata, frame->tempdata);
	else
		yuv420raw_to_yuv420p(frame, frame->rawdata, frame->tempdata);

	/* Deinterlace frame, if necessary */
	if (ov511->sensor == SEN_SAA7111A && frame->rawheight == 480) {
		memmove(frame->rawdata, frame->tempdata,
			MAX_RAW_DATA_SIZE(frame->width, frame->height));
		deinterlace(frame, RAWFMT_YUV420, frame->rawdata,
		            frame->tempdata);
	}

	/* Frame should be (width x height) and not (rawwidth x rawheight) at
         * this point. */

#if 0
	/* Clear output buffer for testing purposes */
	memset(frame->data, 0, MAX_DATA_SIZE(frame->width, frame->height));
#endif

	/* Process frame->tempdata to frame->data */
	switch (frame->format) {
	case VIDEO_PALETTE_RGB565:
		yuv420p_to_rgb(frame, frame->tempdata, frame->data, 16);
		break;
	case VIDEO_PALETTE_RGB24:
		yuv420p_to_rgb(frame, frame->tempdata, frame->data, 24);
		break;
	case VIDEO_PALETTE_YUV422:
	case VIDEO_PALETTE_YUYV:
		yuv420p_to_yuv422(frame, frame->tempdata, frame->data);
		break;
	case VIDEO_PALETTE_YUV420:
	case VIDEO_PALETTE_YUV420P:
		memmove(frame->data, frame->tempdata,
			MAX_RAW_DATA_SIZE(frame->width, frame->height));
		break;
	case VIDEO_PALETTE_YUV422P:
		/* Data is converted in place, so copy it in advance */
		memmove(frame->data, frame->tempdata,
			MAX_RAW_DATA_SIZE(frame->width, frame->height));

		yuv420p_to_yuv422p(frame, frame->data);
		break;
	default:
		err("Cannot convert data to this format");
	}

	if (fix_rgb_offset)
		fixFrameRGBoffset(frame);
}

/**********************************************************************
 *
 * OV51x data transfer, IRQ handler
 *
 **********************************************************************/

static int 
ov511_move_data(struct usb_ov511 *ov511, struct urb *urb)
{
	unsigned char *cdata;
	int data_size, num, offset, i, totlen = 0;
	int aPackNum[FRAMES_PER_DESC];
	struct ov511_frame *frame;
	struct timeval *ts;

	PDEBUG(5, "Moving %d packets", urb->number_of_packets);

	data_size = ov511->packet_size - 1;

	for (i = 0; i < urb->number_of_packets; i++) {
		int n = urb->iso_frame_desc[i].actual_length;
		int st = urb->iso_frame_desc[i].status;

		urb->iso_frame_desc[i].actual_length = 0;
		urb->iso_frame_desc[i].status = 0;

		cdata = urb->transfer_buffer + urb->iso_frame_desc[i].offset;

		aPackNum[i] = n ? cdata[ov511->packet_size - 1] : -1;

		if (!n || ov511->curframe == -1)
			continue;

		if (st)
			PDEBUG(2, "data error: [%d] len=%d, status=%d", i, n, st);

		frame = &ov511->frame[ov511->curframe];

		/* SOF/EOF packets have 1st to 8th bytes zeroed and the 9th
		 * byte non-zero. The EOF packet has image width/height in the
		 * 10th and 11th bytes. The 9th byte is given as follows:
		 *
		 * bit 7: EOF
		 *     6: compression enabled
		 *     5: 422/420/400 modes
		 *     4: 422/420/400 modes
		 *     3: 1
		 *     2: snapshot button on
		 *     1: snapshot frame
		 *     0: even/odd field
		 */

		if (printph) {
			info("packet header (%3d): %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x",
				cdata[ov511->packet_size - 1],
				cdata[0], cdata[1], cdata[2], cdata[3], cdata[4], cdata[5],
				cdata[6], cdata[7], cdata[8], cdata[9], cdata[10], cdata[11]);
		}

		/* Check for SOF/EOF packet */
		if ((cdata[0] | cdata[1] | cdata[2] | cdata[3] |
		     cdata[4] | cdata[5] | cdata[6] | cdata[7]) ||
		     (~cdata[8] & 0x08))
			goto check_middle;

		/* Frame end */
		if (cdata[8] & 0x80) {
			ts = (struct timeval *)(frame->data 
			      + MAX_FRAME_SIZE(ov511->maxwidth, ov511->maxheight));
			do_gettimeofday(ts);

			/* Get the actual frame size from the EOF header */
			frame->rawwidth = ((int)(cdata[9]) + 1) * 8;
			frame->rawheight = ((int)(cdata[10]) + 1) * 8;

	 		PDEBUG(4, "Frame end, curframe = %d, packnum=%d, hw=%d, vw=%d, recvd=%d",
				ov511->curframe,
				(int)(cdata[ov511->packet_size - 1]),
				frame->rawwidth,
				frame->rawheight,
				frame->bytes_recvd);

			/* Validate the header data */
			RESTRICT_TO_RANGE(frame->rawwidth, ov511->minwidth, ov511->maxwidth);
			RESTRICT_TO_RANGE(frame->rawheight, ov511->minheight, ov511->maxheight);

			/* Don't allow byte count to exceed buffer size */
			RESTRICT_TO_RANGE(frame->bytes_recvd,
					  8, 
					  MAX_RAW_DATA_SIZE(ov511->maxwidth,
					                    ov511->maxheight));

			if (frame->scanstate == STATE_LINES) {
		    		int iFrameNext;

				frame->grabstate = FRAME_DONE;	// FIXME: Is this right?

				if (waitqueue_active(&frame->wq)) {
					frame->grabstate = FRAME_DONE;
					wake_up_interruptible(&frame->wq);
				}

				/* If next frame is ready or grabbing,
                                 * point to it */
				iFrameNext = (ov511->curframe + 1) % OV511_NUMFRAMES;
				if (ov511->frame[iFrameNext].grabstate == FRAME_READY
				    || ov511->frame[iFrameNext].grabstate == FRAME_GRABBING) {
					ov511->curframe = iFrameNext;
					ov511->frame[iFrameNext].scanstate = STATE_SCANNING;
				} else {
					if (frame->grabstate == FRAME_DONE) {
						PDEBUG(4, "Frame done! congratulations");
					} else {
						PDEBUG(4, "Frame not ready? state = %d",
							ov511->frame[iFrameNext].grabstate);
					}

					ov511->curframe = -1;
				}
			} else {
				PDEBUG(5, "Frame done, but not scanning");
			}
			/* Image corruption caused by misplaced frame->segment = 0
			 * fixed by carlosf@conectiva.com.br
			 */
		} else {
			/* Frame start */
			PDEBUG(4, "Frame start, framenum = %d", ov511->curframe);

			/* Check to see if it's a snapshot frame */
			/* FIXME?? Should the snapshot reset go here? Performance? */
			if (cdata[8] & 0x02) {
				frame->snapshot = 1;
				PDEBUG(3, "snapshot detected");
			}

			frame->scanstate = STATE_LINES;
			frame->bytes_recvd = 0;
			frame->compressed = cdata[8] & 0x40;
		}

check_middle:
		/* Are we in a frame? */
		if (frame->scanstate != STATE_LINES) {
			PDEBUG(5, "Not in a frame; packet skipped");
			continue;
		}

#if 0
		/* Skip packet if first 9 bytes are zero. These are common, so
		 * we use a less expensive test here instead of later */
		if (frame->compressed) {
			int b, skip = 1;

			for (b = 0; b < 9; b++) { 
				if (cdata[b])
					skip=0;
			}

			if (skip) {
				PDEBUG(5, "Skipping packet (all zero)");
				continue;
			}
		}
#endif
		/* If frame start, skip header */
		if (frame->bytes_recvd == 0)
			offset = 9;
		else
			offset = 0;

		num = n - offset - 1;

		/* Dump all data exactly as received */
		if (dumppix == 2) {
			frame->bytes_recvd += n - 1;
			if (frame->bytes_recvd <= MAX_RAW_DATA_SIZE(ov511->maxwidth, ov511->maxheight))
				memmove(frame->rawdata + frame->bytes_recvd - (n - 1),
					&cdata[0], n - 1);
			else
				PDEBUG(3, "Raw data buffer overrun!! (%d)",
					frame->bytes_recvd
					- MAX_RAW_DATA_SIZE(ov511->maxwidth,
							    ov511->maxheight));
		} else if (!frame->compressed && !remove_zeros) {
			frame->bytes_recvd += num;
			if (frame->bytes_recvd <= MAX_RAW_DATA_SIZE(ov511->maxwidth, ov511->maxheight))
				memmove(frame->rawdata + frame->bytes_recvd - num,
					&cdata[offset], num);
			else
				PDEBUG(3, "Raw data buffer overrun!! (%d)",
					frame->bytes_recvd
					- MAX_RAW_DATA_SIZE(ov511->maxwidth,
							    ov511->maxheight));
		} else { /* Remove all-zero FIFO lines (aligned 32-byte blocks) */
			int b, in = 0, allzero, copied=0;
			if (offset) {
				frame->bytes_recvd += 32 - offset;	// Bytes out
				memmove(frame->rawdata,
					&cdata[offset], 32 - offset);
				in += 32;
			}

			while (in < n - 1) {
				allzero = 1;
				for (b = 0; b < 32; b++) {
					if (cdata[in + b]) {
						allzero = 0;
						break;
					}
				}

				if (allzero) {
					/* Don't copy it */
				} else {
					if (frame->bytes_recvd + copied + 32
					    <= MAX_RAW_DATA_SIZE(ov511->maxwidth, ov511->maxheight)) {
						memmove(frame->rawdata + frame->bytes_recvd + copied,
							&cdata[in], 32);
						copied += 32;
					} else {
						PDEBUG(3, "Raw data buffer overrun!!");
					}
				}
				in += 32;
			}

			frame->bytes_recvd += copied;
		}

	}

	PDEBUG(5, "pn: %d %d %d %d %d %d %d %d %d %d",
		aPackNum[0], aPackNum[1], aPackNum[2], aPackNum[3], aPackNum[4],
		aPackNum[5],aPackNum[6], aPackNum[7], aPackNum[8], aPackNum[9]);

	return totlen;
}

static int 
ov518_move_data(struct usb_ov511 *ov511, struct urb *urb)
{
	unsigned char *cdata;
	int i, data_size, totlen = 0;
	struct ov511_frame *frame;
	struct timeval *ts;

	PDEBUG(5, "Moving %d packets", urb->number_of_packets);

	/* OV518(+) has no packet numbering */
	data_size = ov511->packet_size;

	for (i = 0; i < urb->number_of_packets; i++) {
		int n = urb->iso_frame_desc[i].actual_length;
		int st = urb->iso_frame_desc[i].status;

		urb->iso_frame_desc[i].actual_length = 0;
		urb->iso_frame_desc[i].status = 0;

		cdata = urb->transfer_buffer + urb->iso_frame_desc[i].offset;

		if (!n) {
			PDEBUG(4, "Zero-length packet");
			continue;
		}

		if (ov511->curframe == -1) {
			PDEBUG(4, "No frame currently active");
			continue;
		}

		if (st)
			PDEBUG(2, "data error: [%d] len=%d, status=%d", i, n, st);

		frame = &ov511->frame[ov511->curframe];

#if 0
		{
			int d;
			/* Print all data */
			for (d = 0; d <= data_size - 16; d += 16) {
				info("%4x: %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x", d,
					cdata[d], cdata[d+1], cdata[d+2], cdata[d+3],
					cdata[d+4], cdata[d+5], cdata[d+6], cdata[d+7],
					cdata[d+8], cdata[d+9], cdata[d+10], cdata[d+11],
					cdata[d+12], cdata[d+13], cdata[d+14], cdata[d+15]);
			}
		}
#endif

		if (printph) {
			info("packet header: %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x",
				cdata[0], cdata[1], cdata[2], cdata[3], cdata[4], cdata[5],
				cdata[6], cdata[7], cdata[8], cdata[9], cdata[10], cdata[11]);
		}

		/* A false positive here is likely, until OVT gives me
		 * the definitive SOF/EOF format */
		if ((!(cdata[0] | cdata[1] | cdata[2] | cdata[3] |
		      cdata[5])) && cdata[6]) {
			
			if (frame->scanstate == STATE_LINES) {
				PDEBUG(4, "Detected frame end/start");
				goto eof;
			} else { //scanstate == STATE_SCANNING
				/* Frame start */
				PDEBUG(4, "Frame start, framenum = %d", ov511->curframe);
				goto sof;
			}
		} else {
			goto check_middle;
		}
	
eof:
		ts = (struct timeval *)(frame->data
		      + MAX_FRAME_SIZE(ov511->maxwidth, ov511->maxheight));
		do_gettimeofday(ts);

 		PDEBUG(4, "Frame end, curframe = %d, hw=%d, vw=%d, recvd=%d",
			ov511->curframe,
			(int)(cdata[9]), (int)(cdata[10]), frame->bytes_recvd);

		// FIXME: Since we don't know the header formats yet,
		// there is no way to know what the actual image size is
		frame->rawwidth = frame->width;
		frame->rawheight = frame->height;

		/* Validate the header data */
		RESTRICT_TO_RANGE(frame->rawwidth, ov511->minwidth, ov511->maxwidth);
		RESTRICT_TO_RANGE(frame->rawheight, ov511->minheight, ov511->maxheight);

		/* Don't allow byte count to exceed buffer size */
		RESTRICT_TO_RANGE(frame->bytes_recvd,
				  8, 
				  MAX_RAW_DATA_SIZE(ov511->maxwidth, ov511->maxheight));

		if (frame->scanstate == STATE_LINES) {
	    		int iFrameNext;

			frame->grabstate = FRAME_DONE;	// FIXME: Is this right?

			if (waitqueue_active(&frame->wq)) {
				frame->grabstate = FRAME_DONE;
				wake_up_interruptible(&frame->wq);
			}

			/* If next frame is ready or grabbing,
			 * point to it */
			iFrameNext = (ov511->curframe + 1) % OV511_NUMFRAMES;
			if (ov511->frame[iFrameNext].grabstate == FRAME_READY
			    || ov511->frame[iFrameNext].grabstate == FRAME_GRABBING) {
				ov511->curframe = iFrameNext;
				ov511->frame[iFrameNext].scanstate = STATE_SCANNING;
				frame = &ov511->frame[iFrameNext];
			} else {
				if (frame->grabstate == FRAME_DONE) {
					PDEBUG(4, "Frame done! congratulations");
				} else {
					PDEBUG(4, "Frame not ready? state = %d",
						ov511->frame[iFrameNext].grabstate);
				}

				ov511->curframe = -1;
				PDEBUG(4, "SOF dropped (no active frame)");
				continue;  /* Nowhere to store this frame */
			}
		}
		/* Image corruption caused by misplaced frame->segment = 0
		 * fixed by carlosf@conectiva.com.br
		 */
sof:
		PDEBUG(4, "Starting capture on frame %d", frame->framenum);
// Snapshot not reverse-engineered yet.
#if 0
		/* Check to see if it's a snapshot frame */
		/* FIXME?? Should the snapshot reset go here? Performance? */
		if (cdata[8] & 0x02) {
			frame->snapshot = 1;
			PDEBUG(3, "snapshot detected");
		}
#endif
		frame->scanstate = STATE_LINES;
		frame->bytes_recvd = 0;
//		frame->compressed = 1;

check_middle:
		/* Are we in a frame? */
		if (frame->scanstate != STATE_LINES) {
			PDEBUG(4, "scanstate: no SOF yet");
			continue;
		}

		/* Dump all data exactly as received */
		if (dumppix == 2) {
			frame->bytes_recvd += n;
			if (frame->bytes_recvd <= MAX_RAW_DATA_SIZE(ov511->maxwidth, ov511->maxheight))
				memmove(frame->rawdata + frame->bytes_recvd - n,
					&cdata[0], n);
			else
				PDEBUG(3, "Raw data buffer overrun!! (%d)",
					frame->bytes_recvd
					- MAX_RAW_DATA_SIZE(ov511->maxwidth,
							    ov511->maxheight));
		} else {
			/* All incoming data are divided into 8-byte segments. If the
			 * segment contains all zero bytes, it must be skipped. These
			 * zero-segments allow the OV518 to mainain a constant data rate
			 * regardless of the effectiveness of the compression. Segments
			 * are aligned relative to the beginning of each isochronous
			 * packet. The first segment is a header.
			 */

			int b, in = 0, allzero, copied=0;

// Decompressor expects the header
#if 0
			if (frame->bytes_recvd == 0)
				in += 8;  /* Skip header */
#endif

			while (in < n) {
				allzero = 1;
				for (b = 0; b < 8; b++) {
					if (cdata[in + b]) {
						allzero = 0;
						break;
					}
				}

				if (allzero) {
				/* Don't copy it */
				} else {
					if (frame->bytes_recvd + copied + 8
					    <= MAX_RAW_DATA_SIZE(ov511->maxwidth, ov511->maxheight)) {
						memmove(frame->rawdata + frame->bytes_recvd + copied,
							&cdata[in], 8);
						copied += 8;
					} else {
						PDEBUG(3, "Raw data buffer overrun!!");
					}
				}
				in += 8;
			}
			frame->bytes_recvd += copied;
		}
	}

	return totlen;
}

static void 
ov511_isoc_irq(struct urb *urb)
{
	int len;
	struct usb_ov511 *ov511;

	if (!urb->context) {
		PDEBUG(4, "no context");
		return;
	}

	ov511 = (struct usb_ov511 *) urb->context;

	if (!ov511->dev || !ov511->user) {
		PDEBUG(4, "no device, or not open");
		return;
	}

	if (!ov511->streaming) {
		PDEBUG(4, "hmmm... not streaming, but got interrupt");
		return;
	}

	/* Copy the data received into our frame buffer */
	if (ov511->curframe >= 0) {
		if (ov511->bridge == BRG_OV511 || 
		    ov511->bridge == BRG_OV511PLUS)
			len = ov511_move_data(ov511, urb);
		else if (ov511->bridge == BRG_OV518 ||
			 ov511->bridge == BRG_OV518PLUS)
			len = ov518_move_data(ov511, urb);
		else
			err("Unknown bridge device (%d)", ov511->bridge);
	} else if (waitqueue_active(&ov511->wq)) {
		wake_up_interruptible(&ov511->wq);
	}

	urb->dev = ov511->dev;

	return;
}

/****************************************************************************
 *
 * Stream initialization and termination
 *
 ***************************************************************************/

static int 
ov511_init_isoc(struct usb_ov511 *ov511)
{
	struct urb *urb;
	int fx, err, n, size;

	PDEBUG(3, "*** Initializing capture ***");

	ov511->curframe = -1;

	if (ov511->bridge == BRG_OV511) {
		if (cams == 1)				size = 993;
		else if (cams == 2)			size = 513;
		else if (cams == 3 || cams == 4)	size = 257;
		else {
			err("\"cams\" parameter too high!");
			return -1;
		}
	} else if (ov511->bridge == BRG_OV511PLUS) {
		if (cams == 1)				size = 961;
		else if (cams == 2)			size = 513;
		else if (cams == 3 || cams == 4)	size = 257;
		else if (cams >= 5 && cams <= 8)	size = 129;
		else if (cams >= 9 && cams <= 31)	size = 33;
		else {
			err("\"cams\" parameter too high!");
			return -1;
		}
	} else if (ov511->bridge == BRG_OV518 ||
		   ov511->bridge == BRG_OV518PLUS) {
		if (cams == 1)				size = 896;
		else if (cams == 2)			size = 512;
		else if (cams == 3 || cams == 4)	size = 256;
		else if (cams >= 5 && cams <= 8)	size = 128;
		else {
			err("\"cams\" parameter too high!");
			return -1;
		}
	} else {
		err("invalid bridge type");
		return -1;
	}

	if (packetsize == -1) {
		// FIXME: OV518 is hardcoded to 15 FPS (alternate 5) for now
		if (ov511->bridge == BRG_OV518 ||
		    ov511->bridge == BRG_OV518PLUS)
			ov511_set_packet_size(ov511, 640);
		else
			ov511_set_packet_size(ov511, size);
	} else {
			info("Forcing packet size to %d", packetsize);
			ov511_set_packet_size(ov511, packetsize);
	}

	for (n = 0; n < OV511_NUMSBUF; n++) {
		urb = usb_alloc_urb(FRAMES_PER_DESC);
	
		if (!urb) {
			err("init isoc: usb_alloc_urb ret. NULL");
			return -ENOMEM;
		}
		ov511->sbuf[n].urb = urb;
		urb->dev = ov511->dev;
		urb->context = ov511;
		urb->pipe = usb_rcvisocpipe(ov511->dev, OV511_ENDPOINT_ADDRESS);
		urb->transfer_flags = USB_ISO_ASAP;
		urb->transfer_buffer = ov511->sbuf[n].data;
		urb->complete = ov511_isoc_irq;
		urb->number_of_packets = FRAMES_PER_DESC;
		urb->transfer_buffer_length =
		 ov511->packet_size * FRAMES_PER_DESC;
		for (fx = 0; fx < FRAMES_PER_DESC; fx++) {
			urb->iso_frame_desc[fx].offset = 
			 ov511->packet_size * fx;
			urb->iso_frame_desc[fx].length = ov511->packet_size;
		}
	}

	ov511->streaming = 1;

	ov511->sbuf[OV511_NUMSBUF - 1].urb->next = ov511->sbuf[0].urb;
	for (n = 0; n < OV511_NUMSBUF - 1; n++)
		ov511->sbuf[n].urb->next = ov511->sbuf[n+1].urb;

	for (n = 0; n < OV511_NUMSBUF; n++) {
		ov511->sbuf[n].urb->dev = ov511->dev;
		err = usb_submit_urb(ov511->sbuf[n].urb, GFP_KERNEL);
		if (err)
			err("init isoc: usb_submit_urb(%d) ret %d", n, err);
	}

	return 0;
}

static void 
ov511_stop_isoc(struct usb_ov511 *ov511)
{
	int n;

	if (!ov511->streaming || !ov511->dev)
		return;

	PDEBUG(3, "*** Stopping capture ***");

	ov511_set_packet_size(ov511, 0);

	ov511->streaming = 0;

	/* Unschedule all of the iso td's */
	for (n = OV511_NUMSBUF - 1; n >= 0; n--) {
		if (ov511->sbuf[n].urb) {
			ov511->sbuf[n].urb->next = NULL;
			usb_unlink_urb(ov511->sbuf[n].urb);
			usb_free_urb(ov511->sbuf[n].urb);
			ov511->sbuf[n].urb = NULL;
		}
	}
}

static int 
ov511_new_frame(struct usb_ov511 *ov511, int framenum)
{
	struct ov511_frame *frame;
	int newnum;

	PDEBUG(4, "ov511->curframe = %d, framenum = %d", ov511->curframe,
		framenum);
	if (!ov511->dev)
		return -1;

	/* If we're not grabbing a frame right now and the other frame is */
	/* ready to be grabbed into, then use it instead */
	if (ov511->curframe == -1) {
		newnum = (framenum - 1 + OV511_NUMFRAMES) % OV511_NUMFRAMES;
		if (ov511->frame[newnum].grabstate == FRAME_READY)
			framenum = newnum;
	} else
		return 0;

	frame = &ov511->frame[framenum];

	PDEBUG(4, "framenum = %d, width = %d, height = %d", framenum, 
	       frame->width, frame->height);

	frame->grabstate = FRAME_GRABBING;
	frame->scanstate = STATE_SCANNING;
	frame->snapshot = 0;

	ov511->curframe = framenum;

	/* Make sure it's not too big */
	if (frame->width > ov511->maxwidth)
		frame->width = ov511->maxwidth;

	frame->width &= ~7L;		/* Multiple of 8 */

	if (frame->height > ov511->maxheight)
		frame->height = ov511->maxheight;

	frame->height &= ~3L;		/* Multiple of 4 */

	return 0;
}

/****************************************************************************
 *
 * Buffer management
 *
 ***************************************************************************/
static int 
ov511_alloc(struct usb_ov511 *ov511)
{
	int i;
	int w = ov511->maxwidth;
	int h = ov511->maxheight;

	PDEBUG(4, "entered");
	down(&ov511->buf_lock);

	if (ov511->buf_state == BUF_PEND_DEALLOC) {
		ov511->buf_state = BUF_ALLOCATED;
		del_timer(&ov511->buf_timer);
	}

	if (ov511->buf_state == BUF_ALLOCATED)
		goto out;

	ov511->fbuf = rvmalloc(OV511_NUMFRAMES * MAX_DATA_SIZE(w, h));
	if (!ov511->fbuf)
		goto error;

	ov511->rawfbuf = vmalloc(OV511_NUMFRAMES * MAX_RAW_DATA_SIZE(w, h));
	if (!ov511->rawfbuf) {
		rvfree(ov511->fbuf, OV511_NUMFRAMES * MAX_DATA_SIZE(w, h));
		ov511->fbuf = NULL;
		goto error;
	}
	memset(ov511->rawfbuf, 0, OV511_NUMFRAMES * MAX_RAW_DATA_SIZE(w, h));

	ov511->tempfbuf = vmalloc(OV511_NUMFRAMES * MAX_RAW_DATA_SIZE(w, h));
	if (!ov511->tempfbuf) {
		vfree(ov511->rawfbuf);
		ov511->rawfbuf = NULL;
		rvfree(ov511->fbuf, OV511_NUMFRAMES * MAX_DATA_SIZE(w, h));
		ov511->fbuf = NULL;
		goto error;
	}
	memset(ov511->tempfbuf, 0, OV511_NUMFRAMES * MAX_RAW_DATA_SIZE(w, h));

	for (i = 0; i < OV511_NUMSBUF; i++) {
		ov511->sbuf[i].data = kmalloc(FRAMES_PER_DESC *
			MAX_FRAME_SIZE_PER_DESC, GFP_KERNEL);
		if (!ov511->sbuf[i].data) {
			while (--i) {
				kfree(ov511->sbuf[i].data);
				ov511->sbuf[i].data = NULL;
			}
			vfree(ov511->tempfbuf);
			ov511->tempfbuf = NULL;
			vfree(ov511->rawfbuf);
			ov511->rawfbuf = NULL;
			rvfree(ov511->fbuf,
			       OV511_NUMFRAMES * MAX_DATA_SIZE(w, h));
			ov511->fbuf = NULL;

			goto error;
		}
		PDEBUG(4, "sbuf[%d] @ %p", i, ov511->sbuf[i].data);
	}

	for (i = 0; i < OV511_NUMFRAMES; i++) {
		ov511->frame[i].data = ov511->fbuf + i * MAX_DATA_SIZE(w, h);
		ov511->frame[i].rawdata = ov511->rawfbuf 
		 + i * MAX_RAW_DATA_SIZE(w, h);
		ov511->frame[i].tempdata = ov511->tempfbuf 
		 + i * MAX_RAW_DATA_SIZE(w, h);
		PDEBUG(4, "frame[%d] @ %p", i, ov511->frame[i].data);
	}

	ov511->buf_state = BUF_ALLOCATED;
out:
	up(&ov511->buf_lock);
	PDEBUG(4, "leaving");
	return 0;
error:
	ov511->buf_state = BUF_NOT_ALLOCATED;
	up(&ov511->buf_lock);
	PDEBUG(4, "errored");
	return -ENOMEM;
}

/* 
 * - You must acquire buf_lock before entering this function.
 * - Because this code will free any non-null pointer, you must be sure to null
 *   them if you explicitly free them somewhere else!
 */
static void 
ov511_do_dealloc(struct usb_ov511 *ov511)
{
	int i;
	PDEBUG(4, "entered");

	if (ov511->fbuf) {
		rvfree(ov511->fbuf, OV511_NUMFRAMES
		       * MAX_DATA_SIZE(ov511->maxwidth, ov511->maxheight));
		ov511->fbuf = NULL;
	}

	if (ov511->rawfbuf) {
		vfree(ov511->rawfbuf);
		ov511->rawfbuf = NULL;
	}

	if (ov511->tempfbuf) {
		vfree(ov511->tempfbuf);
		ov511->tempfbuf = NULL;
	}

	for (i = 0; i < OV511_NUMSBUF; i++) {
		if (ov511->sbuf[i].data) {
			kfree(ov511->sbuf[i].data);
			ov511->sbuf[i].data = NULL;
		}
	}

	for (i = 0; i < OV511_NUMFRAMES; i++) {
		ov511->frame[i].data = NULL;
		ov511->frame[i].rawdata = NULL;
		ov511->frame[i].tempdata = NULL;
	}

	PDEBUG(4, "buffer memory deallocated");
	ov511->buf_state = BUF_NOT_ALLOCATED;
	PDEBUG(4, "leaving");
}

static void 
ov511_buf_callback(unsigned long data)
{
	struct usb_ov511 *ov511 = (struct usb_ov511 *)data;
	PDEBUG(4, "entered");
	down(&ov511->buf_lock);

	if (ov511->buf_state == BUF_PEND_DEALLOC)
		ov511_do_dealloc(ov511);

	up(&ov511->buf_lock);
	PDEBUG(4, "leaving");
}

static void 
ov511_dealloc(struct usb_ov511 *ov511, int now)
{
	struct timer_list *bt = &(ov511->buf_timer);
	PDEBUG(4, "entered");
	down(&ov511->buf_lock);

	PDEBUG(4, "deallocating buffer memory %s", now ? "now" : "later");

	if (ov511->buf_state == BUF_PEND_DEALLOC) {
		ov511->buf_state = BUF_ALLOCATED;
		del_timer(bt);
	}

	if (now)
		ov511_do_dealloc(ov511);
	else {
		ov511->buf_state = BUF_PEND_DEALLOC;
		init_timer(bt);
		bt->function = ov511_buf_callback;
		bt->data = (unsigned long)ov511;
		bt->expires = jiffies + buf_timeout * HZ;
		add_timer(bt);
	}
	up(&ov511->buf_lock);
	PDEBUG(4, "leaving");
}

/****************************************************************************
 *
 * V4L API
 *
 ***************************************************************************/

static int 
ov511_open(struct video_device *vdev, int flags)
{
	struct usb_ov511 *ov511 = vdev->priv;
	int err, i;

	PDEBUG(4, "opening");

	down(&ov511->lock);

	err = -EBUSY;
	if (ov511->user) 
		goto out;

	err = -ENOMEM;
	if (ov511_alloc(ov511))
		goto out;

	ov511->sub_flag = 0;

	/* In case app doesn't set them... */
	if (ov51x_set_default_params(ov511) < 0)
		goto out;

	/* Make sure frames are reset */
	for (i = 0; i < OV511_NUMFRAMES; i++) {
		ov511->frame[i].grabstate = FRAME_UNUSED;
		ov511->frame[i].bytes_read = 0;
	}

	/* If compression is on, make sure now that a 
	 * decompressor can be loaded */
	if (ov511->compress && !ov511->decomp_ops) {
		err = ov51x_request_decompressor(ov511);
		if (err)
			goto out;
	}

	err = ov511_init_isoc(ov511);
	if (err) {
		ov511_dealloc(ov511, 0);
		goto out;
	}

	ov511->user++;
	
	if (ov511->led_policy == LED_AUTO)
		ov51x_led_control(ov511, 1);

out:
	up(&ov511->lock);

	return err;
}

static void 
ov511_close(struct video_device *dev)
{
	struct usb_ov511 *ov511 = (struct usb_ov511 *)dev;

	PDEBUG(4, "ov511_close");
	
	down(&ov511->lock);

	ov511->user--;
	ov511_stop_isoc(ov511);

	ov51x_release_decompressor(ov511);

	if (ov511->led_policy == LED_AUTO)
		ov51x_led_control(ov511, 0);

	if (ov511->dev)
		ov511_dealloc(ov511, 0);

	up(&ov511->lock);

	/* Device unplugged while open. Only a minimum of unregistration is done
	 * here; the disconnect callback already did the rest. */
	if (!ov511->dev) {
		ov511_dealloc(ov511, 1);
		video_unregister_device(&ov511->vdev);
		kfree(ov511);
		ov511 = NULL;
	}
}

static int 
ov511_init_done(struct video_device *vdev)
{
#if defined(CONFIG_PROC_FS) && defined(CONFIG_VIDEO_PROC_FS)
	create_proc_ov511_cam((struct usb_ov511 *)vdev);
#endif

	return 0;
}

static long 
ov511_write(struct video_device *vdev, const char *buf,
	    unsigned long count, int noblock)
{
	return -EINVAL;
}

/* Do not call this function directly! */
static int 
ov511_ioctl_internal(struct video_device *vdev, unsigned int cmd, void *arg)
{
	struct usb_ov511 *ov511 = (struct usb_ov511 *)vdev;

	PDEBUG(5, "IOCtl: 0x%X", cmd);

	if (!ov511->dev)
		return -EIO;	

	switch (cmd) {
	case VIDIOCGCAP:
	{
		struct video_capability b;

		PDEBUG(4, "VIDIOCGCAP");

		memset(&b, 0, sizeof(b));
		sprintf(b.name, "%s USB Camera",
			ov511->bridge == BRG_OV511 ? "OV511" :
			ov511->bridge == BRG_OV511PLUS ? "OV511+" :
			ov511->bridge == BRG_OV518 ? "OV518" :
			ov511->bridge == BRG_OV518PLUS ? "OV518+" :
			"unknown");
		b.type = VID_TYPE_CAPTURE | VID_TYPE_SUBCAPTURE;
		if (ov511->has_tuner)
			b.type |= VID_TYPE_TUNER;
		b.channels = ov511->num_inputs;
		b.audios = ov511->has_audio_proc ? 1:0;
		b.maxwidth = ov511->maxwidth;
		b.maxheight = ov511->maxheight;
		b.minwidth = ov511->minwidth;
		b.minheight = ov511->minheight;

		if (copy_to_user(arg, &b, sizeof(b)))
			return -EFAULT;
				
		return 0;
	}
	case VIDIOCGCHAN:
	{
		struct video_channel v;

		PDEBUG(4, "VIDIOCGCHAN");

		if (copy_from_user(&v, arg, sizeof(v)))
			return -EFAULT;

		if ((unsigned)(v.channel) >= ov511->num_inputs) {
			err("Invalid channel (%d)", v.channel);
			return -EINVAL;
		}

		v.norm = ov511->norm;
		v.type = (ov511->has_tuner) ? VIDEO_TYPE_TV : VIDEO_TYPE_CAMERA;
		v.flags = (ov511->has_tuner) ? VIDEO_VC_TUNER : 0;
		v.flags |= (ov511->has_audio_proc) ? VIDEO_VC_AUDIO : 0;
//		v.flags |= (ov511->has_decoder) ? VIDEO_VC_NORM : 0;
		v.tuners = (ov511->has_tuner) ? 1:0;
		decoder_get_input_name(ov511, v.channel, v.name);

		if (copy_to_user(arg, &v, sizeof(v)))
			return -EFAULT;
				
		return 0;
	}
	case VIDIOCSCHAN:
	{
		struct video_channel v;
		int err;

		PDEBUG(4, "VIDIOCSCHAN");

		if (copy_from_user(&v, arg, sizeof(v)))
			return -EFAULT;

		/* Make sure it's not a camera */
		if (!ov511->has_decoder) {
			if (v.channel == 0)
				return 0;
			else
				return -EINVAL;
		}

		if (v.norm != VIDEO_MODE_PAL &&
		    v.norm != VIDEO_MODE_NTSC &&
		    v.norm != VIDEO_MODE_SECAM &&
		    v.norm != VIDEO_MODE_AUTO) {
			err("Invalid norm (%d)", v.norm);
			return -EINVAL;
		}

		if ((unsigned)(v.channel) >= ov511->num_inputs) {
			err("Invalid channel (%d)", v.channel);
			return -EINVAL;
		}

		err = decoder_set_input(ov511, v.channel);
		if (err)
			return err;

		err = decoder_set_norm(ov511, v.norm);
		if (err)
			return err;

		return 0;
	}
	case VIDIOCGPICT:
	{
		struct video_picture p;

		PDEBUG(4, "VIDIOCGPICT");

		memset(&p, 0, sizeof(p));

		if (sensor_get_picture(ov511, &p))
			return -EIO;

		if (copy_to_user(arg, &p, sizeof(p)))
			return -EFAULT;

		return 0;
	}
	case VIDIOCSPICT:
	{
		struct video_picture p;
		int i;

		PDEBUG(4, "VIDIOCSPICT");

		if (copy_from_user(&p, arg, sizeof(p)))
			return -EFAULT;

		if (!ov511_get_depth(p.palette))
			return -EINVAL;

		if (sensor_set_picture(ov511, &p))
			return -EIO;

		if (force_palette && p.palette != force_palette) {
			info("Palette rejected (%d)", p.palette);
			return -EINVAL;
		}

		// FIXME: Format should be independent of frames
		if (p.palette != ov511->frame[0].format) {
			PDEBUG(4, "Detected format change");

			/* If we're collecting previous frame wait
			   before changing modes */
			interruptible_sleep_on(&ov511->wq);
			if (signal_pending(current)) return -EINTR;

			mode_init_regs(ov511, ov511->frame[0].width,
				ov511->frame[0].height,	p.palette,
				ov511->sub_flag);
		}

		PDEBUG(4, "Setting depth=%d, palette=%d", p.depth, p.palette);
		for (i = 0; i < OV511_NUMFRAMES; i++) {
			ov511->frame[i].depth = p.depth;
			ov511->frame[i].format = p.palette;
		}

		return 0;
	}
	case VIDIOCGCAPTURE:
	{
		int vf;

		PDEBUG(4, "VIDIOCGCAPTURE");

		if (copy_from_user(&vf, arg, sizeof(vf)))
			return -EFAULT;
		ov511->sub_flag = vf;
		return 0;
	}
	case VIDIOCSCAPTURE:
	{
		struct video_capture vc;

		PDEBUG(4, "VIDIOCSCAPTURE");

		if (copy_from_user(&vc, arg, sizeof(vc)))
			return -EFAULT;
		if (vc.flags)
			return -EINVAL;
		if (vc.decimation)
			return -EINVAL;

		vc.x &= ~3L;
		vc.y &= ~1L;
		vc.y &= ~31L;

		if (vc.width == 0)
			vc.width = 32;

		vc.height /= 16;
		vc.height *= 16;
		if (vc.height == 0)
			vc.height = 16;

		ov511->subx = vc.x;
		ov511->suby = vc.y;
		ov511->subw = vc.width;
		ov511->subh = vc.height;

		return 0;
	}
	case VIDIOCSWIN:
	{
		struct video_window vw;
		int i, result;

		if (copy_from_user(&vw, arg, sizeof(vw)))
			return -EFAULT;

		PDEBUG(4, "VIDIOCSWIN: width=%d, height=%d",
			vw.width, vw.height);

#if 0
		if (vw.flags)
			return -EINVAL;
		if (vw.clipcount)
			return -EINVAL;
		if (vw.height != ov511->maxheight)
			return -EINVAL;
		if (vw.width != ov511->maxwidth)
			return -EINVAL;
#endif

		/* If we're collecting previous frame wait
		   before changing modes */
		interruptible_sleep_on(&ov511->wq);
		if (signal_pending(current)) return -EINTR;

		result = mode_init_regs(ov511, vw.width, vw.height,
			ov511->frame[0].format, ov511->sub_flag);
		if (result < 0)
			return result;

		for (i = 0; i < OV511_NUMFRAMES; i++) {
			ov511->frame[i].width = vw.width;
			ov511->frame[i].height = vw.height;
		}

		return 0;
	}
	case VIDIOCGWIN:
	{
		struct video_window vw;

		memset(&vw, 0, sizeof(vw));
		vw.x = 0;		/* FIXME */
		vw.y = 0;
		vw.width = ov511->frame[0].width;
		vw.height = ov511->frame[0].height;
		vw.flags = 30;

		PDEBUG(4, "VIDIOCGWIN: %dx%d", vw.width, vw.height);

		if (copy_to_user(arg, &vw, sizeof(vw)))
			return -EFAULT;

		return 0;
	}
	case VIDIOCGMBUF:
	{
		struct video_mbuf vm;
		int i;

		PDEBUG(4, "VIDIOCGMBUF");

		memset(&vm, 0, sizeof(vm));
		vm.size = OV511_NUMFRAMES
			* MAX_DATA_SIZE(ov511->maxwidth, ov511->maxheight);
		vm.frames = OV511_NUMFRAMES;

		vm.offsets[0] = 0;
		for (i = 1; i < OV511_NUMFRAMES; i++) {
			vm.offsets[i] = vm.offsets[i-1]
			   + MAX_DATA_SIZE(ov511->maxwidth, ov511->maxheight);
		}

		if (copy_to_user((void *)arg, (void *)&vm, sizeof(vm)))
			return -EFAULT;

		return 0;
	}
	case VIDIOCMCAPTURE:
	{
		struct video_mmap vm;
		int ret, depth;

		if (copy_from_user((void *)&vm, (void *)arg, sizeof(vm)))
			return -EFAULT;

		PDEBUG(4, "CMCAPTURE");
		PDEBUG(4, "frame: %d, size: %dx%d, format: %d",
			vm.frame, vm.width, vm.height, vm.format);

		depth = ov511_get_depth(vm.format);
		if (!depth) {
			err("VIDIOCMCAPTURE: invalid format (%d)", vm.format);
			return -EINVAL;
		}

		if ((unsigned)vm.frame >= OV511_NUMFRAMES) {
			err("VIDIOCMCAPTURE: invalid frame (%d)", vm.frame);
			return -EINVAL;
		}

		if (vm.width > ov511->maxwidth 
		    || vm.height > ov511->maxheight) {
			err("VIDIOCMCAPTURE: requested dimensions too big");
			return -EINVAL;
		}

		if (ov511->frame[vm.frame].grabstate == FRAME_GRABBING) {
			PDEBUG(4, "VIDIOCMCAPTURE: already grabbing");
			return -EBUSY;
		}

		if (force_palette && vm.format != force_palette) {
			info("palette rejected (%d)", vm.format);
			return -EINVAL;
		}

		if ((ov511->frame[vm.frame].width != vm.width) ||
		    (ov511->frame[vm.frame].height != vm.height) ||
		    (ov511->frame[vm.frame].format != vm.format) ||
		    (ov511->frame[vm.frame].sub_flag != ov511->sub_flag) ||
		    (ov511->frame[vm.frame].depth != depth)) {
			PDEBUG(4, "VIDIOCMCAPTURE: change in image parameters");

			/* If we're collecting previous frame wait
			   before changing modes */
			interruptible_sleep_on(&ov511->wq);
			if (signal_pending(current)) return -EINTR;
			ret = mode_init_regs(ov511, vm.width, vm.height,
				vm.format, ov511->sub_flag);
#if 0
			if (ret < 0) {
				PDEBUG(1, "Got error while initializing regs ");
				return ret;
			}
#endif
			ov511->frame[vm.frame].width = vm.width;
			ov511->frame[vm.frame].height = vm.height;
			ov511->frame[vm.frame].format = vm.format;
			ov511->frame[vm.frame].sub_flag = ov511->sub_flag;
			ov511->frame[vm.frame].depth = depth;
		}

		/* Mark it as ready */
		ov511->frame[vm.frame].grabstate = FRAME_READY;

		PDEBUG(4, "VIDIOCMCAPTURE: renewing frame %d", vm.frame);

		return ov511_new_frame(ov511, vm.frame);
	}
	case VIDIOCSYNC:
	{
		int fnum, rc;
		struct ov511_frame *frame;

		if (copy_from_user((void *)&fnum, arg, sizeof(int)))
			return -EFAULT;

		if ((unsigned)fnum >= OV511_NUMFRAMES) {
			err("VIDIOCSYNC: invalid frame (%d)", fnum);
			return -EINVAL;
		}

		frame = &ov511->frame[fnum];

		PDEBUG(4, "syncing to frame %d, grabstate = %d", fnum,
		       frame->grabstate);

		switch (frame->grabstate) {
		case FRAME_UNUSED:
			return -EINVAL;
		case FRAME_READY:
		case FRAME_GRABBING:
		case FRAME_ERROR:
redo:
			if (!ov511->dev)
				return -EIO;

			rc = wait_event_interruptible(frame->wq,
			    (frame->grabstate == FRAME_DONE)
			    || (frame->grabstate == FRAME_ERROR));

			if (rc)
				return rc;

			if (frame->grabstate == FRAME_ERROR) {
				int ret;

				if ((ret = ov511_new_frame(ov511, fnum)) < 0)
					return ret;
				goto redo;
			}
			/* Fall through */			
		case FRAME_DONE:
			if (ov511->snap_enabled && !frame->snapshot) {
				int ret;
				if ((ret = ov511_new_frame(ov511, fnum)) < 0)
					return ret;
				goto redo;
			}

			frame->grabstate = FRAME_UNUSED;

			/* Reset the hardware snapshot button */
			/* FIXME - Is this the best place for this? */
			if ((ov511->snap_enabled) && (frame->snapshot)) {
				frame->snapshot = 0;
				ov51x_clear_snapshot(ov511);
			}

			/* Decompression, format conversion, etc... */
			ov511_postprocess(ov511, frame);

			break;
		} /* end switch */

		return 0;
	}
	case VIDIOCGFBUF:
	{
		struct video_buffer vb;

		PDEBUG(4, "VIDIOCSCHAN");

		memset(&vb, 0, sizeof(vb));
		vb.base = NULL;	/* frame buffer not supported, not used */

		if (copy_to_user((void *)arg, (void *)&vb, sizeof(vb)))
			return -EFAULT;

		return 0;
	}
	case VIDIOCGUNIT:
	{
		struct video_unit vu;

		PDEBUG(4, "VIDIOCGUNIT");

		memset(&vu, 0, sizeof(vu));

		vu.video = ov511->vdev.minor;	/* Video minor */
		vu.vbi = VIDEO_NO_UNIT;		/* VBI minor */
		vu.radio = VIDEO_NO_UNIT;	/* Radio minor */
		vu.audio = VIDEO_NO_UNIT;	/* Audio minor */
		vu.teletext = VIDEO_NO_UNIT;	/* Teletext minor */

		if (copy_to_user((void *)arg, (void *)&vu, sizeof(vu)))
			return -EFAULT;

		return 0;
	}
	case VIDIOCGTUNER:
	{
		struct video_tuner v;

		PDEBUG(4, "VIDIOCGTUNER");

		if (copy_from_user(&v, arg, sizeof(v)))
			return -EFAULT;

		if (!ov511->has_tuner || v.tuner)	// Only tuner 0
			return -EINVAL;

		strcpy(v.name, "Television");

		// FIXME: Need a way to get the real values
		v.rangelow = 0;
		v.rangehigh = ~0;

		v.flags = VIDEO_TUNER_PAL | VIDEO_TUNER_NTSC |
		    VIDEO_TUNER_SECAM;
		v.mode = 0; 		/* FIXME:  Not sure what this is yet */
		v.signal = 0xFFFF;	/* unknown */

		call_i2c_clients(ov511, cmd, &v);

		if (copy_to_user(arg, &v, sizeof(v)))
			return -EFAULT;

		return 0;
	}
	case VIDIOCSTUNER:
	{
		struct video_tuner v;
		int err;

		PDEBUG(4, "VIDIOCSTUNER");

		if (copy_from_user(&v, arg, sizeof(v)))
			return -EFAULT;

		/* Only no or one tuner for now */
		if (!ov511->has_tuner || v.tuner)
			return -EINVAL;

		/* and it only has certain valid modes */
		if (v.mode != VIDEO_MODE_PAL &&
		    v.mode != VIDEO_MODE_NTSC &&
		    v.mode != VIDEO_MODE_SECAM) return -EOPNOTSUPP;

		/* Is this right/necessary? */
		err = decoder_set_norm(ov511, v.mode);
		if (err)
			return err;

		call_i2c_clients(ov511, cmd, &v);

		return 0;
	}
	case VIDIOCGFREQ:
	{
		unsigned long v = ov511->freq;

		PDEBUG(4, "VIDIOCGFREQ");

		if (!ov511->has_tuner)
			return -EINVAL;
#if 0
		/* FIXME: this is necessary for testing */
		v = 46*16;
#endif
		if (copy_to_user(arg, &v, sizeof(v)))
			return -EFAULT;

		return 0;
	}
	case VIDIOCSFREQ:
	{
		unsigned long v;

		if (!ov511->has_tuner)
			return -EINVAL;

		if (copy_from_user(&v, arg, sizeof(v)))
			return -EFAULT;

		PDEBUG(4, "VIDIOCSFREQ: %lx", v);

		ov511->freq = v;
		call_i2c_clients(ov511, cmd, &v);

		return 0;
	}
	case VIDIOCGAUDIO:
	case VIDIOCSAUDIO:
	{
		/* FIXME: Implement this... */
		return 0;
	}
	default:
		PDEBUG(3, "Unsupported IOCtl: 0x%X", cmd);
		return -ENOIOCTLCMD;
	} /* end switch */

	return 0;
}

static int 
ov511_ioctl(struct video_device *vdev, unsigned int cmd, void *arg)
{
	int rc;
	struct usb_ov511 *ov511 = vdev->priv;

	if (down_interruptible(&ov511->lock))
		return -EINTR;

	rc = ov511_ioctl_internal(vdev, cmd, arg);

	up(&ov511->lock);
	return rc;
}

static inline long 
ov511_read(struct video_device *vdev, char *buf, unsigned long count,
	   int noblock)
{
	struct usb_ov511 *ov511 = vdev->priv;
	int i, rc = 0, frmx = -1;
	struct ov511_frame *frame;

	if (down_interruptible(&ov511->lock))
		return -EINTR;

	PDEBUG(4, "%ld bytes, noblock=%d", count, noblock);

	if (!vdev || !buf) {
		rc = -EFAULT;
		goto error;
	}

	if (!ov511->dev) {
		rc = -EIO;
		goto error;
	}

// FIXME: Only supports two frames
	/* See if a frame is completed, then use it. */
	if (ov511->frame[0].grabstate >= FRAME_DONE)	/* _DONE or _ERROR */
		frmx = 0;
	else if (ov511->frame[1].grabstate >= FRAME_DONE)/* _DONE or _ERROR */
		frmx = 1;

	/* If nonblocking we return immediately */
	if (noblock && (frmx == -1)) {
		rc = -EAGAIN;
		goto error;
	}

	/* If no FRAME_DONE, look for a FRAME_GRABBING state. */
	/* See if a frame is in process (grabbing), then use it. */
	if (frmx == -1) {
		if (ov511->frame[0].grabstate == FRAME_GRABBING)
			frmx = 0;
		else if (ov511->frame[1].grabstate == FRAME_GRABBING)
			frmx = 1;
	}

	/* If no frame is active, start one. */
	if (frmx == -1) {
		if ((rc = ov511_new_frame(ov511, frmx = 0))) {
			err("read: ov511_new_frame error");
			goto error;
		}
	}

	frame = &ov511->frame[frmx];

restart:
	if (!ov511->dev) {
		rc = -EIO;
		goto error;
	}

	/* Wait while we're grabbing the image */
	PDEBUG(4, "Waiting image grabbing");
	rc = wait_event_interruptible(frame->wq, 
		(frame->grabstate == FRAME_DONE)
		|| (frame->grabstate == FRAME_ERROR));

	if (rc)
		goto error;

	PDEBUG(4, "Got image, frame->grabstate = %d", frame->grabstate);
	PDEBUG(4, "bytes_recvd = %d", frame->bytes_recvd);

	if (frame->grabstate == FRAME_ERROR) {
		frame->bytes_read = 0;
		err("** ick! ** Errored frame %d", ov511->curframe);
		if (ov511_new_frame(ov511, frmx)) {
			err("read: ov511_new_frame error");
			goto error;
		}
		goto restart;
	}


	/* Repeat until we get a snapshot frame */
	if (ov511->snap_enabled)
		PDEBUG(4, "Waiting snapshot frame");
	if (ov511->snap_enabled && !frame->snapshot) {
		frame->bytes_read = 0;
		if ((rc = ov511_new_frame(ov511, frmx))) {
			err("read: ov511_new_frame error");
			goto error;
		}
		goto restart;
	}

	/* Clear the snapshot */
	if (ov511->snap_enabled && frame->snapshot) {
		frame->snapshot = 0;
		ov51x_clear_snapshot(ov511);
	}

	/* Decompression, format conversion, etc... */
	ov511_postprocess(ov511, frame);

	PDEBUG(4, "frmx=%d, bytes_read=%ld, length=%ld", frmx,
		frame->bytes_read,
		get_frame_length(frame));

	/* copy bytes to user space; we allow for partials reads */
//	if ((count + frame->bytes_read) 
//	    > get_frame_length((struct ov511_frame *)frame))
//		count = frame->scanlength - frame->bytes_read;

	/* FIXME - count hardwired to be one frame... */
	count = get_frame_length(frame);

	PDEBUG(4, "Copy to user space: %ld bytes", count);
	if ((i = copy_to_user(buf, frame->data + frame->bytes_read, count))) {
		PDEBUG(4, "Copy failed! %d bytes not copied", i);
		rc = -EFAULT;
		goto error;
	}

	frame->bytes_read += count;
	PDEBUG(4, "{copy} count used=%ld, new bytes_read=%ld",
		count, frame->bytes_read);

	/* If all data has been read... */
	if (frame->bytes_read
	    >= get_frame_length(frame)) {
		frame->bytes_read = 0;

// FIXME: Only supports two frames
		/* Mark it as available to be used again. */
		ov511->frame[frmx].grabstate = FRAME_UNUSED;
		if ((rc = ov511_new_frame(ov511, !frmx))) {
			err("ov511_new_frame returned error");
			goto error;
		}
	}

	PDEBUG(4, "read finished, returning %ld (sweet)", count);

	up(&ov511->lock);
	return count;

error:
	up(&ov511->lock);
	return rc;
}

static int 
ov511_mmap(struct vm_area_struct *vma, struct video_device *vdev, const char *adr, unsigned long size)
{
	struct usb_ov511 *ov511 = vdev->priv;
	unsigned long start = (unsigned long)adr;
	unsigned long page, pos;

	if (ov511->dev == NULL)
		return -EIO;

	PDEBUG(4, "mmap: %ld (%lX) bytes", size, size);

	if (size > (((OV511_NUMFRAMES
	              * MAX_DATA_SIZE(ov511->maxwidth, ov511->maxheight)
	              + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))))
		return -EINVAL;

	if (down_interruptible(&ov511->lock))
		return -EINTR;

	pos = (unsigned long)ov511->fbuf;
	while (size > 0) {
		page = kvirt_to_pa(pos);
		if (remap_page_range(vma, start, page, PAGE_SIZE, PAGE_SHARED)) {
			up(&ov511->lock);
			return -EAGAIN;
		}
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	up(&ov511->lock);
	return 0;
}

static struct video_device ov511_template = {
	owner:		THIS_MODULE,
	name:		"OV511 USB Camera",
	type:		VID_TYPE_CAPTURE,
	hardware:	VID_HARDWARE_OV511,
	open:		ov511_open,
	close:		ov511_close,
	read:		ov511_read,
	write:		ov511_write,
	ioctl:		ov511_ioctl,
	mmap:		ov511_mmap,
	initialize:	ov511_init_done,
};

#if defined(CONFIG_PROC_FS) && defined(CONFIG_VIDEO_PROC_FS)
static int 
ov511_control_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		    unsigned long ularg)
{
	struct proc_dir_entry *pde;
	struct usb_ov511 *ov511;
	void *arg = (void *) ularg;
	int rc;

	pde = (struct proc_dir_entry *) inode->u.generic_ip;
	if (!pde)
		return -ENOENT;

	ov511 = (struct usb_ov511 *) pde->data;
	if (!ov511)
		return -ENODEV;

	if (!ov511->dev)
		return -EIO;

	/* Should we pass through standard V4L IOCTLs? */

	switch (cmd) {
	case OV511IOC_GINTVER:
	{
		int ver = OV511_INTERFACE_VER;

		PDEBUG(4, "Get interface version: %d", ver);
		if (copy_to_user(arg, &ver, sizeof(ver)))
			return -EFAULT;

		return 0;
	}
	case OV511IOC_GUSHORT:
	{
		struct ov511_ushort_opt opt;

		if (copy_from_user(&opt, arg, sizeof(opt)))
			return -EFAULT;

		switch (opt.optnum) {
		case OV511_USOPT_BRIGHT:
			rc = sensor_get_brightness(ov511, &(opt.val));
			if (rc)	return rc;
			break;
		case OV511_USOPT_SAT:
			rc = sensor_get_saturation(ov511, &(opt.val));
			if (rc)	return rc;
			break;
		case OV511_USOPT_HUE:
			rc = sensor_get_hue(ov511, &(opt.val));
			if (rc)	return rc;
			break;
		case OV511_USOPT_CONTRAST:
			rc = sensor_get_contrast(ov511, &(opt.val));
			if (rc)	return rc;
			break;
		default:
			err("Invalid get short option number");
			return -EINVAL;
		}

		if (copy_to_user(arg, &opt, sizeof(opt)))
			return -EFAULT;

		return 0;
	}
	case OV511IOC_SUSHORT:
	{
		struct ov511_ushort_opt opt;

		if (copy_from_user(&opt, arg, sizeof(opt)))
			return -EFAULT;

		switch (opt.optnum) {
		case OV511_USOPT_BRIGHT:
			rc = sensor_set_brightness(ov511, opt.val);
			if (rc)	return rc;
			break;
		case OV511_USOPT_SAT:
			rc = sensor_set_saturation(ov511, opt.val);
			if (rc)	return rc;
			break;
		case OV511_USOPT_HUE:
			rc = sensor_set_hue(ov511, opt.val);
			if (rc)	return rc;
			break;
		case OV511_USOPT_CONTRAST:
			rc = sensor_set_contrast(ov511, opt.val);
			if (rc)	return rc;
			break;
		default:
			err("Invalid set short option number");
			return -EINVAL;
		}

		return 0;
	}
	case OV511IOC_GUINT:
	{
		struct ov511_uint_opt opt;

		if (copy_from_user(&opt, arg, sizeof(opt)))
			return -EFAULT;

		switch (opt.optnum) {
		case OV511_UIOPT_POWER_FREQ:
			opt.val = ov511->lightfreq;
			break;
		case OV511_UIOPT_BFILTER:
			opt.val = ov511->bandfilt;
			break;
		case OV511_UIOPT_LED:
			opt.val = ov511->led_policy;
			break;
		case OV511_UIOPT_DEBUG:
			opt.val = debug;
			break;
		case OV511_UIOPT_COMPRESS:
			opt.val = ov511->compress;
			break;
		default:
			err("Invalid get int option number");
			return -EINVAL;
		}

		if (copy_to_user(arg, &opt, sizeof(opt)))
			return -EFAULT;

		return 0;
	}
	case OV511IOC_SUINT:
	{
		struct ov511_uint_opt opt;

		if (copy_from_user(&opt, arg, sizeof(opt)))
			return -EFAULT;

		switch (opt.optnum) {
		case OV511_UIOPT_POWER_FREQ:
			rc = sensor_set_light_freq(ov511, opt.val);
			if (rc)	return rc;
			break;
		case OV511_UIOPT_BFILTER:
			rc = sensor_set_banding_filter(ov511, opt.val);
			if (rc)	return rc;
			break;
		case OV511_UIOPT_LED:
			if (opt.val <= 2) {
				ov511->led_policy = opt.val;
				if (ov511->led_policy == LED_OFF)
					ov51x_led_control(ov511, 0);
				else if (ov511->led_policy == LED_ON)
					ov51x_led_control(ov511, 1);
			} else {
				return -EINVAL;
			}
			break;
		case OV511_UIOPT_DEBUG:
			if (opt.val <= 5)
				debug = opt.val;
			else
				return -EINVAL;
			break;
		case OV511_UIOPT_COMPRESS:
			ov511->compress = opt.val;
			if (ov511->compress) {
				if (ov511->bridge == BRG_OV511 ||
				    ov511->bridge == BRG_OV511PLUS)
					ov511_init_compression(ov511);
				else if (ov511->bridge == BRG_OV518 ||
					 ov511->bridge == BRG_OV518PLUS)
					ov518_init_compression(ov511);
			}
			break;
		default:
			err("Invalid get int option number");
			return -EINVAL;
		}

		return 0;
	}
	case OV511IOC_WI2C:
	{
		struct ov511_i2c_struct w;

		if (copy_from_user(&w, arg, sizeof(w)))
			return -EFAULT;

		return ov51x_i2c_write_slave(ov511, w.slave, w.reg, w.value,
			w.mask);
	}
	case OV511IOC_RI2C:
	{
		struct ov511_i2c_struct r;

		if (copy_from_user(&r, arg, sizeof(r)))
			return -EFAULT;

		rc = ov51x_i2c_read_slave(ov511, r.slave, r.reg);
		if (rc < 0)
			return rc;

		r.value = rc;

		if (copy_to_user(arg, &r, sizeof(r)))
			return -EFAULT;

		return 0;
	}
	default:
		return -EINVAL;
	} /* end switch */

	return 0;
}
#endif

/****************************************************************************
 *
 * OV511 and sensor configuration
 *
 ***************************************************************************/

/* This initializes the OV7610, OV7620, or OV7620AE sensor. The OV7620AE uses
 * the same register settings as the OV7610, since they are very similar.
 */
static int 
ov7xx0_configure(struct usb_ov511 *ov511)
{
	int i, success;
	int rc;

	/* Lawrence Glaister <lg@jfm.bc.ca> reports:
	 *
	 * Register 0x0f in the 7610 has the following effects:
	 *
	 * 0x85 (AEC method 1): Best overall, good contrast range
	 * 0x45 (AEC method 2): Very overexposed
	 * 0xa5 (spec sheet default): Ok, but the black level is
	 *	shifted resulting in loss of contrast
	 * 0x05 (old driver setting): very overexposed, too much
	 *	contrast
	 */
	static struct ov511_regvals aRegvalsNorm7610[] = {
		{ OV511_I2C_BUS, 0x10, 0xff },
		{ OV511_I2C_BUS, 0x16, 0x06 },
		{ OV511_I2C_BUS, 0x28, 0x24 },
		{ OV511_I2C_BUS, 0x2b, 0xac },
		{ OV511_I2C_BUS, 0x12, 0x00 },
		{ OV511_I2C_BUS, 0x38, 0x81 },
		{ OV511_I2C_BUS, 0x28, 0x24 },	/* 0c */
		{ OV511_I2C_BUS, 0x0f, 0x85 },	/* lg's setting */
		{ OV511_I2C_BUS, 0x15, 0x01 },
		{ OV511_I2C_BUS, 0x20, 0x1c },
		{ OV511_I2C_BUS, 0x23, 0x2a },
		{ OV511_I2C_BUS, 0x24, 0x10 },
		{ OV511_I2C_BUS, 0x25, 0x8a },
		{ OV511_I2C_BUS, 0x26, 0xa2 },
		{ OV511_I2C_BUS, 0x27, 0xc2 },
		{ OV511_I2C_BUS, 0x2a, 0x04 },
		{ OV511_I2C_BUS, 0x2c, 0xfe },
		{ OV511_I2C_BUS, 0x2d, 0x93 },
		{ OV511_I2C_BUS, 0x30, 0x71 },
		{ OV511_I2C_BUS, 0x31, 0x60 },
		{ OV511_I2C_BUS, 0x32, 0x26 },
		{ OV511_I2C_BUS, 0x33, 0x20 },
		{ OV511_I2C_BUS, 0x34, 0x48 },
		{ OV511_I2C_BUS, 0x12, 0x24 },
		{ OV511_I2C_BUS, 0x11, 0x01 },
		{ OV511_I2C_BUS, 0x0c, 0x24 },
		{ OV511_I2C_BUS, 0x0d, 0x24 },
		{ OV511_DONE_BUS, 0x0, 0x00 },
	};

	static struct ov511_regvals aRegvalsNorm7620[] = {
		{ OV511_I2C_BUS, 0x00, 0x00 },
		{ OV511_I2C_BUS, 0x01, 0x80 },
		{ OV511_I2C_BUS, 0x02, 0x80 },
		{ OV511_I2C_BUS, 0x03, 0xc0 },
		{ OV511_I2C_BUS, 0x06, 0x60 },
		{ OV511_I2C_BUS, 0x07, 0x00 },
		{ OV511_I2C_BUS, 0x0c, 0x24 },
		{ OV511_I2C_BUS, 0x0c, 0x24 },
		{ OV511_I2C_BUS, 0x0d, 0x24 },
		{ OV511_I2C_BUS, 0x11, 0x01 },
		{ OV511_I2C_BUS, 0x12, 0x24 },
		{ OV511_I2C_BUS, 0x13, 0x01 },
		{ OV511_I2C_BUS, 0x14, 0x84 },
		{ OV511_I2C_BUS, 0x15, 0x01 },
		{ OV511_I2C_BUS, 0x16, 0x03 },
		{ OV511_I2C_BUS, 0x17, 0x2f },
		{ OV511_I2C_BUS, 0x18, 0xcf },
		{ OV511_I2C_BUS, 0x19, 0x06 },
		{ OV511_I2C_BUS, 0x1a, 0xf5 },
		{ OV511_I2C_BUS, 0x1b, 0x00 },
		{ OV511_I2C_BUS, 0x20, 0x18 },
		{ OV511_I2C_BUS, 0x21, 0x80 },
		{ OV511_I2C_BUS, 0x22, 0x80 },
		{ OV511_I2C_BUS, 0x23, 0x00 },
		{ OV511_I2C_BUS, 0x26, 0xa2 },
		{ OV511_I2C_BUS, 0x27, 0xea },
		{ OV511_I2C_BUS, 0x28, 0x20 },
		{ OV511_I2C_BUS, 0x29, 0x00 },
		{ OV511_I2C_BUS, 0x2a, 0x10 },
		{ OV511_I2C_BUS, 0x2b, 0x00 },
		{ OV511_I2C_BUS, 0x2c, 0x88 },
		{ OV511_I2C_BUS, 0x2d, 0x91 },
		{ OV511_I2C_BUS, 0x2e, 0x80 },
		{ OV511_I2C_BUS, 0x2f, 0x44 },
		{ OV511_I2C_BUS, 0x60, 0x27 },
		{ OV511_I2C_BUS, 0x61, 0x02 },
		{ OV511_I2C_BUS, 0x62, 0x5f },
		{ OV511_I2C_BUS, 0x63, 0xd5 },
		{ OV511_I2C_BUS, 0x64, 0x57 },
		{ OV511_I2C_BUS, 0x65, 0x83 },
		{ OV511_I2C_BUS, 0x66, 0x55 },
		{ OV511_I2C_BUS, 0x67, 0x92 },
		{ OV511_I2C_BUS, 0x68, 0xcf },
		{ OV511_I2C_BUS, 0x69, 0x76 },
		{ OV511_I2C_BUS, 0x6a, 0x22 },
		{ OV511_I2C_BUS, 0x6b, 0x00 },
		{ OV511_I2C_BUS, 0x6c, 0x02 },
		{ OV511_I2C_BUS, 0x6d, 0x44 },
		{ OV511_I2C_BUS, 0x6e, 0x80 },
		{ OV511_I2C_BUS, 0x6f, 0x1d },
		{ OV511_I2C_BUS, 0x70, 0x8b },
		{ OV511_I2C_BUS, 0x71, 0x00 },
		{ OV511_I2C_BUS, 0x72, 0x14 },
		{ OV511_I2C_BUS, 0x73, 0x54 },
		{ OV511_I2C_BUS, 0x74, 0x00 },
		{ OV511_I2C_BUS, 0x75, 0x8e },
		{ OV511_I2C_BUS, 0x76, 0x00 },
		{ OV511_I2C_BUS, 0x77, 0xff },
		{ OV511_I2C_BUS, 0x78, 0x80 },
		{ OV511_I2C_BUS, 0x79, 0x80 },
		{ OV511_I2C_BUS, 0x7a, 0x80 },
		{ OV511_I2C_BUS, 0x7b, 0xe2 },
		{ OV511_I2C_BUS, 0x7c, 0x00 },
		{ OV511_DONE_BUS, 0x0, 0x00 },
	};

	PDEBUG(4, "starting configuration");

	/* This looks redundant, but is necessary for WebCam 3 */
	ov511->primary_i2c_slave = OV7xx0_I2C_WRITE_ID;
	if (ov51x_set_slave_ids(ov511, OV7xx0_I2C_WRITE_ID,
				OV7xx0_I2C_READ_ID) < 0)
		return -1;

	if (ov51x_init_ov_sensor(ov511) >= 0) {
		PDEBUG(1, "OV7xx0 sensor initalized (method 1)");
	} else {
		/* Reset the 76xx */
		if (ov51x_i2c_write(ov511, 0x12, 0x80) < 0) return -1;

		/* Wait for it to initialize */
		schedule_timeout(1 + 150 * HZ / 1000);

		i = 0;
		success = 0;
		while (i <= i2c_detect_tries) {
			if ((ov51x_i2c_read(ov511,
					    OV7610_REG_ID_HIGH) == 0x7F) &&
			    (ov51x_i2c_read(ov511,
					    OV7610_REG_ID_LOW) == 0xA2)) {
				success = 1;
				break;
			} else {
				i++;
			}
		}

// Was (i == i2c_detect_tries) previously. This obviously used to always report
// success. Whether anyone actually depended on that bug is unknown
		if ((i >= i2c_detect_tries) && (success == 0)) {
			err("Failed to read sensor ID. You might not have an");
			err("OV7610/20, or it may be not responding. Report");
			err("this to " EMAIL);
			err("This is only a warning. You can attempt to use");
			err("your camera anyway");
// Only issue a warning for now  
//			return -1;
		} else {
			PDEBUG(1, "OV7xx0 initialized (method 2, %dx)", i+1);
		}
	}

	/* Detect sensor (sub)type */
	rc = ov51x_i2c_read(ov511, OV7610_REG_COM_I);

	if (rc < 0) {
		err("Error detecting sensor type");
		return -1;
	} else if ((rc & 3) == 3) {
		info("Sensor is an OV7610");
		ov511->sensor = SEN_OV7610;
	} else if ((rc & 3) == 1) {
		/* I don't know what's different about the 76BE yet */
		if (ov51x_i2c_read(ov511, 0x15) & 1)
			info("Sensor is an OV7620AE");
		else
			info("Sensor is an OV76BE");

		/* OV511+ will return all zero isoc data unless we
		 * configure the sensor as a 7620. Someone needs to
		 * find the exact reg. setting that causes this. */
		if (ov511->bridge == BRG_OV511PLUS) {
			info("Enabling 511+/7620AE workaround");
			ov511->sensor = SEN_OV7620;
		} else {
			ov511->sensor = SEN_OV7620AE;
		}
	} else if ((rc & 3) == 0) {
		info("Sensor is an OV7620");
		ov511->sensor = SEN_OV7620;
	} else {
		err("Unknown image sensor version: %d", rc & 3);
		return -1;
	}

	if (ov511->sensor == SEN_OV7620) {
		PDEBUG(4, "Writing 7620 registers");
		if (ov511_write_regvals(ov511, aRegvalsNorm7620))
			return -1;
	} else {
		PDEBUG(4, "Writing 7610 registers");
		if (ov511_write_regvals(ov511, aRegvalsNorm7610))
			return -1;
	}

	/* Set sensor-specific vars */
	ov511->maxwidth = 640;
	ov511->maxheight = 480;
	ov511->minwidth = 64;
	ov511->minheight = 48;

	// FIXME: These do not match the actual settings yet
	ov511->brightness = 0x80 << 8;
	ov511->contrast = 0x80 << 8;
	ov511->colour = 0x80 << 8;
	ov511->hue = 0x80 << 8;

	return 0;
}

/* This initializes the OV6620, OV6630, OV6630AE, or OV6630AF sensor. */
static int 
ov6xx0_configure(struct usb_ov511 *ov511)
{
	int rc;

	static struct ov511_regvals aRegvalsNorm6x20[] = {
		{ OV511_I2C_BUS, 0x12, 0x80 }, /* reset */
		{ OV511_I2C_BUS, 0x11, 0x01 },
		{ OV511_I2C_BUS, 0x03, 0x60 },
		{ OV511_I2C_BUS, 0x05, 0x7f }, /* For when autoadjust is off */
		{ OV511_I2C_BUS, 0x07, 0xa8 },
		/* The ratio of 0x0c and 0x0d  controls the white point */
		{ OV511_I2C_BUS, 0x0c, 0x24 },
		{ OV511_I2C_BUS, 0x0d, 0x24 },
		{ OV511_I2C_BUS, 0x12, 0x24 }, /* Enable AGC */
		{ OV511_I2C_BUS, 0x14, 0x04 },
		/* 0x16: 0x06 helps frame stability with moving objects */
		{ OV511_I2C_BUS, 0x16, 0x06 },
//		{ OV511_I2C_BUS, 0x20, 0x30 }, /* Aperture correction enable */
		{ OV511_I2C_BUS, 0x26, 0xb2 }, /* BLC enable */
		/* 0x28: 0x05 Selects RGB format if RGB on */
		{ OV511_I2C_BUS, 0x28, 0x05 },
		{ OV511_I2C_BUS, 0x2a, 0x04 }, /* Disable framerate adjust */
//		{ OV511_I2C_BUS, 0x2b, 0xac }, /* Framerate; Set 2a[7] first */
		{ OV511_I2C_BUS, 0x2d, 0x99 },
		{ OV511_I2C_BUS, 0x34, 0xd2 }, /* Max A/D range */
		{ OV511_I2C_BUS, 0x38, 0x8b },
		{ OV511_I2C_BUS, 0x39, 0x40 },
		
		{ OV511_I2C_BUS, 0x3c, 0x39 }, /* Enable AEC mode changing */
		{ OV511_I2C_BUS, 0x3c, 0x3c }, /* Change AEC mode */
		{ OV511_I2C_BUS, 0x3c, 0x24 }, /* Disable AEC mode changing */

		{ OV511_I2C_BUS, 0x3d, 0x80 },
		/* These next two registers (0x4a, 0x4b) are undocumented. They
		 * control the color balance */
		{ OV511_I2C_BUS, 0x4a, 0x80 },
		{ OV511_I2C_BUS, 0x4b, 0x80 },
		{ OV511_I2C_BUS, 0x4d, 0xd2 }, /* This reduces noise a bit */
		{ OV511_I2C_BUS, 0x4e, 0xc1 },
		{ OV511_I2C_BUS, 0x4f, 0x04 },
// Do 50-53 have any effect?
// Toggle 0x12[2] off and on here?
		{ OV511_DONE_BUS, 0x0, 0x00 },
	};

	/* This chip is undocumented so many of these are guesses. OK=verified,
	 * A=Added since 6620, U=unknown function (not a 6620 reg) */
	static struct ov511_regvals aRegvalsNorm6x30[] = {
	/*OK*/	{ OV511_I2C_BUS, 0x12, 0x80 }, /* reset */
	/*00?*/	{ OV511_I2C_BUS, 0x11, 0x01 },
	/*OK*/	{ OV511_I2C_BUS, 0x03, 0x60 },
	/*0A?*/	{ OV511_I2C_BUS, 0x05, 0x7f }, /* For when autoadjust is off */
		{ OV511_I2C_BUS, 0x07, 0xa8 },
		/* The ratio of 0x0c and 0x0d  controls the white point */
	/*OK*/	{ OV511_I2C_BUS, 0x0c, 0x24 },
	/*OK*/	{ OV511_I2C_BUS, 0x0d, 0x24 },
	/*A*/	{ OV511_I2C_BUS, 0x0e, 0x20 },

//	/*24?*/	{ OV511_I2C_BUS, 0x12, 0x28 }, /* Enable AGC */
//		{ OV511_I2C_BUS, 0x12, 0x24 }, /* Enable AGC */

//	/*A*/	{ OV511_I2C_BUS, 0x13, 0x21 },
//	/*A*/	{ OV511_I2C_BUS, 0x13, 0x25 }, /* Tristate Y and UV busses */

//	/*04?*/	{ OV511_I2C_BUS, 0x14, 0x80 },
		/* 0x16: 0x06 helps frame stability with moving objects */
	/*03?*/	{ OV511_I2C_BUS, 0x16, 0x06 },
//	/*OK*/	{ OV511_I2C_BUS, 0x20, 0x30 }, /* Aperture correction enable */
		// 21 & 22? The suggested values look wrong. Go with default
	/*A*/	{ OV511_I2C_BUS, 0x23, 0xc0 },
	/*A*/	{ OV511_I2C_BUS, 0x25, 0x9a }, // Check this against default
//	/*OK*/	{ OV511_I2C_BUS, 0x26, 0xb2 }, /* BLC enable */

		/* 0x28: 0x05 Selects RGB format if RGB on */
//	/*04?*/	{ OV511_I2C_BUS, 0x28, 0x05 },
//	/*04?*/	{ OV511_I2C_BUS, 0x28, 0x45 }, // DEBUG: Tristate UV bus

	/*OK*/	{ OV511_I2C_BUS, 0x2a, 0x04 }, /* Disable framerate adjust */
//	/*OK*/	{ OV511_I2C_BUS, 0x2b, 0xac }, /* Framerate; Set 2a[7] first */
//	/*U*/	{ OV511_I2C_BUS, 0x2c, 0xa0 },
		{ OV511_I2C_BUS, 0x2d, 0x99 },
//	/*A*/	{ OV511_I2C_BUS, 0x33, 0x26 }, // Reserved bits on 6620
//	/*d2?*/	{ OV511_I2C_BUS, 0x34, 0x03 }, /* Max A/D range */
//	/*U*/	{ OV511_I2C_BUS, 0x36, 0x8f }, // May not be necessary
//	/*U*/	{ OV511_I2C_BUS, 0x37, 0x80 }, // May not be necessary
//	/*8b?*/	{ OV511_I2C_BUS, 0x38, 0x83 },
//	/*40?*/	{ OV511_I2C_BUS, 0x39, 0xc0 }, // 6630 adds bit 7
//		{ OV511_I2C_BUS, 0x3c, 0x39 }, /* Enable AEC mode changing */
//		{ OV511_I2C_BUS, 0x3c, 0x3c }, /* Change AEC mode */
//		{ OV511_I2C_BUS, 0x3c, 0x24 }, /* Disable AEC mode changing */
	/*OK*/	{ OV511_I2C_BUS, 0x3d, 0x80 },
//	/*A*/	{ OV511_I2C_BUS, 0x3f, 0x0e },
//	/*U*/	{ OV511_I2C_BUS, 0x40, 0x00 },
//	/*U*/	{ OV511_I2C_BUS, 0x41, 0x00 },
//	/*U*/	{ OV511_I2C_BUS, 0x42, 0x80 },
//	/*U*/	{ OV511_I2C_BUS, 0x43, 0x3f },
//	/*U*/	{ OV511_I2C_BUS, 0x44, 0x80 },
//	/*U*/	{ OV511_I2C_BUS, 0x45, 0x20 },
//	/*U*/	{ OV511_I2C_BUS, 0x46, 0x20 },
//	/*U*/	{ OV511_I2C_BUS, 0x47, 0x80 },
//	/*U*/	{ OV511_I2C_BUS, 0x48, 0x7f },
//	/*U*/	{ OV511_I2C_BUS, 0x49, 0x00 },

		/* These next two registers (0x4a, 0x4b) are undocumented. They
		 * control the color balance */
//	/*OK?*/	{ OV511_I2C_BUS, 0x4a, 0x80 }, // Check these
//	/*OK?*/	{ OV511_I2C_BUS, 0x4b, 0x80 },
//	/*U*/	{ OV511_I2C_BUS, 0x4c, 0xd0 }, 
	/*d2?*/	{ OV511_I2C_BUS, 0x4d, 0x10 }, /* This reduces noise a bit */
	/*c1?*/	{ OV511_I2C_BUS, 0x4e, 0x40 },
	/*04?*/	{ OV511_I2C_BUS, 0x4f, 0x07 },
//	/*U*/	{ OV511_I2C_BUS, 0x50, 0xff },
	/*U*/	{ OV511_I2C_BUS, 0x54, 0x23 },
//	/*U*/	{ OV511_I2C_BUS, 0x55, 0xff },
//	/*U*/	{ OV511_I2C_BUS, 0x56, 0x12 },
	/*U*/	{ OV511_I2C_BUS, 0x57, 0x81 },
//	/*U*/	{ OV511_I2C_BUS, 0x58, 0x75 },
	/*U*/	{ OV511_I2C_BUS, 0x59, 0x01 },
	/*U*/	{ OV511_I2C_BUS, 0x5a, 0x2c },
	/*U*/	{ OV511_I2C_BUS, 0x5b, 0x0f },
//	/*U*/	{ OV511_I2C_BUS, 0x5c, 0x10 },
		{ OV511_DONE_BUS, 0x0, 0x00 },
	};

	PDEBUG(4, "starting sensor configuration");
	
	if (ov51x_init_ov_sensor(ov511) < 0) {
		err("Failed to read sensor ID. You might not have an OV6xx0,");
		err("or it may be not responding. Report this to " EMAIL);
		return -1;
	} else {
		PDEBUG(1, "OV6xx0 sensor detected");
	}

	/* Detect sensor (sub)type */
	rc = ov51x_i2c_read(ov511, OV7610_REG_COM_I);

	if (rc < 0) {
		err("Error detecting sensor type");
		return -1;
	} else if ((rc & 3) == 0) {
		info("Sensor is an OV6630");
		ov511->sensor = SEN_OV6630;
	} else if ((rc & 3) == 1) {
		info("Sensor is an OV6620");
		ov511->sensor = SEN_OV6620;
	} else if ((rc & 3) == 2) {
		info("Sensor is an OV6630AE");
		ov511->sensor = SEN_OV6630;
	} else if ((rc & 3) == 3) {
		info("Sensor is an OV6630AF");
		ov511->sensor = SEN_OV6630;
	} 

	/* Set sensor-specific vars */
	if (ov511->sensor == SEN_OV6620) {
		ov511->maxwidth = 352;
		ov511->maxheight = 288;
	} else {
		/* 352x288 not working with OV518 yet */
		ov511->maxwidth = 320;
		ov511->maxheight = 240;
	}
	ov511->minwidth = 64;
	ov511->minheight = 48;

	// FIXME: These do not match the actual settings yet
	ov511->brightness = 0x80 << 8;
	ov511->contrast = 0x80 << 8;
	ov511->colour = 0x80 << 8;
	ov511->hue = 0x80 << 8;

	if (ov511->sensor == SEN_OV6620) {
		PDEBUG(4, "Writing 6x20 registers");
		if (ov511_write_regvals(ov511, aRegvalsNorm6x20))
			return -1;
	} else {
		PDEBUG(4, "Writing 6x30 registers");
		if (ov511_write_regvals(ov511, aRegvalsNorm6x30))
			return -1;
	}
	
	return 0;
}

/* This initializes the KS0127 and KS0127B video decoders. */
static int 
ks0127_configure(struct usb_ov511 *ov511)
{
	int rc;

// FIXME: I don't know how to sync or reset it yet
#if 0
	if (ov51x_init_ks_sensor(ov511) < 0) {
		err("Failed to initialize the KS0127");
		return -1;
	} else {
		PDEBUG(1, "KS012x(B) sensor detected");
	}
#endif

	/* Detect decoder subtype */
	rc = ov51x_i2c_read(ov511, 0x00);
	if (rc < 0) {
		err("Error detecting sensor type");
		return -1;
	} else if (rc & 0x08) {
		rc = ov51x_i2c_read(ov511, 0x3d);
		if (rc < 0) {
			err("Error detecting sensor type");
			return -1;
		} else if ((rc & 0x0f) == 0) {
			info("Sensor is a KS0127");
			ov511->sensor = SEN_KS0127;
		} else if ((rc & 0x0f) == 9) {
			info("Sensor is a KS0127B Rev. A");
			ov511->sensor = SEN_KS0127B;
		}
	} else {
		err("Error: Sensor is an unsupported KS0122");
		return -1;
	}

	/* Set sensor-specific vars */
	ov511->maxwidth = 640;
	ov511->maxheight = 480;
	ov511->minwidth = 64;
	ov511->minheight = 48;

	// FIXME: These do not match the actual settings yet
	ov511->brightness = 0x80 << 8;
	ov511->contrast = 0x80 << 8;
	ov511->colour = 0x80 << 8;
	ov511->hue = 0x80 << 8;

	/* This device is not supported yet. Bail out now... */
	err("This sensor is not supported yet.");
	return -1;

	return 0;
}

/* This initializes the SAA7111A video decoder. */
static int 
saa7111a_configure(struct usb_ov511 *ov511)
{
	struct usb_device *dev = ov511->dev;
	int rc;

	/* Since there is no register reset command, all registers must be
	 * written, otherwise gives erratic results */
	static struct ov511_regvals aRegvalsNormSAA7111A[] = {
		{ OV511_I2C_BUS, 0x06, 0xce },
		{ OV511_I2C_BUS, 0x07, 0x00 },
		{ OV511_I2C_BUS, 0x10, 0x44 }, /* YUV422, 240/286 lines */
		{ OV511_I2C_BUS, 0x0e, 0x01 }, /* NTSC M or PAL BGHI */
		{ OV511_I2C_BUS, 0x00, 0x00 },
		{ OV511_I2C_BUS, 0x01, 0x00 },
		{ OV511_I2C_BUS, 0x03, 0x23 },
		{ OV511_I2C_BUS, 0x04, 0x00 },
		{ OV511_I2C_BUS, 0x05, 0x00 },
		{ OV511_I2C_BUS, 0x08, 0xc8 }, /* Auto field freq */
		{ OV511_I2C_BUS, 0x09, 0x01 }, /* Chrom. trap off, APER=0.25 */
		{ OV511_I2C_BUS, 0x0a, 0x80 }, /* BRIG=128 */
		{ OV511_I2C_BUS, 0x0b, 0x40 }, /* CONT=1.0 */
		{ OV511_I2C_BUS, 0x0c, 0x40 }, /* SATN=1.0 */
		{ OV511_I2C_BUS, 0x0d, 0x00 }, /* HUE=0 */
		{ OV511_I2C_BUS, 0x0f, 0x00 },
		{ OV511_I2C_BUS, 0x11, 0x0c },
		{ OV511_I2C_BUS, 0x12, 0x00 },
		{ OV511_I2C_BUS, 0x13, 0x00 },
		{ OV511_I2C_BUS, 0x14, 0x00 },
		{ OV511_I2C_BUS, 0x15, 0x00 },
		{ OV511_I2C_BUS, 0x16, 0x00 },
		{ OV511_I2C_BUS, 0x17, 0x00 },
		{ OV511_I2C_BUS, 0x02, 0xc0 },	/* Composite input 0 */
		{ OV511_DONE_BUS, 0x0, 0x00 },
	};

// FIXME: I don't know how to sync or reset it yet
#if 0
	if (ov51x_init_saa_sensor(ov511) < 0) {
		err("Failed to initialize the SAA7111A");
		return -1;
	} else {
		PDEBUG(1, "SAA7111A sensor detected");
	}
#endif

	/* Set sensor-specific vars */
	ov511->maxwidth = 640;
	ov511->maxheight = 480;		/* Even/Odd fields */
	ov511->minwidth = 320;
	ov511->minheight = 240;		/* Even field only */

	ov511->has_decoder = 1;
	ov511->num_inputs = 8;
	ov511->norm = VIDEO_MODE_AUTO;
	ov511->stop_during_set = 0;	/* Decoder guarantees stable image */

	/* Decoder doesn't change these values, so we use these instead of
	 * acutally reading the registers (which doesn't work) */
	ov511->brightness = 0x80 << 8;
	ov511->contrast = 0x40 << 9;
	ov511->colour = 0x40 << 9;
	ov511->hue = 32768;

	PDEBUG(4, "Writing SAA7111A registers");
	if (ov511_write_regvals(ov511, aRegvalsNormSAA7111A))
		return -1;

	/* Detect version of decoder. This must be done after writing the
         * initial regs or the decoder will lock up. */
	rc = ov51x_i2c_read(ov511, 0x00);

	if (rc < 0) {
		err("Error detecting sensor version");
		return -1;
	} else {
		info("Sensor is an SAA7111A (version 0x%x)", rc);
		ov511->sensor = SEN_SAA7111A;
	}

	// FIXME: Fix this for OV518(+)
	/* Latch to negative edge of clock. Otherwise, we get incorrect
	 * colors and jitter in the digital signal. */
	if (ov511->bridge == BRG_OV511 || ov511->bridge == BRG_OV511PLUS)
		ov511_reg_write(dev, 0x11, 0x00);
	else
		warn("SAA7111A not yet supported with OV518/OV518+");

	return 0;
}

/* This initializes the OV511/OV511+ and the sensor */
static int 
ov511_configure(struct usb_ov511 *ov511)
{
	struct usb_device *dev = ov511->dev;
	int i;

	static struct ov511_regvals aRegvalsInit511[] = {
		{ OV511_REG_BUS, OV511_REG_SYSTEM_RESET, 0x7f },
	 	{ OV511_REG_BUS, OV511_REG_SYSTEM_INIT, 0x01 },
	 	{ OV511_REG_BUS, OV511_REG_SYSTEM_RESET, 0x7f },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_INIT, 0x01 },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_RESET, 0x3f },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_INIT, 0x01 },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_RESET, 0x3d },
		{ OV511_DONE_BUS, 0x0, 0x00},
	};

	static struct ov511_regvals aRegvalsNorm511[] = {
		{ OV511_REG_BUS, OV511_REG_DRAM_ENABLE_FLOW_CONTROL, 0x01 },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_SNAPSHOT, 0x01 },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_SNAPSHOT, 0x03 },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_SNAPSHOT, 0x01 },
		{ OV511_REG_BUS, OV511_REG_FIFO_BITMASK, 0x1f },
		{ OV511_REG_BUS, OV511_OMNICE_ENABLE, 0x00 },
		{ OV511_REG_BUS, OV511_OMNICE_LUT_ENABLE, 0x03 },
		{ OV511_DONE_BUS, 0x0, 0x00 },
	};

	static struct ov511_regvals aRegvalsNorm511Plus[] = {
		{ OV511_REG_BUS, OV511_REG_DRAM_ENABLE_FLOW_CONTROL, 0xff },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_SNAPSHOT, 0x01 },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_SNAPSHOT, 0x03 },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_SNAPSHOT, 0x01 },
		{ OV511_REG_BUS, OV511_REG_FIFO_BITMASK, 0xff },
		{ OV511_REG_BUS, OV511_OMNICE_ENABLE, 0x00 },
		{ OV511_REG_BUS, OV511_OMNICE_LUT_ENABLE, 0x03 },
		{ OV511_DONE_BUS, 0x0, 0x00 },
	};

	PDEBUG(4, "");

	ov511->customid = ov511_reg_read(dev, OV511_REG_SYSTEM_CUSTOM_ID);
	if (ov511->customid < 0) {
		err("Unable to read camera bridge registers");
		goto error;
	}

	ov511->desc = -1;
	PDEBUG (1, "CustomID = %d", ov511->customid);
	for (i = 0; clist[i].id >= 0; i++) {
		if (ov511->customid == clist[i].id) {
			info("model: %s", clist[i].description);
			ov511->desc = i;
			break;
		}
	}

	if (clist[i].id == -1) {
		err("Camera type (%d) not recognized", ov511->customid);
		err("Please notify " EMAIL " of the name,");
		err("manufacturer, model, and this number of your camera.");
		err("Also include the output of the detection process.");
	} 

	if (clist[i].id == 6) {	/* USB Life TV (NTSC) */
		ov511->tuner_type = 8;		/* Temic 4036FY5 3X 1981 */
	}

	if (ov511_write_regvals(ov511, aRegvalsInit511)) goto error;

	if (ov511->led_policy == LED_OFF || ov511->led_policy == LED_AUTO)
		ov51x_led_control(ov511, 0);

	/* The OV511+ has undocumented bits in the flow control register.
	 * Setting it to 0xff fixes the corruption with moving objects. */
	if (ov511->bridge == BRG_OV511) {
		if (ov511_write_regvals(ov511, aRegvalsNorm511)) goto error;
	} else if (ov511->bridge == BRG_OV511PLUS) {
		if (ov511_write_regvals(ov511, aRegvalsNorm511Plus)) goto error;
	} else {
		err("Invalid bridge");
	}

	if (ov511_init_compression(ov511)) goto error;

	ov511_set_packet_size(ov511, 0);

	ov511->snap_enabled = snapshot;	

	/* Test for 7xx0 */
	PDEBUG(3, "Testing for 0V7xx0");
	ov511->primary_i2c_slave = OV7xx0_I2C_WRITE_ID;
	if (ov51x_set_slave_ids(ov511, OV7xx0_I2C_WRITE_ID,
				OV7xx0_I2C_READ_ID) < 0)
		goto error;

	if (ov51x_i2c_write(ov511, 0x12, 0x80) < 0) {
		/* Test for 6xx0 */
		PDEBUG(3, "Testing for 0V6xx0");
		ov511->primary_i2c_slave = OV6xx0_I2C_WRITE_ID;
		if (ov51x_set_slave_ids(ov511, OV6xx0_I2C_WRITE_ID,
					OV6xx0_I2C_READ_ID) < 0)
			goto error;

		if (ov51x_i2c_write(ov511, 0x12, 0x80) < 0) {
			/* Test for 8xx0 */
			PDEBUG(3, "Testing for 0V8xx0");
			ov511->primary_i2c_slave = OV8xx0_I2C_WRITE_ID;
			if (ov51x_set_slave_ids(ov511, OV8xx0_I2C_WRITE_ID,
						OV8xx0_I2C_READ_ID))
				goto error;

			if (ov51x_i2c_write(ov511, 0x12, 0x80) < 0) {
				/* Test for SAA7111A */
				PDEBUG(3, "Testing for SAA7111A");
				ov511->primary_i2c_slave = SAA7111A_I2C_WRITE_ID;
				if (ov51x_set_slave_ids(ov511, SAA7111A_I2C_WRITE_ID,
							SAA7111A_I2C_READ_ID))
					goto error;

				if (ov51x_i2c_write(ov511, 0x0d, 0x00) < 0) {
					/* Test for KS0127 */
					PDEBUG(3, "Testing for KS0127");
					ov511->primary_i2c_slave = KS0127_I2C_WRITE_ID;
					if (ov51x_set_slave_ids(ov511, KS0127_I2C_WRITE_ID,
								KS0127_I2C_READ_ID))
						goto error;

					if (ov51x_i2c_write(ov511, 0x10, 0x00) < 0) {
						err("Can't determine sensor slave IDs");
		 				goto error;
					} else {
						if(ks0127_configure(ov511) < 0) {
							err("Failed to configure KS0127");
	 						goto error;
						}
					}
				} else {
					if(saa7111a_configure(ov511) < 0) {
						err("Failed to configure SAA7111A");
	 					goto error;
					}
				}
			} else {
				err("Detected unsupported OV8xx0 sensor");
				goto error;
			}
		} else {
			if(ov6xx0_configure(ov511) < 0) {
				err("Failed to configure OV6xx0");
 				goto error;
			}
		}
	} else {
		if(ov7xx0_configure(ov511) < 0) {
			err("Failed to configure OV7xx0");
	 		goto error;
		}
	}

	return 0;

error:
	err("OV511 Config failed");

	return -EBUSY;
}

/* This initializes the OV518/OV518+ and the sensor */
static int 
ov518_configure(struct usb_ov511 *ov511)
{
	struct usb_device *dev = ov511->dev;

	static struct ov511_regvals aRegvalsInit518[] = {
		{ OV511_REG_BUS, OV511_REG_SYSTEM_RESET, 0x40 },
	 	{ OV511_REG_BUS, OV511_REG_SYSTEM_INIT, 0xe1 },
	 	{ OV511_REG_BUS, OV511_REG_SYSTEM_RESET, 0x3e },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_INIT, 0xe1 },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_RESET, 0x00 },
		{ OV511_REG_BUS, OV511_REG_SYSTEM_INIT, 0xe1 },
		{ OV511_REG_BUS, 0x46, 0x00 }, 
		{ OV511_REG_BUS, 0x5d, 0x03 },
		{ OV511_DONE_BUS, 0x0, 0x00},
	};

	/* New values, based on Windows driver. Since what they do is not
	 * known yet, this may be incorrect. */
	static struct ov511_regvals aRegvalsNorm518[] = {
		{ OV511_REG_BUS, 0x52, 0x02 }, /* Reset snapshot */
		{ OV511_REG_BUS, 0x52, 0x01 }, /* Enable snapshot */
		{ OV511_REG_BUS, 0x31, 0x0f },
		{ OV511_REG_BUS, 0x5d, 0x03 },
		{ OV511_REG_BUS, 0x24, 0x9f },
		{ OV511_REG_BUS, 0x25, 0x90 },
		{ OV511_REG_BUS, 0x20, 0x00 }, /* Was 0x08 */
		{ OV511_REG_BUS, 0x51, 0x04 },
		{ OV511_REG_BUS, 0x71, 0x19 },
		{ OV511_DONE_BUS, 0x0, 0x00 },
	};

	PDEBUG(4, "");

	/* First 5 bits of custom ID reg are a revision ID on OV518 */
	info("Device revision %d",
	     0x1F & ov511_reg_read(dev, OV511_REG_SYSTEM_CUSTOM_ID));

	if (ov511_write_regvals(ov511, aRegvalsInit518)) goto error;

	/* Set LED GPIO pin to output mode */
	if (ov511_reg_write_mask(dev, 0x57,0x00, 0x02) < 0) goto error;

	/* LED is off by default with OV518; have to explicitly turn it on */
	if (ov511->led_policy == LED_OFF || ov511->led_policy == LED_AUTO)
		ov51x_led_control(ov511, 0);
	else
		ov51x_led_control(ov511, 1);

	/* Don't require compression if dumppix is enabled; otherwise it's
	 * required. OV518 has no uncompressed mode, to save RAM. */
	if (!dumppix && !ov511->compress) {
		ov511->compress = 1;
		warn("Compression required with OV518...enabling");
	}

	if (ov511_write_regvals(ov511, aRegvalsNorm518)) goto error;

	if (ov511_reg_write(dev, 0x2f,0x80) < 0) goto error;

	if (ov518_init_compression(ov511)) goto error;

	ov511_set_packet_size(ov511, 0);

	ov511->snap_enabled = snapshot;

	/* Test for 76xx */
	ov511->primary_i2c_slave = OV7xx0_I2C_WRITE_ID;
	if (ov51x_set_slave_ids(ov511, OV7xx0_I2C_WRITE_ID,
				OV7xx0_I2C_READ_ID) < 0)
		goto error;

	/* The OV518 must be more aggressive about sensor detection since
	 * I2C write will never fail if the sensor is not present. We have
	 * to try to initialize the sensor to detect its presence */

	if (ov51x_init_ov_sensor(ov511) < 0) {
		/* Test for 6xx0 */
		ov511->primary_i2c_slave = OV6xx0_I2C_WRITE_ID;
		if (ov51x_set_slave_ids(ov511, OV6xx0_I2C_WRITE_ID,
					OV6xx0_I2C_READ_ID) < 0)
			goto error;

		if (ov51x_init_ov_sensor(ov511) < 0) {
			/* Test for 8xx0 */
			ov511->primary_i2c_slave = OV8xx0_I2C_WRITE_ID;
			if (ov51x_set_slave_ids(ov511, OV8xx0_I2C_WRITE_ID,
						OV8xx0_I2C_READ_ID) < 0)
				goto error;

			if (ov51x_init_ov_sensor(ov511) < 0) {
				err("Can't determine sensor slave IDs");
 				goto error;
			} else {
				err("Detected unsupported OV8xx0 sensor");
				goto error;
			}
		} else {
			if (ov6xx0_configure(ov511) < 0) {
				err("Failed to configure OV6xx0");
 				goto error;
			}
		}
	} else {
		if (ov7xx0_configure(ov511) < 0) {
			err("Failed to configure OV7xx0");
	 		goto error;
		}
	}

	// The OV518 cannot go as low as the sensor can
	ov511->minwidth = 160;
	ov511->minheight = 120;

	return 0;

error:
	err("OV518 Config failed");

	return -EBUSY;
}


/****************************************************************************
 *
 *  USB routines
 *
 ***************************************************************************/

static void *
ov51x_probe(struct usb_device *dev, unsigned int ifnum,
	    const struct usb_device_id *id)
{
	struct usb_interface_descriptor *interface;
	struct usb_ov511 *ov511;
	int i;
	int registered = 0;

	PDEBUG(1, "probing for device...");

	/* We don't handle multi-config cameras */
	if (dev->descriptor.bNumConfigurations != 1)
		return NULL;

	interface = &dev->actconfig->interface[ifnum].altsetting[0];

	/* Checking vendor/product should be enough, but what the hell */
	if (interface->bInterfaceClass != 0xFF)
		return NULL;
	if (interface->bInterfaceSubClass != 0x00)
		return NULL;

	/* Since code below may sleep, we use this as a lock */
	MOD_INC_USE_COUNT;

	if ((ov511 = kmalloc(sizeof(*ov511), GFP_KERNEL)) == NULL) {
		err("couldn't kmalloc ov511 struct");
		goto error_unlock;
	}

	memset(ov511, 0, sizeof(*ov511));

	ov511->dev = dev;
	ov511->iface = interface->bInterfaceNumber;
	ov511->led_policy = led;
	ov511->compress = compress;
	ov511->lightfreq = lightfreq;
	ov511->num_inputs = 1;	   /* Video decoder init functs. change this */
	ov511->stop_during_set = !fastset;
	ov511->tuner_type = tuner;
	ov511->backlight = backlight;

	ov511->auto_brt = autobright;
	ov511->auto_gain = autogain;
	ov511->auto_exp = autoexp;

	switch (dev->descriptor.idProduct) {
	case PROD_OV511:
		info("USB OV511 camera found");
		ov511->bridge = BRG_OV511;
		ov511->bclass = BCL_OV511;
		break;
	case PROD_OV511PLUS:
		info("USB OV511+ camera found");
		ov511->bridge = BRG_OV511PLUS;
		ov511->bclass = BCL_OV511;
		break;
	case PROD_OV518:
		info("USB OV518 camera found");
		ov511->bridge = BRG_OV518;
		ov511->bclass = BCL_OV518;
		break;
	case PROD_OV518PLUS:
		info("USB OV518+ camera found");
		ov511->bridge = BRG_OV518PLUS;
		ov511->bclass = BCL_OV518;
		break;
	case PROD_ME2CAM:
		if (dev->descriptor.idVendor != VEND_MATTEL)
			goto error;
		info("Intel Play Me2Cam (OV511+) found");
		ov511->bridge = BRG_OV511PLUS;
		ov511->bclass = BCL_OV511;
		break;
	default:
		err("Unknown product ID 0x%x", dev->descriptor.idProduct);
		goto error_dealloc;
	}

	/* Workaround for some applications that want data in RGB
	 * instead of BGR. */
	if (force_rgb)
		info("data format set to RGB");

	init_waitqueue_head(&ov511->wq);

	init_MUTEX(&ov511->lock);	/* to 1 == available */
	init_MUTEX(&ov511->buf_lock);
	init_MUTEX(&ov511->param_lock);
	init_MUTEX(&ov511->i2c_lock);
	ov511->buf_state = BUF_NOT_ALLOCATED;

	if (ov511->bridge == BRG_OV518 ||
	    ov511->bridge == BRG_OV518PLUS) {
		if (ov518_configure(ov511) < 0)
			goto error;
	} else {
		if (ov511_configure(ov511) < 0)
			goto error;
	}

	for (i = 0; i < OV511_NUMFRAMES; i++) {
		ov511->frame[i].framenum = i;
		init_waitqueue_head(&ov511->frame[i].wq);
	}

	/* Unnecessary? (This is done on open(). Need to make sure variables
	 * are properly initialized without this before removing it, though). */
	if (ov51x_set_default_params(ov511) < 0)
		goto error;

#ifdef OV511_DEBUG
	if (dump_bridge)
		ov511_dump_regs(dev);
#endif

	memcpy(&ov511->vdev, &ov511_template, sizeof(ov511_template));
	ov511->vdev.priv = ov511;

	for (i = 0; i < OV511_MAX_UNIT_VIDEO; i++) {
		/* Minor 0 cannot be specified; assume user wants autodetect */
		if (unit_video[i] == 0)
			break;

		if (video_register_device(&ov511->vdev, VFL_TYPE_GRABBER,
			unit_video[i]) >= 0) {
			registered = 1;
			break;
		}
	}

	/* Use the next available one */
	if (!registered &&
	    video_register_device(&ov511->vdev, VFL_TYPE_GRABBER, -1) < 0) {
		err("video_register_device failed");
		goto error;
	}

	info("Device registered on minor %d", ov511->vdev.minor);

	MOD_DEC_USE_COUNT;
     	return ov511;

error:
	err("Camera initialization failed");

#if defined(CONFIG_PROC_FS) && defined(CONFIG_VIDEO_PROC_FS)
	/* Safe to call even if entry doesn't exist */
	destroy_proc_ov511_cam(ov511);
#endif

	usb_driver_release_interface(&ov511_driver,
		&dev->actconfig->interface[ov511->iface]);

error_dealloc:
	if (ov511) {
		kfree(ov511);
		ov511 = NULL;
	}

error_unlock:
	MOD_DEC_USE_COUNT;
	return NULL;
}


static void
ov51x_disconnect(struct usb_device *dev, void *ptr)
{
	struct usb_ov511 *ov511 = (struct usb_ov511 *) ptr;
	int n;

	MOD_INC_USE_COUNT;

	PDEBUG(3, "");

	/* We don't want people trying to open up the device */
	if (!ov511->user)
		video_unregister_device(&ov511->vdev);
	else
		PDEBUG(3, "Device open...deferring video_unregister_device");

	for (n = 0; n < OV511_NUMFRAMES; n++)
		ov511->frame[n].grabstate = FRAME_ERROR;

	ov511->curframe = -1;

	/* This will cause the process to request another frame */
	for (n = 0; n < OV511_NUMFRAMES; n++)
		if (waitqueue_active(&ov511->frame[n].wq))
			wake_up_interruptible(&ov511->frame[n].wq);
	if (waitqueue_active(&ov511->wq))
		wake_up_interruptible(&ov511->wq);

	ov511->streaming = 0;

	/* Unschedule all of the iso td's */
	for (n = OV511_NUMSBUF - 1; n >= 0; n--) {
		if (ov511->sbuf[n].urb) {
			ov511->sbuf[n].urb->next = NULL;
			usb_unlink_urb(ov511->sbuf[n].urb);
			usb_free_urb(ov511->sbuf[n].urb);
			ov511->sbuf[n].urb = NULL;
		}
	}

#if defined(CONFIG_PROC_FS) && defined(CONFIG_VIDEO_PROC_FS)
        destroy_proc_ov511_cam(ov511);
#endif

	usb_driver_release_interface(&ov511_driver,
		&ov511->dev->actconfig->interface[ov511->iface]);
	ov511->dev = NULL;

	/* Free the memory */
	if (ov511 && !ov511->user) {
		ov511_dealloc(ov511, 1);
		kfree(ov511);
		ov511 = NULL;
	}

	MOD_DEC_USE_COUNT;
}

static struct usb_driver ov511_driver = {
	name:		"ov511",
	id_table:       device_table,
	probe:		ov51x_probe,
	disconnect:	ov51x_disconnect
};


/****************************************************************************
 *
 *  Module routines
 *
 ***************************************************************************/

/* Returns 0 for success */
int 
ov511_register_decomp_module(int ver, struct ov51x_decomp_ops *ops, int ov518,
			     int mmx)
{
	if (ver != DECOMP_INTERFACE_VER) {
		err("Decompression module has incompatible");
		err("interface version %d", ver);
		err("Interface version %d is required", DECOMP_INTERFACE_VER);
		return -EINVAL;
	}

	if (!ops)
		return -EFAULT;

	if (mmx && !ov51x_mmx_available) {
		err("MMX not available on this system or kernel");
		return -EINVAL;
	}

	lock_kernel();

	if (ov518) {
		if (mmx) {
			if (ov518_mmx_decomp_ops)
				goto err_in_use;
			else
				ov518_mmx_decomp_ops = ops;
		} else {
			if (ov518_decomp_ops)
				goto err_in_use;
			else
				ov518_decomp_ops = ops;
		}
	} else {
		if (mmx) {
			if (ov511_mmx_decomp_ops)
				goto err_in_use;
			else
				ov511_mmx_decomp_ops = ops;
		} else {
			if (ov511_decomp_ops)
				goto err_in_use;
			else
				ov511_decomp_ops = ops;
		}
	}

	MOD_INC_USE_COUNT;

	unlock_kernel();
	return 0;

err_in_use:
	unlock_kernel();
	return -EBUSY;
}

void 
ov511_deregister_decomp_module(int ov518, int mmx)
{
	lock_kernel();

	if (ov518) {
		if (mmx)
			ov518_mmx_decomp_ops = NULL;
		else
			ov518_decomp_ops = NULL;
	} else {
		if (mmx)
			ov511_mmx_decomp_ops = NULL;
		else
			ov511_decomp_ops = NULL;
	}
	
	MOD_DEC_USE_COUNT;

	unlock_kernel();
}

static int __init 
usb_ov511_init(void)
{
#if defined(CONFIG_PROC_FS) && defined(CONFIG_VIDEO_PROC_FS)
        proc_ov511_create();
#endif

	if (usb_register(&ov511_driver) < 0)
		return -1;

	// FIXME: Don't know how to determine this yet
	ov51x_mmx_available = 0;

#if defined (__i386__)
	if (test_bit(X86_FEATURE_MMX, &boot_cpu_data.x86_capability))
		ov51x_mmx_available = 1;
#endif

	info(DRIVER_VERSION " : " DRIVER_DESC);

	return 0;
}

static void __exit 
usb_ov511_exit(void)
{
	usb_deregister(&ov511_driver);
	info("driver deregistered");

#if defined(CONFIG_PROC_FS) && defined(CONFIG_VIDEO_PROC_FS)
        proc_ov511_destroy();
#endif
}

module_init(usb_ov511_init);
module_exit(usb_ov511_exit);

/* No version, for compatibility with binary-only modules */
EXPORT_SYMBOL_NOVERS(ov511_register_decomp_module);
EXPORT_SYMBOL_NOVERS(ov511_deregister_decomp_module);
