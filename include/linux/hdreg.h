#ifndef _LINUX_HDREG_H
#define _LINUX_HDREG_H

/*
 * Command Header sizes for IOCTL commands
 */
#define HDIO_DRIVE_CMD_HDR_SIZE		(4 * sizeof(u8))
#define HDIO_DRIVE_HOB_HDR_SIZE		(8 * sizeof(u8))

struct hd_geometry {
      __u8 heads;
      __u8 sectors;
      __u16 cylinders;
      unsigned long start;
};

/* hd/ide ctl's that pass (arg) ptrs to user space are numbered 0x030n/0x031n */
#define HDIO_GETGEO		0x0301	/* get device geometry */
#define HDIO_GET_UNMASKINTR	0x0302	/* get current unmask setting */
#define HDIO_GET_MULTCOUNT	0x0304	/* get current IDE blockmode setting */
#define HDIO_GET_QDMA		0x0305	/* get use-qdma flag */
#define HDIO_GET_32BIT		0x0309	/* get current io_32bit setting */
#define HDIO_GET_NOWERR		0x030a	/* get ignore-write-error flag */
#define HDIO_GET_DMA		0x030b	/* get use-dma flag */
#define HDIO_GET_NICE		0x030c	/* get nice flags */
#define HDIO_GET_IDENTITY	0x030d	/* get IDE identification info */
#define HDIO_GET_WCACHE		0x030e	/* get write cache mode on|off */
#define HDIO_GET_ACOUSTIC	0x030f	/* get acoustic value */
#define	HDIO_GET_ADDRESS	0x0310	/* */

#define HDIO_GET_BUSSTATE	0x031a	/* get the bus state of the hwif */
#define HDIO_DRIVE_CMD		0x031f	/* execute a special drive command */

/* hd/ide ctl's that pass (arg) non-ptr values are numbered 0x032n/0x033n */
#define HDIO_SET_MULTCOUNT	0x0321	/* change IDE blockmode */
#define HDIO_SET_UNMASKINTR	0x0322	/* permit other irqs during I/O */
#define HDIO_SET_32BIT		0x0324	/* change io_32bit flags */
#define HDIO_SET_NOWERR		0x0325	/* change ignore-write-error flag */
#define HDIO_SET_DMA		0x0326	/* change use-dma flag */
#define HDIO_SET_PIO_MODE	0x0327	/* reconfig interface to new speed */
#define HDIO_SET_NICE		0x0329	/* set nice flags */
#define HDIO_SET_WCACHE		0x032b	/* change write cache enable-disable */
#define HDIO_SET_ACOUSTIC	0x032c	/* change acoustic behavior */
#define HDIO_SET_BUSSTATE	0x032d	/* set the bus state of the hwif */
#define HDIO_SET_QDMA		0x032e	/* change use-qdma flag */
#define HDIO_SET_ADDRESS	0x032f	/* change lba addressing modes */

/* hd/ide ctl's that pass (arg) ptrs to user space are numbered 0x033n/0x033n */
/* 0x330 is reserved - used to be HDIO_GETGEO_BIG */
#define HDIO_GETGEO_BIG_RAW	0x0331	/* */

#endif
