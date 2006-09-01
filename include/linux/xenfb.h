/*
 * linux/include/linux/xenfb.h -- Xen virtual frame buffer device
 *
 * Copyright (C) 2005
 *
 *      Anthony Liguori <aliguori@us.ibm.com>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#ifndef _LINUX_XENFB_H
#define _LINUX_XENFB_H

#include <asm/types.h>

/* out events */

#define XENFB_TYPE_MOTION 1
#define XENFB_TYPE_UPDATE 2

struct xenfb_motion
{
	__u8 type;          /* XENFB_TYPE_MOTION */
	__u16 x;            /* The new x coordinate */
	__u16 y;            /* The new y coordinate */
};

struct xenfb_update
{
	__u8 type;          /* XENFB_TYPE_UPDATE */
	__u16 x;            /* source x */
	__u16 y;            /* source y */
	__u16 width;        /* rect width */
	__u16 height;       /* rect height */
};

union xenfb_out_event
{
	__u8 type;
	struct xenfb_motion motion;
	struct xenfb_update update;
	char _[40];
};

/* in events */

#define XENFB_TYPE_SET_EVENTS 1

#define XENFB_FLAG_MOTION 1
#define XENFB_FLAG_UPDATE 2
#define XENFB_FLAG_COPY 4
#define XENFB_FLAG_FILL 8

struct xenfb_set_events
{
	__u8 type;          /* XENFB_TYPE_SET_EVENTS */
	__u32 flags;        /* combination of XENFB_FLAG_* */
};

union xenfb_in_event
{
	__u8 type;
	struct xenfb_set_events set_events;
	char _[40];
};

/* shared page */

#define XENFB_IN_RING_SIZE (1024 / 40)
#define XENFB_OUT_RING_SIZE (2048 / 40)

#define XENFB_RING_SIZE(ring) (sizeof((ring)) / sizeof(*(ring)))
#define XENFB_RING_REF(ring, idx) (ring)[(idx) % XENFB_RING_SIZE((ring))]

struct xenfb_page
{
	__u8 initialized;
	__u16 width;         /* the width of the framebuffer (in pixels) */
	__u16 height;        /* the height of the framebuffer (in pixels) */
	__u32 line_length;   /* the length of a row of pixels (in bytes) */
	__u32 mem_length;    /* the length of the framebuffer (in bytes) */
	__u8 depth;          /* the depth of a pixel (in bits) */

	unsigned long pd[2];

	__u32 in_cons, in_prod;
	__u32 out_cons, out_prod;

	union xenfb_in_event in[XENFB_IN_RING_SIZE];
	union xenfb_out_event out[XENFB_OUT_RING_SIZE];
};

void xenfb_resume(void);

#endif
