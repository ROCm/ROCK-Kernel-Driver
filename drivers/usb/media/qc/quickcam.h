#ifndef _LINUX_QUICKCAM_H
#define _LINUX_QUICKCAM_H

/* {{{ [fold] SECTION: common definitions with userspace applications */
#define QC_LUT_SIZE	(3*256)		/* Lookup table definition for equalization */
#define QC_LUT_RED	0
#define QC_LUT_GREEN	256
#define QC_LUT_BLUE	512

struct qc_userlut {
	unsigned int flags;
#define QC_USERLUT_DEFAULT 1		/* If set, change default settings or the current camera otherwise */
#define QC_USERLUT_ENABLE  2		/* If set, enable user-specified LUT, or otherwise disable it */
#define QC_USERLUT_VALUES  4		/* Load new (or store old) values into the lookup-table */
	unsigned char lut[QC_LUT_SIZE];	/* Lookup table to set or read */
};

#define VIDEO_PALETTE_BAYER	(('q'<<8) | 1)	/* Grab video in raw Bayer format */
#define VIDEO_PALETTE_MJPEG	(('q'<<8) | 2)	/* Grab video in compressed MJPEG format */
#define VID_HARDWARE_QCAM_USB	(('q'<<8) | 50)	/* Device type */

/* Private IOCTL calls */
#define QC_IOCTLBASE		220				/* Don't use same numbers as Philips driver */
#define VIDIOCQCGDEBUG		_IOR ('v',QC_IOCTLBASE+0, int)	/* Gets the debug output, bitfield */
#define VIDIOCQCSDEBUG		_IOWR('v',QC_IOCTLBASE+0, int)	/* Sets the debug output, bitfield */
#define VIDIOCQCGKEEPSETTINGS	_IOR ('v',QC_IOCTLBASE+1, int)	/* Get keep picture settings across one open to another (0-1) */
#define VIDIOCQCSKEEPSETTINGS	_IOWR('v',QC_IOCTLBASE+1, int)	/* Set keep picture settings across one open to another (0-1) */
#define VIDIOCQCGSETTLE		_IOR ('v',QC_IOCTLBASE+2, int)	/* Get if we let image brightness settle (0-1) */
#define VIDIOCQCSSETTLE		_IOWR('v',QC_IOCTLBASE+2, int)	/* Set if we let image brightness settle (0-1) */
#define VIDIOCQCGSUBSAMPLE	_IOR ('v',QC_IOCTLBASE+3, int)	/* Gets the speed (0-1) */
#define VIDIOCQCSSUBSAMPLE	_IOWR('v',QC_IOCTLBASE+3, int)	/* Sets the speed (0-1) */
#define VIDIOCQCGCOMPRESS	_IOR ('v',QC_IOCTLBASE+4, int)	/* Gets the compression mode (0-1) */
#define VIDIOCQCSCOMPRESS	_IOWR('v',QC_IOCTLBASE+4, int)	/* Sets the compression mode (0-1) */
#define VIDIOCQCGFRAMESKIP	_IOR ('v',QC_IOCTLBASE+5, int)	/* Get frame capture frequency (0-10) */
#define VIDIOCQCSFRAMESKIP	_IOWR('v',QC_IOCTLBASE+5, int)	/* Set frame capture frequency (0-10) */
#define VIDIOCQCGQUALITY	_IOR ('v',QC_IOCTLBASE+6, int)	/* Gets the interpolation mode (0-2) */
#define VIDIOCQCSQUALITY	_IOWR('v',QC_IOCTLBASE+6, int)	/* Sets the interpolation mode (0-2) */
#define VIDIOCQCGADAPTIVE	_IOR ('v',QC_IOCTLBASE+7, int)	/* Get automatic adaptive brightness control (0-1) */
#define VIDIOCQCSADAPTIVE	_IOWR('v',QC_IOCTLBASE+7, int)	/* Set automatic adaptive brightness control (0-1) */
#define VIDIOCQCGEQUALIZE	_IOR ('v',QC_IOCTLBASE+8, int)	/* Get equalize image (0-1) */
#define VIDIOCQCSEQUALIZE	_IOWR('v',QC_IOCTLBASE+8, int)	/* Set equalize image (0-1) */
#define VIDIOCQCGRETRYERRORS	_IOR ('v',QC_IOCTLBASE+9, int)	/* Get if we retry when capture fails (0-1) */
#define VIDIOCQCSRETRYERRORS	_IOWR('v',QC_IOCTLBASE+9, int)	/* Set if we retry when capture fails (0-1) */
#define VIDIOCQCGCOMPATIBLE	_IOR ('v',QC_IOCTLBASE+10,int)	/* Get enable workaround for bugs, bitfield */
#define VIDIOCQCSCOMPATIBLE	_IOWR('v',QC_IOCTLBASE+10,int)	/* Set enable workaround for bugs, bitfield */
#define VIDIOCQCGVIDEONR	_IOR ('v',QC_IOCTLBASE+11,int)	/* Get videodevice number (/dev/videoX) */
#define VIDIOCQCSVIDEONR	_IOWR('v',QC_IOCTLBASE+11,int)	/* Set videodevice number (/dev/videoX) */
#define VIDIOCQCGUSERLUT	_IOR ('v',QC_IOCTLBASE+12,struct qc_userlut)	/* Get user-specified lookup-table correction */
#define VIDIOCQCSUSERLUT	_IOWR('v',QC_IOCTLBASE+12,struct qc_userlut)	/* Set user-specified lookup-table correction */

#define VIDIOCQCGSTV		_IOWR('v',QC_IOCTLBASE+20,int)	/* Read STV chip register */
#define VIDIOCQCSSTV		_IOW ('v',QC_IOCTLBASE+20,int)	/* Write STV chip register */
#define VIDIOCQCGI2C		_IOWR('v',QC_IOCTLBASE+21,int)	/* Read I2C chip register */
#define VIDIOCQCSI2C		_IOW ('v',QC_IOCTLBASE+21,int)	/* Write I2C chip register */

/* Debugging message choices */
#define QC_DEBUGUSER		(1<<0)	/* Messages for interaction with user space (system calls) */
#define QC_DEBUGCAMERA		(1<<1)	/* Messages for interaction with the camera */
#define QC_DEBUGINIT		(1<<2)	/* Messages for each submodule initialization/deinit */
#define QC_DEBUGLOGIC		(1<<3)	/* Messages for entering and failing important functions */
#define QC_DEBUGERRORS		(1<<4)	/* Messages for all error conditions */
#define QC_DEBUGADAPTATION	(1<<5)	/* Messages for automatic exposure control workings */
#define QC_DEBUGCONTROLURBS	(1<<6)	/* Messages for sending I2C control messages via USB */
#define QC_DEBUGBITSTREAM	(1<<7)	/* Messages for finding chunk codes from camera bitstream */
#define QC_DEBUGINTERRUPTS	(1<<8)	/* Messages for each interrupt */
#define QC_DEBUGMUTEX		(1<<9)	/* Messages for acquiring/releasing the mutex */
#define QC_DEBUGCOMMON		(1<<10)	/* Messages for some common warnings */
#define QC_DEBUGFRAME		(1<<11)	/* Messages related to producer-consumer in qc_frame_* functions */
#define QC_DEBUGALL		(~0)	/* Messages for everything */

/* Compatibility choices */
#define QC_COMPAT_16X		(1<<0)
#define QC_COMPAT_DBLBUF	(1<<1)
#define QC_COMPAT_TORGB		(1<<2)	/* Video4Linux API is buggy and doesn't specify byte order for RGB images */
/* }}} */

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/version.h>

#ifdef CONFIG_SMP
#define __SMP__
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#define MODVERSIONS
#endif
#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif
#endif

#include <linux/videodev.h>
#include <linux/usb.h>
#include <asm/pgtable.h>		/* This is required for testing pte_offset_map */
#include <linux/spinlock.h>

/* {{{ [fold] SECTION: user configuration */
#define VERSION		"QuickCam USB $Date: 2004/07/29 18:12:39 $"
#ifndef COMPRESS
#define COMPRESS	1		/* 1=include compression support, 0=otherwise */
#endif
#ifndef DEBUGLEVEL
#define DEBUGLEVEL	QC_DEBUGCOMMON
#endif
#ifdef NDEBUG				/* Enable debugging if DEBUG is defined; if (also) NDEBUG is defined, disable debugging */
#undef DEBUG
#endif
//#define DEBUG				/* Enable debug code */
#ifdef DEBUG
#define PARANOID	1		/* Check consistency of driver state */
#else
#define PARANOID	0
#endif
/* Default (initial) values */
#define DEFAULT_BGR	TRUE		/* Use BGR byte order by default (and torgb converts to RGB)? */

#define DUMPDATA	0		/* Dump data from camera to user, no conversion nor length checks (see show.c) */
/* }}} */
/* {{{ [fold] SECTION: general utility definitions and macros */
#define FALSE			0
#define TRUE			(!FALSE)
typedef unsigned char Bool;
#define BIT(x)		(1<<(x))
#define SIZE(a)		(sizeof(a)/sizeof((a)[0]))
#define MAX(a,b)	((a)>(b)?(a):(b))
#define MIN(a,b)	((a)<(b)?(a):(b))
#define MAX3(a,b,c)	(MAX(a,MAX(b,c)))
#define MIN3(a,b,c)	(MIN(a,MIN(b,c)))
#define CLIP(a,low,high) MAX((low),MIN((high),(a)))
#define ABS(a)		((a)>0?(a):-(a))
#define SGN(a)		((a)<0 ? -1 : ((a)>0 ? 1 : 0))
#define CHECK_ERROR(cond,jump,msg,args...)	if (cond) { PDEBUG(msg, ## args); goto jump; }
#define GET_VENDORID(qc)			((qc)->dev->descriptor.idVendor)
#define GET_PRODUCTID(qc)			((qc)->dev->descriptor.idProduct)
/* }}} */
/* {{{ [fold] SECTION: compatibility */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,2,18)
#error "Too old kernel. At least Linux 2.2.18 is required."
#endif

#if LINUX_VERSION_CODE==KERNEL_VERSION(2,4,19) || LINUX_VERSION_CODE==KERNEL_VERSION(2,4,20)
#warning "Kernels 2.4.19 and 2.4.20 are buggy! Be sure to install patch from:"
#warning "http://www.ee.oulu.fi/~tuukkat/quickcam/linux-2.4.20-videodevfix.patch"
#endif

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,5,0) && LINUX_VERSION_CODE<KERNEL_VERSION(2,6,0)) || LINUX_VERSION_CODE>=KERNEL_VERSION(2,7,0)
#warning "Unsupported kernel, may or may not work. Good luck!"
#endif

#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,4,0) && defined(CONFIG_PROC_FS)
#define HAVE_PROCFS 1		/* FIXME: I don't think there's any reason to disable procfs with 2.2.x */
#else
#define HAVE_PROCFS 0
#warning "procfs support disabled"
#endif

#ifndef HAVE_VMA
#if LINUX_VERSION_CODE>=KERNEL_VERSION(2,5,3) || (defined(RED_HAT_LINUX_KERNEL) && defined(pte_offset_map))
/* Some RedHat 9 2.4.x patched-to-death kernels need this too */
#define HAVE_VMA 1
#else
#define HAVE_VMA 0
#endif
#endif

#if HAVE_VMA && LINUX_VERSION_CODE<KERNEL_VERSION(2,6,0)
#warning "VMA/RMAP compatibility enabled"
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)	/* 2.4.x emulation for 2.2.x kernels */
#define MODULE_DEVICE_TABLE(a,b)
#define USB_DEVICE(vend,prod) idVendor: (vend), idProduct: (prod)
struct usb_device_id {
	u16 idVendor;
	u16 idProduct;
};
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,5)
/* This is rather tricky emulation since if versioning is used,
 * video_register_device is already #defined. */
static inline int qc_video_register_device(struct video_device *dev, int type) { return video_register_device(dev, type); }
#undef video_register_device
#define video_register_device(vdev,type,nr)	qc_video_register_device((vdev), (type))
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,20)
/* In 2.4.19 and earlier kernels this was called "devrequest*".
 * Its fields were differently named, but since the structure was same,
 * we support the older kernels via this uglyish hack. */
struct usb_ctrlrequest {
	u8 bRequestType;
	u8 bRequest;
	u16 wValue;
	u16 wIndex;
	u16 wLength;
} __attribute__ ((packed));
#endif

#if LINUX_VERSION_CODE<KERNEL_VERSION(2,4,20) || LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,0)
/* Things come and go... */
/* Used only for debugging, so this could be actually removed if needed */
#define sem_getcount(sem)	atomic_read(&(sem)->count)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
static inline int qc_usb_submit_urb(struct urb *urb) { return usb_submit_urb(urb); }
static inline struct urb *qc_usb_alloc_urb(int packets) { return usb_alloc_urb(packets); }
#undef usb_submit_urb
#undef usb_alloc_urb
#define usb_submit_urb(u,f)	qc_usb_submit_urb(u)
#define usb_alloc_urb(u,f)	qc_usb_alloc_urb(u)
#define URB_ISO_ASAP		USB_ISO_ASAP
#endif

#ifndef list_for_each_entry
#define list_for_each_entry(pos, head, member)				\
	for (pos = list_entry((head)->next, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = list_entry(pos->member.next, typeof(*pos), member))
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#undef MOD_INC_USE_COUNT
#undef MOD_DEC_USE_COUNT
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#define GET_USE_COUNT(module)	1
#define EXPORT_NO_SYMBOLS
#endif

/* }}} */
/* {{{ [fold] SECTION: debugging */
#undef PDEBUG			/* undef it, just in case */
#define POISON_VAL	0x5B

#ifdef DEBUG

#include <linux/time.h>
/* PDEBUG is rather heavyweight macro and should be used only for debugging--not for general messages */
/* Based on timestamp by Roger Wolff */
#define PDEBUG(fmt, args...)				\
	do {						\
		struct timeval __tv_val;		\
		do_gettimeofday(&__tv_val);		\
		printk(KERN_DEBUG "quickcam [%2ld.%06ld]: ", __tv_val.tv_sec%60, __tv_val.tv_usec);	\
		printk(fmt, ## args); 			\
		printk("\n"); 				\
	} while(0)
#define IDEBUG_VAR	char *idebug_var;
#define IDEBUG_INIT(x)	do { \
	if ((x).idebug_var==((char*)&((x).idebug_var)) + 0xB967E57D) printk(KERN_CRIT __FILE__ ":%i: Init already done\n",__LINE__); \
	(x).idebug_var = ((char*)&((x).idebug_var)) + 0xB967E57D; \
} while(0)
#define IDEBUG_TEST(x)	do { \
	if ((x).idebug_var!=((char*)&((x).idebug_var)) + 0xB967E57D) printk(KERN_CRIT __FILE__ ":%i: Init missing\n",__LINE__); \
} while(0)
#define IDEBUG_EXIT(x)	do { \
	IDEBUG_TEST(x); \
	(x).idebug_var = NULL; \
	POISON(x); \
} while(0)
#define IDEBUG_EXIT_NOPOISON(x)	do { \
	IDEBUG_TEST(x); \
	(x).idebug_var = NULL; \
} while(0)
#define TEST_BUG(condition) \
	do { \
		if ((condition)!=0) { \
			PDEBUG("Badness in %s at %s:%d", __FUNCTION__, __FILE__, __LINE__); \
			return; \
		} \
	} while (0)
#define TEST_BUGR(condition) \
	do { \
		if ((condition)!=0) { \
			PDEBUG("Badness in %s at %s:%d", __FUNCTION__, __FILE__, __LINE__); \
			return -EFAULT; \
		} \
	} while (0)
#define TEST_BUG_MSG(cond, fmt, args...) \
	do { \
		if ((cond)!=0) { \
			PDEBUG(fmt, ## args); \
			PDEBUG("Badness in %s at %s:%d", __FUNCTION__, __FILE__, __LINE__); \
			return; \
		} \
	} while (0)
#define TEST_BUGR_MSG(cond, fmt, args...) \
	do { \
		if ((cond)!=0) { \
			PDEBUG(fmt, ## args); \
			PDEBUG("Badness in %s at %s:%d", __FUNCTION__, __FILE__, __LINE__); \
			return -EFAULT; \
		} \
	} while (0)
#define POISON(obj)	do { memset(&(obj),POISON_VAL,sizeof(obj)); } while(0)

#else

#define PDEBUG(fmt, args...)
#define IDEBUG_VAR
#define IDEBUG_INIT(x)
#define IDEBUG_TEST(x)
#define IDEBUG_EXIT(x)
#define IDEBUG_EXIT_NOPOISON(x)
#define TEST_BUG(x)
#define TEST_BUGR(x)
#define TEST_BUG_MSG(cond, fmt, args...)
#define TEST_BUGR_MSG(cond, fmt, args...)
#define POISON(obj)

#endif /* DEBUG */

//gcc is buggy? This doesn't work
//#define PRINTK(lvl, fmt, args...)	printk(lvl "quickcam: " fmt "\n", ## args)
#define PRINTK(lvl, fmt, args...)	do { printk(lvl "quickcam: " fmt, ## args); printk("\n"); } while (0)
/* }}} */
/* {{{ [fold] SECTION: hardware related stuff */
#define QUICKCAM_ISOPIPE 	0x81

/* Control register of the STV0600 ASIC */
#define STV_ISO_ENABLE 		0x1440
#define STV_SCAN_RATE  		0x1443
#define STV_ISO_SIZE 		0x15C1
#define STV_Y_CTRL   		0x15C3
#define STV_X_CTRL   		0x1680
#define STV_REG00 		0x1500
#define STV_REG01 		0x1501
#define STV_REG02 		0x1502
#define STV_REG03 		0x1503
#define STV_REG04 		0x1504
#define STV_REG23 		0x0423

/* Maximum frame size that any sensor can deliver */
#define MAX_FRAME_WIDTH		360
#define MAX_FRAME_HEIGHT	296
/* }}} */
/* {{{ [fold] SECTION: struct quickcam datatype and related values */

/* {{{ [fold] qc_sensor_data: Sensor related data (unique to each camera) */
struct qc_sensor_data {
	const struct qc_sensor *sensor;	/* Autodetected when camera is plugged in */
	int maxwidth;			/* Maximum size the sensor can deliver */
	int maxheight;
	int width;			/* Size delivered by the sensor (-1=unknown) */
	int height;
	int exposure;			/* Current exposure in effect (sensor-specific value, -1=unknown) */
	int rgain, ggain, bgain;	/* Current gains in effect (sensor-specific values, -1=unknown) */
	unsigned int subsample : 1;	/* Set into subsampling mode? */
	unsigned int compress : 1;	/* Set into compressed mode? */
};
/* }}} */
/* {{{ [fold] qc_i2c_data: I2C command queue for writing commands to camera */
#define I2C_MAXCOMMANDS 	16	/* Should be about 1-2 times the size of transfer buffer (=16) for maximum performance */
#define I2C_FLAG_WORD 		BIT(0)	/* Set if a 16-bit value is sent, otherwise 8-bit value */
#define I2C_FLAG_BREAK 		BIT(1)	/* Set if this is the last command in a packet */
struct qc_i2c_data {
	struct urb *urb;
	struct {
		u8 loval;
		u8 hival;
		u8 regnum;
		u8 flags;
	} commands[I2C_MAXCOMMANDS];
	/* 2=URB scheduled, need to schedule extra packet for QuickCam Web at completion */
	volatile int packets;		/* 0=no URBs scheduled, 1=URB scheduled */
	volatile unsigned int newhead;	/* Points to first free buffer position */
	volatile unsigned int head;	/* Points to oldest command which was not yet flushed */
	volatile unsigned int tail;	/* Points to next position which needs to be send, modified from interrupt */
	wait_queue_head_t wq;		/* Woken up when all pending data is sent */
	IDEBUG_VAR
};
/* }}} */
/* {{{ [fold] qc_isoc_data: Isochronous transfer queue control data for reading from camera */
#define ISOC_URBS		2	/* Number of URBs to use */
#define ISOC_PACKETS		10	/* How many isochronous data packets per URB */
#define ISOC_PACKET_SIZE	1023	/* Max size of one packet (shouldn't be hardcoded JFC was 960) */
struct qc_isoc_data {
	struct urb *urbs[ISOC_URBS];
	unsigned char *buffer;		/* Isochronous data transfer buffers */
	int errorcount;
	Bool streaming;			/* TRUE if URBs are allocated and submitted */
	IDEBUG_VAR
};
/* }}} */
/* {{{ [fold] qc_stream_data: Camera data stream control data */
struct qc_stream_data {
	Bool capturing;			/* Are we capturing data for a frame? */
	int frameskip;			/* How frequently to capture frames? 0=each frame, 1=every other */
	IDEBUG_VAR
};
/* }}} */
/* {{{ [fold] qc_frame_data: Raw frame (bayer/mjpeg) buffers */
#define FRAME_BUFFERS	2		/* We are double buffering */
#define FRAME_DATASIZE	(MAX_FRAME_WIDTH*MAX_FRAME_HEIGHT)	/* About 101 kilobytes (assume that compressed frame is always smaller) */
struct qc_frame_data {
	struct {
		int rawdatalen;		/* Number of used bytes of this frame buffer */
	} buffers[FRAME_BUFFERS];
	unsigned char *rawdatabuf;	/* vmalloc'd chunk containing the all raw frame data buffers concatenated */
	int maxrawdatalen;		/* Maximum amount of data we are willing to accept in bytes, */
					/* zero indicates that we are not grabbing current frame (but just throwing data away) */
	volatile unsigned int head;	/* The buffer to be captured next (empty or grabbing, if full, then whole buffer is full) */
	volatile unsigned int tail;	/* The buffer to be consumed next (full, unless equals head, then it is empty/grabbing) */
	spinlock_t tail_lock;		/* Protects tail changes */
	volatile Bool tail_in_use;	/* TRUE, when consumer is processing the frame pointed to by tail */

	wait_queue_head_t wq;		/* Processes waiting for more data in the buffer */
	volatile int waiting;		/* Number of processes waiting in the wait queues */
	volatile Bool exiting;		/* Set to TRUE when we want to quit */
	volatile int lost_frames;	/* Increased by one for every lost (non-captured by applications) frame, reset when a frame is captured */
	IDEBUG_VAR
};
/* }}} */
/* {{{ [fold] qc_mjpeg_data: MJPEG decoding data */
struct qc_mjpeg_data {
	int depth;			/* Bits per pixel in the final RGB image: 16 or 24 */
	u8 *encV;			/* Temporary buffers for holding YUV image data */
	u8 *encU;
	u8 *encY;
	/* yuv2rgb private data */
	void *table;
	void *table_rV[256];
	void *table_gU[256];
	int table_gV[256];
	void *table_bU[256];
	void (*yuv2rgb_func)(struct qc_mjpeg_data *, u8 *, u8 *, u8 *, u8 *, void *, void *, int);
	IDEBUG_VAR
};
/* }}} */
/* {{{ [fold] qc_fmt_data: Format conversion routines private data */
struct qc_fmt_data {
	unsigned char userlut[QC_LUT_SIZE];	/* User specified fixed look-up table, initialized when camera is plugged in */
	unsigned char lut[QC_LUT_SIZE];		/* Dynamically calculated LUT, for which userlut is applied to */
#if COMPRESS
	struct qc_mjpeg_data mjpeg_data;
	Bool compress;			/* Was compression subsystem initialized? */
#endif
	IDEBUG_VAR
};
/* }}} */
/* {{{ [fold] qc_capt_data: Formatted image capturing control data */
/* qc_capt_data: Formatted image capturing control data. */
#define MAX_FRAME_SIZE (MAX_FRAME_WIDTH*MAX_FRAME_HEIGHT*4)	/* Video Size 356x292x4 bytes for 0RGB 32 bpp mode */
struct qc_capt_data {
	unsigned char *frame;		/* Final image data buffer given to application, size MAX_FRAME_SIZE (mmappable) */
	Bool settled;			/* Has the picture settled after last open? */
	IDEBUG_VAR
};
/* }}} */
/* {{{ [fold] qc_adapt_data: Automatic exposure control data */
/* There are three different exposure control algorithms for different cases */
struct qc_adapt_data {
	int olddelta;
	int oldmidvalue, midvaluesum;
	int oldexposure, exposure;
	int gain;
	int framecounter;
	enum {
		EXPCONTROL_SATURATED,	/* Picture is over/undersaturated, halve/double brightness */
		EXPCONTROL_NEWTON,	/* Using Newton linear estimation */
		EXPCONTROL_FLOAT,	/* Very near correct brightness, float exposure slightly */
	} controlalg;
	IDEBUG_VAR
};
/* }}} */
/* {{{ [fold] qc_settings_data: User settings given by qcset or module parameters, initialized when camera is plugged in */
struct qc_settings_data {
	unsigned int keepsettings  : 1;	/* Keep all settings at each camera open (or reset most of them) */
	unsigned int subsample     : 1;	/* Normal or sub-sample (sub-sample to increase the speed) */
	unsigned int compress      : 1;	/* Enable compressed mode if available (higher framerate) */
	unsigned int frameskip     : 4;	/* How many frames to skip (higher=lower framerate) */
	unsigned int quality       : 3;	/* Quality of format conversion (higher=better but slower) */
	unsigned int adaptive      : 1;	/* Use automatic exposure control */
	unsigned int equalize      : 1;	/* Equalize images */
	unsigned int userlut       : 1;	/* Apply user-specified lookup-table */
	unsigned int retryerrors   : 1;	/* If errors happen when capturing an image, retry a few times? */
	unsigned int compat_16x    : 1;	/* Compatibility: force image size to multiple of 16 */
	unsigned int compat_dblbuf : 1;	/* Compatibility: fake doublebuffering for applications */
	unsigned int compat_torgb  : 1;	/* Compatibility: use RGB data order, not BGR */
	unsigned int settle        : 8;	/* Maximum number of frames to wait image brightness to settle */
	/* Total: 25 bits */
};
/* }}} */

/* Main per-camera data structure, most important thing in whole driver */
struct quickcam {
	/* The following entries are initialized in qc_usb_init() when camera is plugged in */
	struct semaphore lock;			/* Allow only one process to access quickcam at a time */
	struct list_head list;			/* All cameras are in a doubly linked list */
	int users;				/* User count (simultaneous open count) */
	struct usb_device *dev;			/* USB device, set to NULL when camera disconnected and interrupts disabled */
	unsigned char iface;			/* The interface number in the camera device we are bound to */
	Bool connected;				/* Set to FALSE immediately when the camera is disconnected (even before interrupts are disabled) */
	struct video_device vdev;		/* Used for registering the camera driver for Video4Linux */
	struct qc_settings_data settings;	/* General user settings set with e.g. qcset */
#if HAVE_PROCFS
	struct proc_dir_entry *proc_entry;
#endif
	/* The following entries are initialized in qc_v4l_init() when the camera device is opened */
	struct video_picture vpic;		/* Contains the last values set by user (which is reported to user) */
	Bool vpic_pending;			/* Settings in vpic were changed but are not yet in effect */
	struct video_window vwin;		/* Contains the image size and position the user is expecting */

	/* Private structures for each module, initialized in corresponding init-function */
	struct qc_i2c_data i2c_data;		/* Filled when the camera is plugged in or driver loaded */
	struct qc_adapt_data adapt_data;	/* Filled when the camera is plugged in or driver loaded */
	struct qc_sensor_data sensor_data;	/* Mostly filled when the device is opened */
	struct qc_stream_data stream_data;	/* Filled when the device is opened */
	struct qc_frame_data frame_data;	/* Filled when the device is opened */
	struct qc_capt_data capt_data;		/* Filled when the device is opened */
	struct qc_isoc_data isoc_data;		/* Filled when the device is opened */
	struct qc_fmt_data fmt_data;		/* Mostly filled when the device is opened, except for userlut */

	u8 dmabuf[35];				/* Temporary buffer which may be used for DMA transfer */
};
/* }}} */
/* {{{ [fold] SECTION: miscelleneous */
/* Constant information related to a sensor type */
struct qc_sensor {
	char *name;
	char *manufacturer;
	int (*init)(struct quickcam *qc);					/* Initialize sensor */
	int (*start)(struct quickcam *qc);					/* Start sending image data */
	int (*stop)(struct quickcam *qc);					/* Stop sending image data */
	int (*set_size)(struct quickcam *qc, unsigned int w, unsigned int h);	/* Request camera to send the given image size */
			/* Set primary brightness value exp (usually exposure time) and HSV 0-65535 (usually gains) */
	int (*set_levels)(struct quickcam *qc, unsigned int exp, unsigned int gain, unsigned int hue, unsigned int sat);
	int (*set_target)(struct quickcam *qc, unsigned int val);		/* Set target brightness for sensor autoexposure 0-65535 */
	/* Exposure and gain control information */
	Bool autoexposure;							/* Sensor has automatic exposure control */
	int adapt_gainlow;							/* (0-65535) How eagerly to decrease gain when necessary */
	int adapt_gainhigh;							/* (0-65535) How eagerly to increase gain when necessary */
	/* Information needed to access the sensor via I2C */
	int reg23;
	unsigned char i2c_addr;
	/* Identification information used for auto-detection */
	int id_reg;
	unsigned char id;
	int length_id;
	int flag;			/* May be used by sensor driver for private purposes */
};
/* }}} */
/* {{{ [fold] SECTION: function prototypes */
/* USB interface chip control */
int qc_i2c_break(struct quickcam *qc);
int qc_i2c_wait(struct quickcam *qc);
int qc_i2c_set(struct quickcam *qc, unsigned char reg, unsigned char val);
int qc_i2c_setw(struct quickcam *qc, unsigned char reg, unsigned short val);

int qc_stv_set(struct quickcam *qc, unsigned short reg, unsigned char val);
int qc_stv_setw(struct quickcam *qc, unsigned short reg, unsigned short val);
int qc_stv_get(struct quickcam *qc, unsigned short reg);

/* Image format conversion routines */
int qc_fmt_convert(struct quickcam *qc, unsigned char *src, unsigned int src_len, unsigned char *dst, unsigned int dst_len, int *midvalue);
int qc_fmt_issupported(int format);
const char *qc_fmt_getname(int format);
int qc_fmt_getdepth(int format);
int qc_fmt_init(struct quickcam *qc);
void qc_fmt_exit(struct quickcam *qc);

int qc_mjpeg_decode(struct qc_mjpeg_data *md, unsigned char *src, int src_len, unsigned char *dst);
int qc_mjpeg_init(struct qc_mjpeg_data *md, int depth, Bool tobgr);
void qc_mjpeg_exit(struct qc_mjpeg_data *md);

void qc_hsv2rgb(s16 hue, u16 sat, u16 val, int *red, int *green, int *blue);
int qc_get_i2c(struct quickcam *qc, const struct qc_sensor *sensor, int reg);
void qc_frame_flush(struct quickcam *qc);

void qc_usleep(unsigned long usec);
extern int qcdebug;				/* Driver debuglevel */
/* }}} */

#endif /* __KERNEL__ */

#endif /* __LINUX_QUICKCAM_H */
