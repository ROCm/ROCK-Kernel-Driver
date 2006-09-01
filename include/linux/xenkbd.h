/*
 * linux/include/linux/xenkbd.h -- Xen virtual keyboard/mouse
 *
 * Copyright (C) 2005
 *
 *      Anthony Liguori <aliguori@us.ibm.com>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#ifndef _LINUX_XENKBD_H
#define _LINUX_XENKBD_H

#include <asm/types.h>

/* in events */

#define XENKBD_TYPE_MOTION  1     /* mouse movement event */
#define XENKBD_TYPE_BUTTON  2     /* mouse button event */
#define XENKBD_TYPE_KEY     3     /* keyboard event */

struct xenkbd_motion
{
	__u8 type;         /* XENKBD_TYPE_MOTION */
	__s16 rel_x;       /* relative X motion */
	__s16 rel_y;       /* relative Y motion */
};

struct xenkbd_button
{
	__u8 type;         /* XENKBD_TYPE_BUTTON */
	__u8 pressed;      /* 1 if pressed; 0 otherwise */
	__u8 button;       /* the button (0, 1, 2 is right, middle, left) */
};

struct xenkbd_key
{
	__u8 type;         /* XENKBD_TYPE_KEY */
	__u8 pressed;      /* 1 if pressed; 0 otherwise */
	__u16 keycode;     /* KEY_* from linux/input.h */
};

union xenkbd_in_event
{
	__u8 type;
	struct xenkbd_motion motion;
	struct xenkbd_button button;
	struct xenkbd_key key;
	char _[40];
};

/* out events */

union xenkbd_out_event
{
	__u8 type;
	char _[40];
};

/* shared page */

#define XENKBD_IN_RING_SIZE (2048 / 40)
#define XENKBD_OUT_RING_SIZE (1024 / 40)

#define XENKBD_RING_SIZE(ring) (sizeof((ring)) / sizeof(*(ring)))
#define XENKBD_RING_REF(ring, idx) (ring)[(idx) % XENKBD_RING_SIZE((ring))]

struct xenkbd_info
{
	__u8 initialized;
	__u32 in_cons, in_prod;
	__u32 out_cons, out_prod;

	union xenkbd_in_event in[XENKBD_IN_RING_SIZE];
	union xenkbd_out_event out[XENKBD_OUT_RING_SIZE];
};

void xenkbd_resume(void);

#endif
