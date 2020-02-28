/*
 * usbif.h
 *
 * USB I/O interface for Xen guest OSes.
 *
 * Copyright (C) 2009, FUJITSU LABORATORIES LTD.
 * Author: Noboru Iwamatsu <n_iwamatsu@jp.fujitsu.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __XEN_PUBLIC_IO_USBIF_H__
#define __XEN_PUBLIC_IO_USBIF_H__

#include "ring.h"
#include "../grant_table.h"

/*
 * Feature and Parameter Negotiation
 * =================================
 * The two halves of a Xen pvUSB driver utilize nodes within the XenStore to
 * communicate capabilities and to negotiate operating parameters. This
 * section enumerates these nodes which reside in the respective front and
 * backend portions of the XenStore, following the XenBus convention.
 *
 * Any specified default value is in effect if the corresponding XenBus node
 * is not present in the XenStore.
 *
 * XenStore nodes in sections marked "PRIVATE" are solely for use by the
 * driver side whose XenBus tree contains them.
 *
 *****************************************************************************
 *                            Backend XenBus Nodes
 *****************************************************************************
 *
 *------------------ Backend Device Identification (PRIVATE) ------------------
 *
 * num-ports
 *      Values:         unsigned [1...31]
 *
 *      Number of ports for this (virtual) USB host connector.
 *
 * usb-ver
 *      Values:         unsigned [1...2]
 *
 *      USB version of this host connector: 1 = USB 1.1, 2 = USB 2.0.
 *
 * port/[1...31]
 *      Values:         string
 *
 *      Physical USB device connected to the given port, e.g. "3-1.5".
 *
 *****************************************************************************
 *                            Frontend XenBus Nodes
 *****************************************************************************
 *
 *----------------------- Request Transport Parameters -----------------------
 *
 * event-channel
 *      Values:         unsigned
 *
 *      The identifier of the Xen event channel used to signal activity
 *      in the ring buffer.
 *
 * urb-ring-ref
 *      Values:         unsigned
 *
 *      The Xen grant reference granting permission for the backend to map
 *      the sole page in a single page sized ring buffer. This is the ring
 *      buffer for urb requests.
 *
 * conn-ring-ref
 *      Values:         unsigned
 *
 *      The Xen grant reference granting permission for the backend to map
 *      the sole page in a single page sized ring buffer. This is the ring
 *      buffer for connection/disconnection requests.
 *
 * protocol
 *      Values:         string (XEN_IO_PROTO_ABI_*)
 *      Default Value:  XEN_IO_PROTO_ABI_NATIVE
 *
 *      The machine ABI rules governing the format of all ring request and
 *      response structures.
 *
 */

enum xenusb_spec_version {
	XENUSB_VER_UNKNOWN = 0,
	XENUSB_VER_USB11,
	XENUSB_VER_USB20,
	XENUSB_VER_USB30,	/* not supported yet */
};

/*
 *  USB pipe in xenusb_request
 *
 *  - port number:	bits 0-4
 *				(USB_MAXCHILDREN is 31)
 *
 *  - operation flag:	bit 5
 *				(0 = submit urb,
 *				 1 = unlink urb)
 *
 *  - direction:	bit 7
 *				(0 = Host-to-Device [Out]
 *				 1 = Device-to-Host [In])
 *
 *  - device address:	bits 8-14
 *
 *  - endpoint:		bits 15-18
 *
 *  - pipe type:	bits 30-31
 *				(00 = isochronous, 01 = interrupt,
 *				 10 = control, 11 = bulk)
 */

#define XENUSB_PIPE_PORT_MASK	0x0000001f
#define XENUSB_PIPE_UNLINK	0x00000020
#define XENUSB_PIPE_DIR		0x00000080
#define XENUSB_PIPE_DEV_MASK	0x0000007f
#define XENUSB_PIPE_DEV_SHIFT	8
#define XENUSB_PIPE_EP_MASK	0x0000000f
#define XENUSB_PIPE_EP_SHIFT	15
#define XENUSB_PIPE_TYPE_MASK	0x00000003
#define XENUSB_PIPE_TYPE_SHIFT	30
#define XENUSB_PIPE_TYPE_ISOC	0
#define XENUSB_PIPE_TYPE_INT	1
#define XENUSB_PIPE_TYPE_CTRL	2
#define XENUSB_PIPE_TYPE_BULK	3

#define xenusb_pipeportnum(pipe)		((pipe) & XENUSB_PIPE_PORT_MASK)
#define xenusb_setportnum_pipe(pipe, portnum)	((pipe) | (portnum))

#define xenusb_pipeunlink(pipe)			((pipe) & XENUSB_PIPE_UNLINK)
#define xenusb_pipesubmit(pipe)			(!xenusb_pipeunlink(pipe))
#define xenusb_setunlink_pipe(pipe)		((pipe) | XENUSB_PIPE_UNLINK)

#define xenusb_pipein(pipe)			((pipe) & XENUSB_PIPE_DIR)
#define xenusb_pipeout(pipe)			(!xenusb_pipein(pipe))

#define xenusb_pipedevice(pipe)			\
		(((pipe) >> XENUSB_PIPE_DEV_SHIFT) & XENUSB_PIPE_DEV_MASK)

#define xenusb_pipeendpoint(pipe)		\
		(((pipe) >> XENUSB_PIPE_EP_SHIFT) & XENUSB_PIPE_EP_MASK)

#define xenusb_pipetype(pipe)			\
		(((pipe) >> XENUSB_PIPE_TYPE_SHIFT) & XENUSB_PIPE_TYPE_MASK)
#define xenusb_pipeisoc(pipe)	(xenusb_pipetype(pipe) == XENUSB_PIPE_TYPE_ISOC)
#define xenusb_pipeint(pipe)	(xenusb_pipetype(pipe) == XENUSB_PIPE_TYPE_INT)
#define xenusb_pipectrl(pipe)	(xenusb_pipetype(pipe) == XENUSB_PIPE_TYPE_CTRL)
#define xenusb_pipebulk(pipe)	(xenusb_pipetype(pipe) == XENUSB_PIPE_TYPE_BULK)

#define XENUSB_MAX_SEGMENTS_PER_REQUEST (16)
#define XENUSB_MAX_PORTNR	31

/*
 * RING for transferring urbs.
 */
struct xenusb_request_segment {
	grant_ref_t gref;
	__u16 offset;
	__u16 length;
};

struct xenusb_urb_request {
	__u16 id;			/* request id */
	__u16 nr_buffer_segs;	/* # of urb->transfer_buffer segments */

	/* basic urb parameter */
	__u32 pipe;
	__u16 transfer_flags;
#define XENUSB_SHORT_NOT_OK	0x0001
	__u16 buffer_length;
	union {
		__u8 ctrl[8];			/* pipe type control */
						/* setup packet */

		struct {
			__u16 interval;		/* max (1024*8) in usb core */
			__u16 start_frame;		/* start frame */
			__u16 number_of_packets;	/* # of ISO packets */
			__u16 nr_frame_desc_segs;
					/* # of iso_frame_desc segments */
		} isoc;				/* pipe type isochronous */

		struct {
			__u16 interval;		/* max (1024*8) in usb core */
			__u16 pad[3];
		} intr;				/* pipe type interrupt */

		struct {
			__u16 unlink_id;	/* unlink request id */
			__u16 pad[3];
		} unlink;			/* pipe unlink */

	} u;

	/* urb data segments */
	struct xenusb_request_segment seg[XENUSB_MAX_SEGMENTS_PER_REQUEST];
};

struct xenusb_urb_response {
	__u16 id;		/* request id */
	__u16 start_frame;	/* start frame (ISO) */
	__s32 status;		/* status (non-ISO) */
	__s32 actual_length;	/* actual transfer length */
	__s32 error_count;	/* number of ISO errors */
};

DEFINE_RING_TYPES(xenusb_urb, struct xenusb_urb_request,
		  struct xenusb_urb_response);
#define XENUSB_URB_RING_SIZE __CONST_RING_SIZE(xenusb_urb, PAGE_SIZE)

/*
 * RING for notifying connect/disconnect events to frontend
 */
struct xenusb_conn_request {
	__u16 id;
};

struct xenusb_conn_response {
	__u16 id;		/* request id */
	__u8 portnum;		/* port number */
	__u8 speed;		/* usb device speed */
#define XENUSB_SPEED_NONE	0
#define XENUSB_SPEED_LOW	1
#define XENUSB_SPEED_FULL	2
#define XENUSB_SPEED_HIGH	3
};

DEFINE_RING_TYPES(xenusb_conn, struct xenusb_conn_request,
		  struct xenusb_conn_response);
#define XENUSB_CONN_RING_SIZE __CONST_RING_SIZE(xenusb_conn, PAGE_SIZE)

#endif /* __XEN_PUBLIC_IO_USBIF_H__ */
