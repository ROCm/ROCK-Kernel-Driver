/* 
 * dmxdev.h
 *
 * Copyright (C) 2000 Ralph  Metzler <ralph@convergence.de>
 *                  & Marcus Metzler <marcus@convergence.de>
                      for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _DMXDEV_H_
#define _DMXDEV_H_

#ifndef __KERNEL__ 
#define __KERNEL__ 
#endif 

#include <linux/dvb/dmx.h>

#include <linux/version.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/fs.h>

#include "dvbdev.h"
#include "demux.h"

typedef enum {
	DMXDEV_TYPE_NONE,
	DMXDEV_TYPE_SEC,
	DMXDEV_TYPE_PES,
} dmxdev_type_t;

typedef enum {
	DMXDEV_STATE_FREE,
	DMXDEV_STATE_ALLOCATED,
	DMXDEV_STATE_SET,
	DMXDEV_STATE_GO,
	DMXDEV_STATE_DONE,
	DMXDEV_STATE_TIMEDOUT
} dmxdev_state_t;

typedef struct dmxdev_buffer_s {
        uint8_t *data;
        uint32_t size;
        int32_t  pread;
        int32_t  pwrite;
	wait_queue_head_t queue;
        int error;
} dmxdev_buffer_t;


typedef struct dmxdev_filter_s {
	struct dvb_device *dvbdev;

        union {
	        dmx_section_filter_t *sec;
	} filter;

        union {
                dmx_ts_feed_t *ts;
                dmx_section_feed_t *sec;
	} feed;

        union {
	        struct dmx_sct_filter_params sec;
	        struct dmx_pes_filter_params pes;
	} params;

        int type;
        dmxdev_state_t state;
        struct dmxdev_s *dev;
        dmxdev_buffer_t buffer;

	struct semaphore mutex;

        // only for sections
        struct timer_list timer;
        int todo;
        uint8_t secheader[3];

        u16 pid;
} dmxdev_filter_t;


typedef struct dmxdev_dvr_s {
        int state;
        struct dmxdev_s *dev;
        dmxdev_buffer_t buffer;
} dmxdev_dvr_t;


typedef struct dmxdev_s {
	struct dvb_device *dvbdev;
	struct dvb_device *dvr_dvbdev;

        dmxdev_filter_t *filter;
        dmxdev_dvr_t *dvr;
        dmx_demux_t *demux;

        int filternum;
        int capabilities;
#define DMXDEV_CAP_DUPLEX 1
        dmx_frontend_t *dvr_orig_fe;

        dmxdev_buffer_t dvr_buffer;
#define DVR_BUFFER_SIZE (10*188*1024)

	struct semaphore mutex;
	spinlock_t lock;
} dmxdev_t;


int dvb_dmxdev_init(dmxdev_t *dmxdev, struct dvb_adapter *);
void dvb_dmxdev_release(dmxdev_t *dmxdev);

#endif /* _DMXDEV_H_ */
