/*
 * Header file for USB IBM C-It Video Camera driver.
 *
 * Supports IBM C-It Video Camera.
 *
 * This driver is based on earlier work of:
 *
 * (C) Copyright 1999 Johannes Erdfelt
 * (C) Copyright 1999 Randy Dunlap
 */

#ifndef __LINUX_IBMCAM_H
#define __LINUX_IBMCAM_H

#include <linux/list.h>

#define USES_IBMCAM_PUTPIXEL    0       /* 0=Fast/oops 1=Slow/secure */

/* Video Size 384 x 288 x 3 bytes for RGB */
/* 384 because xawtv tries to grab 384 even though we tell it 352 is our max */
#define V4L_FRAME_WIDTH         384
#define V4L_FRAME_WIDTH_USED	352
#define V4L_FRAME_HEIGHT        288
#define V4L_BYTES_PER_PIXEL     3
#define MAX_FRAME_SIZE          (V4L_FRAME_WIDTH * V4L_FRAME_HEIGHT * V4L_BYTES_PER_PIXEL)

/* Camera capabilities (maximum) */
#define CAMERA_IMAGE_WIDTH      352
#define CAMERA_IMAGE_HEIGHT     288
#define CAMERA_IMAGE_LINE_SZ    ((CAMERA_IMAGE_WIDTH * 3) / 2) /* Bytes */
#define CAMERA_URB_FRAMES       32
#define CAMERA_MAX_ISO_PACKET   1023 /* 1022 actually sent by camera */

#define IBMCAM_NUMFRAMES	2
#define IBMCAM_NUMSBUF		2

#define FRAMES_PER_DESC		(CAMERA_URB_FRAMES)
#define FRAME_SIZE_PER_DESC	(CAMERA_MAX_ISO_PACKET)

/* This macro restricts an int variable to an inclusive range */
#define RESTRICT_TO_RANGE(v,mi,ma) { if ((v) < (mi)) (v) = (mi); else if ((v) > (ma)) (v) = (ma); }

/*
 * This macro performs bounds checking - use it when working with
 * new formats, or else you may get oopses all over the place.
 * If pixel falls out of bounds then it gets shoved back (as close
 * to place of offence as possible) and is painted bright red.
 */
#define IBMCAM_PUTPIXEL(fr, ix, iy, vr, vg, vb) { \
	register unsigned char *pf; \
	int limiter = 0, mx, my; \
	mx = ix; \
	my = iy; \
	if (mx < 0) { \
		mx=0; \
		limiter++; \
	} else if (mx >= 352) { \
		mx=351; \
		limiter++; \
	} \
	if (my < 0) { \
		my = 0; \
		limiter++; \
	} else if (my >= V4L_FRAME_HEIGHT) { \
		my = V4L_FRAME_HEIGHT - 1; \
		limiter++; \
	} \
	pf = (fr)->data + V4L_BYTES_PER_PIXEL*((iy)*352 + (ix)); \
	if (limiter) { \
		*pf++ = 0; \
		*pf++ = 0; \
		*pf++ = 0xFF; \
	} else { \
		*pf++ = (vb); \
		*pf++ = (vg); \
		*pf++ = (vr); \
	} \
}

/*
 * We use macros to do YUV -> RGB conversion because this is
 * very important for speed and totally unimportant for size.
 *
 * YUV -> RGB Conversion
 * ---------------------
 *
 * B = 1.164*(Y-16)		    + 2.018*(V-128)
 * G = 1.164*(Y-16) - 0.813*(U-128) - 0.391*(V-128)
 * R = 1.164*(Y-16) + 1.596*(U-128)
 *
 * If you fancy integer arithmetics (as you should), hear this:
 *
 * 65536*B = 76284*(Y-16)		  + 132252*(V-128)
 * 65536*G = 76284*(Y-16) -  53281*(U-128) -  25625*(V-128)
 * 65536*R = 76284*(Y-16) + 104595*(U-128)
 *
 * Make sure the output values are within [0..255] range.
 */
#define LIMIT_RGB(x) (((x) < 0) ? 0 : (((x) > 255) ? 255 : (x)))
#define YUV_TO_RGB_BY_THE_BOOK(my,mu,mv,mr,mg,mb) { \
    int mm_y, mm_yc, mm_u, mm_v, mm_r, mm_g, mm_b; \
    mm_y = (my) - 16;  \
    mm_u = (mu) - 128; \
    mm_v = (mv) - 128; \
    mm_yc= mm_y * 76284; \
    mm_b = (mm_yc		+ 132252*mm_v	) >> 16; \
    mm_g = (mm_yc -  53281*mm_u -  25625*mm_v	) >> 16; \
    mm_r = (mm_yc + 104595*mm_u			) >> 16; \
    mb = LIMIT_RGB(mm_b); \
    mg = LIMIT_RGB(mm_g); \
    mr = LIMIT_RGB(mm_r); \
}

/* Debugging aid */
#define IBMCAM_SAY_AND_WAIT(what) { \
	wait_queue_head_t wq; \
	init_waitqueue_head(&wq); \
	printk(KERN_INFO "Say: %s\n", what); \
	interruptible_sleep_on_timeout (&wq, HZ*3); \
}

/*
 * This macro checks if ibmcam is still operational. The 'ibmcam'
 * pointer must be valid, ibmcam->dev must be valid, we are not
 * removing the device and the device has not erred on us.
 */
#define IBMCAM_IS_OPERATIONAL(ibm_cam) (\
	(ibm_cam != NULL) && \
	((ibm_cam)->dev != NULL) && \
	((ibm_cam)->last_error == 0) && \
	(!(ibm_cam)->remove_pending))

enum {
	STATE_SCANNING,		/* Scanning for header */
	STATE_LINES,		/* Parsing lines */
};

enum {
	FRAME_UNUSED,		/* Unused (no MCAPTURE) */
	FRAME_READY,		/* Ready to start grabbing */
	FRAME_GRABBING,		/* In the process of being grabbed into */
	FRAME_DONE,		/* Finished grabbing, but not been synced yet */
	FRAME_ERROR,		/* Something bad happened while processing */
};

struct usb_device;

struct ibmcam_sbuf {
	char *data;
	urb_t *urb;
};

struct ibmcam_frame {
	char *data;		/* Frame buffer */
	int order_uv;		/* True=UV False=VU */
	int order_yc;		/* True=Yc False=cY ('c'=either U or V) */
	unsigned char hdr_sig;	/* "00 FF 00 ??" where 'hdr_sig' is '??' */

	int width;		/* Width application is expecting */
	int height;		/* Height */

	int frmwidth;		/* Width the frame actually is */
	int frmheight;		/* Height */

	volatile int grabstate;	/* State of grabbing */
	int scanstate;		/* State of scanning */

	int curline;		/* Line of frame we're working on */

	long scanlength;	/* uncompressed, raw data length of frame */
	long bytes_read;	/* amount of scanlength that has been read from *data */

	wait_queue_head_t wq;	/* Processes waiting */
};

#define	IBMCAM_MODEL_1	1	/* XVP-501, 3 interfaces, rev. 0.02 */
#define IBMCAM_MODEL_2	2	/* KSX-X9903, 2 interfaces, rev. 3.0a */

struct usb_ibmcam {
	struct video_device vdev;

	/* Device structure */
	struct usb_device *dev;

	unsigned char iface;                            /* Video interface number */
	unsigned char ifaceAltActive, ifaceAltInactive; /* Alt settings */

	struct semaphore lock;
	int user;		/* user count for exclusive use */

	int ibmcam_used;        /* Is this structure in use? */
	int initialized;	/* Had we already sent init sequence? */
	int camera_model;	/* What type of IBM camera we got? */
	int streaming;		/* Are we streaming Isochronous? */
	int grabbing;		/* Are we grabbing? */
	int last_error;		/* What calamity struck us? */

	int compress;		/* Should the next frame be compressed? */

	char *fbuf;		/* Videodev buffer area */
	int fbuf_size;		/* Videodev buffer size */

	int curframe;
	struct ibmcam_frame frame[IBMCAM_NUMFRAMES];	/* Double buffering */

	int cursbuf;		/* Current receiving sbuf */
	struct ibmcam_sbuf sbuf[IBMCAM_NUMSBUF];	/* Double buffering */
	volatile int remove_pending;	/* If set then about to exit */

        /*
	 * Scratch space from the Isochronous pipe.
	 * Scratch buffer should contain at least one pair of lines
	 * (CAMERA_IMAGE_LINE_SZ). We set it to two pairs here.
	 * This will be approximately 2 KB. HOWEVER in reality this
	 * buffer must be as large as hundred of KB because otherwise
	 * you'll get lots of overflows because V4L client may request
	 * frames not as uniformly as USB sources them.
	 */
	unsigned char *scratch;
	int scratchlen;

	struct video_picture vpic, vpic_old;	/* Picture settings */
	struct video_capability vcap;		/* Video capabilities */
	struct video_channel vchan;	/* May be used for tuner support */
	unsigned char video_endp;	/* 0x82 for IBM camera */
        int has_hdr;
        int frame_num;
	int iso_packet_len;		/* Videomode-dependent, saves bus bandwidth */

	/* Statistics that can be overlayed on screen */
        unsigned long urb_count;        /* How many URBs we received so far */
        unsigned long urb_length;       /* Length of last URB */
        unsigned long data_count;       /* How many bytes we received */
        unsigned long header_count;     /* How many frame headers we found */
	unsigned long scratch_ovf_count;/* How many times we overflowed scratch */
	unsigned long iso_skip_count;	/* How many empty ISO packets received */
	unsigned long iso_err_count;	/* How many bad ISO packets received */
};

#endif /* __LINUX_IBMCAM_H */
