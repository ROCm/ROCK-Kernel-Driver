/*
 * TTUSB DEC Driver
 *
 * Copyright (C) 2003 Alex Woods <linux-dvb@giblets.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef _TTUSB_DEC_H
#define _TTUSB_DEC_H

#include <asm/semaphore.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_filter.h"
#include "dvb_i2c.h"
#include "dvb_net.h"

#define DRIVER_NAME		"TechnoTrend/Hauppauge DEC USB"

#define COMMAND_PIPE		0x03
#define RESULT_PIPE		0x84
#define STREAM_PIPE		0x88

#define COMMAND_PACKET_SIZE	0x3c
#define ARM_PACKET_SIZE		0x1000

#define ISO_BUF_COUNT		0x04
#define FRAMES_PER_ISO_BUF	0x04
#define ISO_FRAME_SIZE		0x0380

#define	MAX_AV_PES_LENGTH	6144

struct ttusb_dec {
	/* DVB bits */
	struct dvb_adapter	*adapter;
	struct dmxdev		dmxdev;
	struct dvb_demux	demux;
	struct dmx_frontend	frontend;
	struct dvb_i2c_bus	*i2c_bus;
	struct dvb_net		dvb_net;

	u16			pid[DMX_PES_OTHER];

	/* USB bits */
	struct usb_device	*udev;
	u8			trans_count;
	unsigned int		command_pipe;
	unsigned int		result_pipe;
	unsigned int		stream_pipe;
	int			interface;
	struct semaphore	usb_sem;

	void			*iso_buffer;
	dma_addr_t		iso_dma_handle;
	struct urb		*iso_urb[ISO_BUF_COUNT];
	int			iso_stream_count;
	struct semaphore	iso_sem;

	u8			av_pes[MAX_AV_PES_LENGTH + 4];
	int			av_pes_state;
	int			av_pes_length;
	int			av_pes_payload_length;

	struct dvb_filter_pes2ts	a_pes2ts;
	struct dvb_filter_pes2ts	v_pes2ts;

	u8			v_pes[16 + MAX_AV_PES_LENGTH];
	int			v_pes_length;
	int			v_pes_postbytes;

	struct list_head	urb_frame_list;
	struct tasklet_struct	urb_tasklet;
	spinlock_t		urb_frame_list_lock;
};

struct urb_frame {
	u8			data[ISO_FRAME_SIZE];
	int			length;
	struct list_head	urb_frame_list;
};

#endif
