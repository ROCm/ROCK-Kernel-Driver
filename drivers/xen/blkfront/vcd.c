/*******************************************************************************
 * vcd.c
 *
 * Implements CDROM cmd packet passing between frontend guest and backend driver.
 *
 * Copyright (c) 2008, Pat Campell  plc@novell.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#define REVISION "$Revision: 1.0 $"

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/list.h>
#include <linux/cdrom.h>
#include <xen/interface/io/cdromif.h>
#include "block.h"

/* List of cdrom_device_info, can have as many as blkfront supports */
struct vcd_disk {
	struct list_head vcd_entry;
	struct cdrom_device_info vcd_cdrom_info;
	spinlock_t vcd_cdrom_info_lock;
};
static LIST_HEAD(vcd_disks);
static DEFINE_SPINLOCK(vcd_disks_lock);

static struct vcd_disk *xencdrom_get_list_entry(struct gendisk *disk)
{
	struct vcd_disk *ret_vcd = NULL;
	struct vcd_disk *vcd;

	spin_lock(&vcd_disks_lock);
	list_for_each_entry(vcd, &vcd_disks, vcd_entry) {
		if (vcd->vcd_cdrom_info.disk == disk) {
			spin_lock(&vcd->vcd_cdrom_info_lock);
			ret_vcd = vcd;
			break;
		}
	}
	spin_unlock(&vcd_disks_lock);
	return ret_vcd;
}

static void submit_message(struct blkfront_info *info, void *sp)
{
	struct request *req = NULL;

	req = blk_get_request(info->rq, READ, __GFP_WAIT);
	if (blk_rq_map_kern(info->rq, req, sp, PAGE_SIZE, __GFP_WAIT))
		goto out;

	req->rq_disk = info->gd;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18)
	req->cmd_type = REQ_TYPE_BLOCK_PC;
	req->cmd_flags |= REQ_NOMERGE;
#else
	req->flags |= REQ_BLOCK_PC;
#endif
	req->__sector = 0;
	req->__data_len = PAGE_SIZE;
	req->timeout = 60*HZ;

	blk_execute_rq(req->q, info->gd, req, 1);

out:
	blk_put_request(req);
}

static int submit_cdrom_cmd(struct blkfront_info *info,
			    struct packet_command *cgc)
{
	int ret = 0;
	struct page *page;
	size_t size;
	union xen_block_packet *sp;
	struct xen_cdrom_packet *xcp;
	struct vcd_generic_command *vgc;

	if (cgc->buffer && cgc->buflen > MAX_PACKET_DATA) {
		printk(KERN_WARNING "%s() Packet buffer length is to large \n", __func__);
		return -EIO;
	}

	page = alloc_page(GFP_NOIO);
	if (!page) {
		printk(KERN_CRIT "%s() Unable to allocate page\n", __func__);
		return -ENOMEM;
	}

	size = PAGE_SIZE;
	memset(page_address(page), 0, PAGE_SIZE);
	sp = page_address(page);
	xcp = &(sp->xcp);
	xcp->type = XEN_TYPE_CDROM_PACKET;
	xcp->payload_offset = PACKET_PAYLOAD_OFFSET;

	vgc = (struct vcd_generic_command *)((char *)sp + xcp->payload_offset);
	memcpy(vgc->cmd, cgc->cmd, CDROM_PACKET_SIZE);
	vgc->stat = cgc->stat;
	vgc->data_direction = cgc->data_direction;
	vgc->quiet = cgc->quiet;
	vgc->timeout = cgc->timeout;
	if (cgc->sense) {
		vgc->sense_offset = PACKET_SENSE_OFFSET;
		memcpy((char *)sp + vgc->sense_offset, cgc->sense, sizeof(struct request_sense));
	}
	if (cgc->buffer) {
		vgc->buffer_offset = PACKET_BUFFER_OFFSET;
		memcpy((char *)sp + vgc->buffer_offset, cgc->buffer, cgc->buflen);
		vgc->buflen = cgc->buflen;
	}

	submit_message(info,sp);

	if (xcp->ret)
		ret = xcp->err;

	if (cgc->sense) {
		memcpy(cgc->sense, (char *)sp + PACKET_SENSE_OFFSET, sizeof(struct request_sense));
	}
	if (cgc->buffer && cgc->buflen) {
		memcpy(cgc->buffer, (char *)sp + PACKET_BUFFER_OFFSET, cgc->buflen);
	}

	__free_page(page);
	return ret;
}


static int xencdrom_open(struct cdrom_device_info *cdi, int purpose)
{
	int ret = 0;
	struct page *page;
	struct blkfront_info *info;
	union xen_block_packet *sp;
	struct xen_cdrom_open *xco;

	info = cdi->disk->private_data;

	if (!info->xbdev)
		return -ENODEV;

	if (strlen(info->xbdev->otherend) > MAX_PACKET_DATA) {
		return -EIO;
	}

	page = alloc_page(GFP_NOIO);
	if (!page) {
		printk(KERN_CRIT "%s() Unable to allocate page\n", __func__);
		return -ENOMEM;
	}

	memset(page_address(page), 0, PAGE_SIZE);
	sp = page_address(page);
	xco = &(sp->xco);
	xco->type = XEN_TYPE_CDROM_OPEN;
	xco->payload_offset = sizeof(struct xen_cdrom_open);
	strcpy((char *)sp + xco->payload_offset, info->xbdev->otherend);

	submit_message(info,sp);

	if (xco->ret) {
		ret = xco->err;
		goto out;
	}

	if (xco->media_present)
		set_capacity(cdi->disk, xco->sectors);

out:
	__free_page(page);
	return ret;
}

static void xencdrom_release(struct cdrom_device_info *cdi)
{
}

static int xencdrom_media_changed(struct cdrom_device_info *cdi, int disc_nr)
{
	int ret;
	struct page *page;
	struct blkfront_info *info;
	union xen_block_packet *sp;
	struct xen_cdrom_media_changed *xcmc;

	info = cdi->disk->private_data;

	page = alloc_page(GFP_NOIO);
	if (!page) {
		printk(KERN_CRIT "%s() Unable to allocate page\n", __func__);
		return -ENOMEM;
	}

	memset(page_address(page), 0, PAGE_SIZE);
	sp = page_address(page);
	xcmc = &(sp->xcmc);
	xcmc->type = XEN_TYPE_CDROM_MEDIA_CHANGED;
	submit_message(info,sp);
	ret = xcmc->media_changed;

	__free_page(page);

	return ret;
}

static int xencdrom_tray_move(struct cdrom_device_info *cdi, int position)
{
	int ret;
	struct packet_command cgc;
	struct blkfront_info *info;

	info = cdi->disk->private_data;
	init_cdrom_command(&cgc, NULL, 0, CGC_DATA_NONE);
	cgc.cmd[0] = GPCMD_START_STOP_UNIT;
	if (position)
		cgc.cmd[4] = 2;
	else
		cgc.cmd[4] = 3;
	ret = submit_cdrom_cmd(info, &cgc);
	return ret;
}

static int xencdrom_lock_door(struct cdrom_device_info *cdi, int lock)
{
	int ret = 0;
	struct blkfront_info *info;
	struct packet_command cgc;

	info = cdi->disk->private_data;
	init_cdrom_command(&cgc, NULL, 0, CGC_DATA_NONE);
	cgc.cmd[0] = GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL;
	cgc.cmd[4] = lock;
	ret = submit_cdrom_cmd(info, &cgc);
	return ret;
}

static int xencdrom_packet(struct cdrom_device_info *cdi,
		struct packet_command *cgc)
{
	int ret = -EIO;
	struct blkfront_info *info;

	info = cdi->disk->private_data;
	ret = submit_cdrom_cmd(info, cgc);
	cgc->stat = ret;
	return ret;
}

static int xencdrom_audio_ioctl(struct cdrom_device_info *cdi, unsigned int cmd,
		void *arg)
{
	return -EINVAL;
}

/* Query backend to see if CDROM packets are supported */
static int xencdrom_supported(struct blkfront_info *info)
{
	struct page *page;
	union xen_block_packet *sp;
	struct xen_cdrom_support *xcs;

	page = alloc_page(GFP_NOIO);
	if (!page) {
		printk(KERN_CRIT "%s() Unable to allocate page\n", __func__);
		return -ENOMEM;
	}

	memset(page_address(page), 0, PAGE_SIZE);
	sp = page_address(page);
	xcs = &(sp->xcs);
	xcs->type = XEN_TYPE_CDROM_SUPPORT;
	submit_message(info,sp);
	return xcs->supported;
}

static struct cdrom_device_ops xencdrom_dops = {
    .open           = xencdrom_open,
    .release        = xencdrom_release,
    .media_changed  = xencdrom_media_changed,
    .tray_move      = xencdrom_tray_move,
    .lock_door      = xencdrom_lock_door,
    .generic_packet = xencdrom_packet,
    .audio_ioctl    = xencdrom_audio_ioctl,
    .capability     = (CDC_CLOSE_TRAY | CDC_OPEN_TRAY | CDC_LOCK | \
                       CDC_MEDIA_CHANGED | CDC_GENERIC_PACKET |  CDC_DVD | \
                       CDC_CD_R),
    .n_minors       = 1,
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
static int xencdrom_block_open(struct inode *inode, struct file *file)
{
	struct block_device *bd = inode->i_bdev;
#else
static int xencdrom_block_open(struct block_device *bd, fmode_t mode)
{
#endif
	struct blkfront_info *info = bd->bd_disk->private_data;
	struct vcd_disk *vcd;
	int ret = 0;

	if (!info->xbdev)
		return -ENODEV;

	if ((vcd = xencdrom_get_list_entry(info->gd))) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
		ret = cdrom_open(&vcd->vcd_cdrom_info, inode, file);
#else
		ret = cdrom_open(&vcd->vcd_cdrom_info, bd, mode);
#endif
		info->users = vcd->vcd_cdrom_info.use_count;
		spin_unlock(&vcd->vcd_cdrom_info_lock);
	}
	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
static int xencdrom_block_release(struct inode *inode, struct file *file)
{
	struct gendisk *gd = inode->i_bdev->bd_disk;
#else
static int xencdrom_block_release(struct gendisk *gd, fmode_t mode)
{
#endif
	struct blkfront_info *info = gd->private_data;
	struct vcd_disk *vcd;
	int ret = 0;

	if ((vcd = xencdrom_get_list_entry(info->gd))) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
		ret = cdrom_release(&vcd->vcd_cdrom_info, file);
#else
		cdrom_release(&vcd->vcd_cdrom_info, mode);
#endif
		spin_unlock(&vcd->vcd_cdrom_info_lock);
		if (vcd->vcd_cdrom_info.use_count == 0) {
			info->users = 1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
			blkif_release(inode, file);
#else
			blkif_release(gd, mode);
#endif
		}
	}
	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
static int xencdrom_block_ioctl(struct inode *inode, struct file *file,
				unsigned cmd, unsigned long arg)
{
	struct block_device *bd = inode->i_bdev;
#else
static int xencdrom_block_ioctl(struct block_device *bd, fmode_t mode,
				unsigned cmd, unsigned long arg)
{
#endif
	struct blkfront_info *info = bd->bd_disk->private_data;
	struct vcd_disk *vcd;
	int ret = 0;

	if (!(vcd = xencdrom_get_list_entry(info->gd)))
		goto out;

	switch (cmd) {
	case 2285: /* SG_IO */
		ret = -ENOSYS;
		break;
	case CDROMEJECT:
		ret = xencdrom_tray_move(&vcd->vcd_cdrom_info, 1);
		break;
	case CDROMCLOSETRAY:
		ret = xencdrom_tray_move(&vcd->vcd_cdrom_info, 0);
		break;
	case CDROM_GET_CAPABILITY:
		ret = vcd->vcd_cdrom_info.ops->capability & ~vcd->vcd_cdrom_info.mask;
		break;
	case CDROM_SET_OPTIONS:
		ret = vcd->vcd_cdrom_info.options;
		break;
	case CDROM_SEND_PACKET:
		ret = submit_cdrom_cmd(info, (struct packet_command *)arg);
		break;
	default:
		/* Not supported, augment supported above if necessary */
		printk("%s():%d Unsupported IOCTL:%x \n", __func__, __LINE__, cmd);
		ret = -ENOTTY;
		break;
	}
	spin_unlock(&vcd->vcd_cdrom_info_lock);
out:
	return ret;
}

/* Called as result of cdrom_open, vcd_cdrom_info_lock already held */
static int xencdrom_block_media_changed(struct gendisk *disk)
{
	struct vcd_disk *vcd;
	struct vcd_disk *ret_vcd = NULL;
	int ret = 0;

	spin_lock(&vcd_disks_lock);
	list_for_each_entry(vcd, &vcd_disks, vcd_entry) {
		if (vcd->vcd_cdrom_info.disk == disk) {
			ret_vcd = vcd;
			break;
		}
	}
	spin_unlock(&vcd_disks_lock);
	if (ret_vcd) {
		ret = cdrom_media_changed(&ret_vcd->vcd_cdrom_info);
	}
	return ret;
}

static const struct block_device_operations xencdrom_bdops =
{
	.owner		= THIS_MODULE,
	.open		= xencdrom_block_open,
	.release	= xencdrom_block_release,
	.ioctl		= xencdrom_block_ioctl,
	.media_changed	= xencdrom_block_media_changed,
};

void register_vcd(struct blkfront_info *info)
{
	struct gendisk *gd = info->gd;
	struct vcd_disk *vcd;

	/* Make sure this is for a CD device */
	if (!(gd->flags & GENHD_FL_CD))
		goto out;

	/* Make sure we have backend support */
	if (!xencdrom_supported(info)) {
		goto out;
	}

	/* Create new vcd_disk and fill in cdrom_info */
	vcd = (struct vcd_disk *)kzalloc(sizeof(struct vcd_disk), GFP_KERNEL);
	if (!vcd) {
		printk(KERN_INFO "%s():  Unable to allocate vcd struct!\n", __func__);
		goto out;
	}
	spin_lock_init(&vcd->vcd_cdrom_info_lock);

	vcd->vcd_cdrom_info.ops	= &xencdrom_dops;
	vcd->vcd_cdrom_info.speed = 4;
	vcd->vcd_cdrom_info.capacity = 1;
	vcd->vcd_cdrom_info.options	= 0;
	strcpy(vcd->vcd_cdrom_info.name, gd->disk_name);
	vcd->vcd_cdrom_info.mask = (CDC_CD_RW | CDC_DVD_R | CDC_DVD_RAM |
			CDC_SELECT_DISC | CDC_SELECT_SPEED |
			CDC_MRW | CDC_MRW_W | CDC_RAM);

	if (register_cdrom(&(vcd->vcd_cdrom_info)) != 0) {
		printk(KERN_WARNING "%s() Cannot register blkdev as a cdrom %d!\n", __func__,
				gd->major);
		goto err_out;
	}
	gd->fops = &xencdrom_bdops;
	vcd->vcd_cdrom_info.disk = gd;

	spin_lock(&vcd_disks_lock);
	list_add(&(vcd->vcd_entry), &vcd_disks);
	spin_unlock(&vcd_disks_lock);
out:
	return;
err_out:
	kfree(vcd);
}

void unregister_vcd(struct blkfront_info *info) {
	struct gendisk *gd = info->gd;
	struct vcd_disk *vcd;

	spin_lock(&vcd_disks_lock);
	list_for_each_entry(vcd, &vcd_disks, vcd_entry) {
		if (vcd->vcd_cdrom_info.disk == gd) {
			spin_lock(&vcd->vcd_cdrom_info_lock);
			unregister_cdrom(&vcd->vcd_cdrom_info);
			list_del(&vcd->vcd_entry);
			spin_unlock(&vcd->vcd_cdrom_info_lock);
			kfree(vcd);
			break;
		}
	}
	spin_unlock(&vcd_disks_lock);
}

