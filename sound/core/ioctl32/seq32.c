/*
 *   32bit -> 64bit ioctl wrapper for sequencer API
 *   Copyright (c) by Takashi Iwai <tiwai@suse.de>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#define __NO_VERSION__
#include <sound/driver.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/timer.h>
#include <asm/uaccess.h>
#include <sound/asequencer.h>
#include "ioctl32.h"

struct ioctl32_mapper seq_mappers[] = {
	{ SNDRV_SEQ_IOCTL_PVERSION, NULL },
	{ SNDRV_SEQ_IOCTL_CLIENT_ID, NULL },
	{ SNDRV_SEQ_IOCTL_SYSTEM_INFO, NULL },
	{ SNDRV_SEQ_IOCTL_GET_CLIENT_INFO, NULL },
	{ SNDRV_SEQ_IOCTL_SET_CLIENT_INFO, NULL },
	{ SNDRV_SEQ_IOCTL_CREATE_PORT, NULL },
	{ SNDRV_SEQ_IOCTL_DELETE_PORT, NULL },
	{ SNDRV_SEQ_IOCTL_GET_PORT_INFO, NULL },
	{ SNDRV_SEQ_IOCTL_SET_PORT_INFO, NULL },
	{ SNDRV_SEQ_IOCTL_SUBSCRIBE_PORT, NULL },
	{ SNDRV_SEQ_IOCTL_UNSUBSCRIBE_PORT, NULL },
	{ SNDRV_SEQ_IOCTL_CREATE_QUEUE, NULL },
	{ SNDRV_SEQ_IOCTL_DELETE_QUEUE, NULL },
	{ SNDRV_SEQ_IOCTL_GET_QUEUE_INFO, NULL },
	{ SNDRV_SEQ_IOCTL_SET_QUEUE_INFO, NULL },
	{ SNDRV_SEQ_IOCTL_GET_NAMED_QUEUE, NULL },
	{ SNDRV_SEQ_IOCTL_GET_QUEUE_STATUS, NULL },
	{ SNDRV_SEQ_IOCTL_GET_QUEUE_TEMPO, NULL },
	{ SNDRV_SEQ_IOCTL_SET_QUEUE_TEMPO, NULL },
	{ SNDRV_SEQ_IOCTL_GET_QUEUE_OWNER, NULL },
	{ SNDRV_SEQ_IOCTL_SET_QUEUE_OWNER, NULL },
	{ SNDRV_SEQ_IOCTL_GET_QUEUE_TIMER, NULL },
	{ SNDRV_SEQ_IOCTL_SET_QUEUE_TIMER, NULL },
	{ SNDRV_SEQ_IOCTL_GET_QUEUE_CLIENT, NULL },
	{ SNDRV_SEQ_IOCTL_SET_QUEUE_CLIENT, NULL },
	{ SNDRV_SEQ_IOCTL_GET_CLIENT_POOL, NULL },
	{ SNDRV_SEQ_IOCTL_SET_CLIENT_POOL, NULL },
	{ SNDRV_SEQ_IOCTL_REMOVE_EVENTS, NULL },
	{ SNDRV_SEQ_IOCTL_QUERY_SUBS, NULL },
	{ SNDRV_SEQ_IOCTL_GET_SUBSCRIPTION, NULL },
	{ SNDRV_SEQ_IOCTL_QUERY_NEXT_CLIENT, NULL },
	{ SNDRV_SEQ_IOCTL_QUERY_NEXT_PORT, NULL },
	{ 0 },
};
