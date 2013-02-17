/*
 * Copyright (c) 2012, Microsoft Corporation.
 *
 * Author:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/*
 * Hyper-V Synthetic Video Frame Buffer Driver
 *
 * This is the driver for the Hyper-V Synthetic Video, which supports
 * screen resolution up to Full HD 1920x1080 with 32 bit color on Windows
 * Server 2012, and 1600x1200 with 16 bit color on Windows Server 2008 R2
 * or earlier.
 *
 * It also solves the double mouse cursor issue of the emulated video mode.
 *
 * The default screen resolution is 1152x864, which may be changed by a
 * kernel parameter:
 *     video=hyperv_fb:<width>x<height>
 *     For example: video=hyperv_fb:1280x1024
 *
 * Portrait orientation is also supported:
 *     For example: video=hyperv_fb:864x1152
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/fb.h>

#include <linux/hyperv.h>


/* Hyper-V Synthetic Video Protocol definitions and structures */
#define MAX_VMBUS_PKT_SIZE 0x4000

#define SYNTHVID_VERSION(major, minor) ((minor) << 16 | (major))
#define SYNTHVID_VERSION_WIN7 SYNTHVID_VERSION(3, 0)
#define SYNTHVID_VERSION_WIN8 SYNTHVID_VERSION(3, 2)

#define SYNTHVID_DEPTH_WIN7 16
#define SYNTHVID_DEPTH_WIN8 32

#define SYNTHVID_FB_SIZE_WIN7 (4 * 1024 * 1024)
#define SYNTHVID_WIDTH_MAX_WIN7 1600
#define SYNTHVID_HEIGHT_MAX_WIN7 1200

#define SYNTHVID_FB_SIZE_WIN8 (8 * 1024 * 1024)


enum pipe_msg_type {
	PIPE_MSG_INVALID,
	PIPE_MSG_DATA,
	PIPE_MSG_MAX
};

struct pipe_msg_hdr {
	u32 type;
	u32 size; /* size of message after this field */
} __packed;


enum synthvid_msg_type {
	SYNTHVID_ERROR			= 0,
	SYNTHVID_VERSION_REQUEST	= 1,
	SYNTHVID_VERSION_RESPONSE	= 2,
	SYNTHVID_VRAM_LOCATION		= 3,
	SYNTHVID_VRAM_LOCATION_ACK	= 4,
	SYNTHVID_SITUATION_UPDATE	= 5,
	SYNTHVID_SITUATION_UPDATE_ACK	= 6,
	SYNTHVID_POINTER_POSITION	= 7,
	SYNTHVID_POINTER_SHAPE		= 8,
	SYNTHVID_FEATURE_CHANGE		= 9,
	SYNTHVID_DIRT			= 10,

	SYNTHVID_MAX			= 11
};

struct synthvid_msg_hdr {
	u32 type;
	u32 size;  /* size of this header + payload after this field*/
} __packed;


struct synthvid_version_req {
	u32 version;
} __packed;

struct synthvid_version_resp {
	u32 version;
	u8 is_accepted;
	u8 max_video_outputs;
} __packed;

struct synthvid_vram_location {
	u64 user_ctx;
	u8 is_vram_gpa_specified;
	u64 vram_gpa;
} __packed;

struct synthvid_vram_location_ack {
	u64 user_ctx;
} __packed;

struct video_output_situation {
	u8 active;
	u32 vram_offset;
	u8 depth_bits;
	u32 width_pixels;
	u32 height_pixels;
	u32 pitch_bytes;
} __packed;

struct synthvid_situation_update {
	u64 user_ctx;
	u8 video_output_count;
	struct video_output_situation video_output[1];
} __packed;

struct synthvid_situation_update_ack {
	u64 user_ctx;
} __packed;

struct synthvid_pointer_position {
	u8 is_visible;
	u8 video_output;
	s32 image_x;
	s32 image_y;
} __packed;


#define CURSOR_MAX_X 96
#define CURSOR_MAX_Y 96
#define CURSOR_ARGB_PIXEL_SIZE 4
#define CURSOR_MAX_SIZE (CURSOR_MAX_X * CURSOR_MAX_Y * CURSOR_ARGB_PIXEL_SIZE)
#define CURSOR_COMPLETE (-1)

struct synthvid_pointer_shape {
	u8 part_idx;
	u8 is_argb;
	u32 width; /* CURSOR_MAX_X at most */
	u32 height; /* CURSOR_MAX_Y at most */
	u32 hot_x; /* hotspot relative to upper-left of pointer image */
	u32 hot_y;
	u8 data[4];
} __packed;

struct synthvid_feature_change {
	u8 is_dirt_needed;
	u8 is_ptr_pos_needed;
	u8 is_ptr_shape_needed;
	u8 is_situ_needed;
} __packed;

struct rect {
	s32 x1, y1; /* top left corner */
	s32 x2, y2; /* bottom right corner, exclusive */
} __packed;

struct synthvid_dirt {
	u8 video_output;
	u8 dirt_count;
	struct rect rect[1];
} __packed;

struct synthvid_msg {
	struct pipe_msg_hdr pipe_hdr;
	struct synthvid_msg_hdr vid_hdr;
	union {
		struct synthvid_version_req ver_req;
		struct synthvid_version_resp ver_resp;
		struct synthvid_vram_location vram;
		struct synthvid_vram_location_ack vram_ack;
		struct synthvid_situation_update situ;
		struct synthvid_situation_update_ack situ_ack;
		struct synthvid_pointer_position ptr_pos;
		struct synthvid_pointer_shape ptr_shape;
		struct synthvid_feature_change feature_chg;
		struct synthvid_dirt dirt;
	};
} __packed;



/* FB driver definitions and structures */
#define HVFB_WIDTH 1152 /* default screen width */
#define HVFB_HEIGHT 864 /* default screen height */
#define HVFB_WIDTH_MIN 640
#define HVFB_HEIGHT_MIN 480

#define RING_BUFSIZE (256 * 1024)
#define VSP_TIMEOUT (10 * HZ)
#define HVFB_UPDATE_DELAY (HZ / 30)

struct hvfb_par {
	struct fb_info *info;
	bool info_ready; /* info and its fields are set up */
	struct completion wait;
	u32 synthvid_version;

	struct delayed_work dwork;
	spinlock_t area_lock; /* protect changed area */
	int x1, y1, x2, y2; /* changed rectangle area */

	u32 pseudo_palette[16];
	u8 init_buf[MAX_VMBUS_PKT_SIZE];
	u8 recv_buf[MAX_VMBUS_PKT_SIZE];
};

static uint screen_width = HVFB_WIDTH;
static uint screen_height = HVFB_HEIGHT;
static uint screen_depth;
static uint screen_fb_size;

/* Send message to Hyper-V host */
static inline int synthvid_send(struct hv_device *hdev,
				struct synthvid_msg *msg)
{
	static atomic64_t request_id = ATOMIC64_INIT(0);
	int ret;

	msg->pipe_hdr.type = PIPE_MSG_DATA;
	msg->pipe_hdr.size = msg->vid_hdr.size;

	ret = vmbus_sendpacket(hdev->channel, msg,
			       msg->vid_hdr.size + sizeof(struct pipe_msg_hdr),
			       atomic64_inc_return(&request_id),
			       VM_PKT_DATA_INBAND, 0);

	if (ret)
		pr_err("Unable to send packet via vmbus\n");

	return ret;
}


/* Send screen resolution info to host */
static int synthvid_send_situ(struct hv_device *hdev)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par;
	struct synthvid_msg msg;

	if (!info)
		return -ENODEV;

	par = info->par;
	if (!par->info_ready)
		return -ENODEV;

	memset(&msg, 0, sizeof(struct synthvid_msg));

	msg.vid_hdr.type = SYNTHVID_SITUATION_UPDATE;
	msg.vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_situation_update);
	msg.situ.user_ctx = 0;
	msg.situ.video_output_count = 1;
	msg.situ.video_output[0].active = 1;
	msg.situ.video_output[0].vram_offset = 0;
	msg.situ.video_output[0].depth_bits = info->var.bits_per_pixel;
	msg.situ.video_output[0].width_pixels = info->var.xres;
	msg.situ.video_output[0].height_pixels = info->var.yres;
	msg.situ.video_output[0].pitch_bytes = info->fix.line_length;

	synthvid_send(hdev, &msg);

	return 0;
}

/* Send mouse pointer info to host */
static int synthvid_send_ptr(struct hv_device *hdev)
{
	struct synthvid_msg msg;

	memset(&msg, 0, sizeof(struct synthvid_msg));
	msg.vid_hdr.type = SYNTHVID_POINTER_POSITION;
	msg.vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_pointer_position);
	msg.ptr_pos.is_visible = 1;
	msg.ptr_pos.video_output = 0;
	msg.ptr_pos.image_x = 0;
	msg.ptr_pos.image_y = 0;
	synthvid_send(hdev, &msg);

	memset(&msg, 0, sizeof(struct synthvid_msg));
	msg.vid_hdr.type = SYNTHVID_POINTER_SHAPE;
	msg.vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_pointer_shape);
	msg.ptr_shape.part_idx = CURSOR_COMPLETE;
	msg.ptr_shape.is_argb = 1;
	msg.ptr_shape.width = 1;
	msg.ptr_shape.height = 1;
	msg.ptr_shape.hot_x = 0;
	msg.ptr_shape.hot_y = 0;
	msg.ptr_shape.data[0] = 0;
	msg.ptr_shape.data[1] = 1;
	msg.ptr_shape.data[2] = 1;
	msg.ptr_shape.data[3] = 1;
	synthvid_send(hdev, &msg);

	return 0;
}

/* Send updated screen area (dirty rectangle) location to host */
static int synthvid_update(struct fb_info *info, int x1, int y1,
			   int x2, int y2)
{
	struct hv_device *hdev = device_to_hv_device(info->device);
	struct synthvid_msg msg;

	memset(&msg, 0, sizeof(struct synthvid_msg));

	msg.vid_hdr.type = SYNTHVID_DIRT;
	msg.vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_dirt);
	msg.dirt.video_output = 0;
	msg.dirt.dirt_count = 1;
	msg.dirt.rect[0].x1 = x1;
	msg.dirt.rect[0].y1 = y1;
	msg.dirt.rect[0].x2 = x2;
	msg.dirt.rect[0].y2 = y2;

	synthvid_send(hdev, &msg);

	return 0;
}


/*
 * Actions on received messages from host:
 * Complete the wait event.
 * Or, reply with screen and cursor info.
 */
static void synthvid_recv_sub(struct hv_device *hdev)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par;
	struct synthvid_msg *msg;

	if (!info)
		return;

	par = info->par;
	msg = (struct synthvid_msg *)par->recv_buf;

	/* Complete the wait event */
	if (msg->vid_hdr.type == SYNTHVID_VERSION_RESPONSE ||
	    msg->vid_hdr.type == SYNTHVID_VRAM_LOCATION_ACK) {
		memcpy(par->init_buf, msg, MAX_VMBUS_PKT_SIZE);
		complete(&par->wait);
		return;
	}

	/* Reply with screen and cursor info */
	if (msg->vid_hdr.type == SYNTHVID_FEATURE_CHANGE) {
		synthvid_send_situ(hdev);
		synthvid_send_ptr(hdev);
	}

}

/* Receive callback for messages from the host */
static void synthvid_receive(void *ctx)
{
	struct hv_device *hdev = ctx;
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par;
	struct synthvid_msg *recv_buf;
	u32 bytes_recvd;
	u64 req_id;
	int ret;

	if (!info)
		return;

	par = info->par;
	recv_buf = (struct synthvid_msg *)par->recv_buf;

	do {
		ret = vmbus_recvpacket(hdev->channel, recv_buf,
				       MAX_VMBUS_PKT_SIZE,
				       &bytes_recvd, &req_id);
		if (bytes_recvd > 0 &&
		    recv_buf->pipe_hdr.type == PIPE_MSG_DATA)
			synthvid_recv_sub(hdev);
	} while (bytes_recvd > 0 && ret == 0);
}

/* Check synthetic video protocol version with the host */
static int synthvid_negotiate_ver(struct hv_device *hdev, u32 ver)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par = info->par;
	struct synthvid_msg *msg = (struct synthvid_msg *)par->init_buf;
	int t, ret = 0;

	memset(msg, 0, sizeof(struct synthvid_msg));
	msg->vid_hdr.type = SYNTHVID_VERSION_REQUEST;
	msg->vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_version_req);
	msg->ver_req.version = ver;
	synthvid_send(hdev, msg);

	t = wait_for_completion_timeout(&par->wait, VSP_TIMEOUT);
	if (!t) {
		pr_err("Time out on waiting version response\n");
		ret = -ETIMEDOUT;
		goto out;
	}
	if (!msg->ver_resp.is_accepted) {
		ret = -ENODEV;
		goto out;
	}

	par->synthvid_version = ver;

out:
	return ret;
}

/* Connect to VSP (Virtual Service Provider) on host */
static int synthvid_connect_vsp(struct hv_device *hdev)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par = info->par;
	int ret;

	ret = vmbus_open(hdev->channel, RING_BUFSIZE, RING_BUFSIZE,
			 NULL, 0, synthvid_receive, hdev);
	if (ret) {
		pr_err("Unable to open vmbus channel\n");
		return ret;
	}

	/* Negotiate the protocol version with host */
	if (vmbus_proto_version == VERSION_WS2008 ||
	    vmbus_proto_version == VERSION_WIN7)
		ret = synthvid_negotiate_ver(hdev, SYNTHVID_VERSION_WIN7);
	else
		ret = synthvid_negotiate_ver(hdev, SYNTHVID_VERSION_WIN8);

	if (ret) {
		pr_err("Synthetic video device version not accepted\n");
		goto error;
	}

	if (par->synthvid_version == SYNTHVID_VERSION_WIN7) {
		screen_depth = SYNTHVID_DEPTH_WIN7;
		screen_fb_size = SYNTHVID_FB_SIZE_WIN7;
	} else {
		screen_depth = SYNTHVID_DEPTH_WIN8;
		screen_fb_size = SYNTHVID_FB_SIZE_WIN8;
	}

	return 0;

error:
	vmbus_close(hdev->channel);
	return ret;
}

/* Send VRAM and Situation messages to the host */
static int synthvid_send_config(struct hv_device *hdev)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	struct hvfb_par *par = info->par;
	struct synthvid_msg *msg = (struct synthvid_msg *)par->init_buf;
	int t, ret = 0;

	/* Send VRAM location */
	memset(msg, 0, sizeof(struct synthvid_msg));
	msg->vid_hdr.type = SYNTHVID_VRAM_LOCATION;
	msg->vid_hdr.size = sizeof(struct synthvid_msg_hdr) +
		sizeof(struct synthvid_vram_location);
	msg->vram.user_ctx = msg->vram.vram_gpa =
		virt_to_phys(info->screen_base);
	msg->vram.is_vram_gpa_specified = 1;
	synthvid_send(hdev, msg);

	t = wait_for_completion_timeout(&par->wait, VSP_TIMEOUT);
	if (!t) {
		pr_err("Time out on waiting vram location ack\n");
		ret = -ETIMEDOUT;
		goto error;
	}
	if (msg->vram_ack.user_ctx != virt_to_phys(info->screen_base)) {
		pr_err("Unable to set VRAM location\n");
		ret = -ENODEV;
		goto error;
	}

	/* Send situation and pointer update */
	synthvid_send_situ(hdev);
	synthvid_send_ptr(hdev);

	return 0;

error:
	return ret;
}


static inline void hvfb_init_rect(struct hvfb_par *par)
{
	par->x1 = par->y1 = INT_MAX;
	par->x2 = par->y2 = 0;
}

/*
 * Delayed work callback:
 * It is called at HVFB_UPDATE_DELAY or longer time interval to process
 * screen updates from non-deferred I/O, such as imageblit, which can happen
 * at high frequency.
 */
static void hvfb_update_work(struct work_struct *w)
{
	struct hvfb_par *par = container_of(w, struct hvfb_par, dwork.work);
	struct fb_info *info = par->info;
	int x1, y1, x2, y2;
	ulong flags;

	spin_lock_irqsave(&par->area_lock, flags);

	x1 = par->x1;
	y1 = par->y1;
	x2 = par->x2;
	y2 = par->y2;

	hvfb_init_rect(par);

	spin_unlock_irqrestore(&par->area_lock, flags);

	if (x1 == INT_MAX)
		return;

	synthvid_update(info, x1, y1, x2, y2);
}

/*
 * Record the updated screen area, and schedule the delayed work.
 * If the work is still on the queue, schedule_delayed_work() will
 * automatically ignore the new request. But the updated area is
 * always recorded, and will be handled by the queued work.
 */
static void hvfb_add_rect(struct fb_info *info, int x1, int y1,
			  int x2, int y2)
{
	struct hvfb_par *par = info->par;
	ulong flags;

	spin_lock_irqsave(&par->area_lock, flags);

	if (x1 < par->x1)
		par->x1 = x1;
	if (y1 < par->y1)
		par->y1 = y1;

	if (x2 > par->x2)
		par->x2 = x2;
	if (y2 > par->y2)
		par->y2 = y2;

	spin_unlock_irqrestore(&par->area_lock, flags);

	schedule_delayed_work(&par->dwork, HVFB_UPDATE_DELAY);
}



/* Framebuffer operation handlers */

static ssize_t hvfb_write(struct fb_info *info, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	ssize_t ret;

	ret = fb_sys_write(info, buf, count, ppos);

	hvfb_add_rect(info, 0, 0,
		      info->var.xres, info->var.yres);

	return ret;
}


static int hvfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	if (var->xres < HVFB_WIDTH_MIN || var->yres < HVFB_HEIGHT_MIN ||
	    var->xres > screen_width || var->yres >  screen_height ||
	    var->bits_per_pixel != screen_depth)
		return -EINVAL;

	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;

	return 0;
}

static int hvfb_set_par(struct fb_info *info)
{
	struct hv_device *hdev = device_to_hv_device(info->device);

	return synthvid_send_situ(hdev);
}


static inline u32 chan_to_field(u32 chan, struct fb_bitfield *bf)
{
	return ((chan & 0xffff) >> (16 - bf->length)) << bf->offset;
}

static int hvfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			  unsigned blue, unsigned transp, struct fb_info *info)
{
	u32 *pal = info->pseudo_palette;

	if (regno > 15)
		return -EINVAL;

	pal[regno] = chan_to_field(red, &info->var.red)
		| chan_to_field(green, &info->var.green)
		| chan_to_field(blue, &info->var.blue);

	return 0;
}

static void hvfb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	sys_fillrect(info, rect);

	hvfb_add_rect(info, rect->dx, rect->dy,
		      rect->dx + rect->width, rect->dy + rect->height);
}

static void hvfb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	sys_copyarea(info, area);

	hvfb_add_rect(info, area->dx, area->dy,
		      area->dx + area->width, area->dy + area->height);
}

static void hvfb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	sys_imageblit(info, image);

	hvfb_add_rect(info, image->dx, image->dy,
		      image->dx + image->width, image->dy + image->height);
}

static struct fb_ops hvfb_ops = {
	.owner = THIS_MODULE,
	.fb_read = fb_sys_read,
	.fb_write = hvfb_write,
	.fb_check_var = hvfb_check_var,
	.fb_set_par = hvfb_set_par,
	.fb_setcolreg = hvfb_setcolreg,
	.fb_fillrect = hvfb_fillrect,
	.fb_copyarea = hvfb_copyarea,
	.fb_imageblit = hvfb_imageblit,
};


/*
 * Callback for the deferred I/O.
 * Applications, such as X-Window, use deferred I/O mechanism to put screen
 * updates in batches, and send to fb driver periodically.
 */
static void hvfb_deferred_io(struct fb_info *info, struct list_head *pl)
{
	struct page *page;
	ulong pa, pb, min = ULONG_MAX, max = 0;
	int ymin, ymax;

	list_for_each_entry(page, pl, lru) {
		pa = page->index << PAGE_SHIFT;
		pb = pa + PAGE_SIZE - 1;
		if (pa < min)
			min = pa;
		if (pb > max)
			max = pb;
	}

	if (max == 0)
		return;

	ymin = min / info->fix.line_length;
	ymax = max / info->fix.line_length + 1;
	if (ymax > info->var.yres)
		ymax = info->var.yres;

	synthvid_update(info, 0, ymin, info->var.xres, ymax);
}

static struct fb_deferred_io hvfb_defio = {
	.delay = HVFB_UPDATE_DELAY,
	.deferred_io = hvfb_deferred_io,
};


/* Get options from kernel paramenter "video=" */
static void hvfb_get_option(struct fb_info *info)
{
	struct hvfb_par *par = info->par;
	char *opt = NULL, *p;
	uint x = 0, y = 0;

	if (fb_get_options("hyperv_fb", &opt) || !opt || !*opt)
		return;

	p = strsep(&opt, "x");
	if (!*p || kstrtouint(p, 0, &x) ||
	    !opt || !*opt || kstrtouint(opt, 0, &y)) {
		pr_err("Screen option is invalid: skipped\n");
		return;
	}

	if (x < HVFB_WIDTH_MIN || y < HVFB_HEIGHT_MIN ||
	    (par->synthvid_version == SYNTHVID_VERSION_WIN8 &&
	     x * y * screen_depth / 8 > SYNTHVID_FB_SIZE_WIN8) ||
	    (par->synthvid_version == SYNTHVID_VERSION_WIN7 &&
	     (x > SYNTHVID_WIDTH_MAX_WIN7 || y > SYNTHVID_HEIGHT_MAX_WIN7))) {
		pr_err("Screen resolution option is out of range: skipped\n");
		return;
	}

	screen_width = x;
	screen_height = y;
	return;
}


/* Allocate framebuffer memory */
#define FOUR_MEGA (4*1024*1024)
#define NALLOC 10
static void *hvfb_getmem(void)
{
	ulong *p;
	int i, j;
	ulong ret = 0;

	if (screen_fb_size == FOUR_MEGA) {
		ret = __get_free_pages(GFP_KERNEL|__GFP_ZERO,
				       get_order(FOUR_MEGA));
		goto out1;
	}

	if (screen_fb_size != FOUR_MEGA * 2)
		return NULL;

	/*
	 * Windows 2012 requires frame buffer size to be 8MB, which exceeds
	 * the limit of __get_free_pages(). So, we allocate multiple 4MB
	 * chunks, and find out two adjacent ones.
	 */
	p = kmalloc(NALLOC * sizeof(ulong), GFP_KERNEL);
	if (!p)
		return NULL;

	for (i = 0; i < NALLOC; i++)
		p[i] = __get_free_pages(GFP_KERNEL|__GFP_ZERO,
					get_order(FOUR_MEGA));

	for (i = 0; i < NALLOC; i++)
		for (j = 0; j < NALLOC; j++) {
			if (p[j] && p[i] && virt_to_phys((void *)p[j]) -
			    virt_to_phys((void *)p[i]) == FOUR_MEGA &&
			    p[j] - p[i] == FOUR_MEGA) {
				ret = p[i];
				goto out;
			}
		}

out:
	for (i = 0; i < NALLOC; i++)
		if (p[i] && !(ret && (p[i] == ret || p[i] == ret + FOUR_MEGA)))
			free_pages(p[i], get_order(FOUR_MEGA));

	kfree(p);

out1:
	if (!ret)
		return NULL;

	/* Get an extra ref-count to prevent page error when x-window exits */
	for (i = 0; i < screen_fb_size; i += PAGE_SIZE)
		atomic_inc(&virt_to_page((void *)ret + i)->_count);

	return (void *)ret;
}

/* Free the frame buffer */
static void hvfb_putmem(void *p)
{
	int i;

	if (!p)
		return;

	for (i = 0; i < screen_fb_size; i += PAGE_SIZE)
		atomic_dec(&virt_to_page(p + i)->_count);

	free_pages((ulong)p, get_order(FOUR_MEGA));

	if (screen_fb_size == FOUR_MEGA * 2)
		free_pages((ulong)p + FOUR_MEGA, get_order(FOUR_MEGA));
}


static int hvfb_probe(struct hv_device *hdev,
		      const struct hv_vmbus_device_id *dev_id)
{
	struct fb_info *info;
	void *fb = NULL;
	struct hvfb_par *par;
	int ret = 0;

	info = framebuffer_alloc(sizeof(struct hvfb_par), &hdev->device);
	if (!info) {
		pr_err("No memory for framebuffer info\n");
		return -ENOMEM;
	}

	par = info->par;
	par->info = info;
	par->info_ready = false;
	init_completion(&par->wait);

	/* Connect to VSP */
	hv_set_drvdata(hdev, info);
	ret = synthvid_connect_vsp(hdev);
	if (ret) {
		pr_err("Unable to connect to VSP\n");
		goto error1;
	}

	fb = hvfb_getmem();
	if (!fb) {
		pr_err("No memory for framebuffer\n");
		ret = -ENOMEM;
		goto error2;
	}

	hvfb_get_option(info);
	pr_info("Screen resolution: %dx%d, Color depth: %d\n",
		screen_width, screen_height, screen_depth);


	/* Set up fb_info */
	info->flags = FBINFO_DEFAULT | FBINFO_VIRTFB;

	info->var.xres_virtual = info->var.xres = screen_width;
	info->var.yres_virtual = info->var.yres = screen_height;
	info->var.bits_per_pixel = screen_depth;

	if (info->var.bits_per_pixel == 16) {
		info->var.red = (struct fb_bitfield){11, 5, 0};
		info->var.green = (struct fb_bitfield){5, 6, 0};
		info->var.blue = (struct fb_bitfield){0, 5, 0};

	} else {
		info->var.red = (struct fb_bitfield){16, 8, 0};
		info->var.green = (struct fb_bitfield){8, 8, 0};
		info->var.blue = (struct fb_bitfield){0, 8, 0};
	}

	info->var.activate = FB_ACTIVATE_NOW;
	info->var.height = -1;
	info->var.width = -1;
	info->var.vmode = FB_VMODE_NONINTERLACED;

	strcpy(info->fix.id, "hyperv");
	info->fix.smem_start = virt_to_phys(fb);
	info->fix.smem_len = screen_width * screen_height * screen_depth / 8;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.line_length = screen_width * screen_depth / 8;
	info->fix.accel = FB_ACCEL_NONE;

	info->fbops = &hvfb_ops;
	info->screen_base = fb;
	info->pseudo_palette = par->pseudo_palette;

	par->info_ready = true;

	/* Send config to host */
	ret = synthvid_send_config(hdev);
	if (ret)
		goto error2;

	info->fbdefio = &hvfb_defio;
	fb_deferred_io_init(info);

	INIT_DELAYED_WORK(&par->dwork, hvfb_update_work);
	spin_lock_init(&par->area_lock);
	hvfb_init_rect(par);
	ret = register_framebuffer(info);
	if (ret) {
		pr_err("Unable to register framebuffer\n");
		goto error;
	}

	return 0;

error:
	cancel_delayed_work_sync(&par->dwork);
	fb_deferred_io_cleanup(info);
error2:
	vmbus_close(hdev->channel);
error1:
	hv_set_drvdata(hdev, NULL);
	framebuffer_release(info);
	hvfb_putmem(fb);
	return ret;
}


static int hvfb_remove(struct hv_device *hdev)
{
	struct fb_info *info = hv_get_drvdata(hdev);
	void *fb = info->screen_base;
	struct hvfb_par *par = info->par;

	fb_deferred_io_cleanup(info);
	unregister_framebuffer(info);
	cancel_delayed_work_sync(&par->dwork);

	vmbus_close(hdev->channel);
	hv_set_drvdata(hdev, NULL);

	framebuffer_release(info);
	hvfb_putmem(fb);

	return 0;
}


static const struct hv_vmbus_device_id id_table[] = {
	/* Synthetic Video Device GUID */
	{HV_SYNTHVID_GUID},
	{}
};

MODULE_DEVICE_TABLE(vmbus, id_table);

static struct hv_driver hvfb_drv = {
	.name = KBUILD_MODNAME,
	.id_table = id_table,
	.probe = hvfb_probe,
	.remove = hvfb_remove,
};


static int __init hvfb_drv_init(void)
{
	return vmbus_driver_register(&hvfb_drv);
}

static void __exit hvfb_drv_exit(void)
{
	vmbus_driver_unregister(&hvfb_drv);
}

module_init(hvfb_drv_init);
module_exit(hvfb_drv_exit);

MODULE_LICENSE("GPL");
MODULE_VERSION(HV_DRV_VERSION);
MODULE_DESCRIPTION("Microsoft Hyper-V Synthetic Video Frame Buffer Driver");
