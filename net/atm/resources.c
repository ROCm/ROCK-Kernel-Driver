/* net/atm/resources.c - Statically allocated resources */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */

/* Fixes
 * Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 * 2002/01 - don't free the whole struct sock on sk->destruct time,
 * 	     use the default destruct function initialized by sock_init_data */


#include <linux/config.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/atmdev.h>
#include <linux/kernel.h> /* for barrier */
#include <linux/module.h>
#include <linux/bitops.h>
#include <net/sock.h>	 /* for struct sock */

#include "common.h"
#include "resources.h"


#ifndef NULL
#define NULL 0
#endif


LIST_HEAD(atm_devs);
spinlock_t atm_dev_lock = SPIN_LOCK_UNLOCKED;

static struct atm_dev *__alloc_atm_dev(const char *type)
{
	struct atm_dev *dev;

	dev = kmalloc(sizeof(*dev), GFP_ATOMIC);
	if (!dev)
		return NULL;
	memset(dev, 0, sizeof(*dev));
	dev->type = type;
	dev->signal = ATM_PHY_SIG_UNKNOWN;
	dev->link_rate = ATM_OC3_PCR;
	spin_lock_init(&dev->lock);

	return dev;
}

static void __free_atm_dev(struct atm_dev *dev)
{
	kfree(dev);
}

static struct atm_dev *__atm_dev_lookup(int number)
{
	struct atm_dev *dev;
	struct list_head *p;

	list_for_each(p, &atm_devs) {
		dev = list_entry(p, struct atm_dev, dev_list);
		if ((dev->ops) && (dev->number == number)) {
			atm_dev_hold(dev);
			return dev;
		}
	}
	return NULL;
}

struct atm_dev *atm_dev_lookup(int number)
{
	struct atm_dev *dev;

	spin_lock(&atm_dev_lock);
	dev = __atm_dev_lookup(number);
	spin_unlock(&atm_dev_lock);
	return dev;
}

struct atm_dev *atm_dev_register(const char *type, const struct atmdev_ops *ops,
				 int number, unsigned long *flags)
{
	struct atm_dev *dev, *inuse;

	dev = __alloc_atm_dev(type);
	if (!dev) {
		printk(KERN_ERR "atm_dev_register: no space for dev %s\n",
		    type);
		return NULL;
	}
	spin_lock(&atm_dev_lock);
	if (number != -1) {
		if ((inuse = __atm_dev_lookup(number))) {
			atm_dev_release(inuse);
			spin_unlock(&atm_dev_lock);
			__free_atm_dev(dev);
			return NULL;
		}
		dev->number = number;
	} else {
		dev->number = 0;
		while ((inuse = __atm_dev_lookup(dev->number))) {
			atm_dev_release(inuse);
			dev->number++;
		}
	}

	dev->ops = ops;
	if (flags)
		dev->flags = *flags;
	else
		memset(&dev->flags, 0, sizeof(dev->flags));
	memset(&dev->stats, 0, sizeof(dev->stats));
	atomic_set(&dev->refcnt, 1);
	list_add_tail(&dev->dev_list, &atm_devs);
	spin_unlock(&atm_dev_lock);

#ifdef CONFIG_PROC_FS
	if (ops->proc_read) {
		if (atm_proc_dev_register(dev) < 0) {
			printk(KERN_ERR "atm_dev_register: "
			       "atm_proc_dev_register failed for dev %s\n",
			       type);
			spin_lock(&atm_dev_lock);
			list_del(&dev->dev_list);
			spin_unlock(&atm_dev_lock);
			__free_atm_dev(dev);
			return NULL;
		}
	}
#endif

	return dev;
}


void atm_dev_deregister(struct atm_dev *dev)
{
	unsigned long warning_time;

#ifdef CONFIG_PROC_FS
	if (dev->ops->proc_read)
		atm_proc_dev_deregister(dev);
#endif
	spin_lock(&atm_dev_lock);
	list_del(&dev->dev_list);
	spin_unlock(&atm_dev_lock);

        warning_time = jiffies;
        while (atomic_read(&dev->refcnt) != 1) {
                current->state = TASK_INTERRUPTIBLE;
                schedule_timeout(HZ / 4);
                current->state = TASK_RUNNING;
                if ((jiffies - warning_time) > 10 * HZ) {
                        printk(KERN_EMERG "atm_dev_deregister: waiting for "
                               "dev %d to become free. Usage count = %d\n",
                               dev->number, atomic_read(&dev->refcnt));
                        warning_time = jiffies;
                }
        }

	__free_atm_dev(dev);
}

void shutdown_atm_dev(struct atm_dev *dev)
{
	if (atomic_read(&dev->refcnt) > 1) {
		set_bit(ATM_DF_CLOSE, &dev->flags);
		return;
	}
	if (dev->ops->dev_close)
		dev->ops->dev_close(dev);
	atm_dev_deregister(dev);
}

struct sock *alloc_atm_vcc_sk(int family)
{
	struct sock *sk;
	struct atm_vcc *vcc;

	sk = sk_alloc(family, GFP_KERNEL, 1, NULL);
	if (!sk)
		return NULL;
	vcc = atm_sk(sk) = kmalloc(sizeof(*vcc), GFP_KERNEL);
	if (!vcc) {
		sk_free(sk);
		return NULL;
	}
	sock_init_data(NULL, sk);
	memset(vcc, 0, sizeof(*vcc));
	vcc->sk = sk;

	return sk;
}


static void unlink_vcc(struct atm_vcc *vcc)
{
	unsigned long flags;
	if (vcc->dev) {
		spin_lock_irqsave(&vcc->dev->lock, flags);
		if (vcc->prev)
			vcc->prev->next = vcc->next;
		else
			vcc->dev->vccs = vcc->next;

		if (vcc->next)
			vcc->next->prev = vcc->prev;
		else
			vcc->dev->last = vcc->prev;
		spin_unlock_irqrestore(&vcc->dev->lock, flags);
	}
}


void free_atm_vcc_sk(struct sock *sk)
{
	unlink_vcc(atm_sk(sk));
	sk_free(sk);
}

void bind_vcc(struct atm_vcc *vcc,struct atm_dev *dev)
{
	unsigned long flags;

	unlink_vcc(vcc);
	vcc->dev = dev;
	if (dev) {
		spin_lock_irqsave(&dev->lock, flags);
		vcc->next = NULL;
		vcc->prev = dev->last;
		if (dev->vccs)
			dev->last->next = vcc;
		else
			dev->vccs = vcc;
		dev->last = vcc;
		spin_unlock_irqrestore(&dev->lock, flags);
	}
}

EXPORT_SYMBOL(atm_dev_register);
EXPORT_SYMBOL(atm_dev_deregister);
EXPORT_SYMBOL(atm_dev_lookup);
EXPORT_SYMBOL(shutdown_atm_dev);
EXPORT_SYMBOL(bind_vcc);
