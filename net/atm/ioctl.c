/* ATM ioctl handling */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */
/* 2003 John Levon  <levon@movementarian.org> */


#include <linux/config.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/net.h>		/* struct socket, struct proto_ops */
#include <linux/atm.h>		/* ATM stuff */
#include <linux/atmdev.h>
#include <linux/atmclip.h>	/* CLIP_*ENCAP */
#include <linux/atmarp.h>	/* manifest constants */
#include <linux/sonet.h>	/* for ioctls */
#include <linux/atmsvc.h>
#include <linux/atmmpc.h>
#include <net/atmclip.h>
#include <asm/ioctls.h>

#include "resources.h"
#include "signaling.h"		/* for WAITING and sigd_attach */

#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
#include <linux/atmlec.h>
#include "lec.h"
#include "lec_arpc.h"
struct atm_lane_ops *atm_lane_ops;
static DECLARE_MUTEX(atm_lane_ops_mutex);

void atm_lane_ops_set(struct atm_lane_ops *hook)
{
	down(&atm_lane_ops_mutex);
	atm_lane_ops = hook;
	up(&atm_lane_ops_mutex);
}

int try_atm_lane_ops(void)
{
	down(&atm_lane_ops_mutex);
	if (atm_lane_ops && try_module_get(atm_lane_ops->owner)) {
		up(&atm_lane_ops_mutex);
		return 1;
	}
	up(&atm_lane_ops_mutex);
	return 0;
}

#if defined(CONFIG_ATM_LANE_MODULE) || defined(CONFIG_ATM_MPOA_MODULE)
EXPORT_SYMBOL(atm_lane_ops);
EXPORT_SYMBOL(try_atm_lane_ops);
EXPORT_SYMBOL(atm_lane_ops_set);
#endif
#endif

static DECLARE_MUTEX(ioctl_mutex);
static LIST_HEAD(ioctl_list);


void register_atm_ioctl(struct atm_ioctl *ioctl)
{
	down(&ioctl_mutex);
	list_add_tail(&ioctl->list, &ioctl_list);
	up(&ioctl_mutex);
}

void deregister_atm_ioctl(struct atm_ioctl *ioctl)
{
	down(&ioctl_mutex);
	list_del(&ioctl->list);
	up(&ioctl_mutex);
}

EXPORT_SYMBOL(register_atm_ioctl);
EXPORT_SYMBOL(deregister_atm_ioctl);

int vcc_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct atm_vcc *vcc;
	int error;
	struct list_head * pos;

	vcc = ATM_SD(sock);
	switch (cmd) {
		case SIOCOUTQ:
			if (sock->state != SS_CONNECTED ||
			    !test_bit(ATM_VF_READY, &vcc->flags)) {
				error =  -EINVAL;
				goto done;
			}
			error = put_user(vcc->sk->sk_sndbuf -
					 atomic_read(&vcc->sk->sk_wmem_alloc),
					 (int *) arg) ? -EFAULT : 0;
			goto done;
		case SIOCINQ:
			{
				struct sk_buff *skb;

				if (sock->state != SS_CONNECTED) {
					error = -EINVAL;
					goto done;
				}
				skb = skb_peek(&vcc->sk->sk_receive_queue);
				error = put_user(skb ? skb->len : 0,
					 	 (int *) arg) ? -EFAULT : 0;
				goto done;
			}
		case SIOCGSTAMP: /* borrowed from IP */
			if (!vcc->sk->sk_stamp.tv_sec) {
				error = -ENOENT;
				goto done;
			}
			error = copy_to_user((void *)arg, &vcc->sk->sk_stamp,
					     sizeof(struct timeval)) ? -EFAULT : 0;
			goto done;
		case ATM_SETSC:
			printk(KERN_WARNING "ATM_SETSC is obsolete\n");
			error = 0;
			goto done;
		case ATMSIGD_CTRL:
			if (!capable(CAP_NET_ADMIN)) {
				error = -EPERM;
				goto done;
			}
			/*
			 * The user/kernel protocol for exchanging signalling
			 * info uses kernel pointers as opaque references,
			 * so the holder of the file descriptor can scribble
			 * on the kernel... so we should make sure that we
			 * have the same privledges that /proc/kcore needs
			 */
			if (!capable(CAP_SYS_RAWIO)) {
				error = -EPERM;
				goto done;
			}
			error = sigd_attach(vcc);
			if (!error)
				sock->state = SS_CONNECTED;
			goto done;
#if defined(CONFIG_ATM_LANE) || defined(CONFIG_ATM_LANE_MODULE)
                case ATMLEC_CTRL:
                        if (!capable(CAP_NET_ADMIN)) {
				error = -EPERM;
				goto done;
			}
#if defined(CONFIG_ATM_LANE_MODULE)
                        if (!atm_lane_ops)
				request_module("lec");
#endif
			if (try_atm_lane_ops()) {
				error = atm_lane_ops->lecd_attach(vcc, (int) arg);
				module_put(atm_lane_ops->owner);
				if (error >= 0)
					sock->state = SS_CONNECTED;
			} else
				error = -ENOSYS;
			goto done;
                case ATMLEC_MCAST:
			if (!capable(CAP_NET_ADMIN)) {
				error = -EPERM;
				goto done;
			}
			if (try_atm_lane_ops()) {
				error = atm_lane_ops->mcast_attach(vcc, (int) arg);
				module_put(atm_lane_ops->owner);
			} else
				error = -ENOSYS;
			goto done;
                case ATMLEC_DATA:
			if (!capable(CAP_NET_ADMIN)) {
				error = -EPERM;
				goto done;
			}
			if (try_atm_lane_ops()) {
				error = atm_lane_ops->vcc_attach(vcc, (void *) arg);
				module_put(atm_lane_ops->owner);
			} else
				error = -ENOSYS;
			goto done;
#endif
		default:
			break;
	}

	if (cmd == ATMMPC_CTRL || cmd == ATMMPC_DATA)
		request_module("mpoa");
	if (cmd == ATMARPD_CTRL)
		request_module("clip");

	error = -ENOIOCTLCMD;

	down(&ioctl_mutex);
	list_for_each(pos, &ioctl_list) {
		struct atm_ioctl * ic = list_entry(pos, struct atm_ioctl, list);
		if (try_module_get(ic->owner)) {
			error = ic->ioctl(sock, cmd, arg);
			module_put(ic->owner);
			if (error != -ENOIOCTLCMD)
				break;
		}
	}
	up(&ioctl_mutex);

	if (error != -ENOIOCTLCMD)
		goto done;

	error = atm_dev_ioctl(cmd, arg);

done:
	return error;
}
