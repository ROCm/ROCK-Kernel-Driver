/*
 * $Id$
 *
 * konicawc.c - konica webcam driver
 *
 * Author: Simon Evans <spse@secret.org.uk>
 *
 * Copyright (C) 2002 Simon Evans
 *
 * Licence: GPL
 * 
 * Driver for USB webcams based on Konica chipset. This
 * chipset is used in Intel YC76 camera.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include "usbvideo.h"

#define MAX_BRIGHTNESS	108
#define MAX_CONTRAST	108
#define MAX_SATURATION	108
#define MAX_SHARPNESS	108
#define MAX_WHITEBAL	372
#define MAX_SPEED	6
#define MAX_CAMERAS	1

#define DRIVER_VERSION	"v1.1"
#define DRIVER_DESC	"Konica Webcam driver"

enum ctrl_req {
	SetWhitebal	= 0x01,
	SetBrightness	= 0x02,
        SetSharpness	= 0x03,
	SetContrast	= 0x04,
	SetSaturation	= 0x05,
};


enum frame_sizes {
	SIZE_160X136	= 0,
	SIZE_176X144	= 1,
	SIZE_320X240	= 2,
};


static usbvideo_t *cams;

/* Some default values for inital camera settings,
   can be set by modprobe */

static int debug;
static enum frame_sizes size;	
static int speed = 6;		/* Speed (fps) 0 (slowest) to 6 (fastest) */
static int brightness =	MAX_BRIGHTNESS/2;
static int contrast =	MAX_CONTRAST/2;
static int saturation =	MAX_SATURATION/2;
static int sharpness =	MAX_SHARPNESS/2;
static int whitebal =	3*(MAX_WHITEBAL/4);

static int speed_to_interface[] = { 1, 0, 3, 2, 4, 5, 6 };

/* These FPS speeds are from the windows config box. They are
 * indexed on size (0-2) and speed (0-6). Divide by 3 to get the
 * real fps.
 */

static int speed_to_fps[3][7] = { { 24, 40, 48, 60, 72, 80, 100 },
				  { 18, 30, 36, 45, 54, 60, 75  },
				  { 6,  10, 12, 15, 18, 20, 25  } };


static int camera_sizes[][2] = { { 160, 136 },
				 { 176, 144 },
				 { 320, 240 },
				 { } /* List terminator */
};

struct konicawc {
	u8 brightness;		/* camera uses 0 - 9, x11 for real value */
	u8 contrast;		/* as above */
	u8 saturation;		/* as above */
	u8 sharpness;		/* as above */
	u8 white_bal;		/* 0 - 33, x11 for real value */
	u8 speed;		/* Stored as 0 - 6, used as index in speed_to_* (above) */
	u8 size;		/* Frame Size */
	int height;
	int width;
	struct urb *sts_urb[USBVIDEO_NUMSBUF];
	u8 sts_buf[USBVIDEO_NUMSBUF][FRAMES_PER_DESC];
	struct urb *last_data_urb;
	int lastframe;
};


#define konicawc_set_misc(uvd, req, value, index)		konicawc_ctrl_msg(uvd, USB_DIR_OUT, req, value, index, NULL, 0)
#define konicawc_get_misc(uvd, req, value, index, buf, sz)	konicawc_ctrl_msg(uvd, USB_DIR_IN, req, value, index, buf, sz)
#define konicawc_set_value(uvd, value, index)			konicawc_ctrl_msg(uvd, USB_DIR_OUT, 2, value, index, NULL, 0)


static int konicawc_ctrl_msg(uvd_t *uvd, u8 dir, u8 request, u16 value, u16 index, void *buf, int len)
{
        int retval = usb_control_msg(uvd->dev,
		dir ? usb_rcvctrlpipe(uvd->dev, 0) : usb_sndctrlpipe(uvd->dev, 0),
		    request, 0x40 | dir, value, index, buf, len, HZ);
        return retval < 0 ? retval : 0;
}


static int konicawc_setup_on_open(uvd_t *uvd)
{
	struct konicawc *cam = (struct konicawc *)uvd->user_data;

	konicawc_set_misc(uvd, 0x2, 0, 0x0b);
	dbg("setting brightness to %d (%d)", cam->brightness,
	    cam->brightness * 11);
	konicawc_set_value(uvd, cam->brightness, SetBrightness);
	dbg("setting white balance to %d (%d)", cam->white_bal,
	    cam->white_bal * 11);
	konicawc_set_value(uvd, cam->white_bal, SetWhitebal);
	dbg("setting contrast to %d (%d)", cam->contrast,
	    cam->contrast * 11);
	konicawc_set_value(uvd, cam->contrast, SetContrast);
	dbg("setting saturation to %d (%d)", cam->saturation,
	    cam->saturation * 11);
	konicawc_set_value(uvd, cam->saturation, SetSaturation);
	dbg("setting sharpness to %d (%d)", cam->sharpness,
	    cam->sharpness * 11);
	konicawc_set_value(uvd, cam->sharpness, SetSharpness);
	dbg("setting size %d", cam->size);
	switch(cam->size) {
	case 0:
		konicawc_set_misc(uvd, 0x2, 0xa, 0x08);
		break;

	case 1:
		konicawc_set_misc(uvd, 0x2, 4, 0x08);
		break;

	case 2:
		konicawc_set_misc(uvd, 0x2, 5, 0x08);
		break;
	}
	konicawc_set_misc(uvd, 0x2, 1, 0x0b);
	cam->lastframe = -1;
	return 0;
}


static void konicawc_adjust_picture(uvd_t *uvd)
{
	struct konicawc *cam = (struct konicawc *)uvd->user_data;

	dbg("new brightness: %d", uvd->vpic.brightness);
	uvd->vpic.brightness = (uvd->vpic.brightness > MAX_BRIGHTNESS) ? MAX_BRIGHTNESS : uvd->vpic.brightness;
	if(cam->brightness != uvd->vpic.brightness / 11) {
	   cam->brightness = uvd->vpic.brightness / 11;
	   dbg("setting brightness to %d (%d)", cam->brightness,
	       cam->brightness * 11);
	   konicawc_set_value(uvd, cam->brightness, SetBrightness);
	}

	dbg("new contrast: %d", uvd->vpic.contrast);
	uvd->vpic.contrast = (uvd->vpic.contrast > MAX_CONTRAST) ? MAX_CONTRAST : uvd->vpic.contrast;
	if(cam->contrast != uvd->vpic.contrast / 11) {
		cam->contrast = uvd->vpic.contrast / 11;
		dbg("setting contrast to %d (%d)", cam->contrast,
		    cam->contrast * 11);
		konicawc_set_value(uvd, cam->contrast, SetContrast);
	}
}


static int konicawc_compress_iso(uvd_t *uvd, struct urb *dataurb, struct urb *stsurb)
{
	char *cdata;
	int i, totlen = 0;
	unsigned char *status = stsurb->transfer_buffer;
	int keep = 0, discard = 0, bad = 0;
	static int buttonsts = 0;

	for (i = 0; i < dataurb->number_of_packets; i++) {
		int button = buttonsts;
		unsigned char sts;
		int n = dataurb->iso_frame_desc[i].actual_length;
		int st = dataurb->iso_frame_desc[i].status;
		cdata = dataurb->transfer_buffer + 
			dataurb->iso_frame_desc[i].offset;

		/* Detect and ignore errored packets */
		if (st < 0) {
			if (debug >= 1)
				err("Data error: packet=%d. len=%d. status=%d.",
				    i, n, st);
			uvd->stats.iso_err_count++;
			continue;
		}

		/* Detect and ignore empty packets */
		if (n <= 0) {
			uvd->stats.iso_skip_count++;
			continue;
		}

		/* See what the status data said about the packet */
		sts = *(status+stsurb->iso_frame_desc[i].offset);

		/* sts: 0x80-0xff: frame start with frame number (ie 0-7f)
		 * otherwise:
		 * bit 0 0:drop packet (padding data)
		 *	 1 keep packet
		 *
		 * bit 4 0 button not clicked
		 *       1 button clicked
		 * button is used to `take a picture' (in software)
		 */

		if(sts < 0x80) {
			button = sts & 0x40;
			sts &= ~0x40;
		}
		
		/* work out the button status, but dont do
		   anything with it for now */
		   
		if(button != buttonsts) {
			dbg("button: %sclicked", button ? "" : "un");
			buttonsts = button;
		}

		if(sts == 0x01) { /* drop frame */
			discard++;
			continue;
		}
		
		if((sts > 0x01) && (sts < 0x80)) {
			info("unknown status %2.2x", sts);
			bad++;
			continue;
		}

		keep++;
		if(*(status+i) & 0x80) { /* frame start */
			unsigned char marker[] = { 0, 0xff, 0, 0x00 };
			if(debug > 1)
				dbg("Adding Marker packet = %d, frame = %2.2x",
				    i, *(status+i));
			marker[3] = *(status+i) - 0x80;
			RingQueue_Enqueue(&uvd->dp, marker, 4);			
			totlen += 4;
		}
		totlen += n;	/* Little local accounting */
		if(debug > 5)
			dbg("Adding packet %d, bytes = %d", i, n);
		RingQueue_Enqueue(&uvd->dp, cdata, n);

	}
	if(debug > 8) {
		dbg("finished: keep = %d discard = %d bad = %d added %d bytes",
		    keep, discard, bad, totlen);
	}
	return totlen;
}


static void konicawc_isoc_irq(struct urb *urb)
{
	int i, ret, len = 0;
	uvd_t *uvd = urb->context;
	struct konicawc *cam = (struct konicawc *)uvd->user_data;

	/* We don't want to do anything if we are about to be removed! */
	if (!CAMERA_IS_OPERATIONAL(uvd))
		return;

	if (!uvd->streaming) {
		if (debug >= 1)
			info("Not streaming, but interrupt!");
		return;
	}

	if (urb->actual_length > 32) {
		cam->last_data_urb = urb;
		goto urb_done_with;
	}

	uvd->stats.urb_count++;
	if (urb->actual_length <= 0)
		goto urb_done_with;

	/* Copy the data received into ring queue */
	if(cam->last_data_urb) {
		len = konicawc_compress_iso(uvd, cam->last_data_urb, urb);
		for (i = 0; i < FRAMES_PER_DESC; i++) {
			cam->last_data_urb->iso_frame_desc[i].status = 0;
		}
		cam->last_data_urb = NULL;
	}
	uvd->stats.urb_length = len;
	uvd->stats.data_count += len;
	if(len)
		RingQueue_WakeUpInterruptible(&uvd->dp);

urb_done_with:
	for (i = 0; i < FRAMES_PER_DESC; i++) {
		urb->iso_frame_desc[i].status = 0;
	}
	urb->dev = uvd->dev;
	urb->status = 0;
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if(ret)
		err("usb_submit_urb error (%d)", ret);
	return;
}


static int konicawc_start_data(uvd_t *uvd)
{
	struct usb_device *dev = uvd->dev;
	int i, errFlag;
	struct konicawc *cam = (struct konicawc *)uvd->user_data;

	if (!CAMERA_IS_OPERATIONAL(uvd)) {
		err("Camera is not operational");
		return -EFAULT;
	}
	uvd->curframe = -1;

	/* Alternate interface 1 is is the biggest frame size */
	i = usb_set_interface(dev, uvd->iface, uvd->ifaceAltActive);
	if (i < 0) {
		err("usb_set_interface error");
		uvd->last_error = i;
		return -EBUSY;
	}

	/* We double buffer the Iso lists */
	for (i=0; i < USBVIDEO_NUMSBUF; i++) {
		int j, k;
		struct urb *urb = uvd->sbuf[i].urb;
		urb->dev = dev;
		urb->context = uvd;
		urb->pipe = usb_rcvisocpipe(dev, uvd->video_endp);
		urb->interval = 1;
		urb->transfer_flags = USB_ISO_ASAP;
		urb->transfer_buffer = uvd->sbuf[i].data;
		urb->complete = konicawc_isoc_irq;
		urb->number_of_packets = FRAMES_PER_DESC;
		urb->transfer_buffer_length = uvd->iso_packet_len * FRAMES_PER_DESC;
		for (j=k=0; j < FRAMES_PER_DESC; j++, k += uvd->iso_packet_len) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length = uvd->iso_packet_len;
		}

		urb = cam->sts_urb[i];
		urb->dev = dev;
		urb->context = uvd;
		urb->pipe = usb_rcvisocpipe(dev, uvd->video_endp-1);
		urb->interval = 1;
		urb->transfer_flags = USB_ISO_ASAP;
		urb->transfer_buffer = cam->sts_buf[i];
		urb->complete = konicawc_isoc_irq;
		urb->number_of_packets = FRAMES_PER_DESC;
		urb->transfer_buffer_length = FRAMES_PER_DESC;
		for (j=0; j < FRAMES_PER_DESC; j++) {
			urb->iso_frame_desc[j].offset = j;
			urb->iso_frame_desc[j].length = 1;
		}
	}

	cam->last_data_urb = NULL;
	
	/* Submit all URBs */
	for (i=0; i < USBVIDEO_NUMSBUF; i++) {
		errFlag = usb_submit_urb(uvd->sbuf[i].urb, GFP_KERNEL);
		if (errFlag)
			err ("usb_submit_isoc(%d) ret %d", i, errFlag);

		errFlag = usb_submit_urb(cam->sts_urb[i], GFP_KERNEL);
		if (errFlag)
			err("usb_submit_isoc(%d) ret %d", i, errFlag);
	}

	uvd->streaming = 1;
	if (debug > 1)
		dbg("streaming=1 video_endp=$%02x", uvd->video_endp);
	return 0;
}


static void konicawc_stop_data(uvd_t *uvd)
{
	int i, j;
	struct konicawc *cam;

	if ((uvd == NULL) || (!uvd->streaming) || (uvd->dev == NULL))
		return;

	cam = (struct konicawc *)uvd->user_data;
	cam->last_data_urb = NULL;

	/* Unschedule all of the iso td's */
	for (i=0; i < USBVIDEO_NUMSBUF; i++) {
		j = usb_unlink_urb(uvd->sbuf[i].urb);
		if (j < 0)
			err("usb_unlink_urb() error %d.", j);

		j = usb_unlink_urb(cam->sts_urb[i]);
		if (j < 0)
			err("usb_unlink_urb() error %d.", j);
	}

	uvd->streaming = 0;

	if (!uvd->remove_pending) {
		/* Set packet size to 0 */
		j = usb_set_interface(uvd->dev, uvd->iface, uvd->ifaceAltInactive);
		if (j < 0) {
			err("usb_set_interface() error %d.", j);
			uvd->last_error = j;
		}
	}
}


static void konicawc_process_isoc(uvd_t *uvd, usbvideo_frame_t *frame)
{	
	int n;
	int maxline, yplanesz;
	struct konicawc *cam = (struct konicawc *)uvd->user_data;
	assert(uvd != NULL);
	assert(frame != NULL);

	maxline = (cam->height * cam->width * 3) / (2 * 384);
	yplanesz = cam->height * cam->width;
	if(debug > 5)
		dbg("maxline = %d yplanesz = %d", maxline, yplanesz);
	
	if(debug > 3)
		dbg("Frame state = %d", frame->scanstate);

	if(frame->scanstate == ScanState_Scanning) {
		int drop = 0;
		int curframe;
		int fdrops = 0;
		if(debug > 3)
			dbg("Searching for marker, queue len = %d", RingQueue_GetLength(&uvd->dp));
		while(RingQueue_GetLength(&uvd->dp) >= 4) {
			if ((RING_QUEUE_PEEK(&uvd->dp, 0) == 0x00) &&
			    (RING_QUEUE_PEEK(&uvd->dp, 1) == 0xff) &&
			    (RING_QUEUE_PEEK(&uvd->dp, 2) == 0x00) &&
			    (RING_QUEUE_PEEK(&uvd->dp, 3) < 0x80)) {
				curframe = RING_QUEUE_PEEK(&uvd->dp, 3);
				if(cam->lastframe != -1) {
					if(curframe < cam->lastframe) {
						fdrops = (curframe + 0x80) - cam->lastframe;
					} else {
						fdrops = curframe - cam->lastframe;
					}
					fdrops--;
					if(fdrops)
						info("Dropped %d frames (%d -> %d)", fdrops,
						     cam->lastframe, curframe);
				}
				cam->lastframe = curframe;
				frame->curline = 0;
				frame->scanstate = ScanState_Lines;
				RING_QUEUE_DEQUEUE_BYTES(&uvd->dp, 4);
				break;
			}
			RING_QUEUE_DEQUEUE_BYTES(&uvd->dp, 1);
			drop++;
		}
	}

	if(frame->scanstate == ScanState_Scanning)
		return;
		
	/* Try to move data from queue into frame buffer 
	 * We get data in blocks of 384 bytes made up of:
	 * 256 Y, 64 U, 64 V.
	 * This needs to be written out as a Y plane, a U plane and a V plane.
	 */
		
	while ( frame->curline < maxline && (n = RingQueue_GetLength(&uvd->dp)) >= 384) {
		/* Y */
		RingQueue_Dequeue(&uvd->dp, frame->data + (frame->curline * 256), 256);
		/* U */
		RingQueue_Dequeue(&uvd->dp, frame->data + yplanesz + (frame->curline * 64), 64);
		/* V */
		RingQueue_Dequeue(&uvd->dp, frame->data + (5 * yplanesz)/4 + (frame->curline * 64), 64);
		frame->seqRead_Length += 384;
		frame->curline++;
	}
	/* See if we filled the frame */
	if (frame->curline == maxline) {
		if(debug > 5)
			dbg("got whole frame");

		frame->frameState = FrameState_Done_Hold;
		frame->curline = 0;
		uvd->curframe = -1;
		uvd->stats.frame_num++;
	}
}


static int konicawc_calculate_fps(uvd_t *uvd)
{
	struct konicawc *t = uvd->user_data;
	dbg("fps = %d", speed_to_fps[t->size][t->speed]/3);

	return speed_to_fps[t->size][t->speed]/3;
}


static void konicawc_configure_video(uvd_t *uvd)
{
	struct konicawc *cam = (struct konicawc *)uvd->user_data;
	u8 buf[2];

	memset(&uvd->vpic, 0, sizeof(uvd->vpic));
	memset(&uvd->vpic_old, 0x55, sizeof(uvd->vpic_old));

	RESTRICT_TO_RANGE(brightness, 0, MAX_BRIGHTNESS);
	RESTRICT_TO_RANGE(contrast, 0, MAX_CONTRAST);
	RESTRICT_TO_RANGE(saturation, 0, MAX_SATURATION);
	RESTRICT_TO_RANGE(sharpness, 0, MAX_SHARPNESS);
	RESTRICT_TO_RANGE(whitebal, 0, MAX_WHITEBAL);

	cam->brightness = brightness / 11;
	cam->contrast = contrast / 11;
	cam->saturation = saturation / 11;
	cam->sharpness = sharpness / 11;
	cam->white_bal = whitebal / 11;

	uvd->vpic.colour = 108;
	uvd->vpic.hue = 108;
	uvd->vpic.brightness = brightness;
	uvd->vpic.contrast = contrast;
	uvd->vpic.whiteness = whitebal;
	uvd->vpic.depth = 6;
	uvd->vpic.palette = VIDEO_PALETTE_YUV420P;

	memset(&uvd->vcap, 0, sizeof(uvd->vcap));
	strcpy(uvd->vcap.name, "Konica Webcam");
	uvd->vcap.type = VID_TYPE_CAPTURE;
	uvd->vcap.channels = 1;
	uvd->vcap.audios = 0;
	uvd->vcap.minwidth = camera_sizes[cam->size][0];
	uvd->vcap.minheight = camera_sizes[cam->size][1];
	uvd->vcap.maxwidth = camera_sizes[cam->size][0];
	uvd->vcap.maxheight = camera_sizes[cam->size][1];

	memset(&uvd->vchan, 0, sizeof(uvd->vchan));
	uvd->vchan.flags = 0 ;
	uvd->vchan.tuners = 0;
	uvd->vchan.channel = 0;
	uvd->vchan.type = VIDEO_TYPE_CAMERA;
	strcpy(uvd->vchan.name, "Camera");

	/* Talk to device */
	dbg("device init");
	if(!konicawc_get_misc(uvd, 0x3, 0, 0x10, buf, 2))
		dbg("3,10 -> %2.2x %2.2x", buf[0], buf[1]);
	if(!konicawc_get_misc(uvd, 0x3, 0, 0x10, buf, 2))
		dbg("3,10 -> %2.2x %2.2x", buf[0], buf[1]);
	if(konicawc_set_misc(uvd, 0x2, 0, 0xd))
		dbg("2,0,d failed");
	dbg("setting initial values");

}


static void *konicawc_probe(struct usb_device *dev, unsigned int ifnum, const struct usb_device_id *devid)
{
	uvd_t *uvd = NULL;
	int i, nas;
	int actInterface=-1, inactInterface=-1, maxPS=0;
	unsigned char video_ep = 0;
	
	if (debug >= 1)
		dbg("konicawc_probe(%p,%u.)", dev, ifnum);

	/* We don't handle multi-config cameras */
	if (dev->descriptor.bNumConfigurations != 1)
		return NULL;

	info("Konica Webcam (rev. 0x%04x)", dev->descriptor.bcdDevice);
	RESTRICT_TO_RANGE(speed, 0, MAX_SPEED);

	/* Validate found interface: must have one ISO endpoint */
	nas = dev->actconfig->interface[ifnum].num_altsetting;
	if (debug > 0)
		info("Number of alternate settings=%d.", nas);
	if (nas < 8) {
		err("Too few alternate settings for this camera!");
		return NULL;
	}
	/* Validate all alternate settings */
	for (i=0; i < nas; i++) {
		const struct usb_interface_descriptor *interface;
		const struct usb_endpoint_descriptor *endpoint;

		interface = &dev->actconfig->interface[ifnum].altsetting[i];
		if (interface->bNumEndpoints != 2) {
			err("Interface %d. has %u. endpoints!",
			    ifnum, (unsigned)(interface->bNumEndpoints));
			return NULL;
		}
		endpoint = &interface->endpoint[1];
		dbg("found endpoint: addr: 0x%2.2x maxps = 0x%4.4x",
		    endpoint->bEndpointAddress, endpoint->wMaxPacketSize);
		if (video_ep == 0)
			video_ep = endpoint->bEndpointAddress;
		else if (video_ep != endpoint->bEndpointAddress) {
			err("Alternate settings have different endpoint addresses!");
			return NULL;
		}
		if ((endpoint->bmAttributes & 0x03) != 0x01) {
			err("Interface %d. has non-ISO endpoint!", ifnum);
			return NULL;
		}
		if ((endpoint->bEndpointAddress & 0x80) == 0) {
			err("Interface %d. has ISO OUT endpoint!", ifnum);
			return NULL;
		}
		if (endpoint->wMaxPacketSize == 0) {
			if (inactInterface < 0)
				inactInterface = i;
			else {
				err("More than one inactive alt. setting!");
				return NULL;
			}
		} else {
			if (i == speed_to_interface[speed]) {
				/* This one is the requested one */
				actInterface = i;
				maxPS = endpoint->wMaxPacketSize;
				if (debug > 0) {
					info("Selecting requested active setting=%d. maxPS=%d.",
					     i, maxPS);
				}
			}
		}
	}
	if(actInterface == -1) {
		err("Cant find required endpoint");
		return NULL;
	}


	/* Code below may sleep, need to lock module while we are here */
	MOD_INC_USE_COUNT;
	uvd = usbvideo_AllocateDevice(cams);
	if (uvd != NULL) {
		struct konicawc *cam = (struct konicawc *)(uvd->user_data);
		/* Here uvd is a fully allocated uvd_t object */
		for(i = 0; i < USBVIDEO_NUMSBUF; i++) {
			cam->sts_urb[i] = usb_alloc_urb(FRAMES_PER_DESC, GFP_KERNEL);
			if(cam->sts_urb[i] == NULL) {
				while(i--) {
					usb_free_urb(cam->sts_urb[i]);
				}
				err("cant allocate urbs");
				return NULL;
			}
		}
		cam->speed = speed;
		switch(size) {
		case SIZE_160X136:
		default:
			cam->height = 136;
			cam->width = 160;
			cam->size = SIZE_160X136;
			break;

		case SIZE_176X144:
			cam->height = 144;
			cam->width = 176;
			cam->size = SIZE_176X144;
			break;

		case SIZE_320X240:
			cam->height = 240;
			cam->width = 320;
			cam->size = SIZE_320X240;
			break;
		}

		uvd->flags = 0;
		uvd->debug = debug;
		uvd->dev = dev;
		uvd->iface = ifnum;
		uvd->ifaceAltInactive = inactInterface;
		uvd->ifaceAltActive = actInterface;
		uvd->video_endp = video_ep;
		uvd->iso_packet_len = maxPS;
		uvd->paletteBits = 1L << VIDEO_PALETTE_YUV420P;
		uvd->defaultPalette = VIDEO_PALETTE_YUV420P;
		uvd->canvas = VIDEOSIZE(cam->width, cam->height);
		uvd->videosize = uvd->canvas;

		/* Initialize konicawc specific data */
		konicawc_configure_video(uvd);

		i = usbvideo_RegisterVideoDevice(uvd);
		uvd->max_frame_size = (cam->width * cam->height * 3)/2;
		if (i != 0) {
			err("usbvideo_RegisterVideoDevice() failed.");
			uvd = NULL;
		}
	}
	MOD_DEC_USE_COUNT;
	return uvd;
}


static void konicawc_free_uvd(uvd_t *uvd)
{
	int i;
	struct konicawc *cam = (struct konicawc *)uvd->user_data;

	for (i=0; i < USBVIDEO_NUMSBUF; i++) {
		usb_free_urb(cam->sts_urb[i]);
		cam->sts_urb[i] = NULL;
	}
}


static struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x04c8, 0x0720) }, /* Intel YC 76 */
	{ }  /* Terminating entry */
};


static int __init konicawc_init(void)
{
	usbvideo_cb_t cbTbl;
	info(DRIVER_DESC " " DRIVER_VERSION);
	memset(&cbTbl, 0, sizeof(cbTbl));
	cbTbl.probe = konicawc_probe;
	cbTbl.setupOnOpen = konicawc_setup_on_open;
	cbTbl.processData = konicawc_process_isoc;
	cbTbl.getFPS = konicawc_calculate_fps;
	cbTbl.startDataPump = konicawc_start_data;
	cbTbl.stopDataPump = konicawc_stop_data;
	cbTbl.adjustPicture = konicawc_adjust_picture;
	cbTbl.userFree = konicawc_free_uvd;
	return usbvideo_register(
		&cams,
		MAX_CAMERAS,
		sizeof(struct konicawc),
		"konicawc",
		&cbTbl,
		THIS_MODULE,
		id_table);
}


static void __exit konicawc_cleanup(void)
{
	usbvideo_Deregister(&cams);
}


MODULE_DEVICE_TABLE(usb, id_table);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simon Evans <spse@secret.org.uk>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_PARM(speed, "i");
MODULE_PARM_DESC(speed, "FPS speed: 0 (slowest) - 6 (fastest)");
MODULE_PARM(size, "i");
MODULE_PARM_DESC(size, "Frame Size 0: 160x136 1: 176x144 2: 320x240");
MODULE_PARM(brightness, "i");
MODULE_PARM_DESC(brightness, "Initial brightness 0 - 108");
MODULE_PARM(contrast, "i");
MODULE_PARM_DESC(contrast, "Initial contrast 0 - 108");
MODULE_PARM(saturation, "i");
MODULE_PARM_DESC(saturation, "Initial saturation 0 - 108");
MODULE_PARM(sharpness, "i");
MODULE_PARM_DESC(sharpness, "Initial brightness 0 - 108");
MODULE_PARM(whitebal, "i");
MODULE_PARM_DESC(whitebal, "Initial white balance 0 - 363");
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug level: 0-9 (default=0)");
module_init(konicawc_init);
module_exit(konicawc_cleanup);
