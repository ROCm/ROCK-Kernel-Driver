#include <linux/module.h>

#include "dmxdev.h"
#include "dvb_filter.h"
#include "dvb_frontend.h"
#include "dvb_i2c.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_net.h"

/* if the miracle happens and "generic_usercopy()" is included into
   the kernel, then this can vanish. please don't make the mistake and
   define this as video_usercopy(). this will introduce a dependecy
   to the v4l "videodev.o" module, which is unnecessary for some
   cards (ie. the budget dvb-cards don't need the v4l module...) */
int dvb_usercopy(struct inode *inode, struct file *file,
	             unsigned int cmd, unsigned long arg,
		     int (*func)(struct inode *inode, struct file *file,
		     unsigned int cmd, void *arg))
{
        char    sbuf[128];
        void    *mbuf = NULL;
        void    *parg = NULL;
        int     err  = -EINVAL;

        /*  Copy arguments into temp kernel buffer  */
        switch (_IOC_DIR(cmd)) {
        case _IOC_NONE:
                parg = (void *)arg;
                break;
        case _IOC_READ: /* some v4l ioctls are marked wrong ... */
        case _IOC_WRITE:
        case (_IOC_WRITE | _IOC_READ):
                if (_IOC_SIZE(cmd) <= sizeof(sbuf)) {
                        parg = sbuf;
                } else {
                        /* too big to allocate from stack */
                        mbuf = kmalloc(_IOC_SIZE(cmd),GFP_KERNEL);
                        if (NULL == mbuf)
                                return -ENOMEM;
                        parg = mbuf;
                }

                err = -EFAULT;
                if (copy_from_user(parg, (void *)arg, _IOC_SIZE(cmd)))
                        goto out;
                break;
        }

        /* call driver */
        if ((err = func(inode, file, cmd, parg)) == -ENOIOCTLCMD)
                err = -EINVAL;

        if (err < 0)
                goto out;

        /*  Copy results into user buffer  */
        switch (_IOC_DIR(cmd))
        {
        case _IOC_READ:
        case (_IOC_WRITE | _IOC_READ):
                if (copy_to_user((void *)arg, parg, _IOC_SIZE(cmd)))
                        err = -EFAULT;
                break;
        }

out:
        if (mbuf)
                kfree(mbuf);

        return err;
}
EXPORT_SYMBOL(dvb_usercopy);

EXPORT_SYMBOL(dvb_dmxdev_init);
EXPORT_SYMBOL(dvb_dmxdev_release);
EXPORT_SYMBOL(dvb_dmx_init);
EXPORT_SYMBOL(dvb_dmx_release);
EXPORT_SYMBOL(dvb_dmx_swfilter_packet);
EXPORT_SYMBOL(dvb_dmx_swfilter_packets);
EXPORT_SYMBOL(dvb_dmx_swfilter);

EXPORT_SYMBOL(dvb_register_frontend);
EXPORT_SYMBOL(dvb_unregister_frontend);
EXPORT_SYMBOL(dvb_add_frontend_ioctls);
EXPORT_SYMBOL(dvb_remove_frontend_ioctls);
EXPORT_SYMBOL(dvb_add_frontend_notifier);
EXPORT_SYMBOL(dvb_remove_frontend_notifier);

EXPORT_SYMBOL(dvb_register_i2c_bus);
EXPORT_SYMBOL(dvb_unregister_i2c_bus);
EXPORT_SYMBOL(dvb_register_i2c_device);
EXPORT_SYMBOL(dvb_unregister_i2c_device);

EXPORT_SYMBOL(dvb_net_init);
EXPORT_SYMBOL(dvb_net_release);

EXPORT_SYMBOL(dvb_register_adapter);
EXPORT_SYMBOL(dvb_unregister_adapter);
EXPORT_SYMBOL(dvb_register_device);
EXPORT_SYMBOL(dvb_unregister_device);
EXPORT_SYMBOL(dvb_generic_ioctl);
EXPORT_SYMBOL(dvb_generic_open);
EXPORT_SYMBOL(dvb_generic_release);

EXPORT_SYMBOL(dvb_filter_pes2ts_init);
EXPORT_SYMBOL(dvb_filter_pes2ts);
EXPORT_SYMBOL(dvb_filter_get_ac3info);

