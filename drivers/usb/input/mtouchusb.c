/******************************************************************************
 * mtouchusb.c  --  Driver for Microtouch (Now 3M) USB Touchscreens
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Based upon original work by Radoslaw Garbacz (usb-support@ite.pl)
 *  (http://freshmeat.net/projects/3mtouchscreendriver)
 *
 * History
 *
 *  0.3 & 0.4  2002 (TEJ) tejohnson@yahoo.com
 *    Updated to 2.4.18, then 2.4.19
 *    Old version still relied on stealing a minor
 *
 *  0.5  02/26/2004 (TEJ) tejohnson@yahoo.com
 *    Complete rewrite using Linux Input in 2.6.3
 *    Unfortunately no calibration support at this time
 *
 *****************************************************************************/

#include <linux/config.h>

#ifdef CONFIG_USB_DEBUG
        #define DEBUG
#else
        #undef DEBUG
#endif

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>

#define MTOUCHUSB_MIN_XC                0xc8
#define MTOUCHUSB_MAX_XC                0xff78
#define MTOUCHUSB_XC_FUZZ               0x0
#define MTOUCHUSB_XC_FLAT               0x0
#define MTOUCHUSB_MIN_YC                0x0
#define MTOUCHUSB_MAX_YC                0xff78
#define MTOUCHUSB_YC_FUZZ               0x0
#define MTOUCHUSB_YC_FLAT               0x0
#define MTOUCHUSB_ASYC_REPORT           1
#define MTOUCHUSB_REPORT_SIZE_DATA      11
#define MTOUCHUSB_REQ_CTRLLR_ID         10

#define MTOUCHUSB_GET_XC(data)          (data[4]<<8 | data[3])
#define MTOUCHUSB_GET_YC(data)          (data[6]<<8 | data[5])
#define MTOUCHUSB_GET_TOUCHED(data)     ((data[2] & 0x40) ? 1:0)

#define DRIVER_VERSION "v0.1"
#define DRIVER_AUTHOR "Todd E. Johnson, tejohnson@yahoo.com"
#define DRIVER_DESC "Microtouch USB HID Touchscreen Driver"

struct mtouch_usb {
        unsigned char *data;
        dma_addr_t data_dma;
        struct urb *irq;
        struct usb_device *udev;
        struct input_dev input;
        int open;
        char name[128];
        char phys[64];
};

static __s32 vendor=-1, product=-1;

static struct usb_device_id mtouchusb_devices [] = {
        { USB_DEVICE(0x0596, 0x0001) }, /* 3M (Formerly MicroTouch) 14-206 */
        { }                             /* Terminating entry */
};

static void mtouchusb_irq(struct urb *urb, struct pt_regs *regs)
{
        struct mtouch_usb *mtouch = urb->context;
        int retval;

        switch (urb->status) {
                case 0:
                        /* success */
                        break;
                case -ETIMEDOUT:
                        /* this urb is timing out */
                        dbg("%s - urb timed out - was the device unplugged?",
                            __FUNCTION__);
                        return;
                case -ECONNRESET:
                case -ENOENT:
                case -ESHUTDOWN:
                        /* this urb is terminated, clean up */
                        dbg("%s - urb shutting down with status: %d",
                            __FUNCTION__, urb->status);
                        return;
                default:
                        dbg("%s - nonzero urb status received: %d",
                            __FUNCTION__, urb->status);
                        goto exit;
        }

        input_regs(&mtouch->input, regs);
        input_report_key(&mtouch->input, BTN_TOUCH,
                         MTOUCHUSB_GET_TOUCHED(mtouch->data));
        input_report_abs(&mtouch->input, ABS_X,
                         MTOUCHUSB_GET_XC(mtouch->data));
        input_report_abs(&mtouch->input, ABS_Y,
                         MTOUCHUSB_GET_YC(mtouch->data));
        input_sync(&mtouch->input);

exit:
        retval = usb_submit_urb (urb, GFP_ATOMIC);
        if (retval)
                err ("%s - usb_submit_urb failed with result: %d",
                     __FUNCTION__, retval);
}

static int mtouchusb_open (struct input_dev *input)
{
        struct mtouch_usb *mtouch = input->private;

        if (mtouch->open++)
                return 0;

        mtouch->irq->dev = mtouch->udev;

        if (usb_submit_urb (mtouch->irq, GFP_ATOMIC))
                return -EIO;

        return 0;
}

static void mtouchusb_close (struct input_dev *input)
{
        struct mtouch_usb *mtouch = input->private;

        if (!--mtouch->open)
                usb_unlink_urb (mtouch->irq);
}

static int mtouchusb_alloc_buffers(struct usb_device *udev, struct mtouch_usb *mtouch)
{
        dbg("%s - called", __FUNCTION__);

        mtouch->data = usb_buffer_alloc(udev, MTOUCHUSB_REPORT_SIZE_DATA,
                                        SLAB_ATOMIC, &mtouch->data_dma);

        if (!mtouch->data)
                return -1;

        return 0;
}

static void mtouchusb_free_buffers(struct usb_device *udev, struct mtouch_usb *mtouch)
{
        dbg("%s - called", __FUNCTION__);

        if (mtouch->data)
                usb_buffer_free(udev, MTOUCHUSB_REPORT_SIZE_DATA,
                                mtouch->data, mtouch->data_dma);
}

static int mtouchusb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
        struct mtouch_usb *mtouch;
        struct usb_host_interface *interface;
        struct usb_endpoint_descriptor *endpoint;
        struct usb_device *udev = interface_to_usbdev (intf);
        char path[64];
        char *buf;
        int nRet;
        int ix;
        char valid_device = 0;

        dbg("%s - called", __FUNCTION__);
        if (vendor != -1 && product != -1) {
                info("%s - User specified USB Touch -- Vend:Prod - %x:%x",
                     __FUNCTION__, vendor, product);
        }

        for (ix = 0; ix < sizeof (mtouchusb_devices) /
             sizeof (struct usb_device_id); ix++) {
                if ((udev->descriptor.idVendor ==
                     mtouchusb_devices [ix].idVendor) &&
                     (udev->descriptor.idProduct ==
                     mtouchusb_devices [ix].idProduct)) {
                        valid_device = 1;
                        break;
                }
        }

        if (udev->descriptor.idVendor == vendor &&
            udev->descriptor.idProduct == product) {  /* User specified */
                valid_device = 1;
        }

        if (!valid_device) {
                err("%s - No valid device!", __FUNCTION__);
                return -EIO;
        }

        if (udev->descriptor.bNumConfigurations != 1) {
                err("%s -  Only one device configuration is supported.",
                    __FUNCTION__);
                return -EIO;
        }

        dbg("%s - setting interface", __FUNCTION__);
        interface = &intf->altsetting[intf->act_altsetting];

        dbg("%s - setting endpoint", __FUNCTION__);
        endpoint = &interface->endpoint[0].desc;

        if (interface->desc.bNumEndpoints != 1) {
                err("%s - Only one endpoint is supported.", __FUNCTION__);
                return -EIO;
        }

        if (!(mtouch = kmalloc (sizeof (struct mtouch_usb), GFP_KERNEL))) {
                err("%s - Out of memory.", __FUNCTION__);
                return -ENOMEM;
        }

        memset(mtouch, 0, sizeof(struct mtouch_usb));
        mtouch->udev = udev;

        dbg("%s - allocating buffers", __FUNCTION__);
        if (mtouchusb_alloc_buffers(udev, mtouch)) {
                mtouchusb_free_buffers(udev, mtouch);
                kfree(mtouch);
                return -ENOMEM;
        }

        mtouch->input.private = mtouch;
        mtouch->input.open = mtouchusb_open;
        mtouch->input.close = mtouchusb_close;

        usb_make_path(udev, path, 64);
        sprintf(mtouch->phys, "%s/input0", path);

        mtouch->input.name = mtouch->name;
        mtouch->input.phys = mtouch->phys;
        mtouch->input.id.bustype = BUS_USB;
        mtouch->input.id.vendor = udev->descriptor.idVendor;
        mtouch->input.id.product = udev->descriptor.idProduct;
        mtouch->input.id.version = udev->descriptor.bcdDevice;
        mtouch->input.dev = &intf->dev;

        mtouch->input.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
        mtouch->input.absbit[0] = BIT(ABS_X) | BIT(ABS_Y);
        mtouch->input.keybit[LONG(BTN_TOUCH)] = BIT(BTN_TOUCH);

        /* Used to Scale Compensated Data and Flip Y */
        mtouch->input.absmin[ABS_X] =  MTOUCHUSB_MIN_XC;
        mtouch->input.absmax[ABS_X] =  MTOUCHUSB_MAX_XC;
        mtouch->input.absfuzz[ABS_X] = MTOUCHUSB_XC_FUZZ;
        mtouch->input.absflat[ABS_X] = MTOUCHUSB_XC_FLAT;
        mtouch->input.absmin[ABS_Y] =  MTOUCHUSB_MAX_YC;
        mtouch->input.absmax[ABS_Y] =  MTOUCHUSB_MIN_YC;
        mtouch->input.absfuzz[ABS_Y] = MTOUCHUSB_YC_FUZZ;
        mtouch->input.absflat[ABS_Y] = MTOUCHUSB_YC_FLAT;

        if (!(buf = kmalloc(63, GFP_KERNEL))) {
                kfree(mtouch);
                return -ENOMEM;
        }

        if (udev->descriptor.iManufacturer &&
            usb_string(udev, udev->descriptor.iManufacturer, buf, 63) > 0)
                        strcat(mtouch->name, buf);
        if (udev->descriptor.iProduct &&
            usb_string(udev, udev->descriptor.iProduct, buf, 63) > 0)
                        sprintf(mtouch->name, "%s %s", mtouch->name, buf);

        if (!strlen(mtouch->name))
                sprintf(mtouch->name, "USB Touchscreen %04x:%04x",
                        mtouch->input.id.vendor, mtouch->input.id.product);

        kfree(buf);

        nRet = usb_control_msg(mtouch->udev,
                               usb_rcvctrlpipe(udev, 0x80),
                               USB_REQ_GET_CONFIGURATION,
                               USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                               0,
                               0x81,
                               NULL,
                               0,
                               HZ * USB_CTRL_SET_TIMEOUT);
        dbg("%s - usb_control_msg - USB_REQ_GET_CONFIGURATION - bytes|err: %d",
            __FUNCTION__, nRet);

        dbg("%s - usb_alloc_urb: mtouch->irq", __FUNCTION__);
        mtouch->irq = usb_alloc_urb(0, GFP_KERNEL);
        if (!mtouch->irq) {
                dbg("%s - usb_alloc_urb failed: mtouch->irq", __FUNCTION__);
                mtouchusb_free_buffers(udev, mtouch);
                kfree(mtouch);
                return -ENOMEM;
        }

        dbg("%s - usb_fill_int_urb", __FUNCTION__);
        usb_fill_int_urb(mtouch->irq,
                         mtouch->udev,
                         usb_rcvintpipe(mtouch->udev, 0x81),
                         mtouch->data,
                         MTOUCHUSB_REPORT_SIZE_DATA,
                         mtouchusb_irq,
                         mtouch,
                         endpoint->bInterval);

        dbg("%s - input_register_device", __FUNCTION__);
        input_register_device(&mtouch->input);

        nRet = usb_control_msg(mtouch->udev,
                               usb_rcvctrlpipe(udev, 0x80),
                               MTOUCHUSB_ASYC_REPORT,
                               USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                               MTOUCHUSB_ASYC_REPORT,
                               MTOUCHUSB_ASYC_REPORT,
                               NULL,
                               0,
                               HZ * USB_CTRL_SET_TIMEOUT);
        dbg("%s - usb_control_msg - MTOUCHUSB_ASYC_REPORT - bytes|err: %d",
            __FUNCTION__, nRet);

        printk(KERN_INFO "input: %s on %s\n", mtouch->name, path);
        usb_set_intfdata(intf, mtouch);

        return 0;
}

static void mtouchusb_disconnect(struct usb_interface *intf)
{
        struct mtouch_usb *mtouch = usb_get_intfdata (intf);

        dbg("%s - called", __FUNCTION__);
        usb_set_intfdata(intf, NULL);
        if (mtouch) {
                dbg("%s - mtouch is initialized, cleaning up", __FUNCTION__);
                usb_unlink_urb(mtouch->irq);
                input_unregister_device(&mtouch->input);
                usb_free_urb(mtouch->irq);
                mtouchusb_free_buffers(interface_to_usbdev(intf), mtouch);
                kfree(mtouch);
        }
}

MODULE_DEVICE_TABLE (usb, mtouchusb_devices);

static struct usb_driver mtouchusb_driver = {
        .owner =      THIS_MODULE,
        .name =       "mtouchusb",
        .probe =      mtouchusb_probe,
        .disconnect = mtouchusb_disconnect,
        .id_table =   mtouchusb_devices,
};

static int __init mtouchusb_init(void) {
        dbg("%s - called", __FUNCTION__);
        return usb_register(&mtouchusb_driver);
}

static void __exit mtouchusb_cleanup(void) {
        dbg("%s - called", __FUNCTION__);
        usb_deregister(&mtouchusb_driver);
}

module_init(mtouchusb_init);
module_exit(mtouchusb_cleanup);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");
MODULE_PARM(vendor, "i");
MODULE_PARM_DESC(vendor, "User specified USB idVendor");
MODULE_PARM(product, "i");
MODULE_PARM_DESC(product, "User specified USB idProduct");


