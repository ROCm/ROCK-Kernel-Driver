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
struct atm_vcc *nodev_vccs = NULL;
extern spinlock_t atm_dev_lock;

/* Caller must hold atm_dev_lock. */
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
	list_add_tail(&dev->dev_list, &atm_devs);

	return dev;
}

/* Caller must hold atm_dev_lock. */
static void __free_atm_dev(struct atm_dev *dev)
{
	list_del(&dev->dev_list);
	kfree(dev);
}

/* Caller must hold atm_dev_lock. */
struct atm_dev *atm_find_dev(int number)
{
	struct atm_dev *dev;
	struct list_head *p;

	list_for_each(p, &atm_devs) {
		dev = list_entry(p, struct atm_dev, dev_list);
		if (dev->ops && dev->number == number)
			return dev;
	}
	return NULL;
}


struct atm_dev *atm_dev_register(const char *type, const struct atmdev_ops *ops,
				 int number, unsigned long *flags)
{
	struct atm_dev *dev;

	spin_lock(&atm_dev_lock);

	dev = __alloc_atm_dev(type);
	if (!dev) {
		printk(KERN_ERR "atm_dev_register: no space for dev %s\n",
		    type);
		goto done;
	}
	if (number != -1) {
		if (atm_find_dev(number)) {
			__free_atm_dev(dev);
			dev = NULL;
			goto done;
		}
		dev->number = number;
	} else {
		dev->number = 0;
		while (atm_find_dev(dev->number))
			dev->number++;
	}
	dev->vccs = dev->last = NULL;
	dev->dev_data = NULL;
	barrier();
	dev->ops = ops;
	if (flags)
		dev->flags = *flags;
	else
		memset(&dev->flags, 0, sizeof(dev->flags));
	memset(&dev->stats, 0, sizeof(dev->stats));

#ifdef CONFIG_PROC_FS
	if (ops->proc_read) {
		if (atm_proc_dev_register(dev) < 0) {
			printk(KERN_ERR "atm_dev_register: "
			       "atm_proc_dev_register failed for dev %s\n",
			       type);
			__free_atm_dev(dev);
			dev = NULL;
			goto done;
		}
	}
#endif

done:
	spin_unlock(&atm_dev_lock);
	return dev;
}


void atm_dev_deregister(struct atm_dev *dev)
{
#ifdef CONFIG_PROC_FS
	if (dev->ops->proc_read)
		atm_proc_dev_deregister(dev);
#endif
	spin_lock(&atm_dev_lock);
	__free_atm_dev(dev);
	spin_unlock(&atm_dev_lock);
}

void shutdown_atm_dev(struct atm_dev *dev)
{
	if (dev->vccs) {
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
	if (nodev_vccs)
		nodev_vccs->prev = vcc;
	vcc->prev = NULL;
	vcc->next = nodev_vccs;
	nodev_vccs = vcc;
	return sk;
}


static void unlink_vcc(struct atm_vcc *vcc,struct atm_dev *hold_dev)
{
	if (vcc->prev)
		vcc->prev->next = vcc->next;
	else if (vcc->dev)
		vcc->dev->vccs = vcc->next;
	else
		nodev_vccs = vcc->next;
	if (vcc->next)
		vcc->next->prev = vcc->prev;
	else if (vcc->dev)
		vcc->dev->last = vcc->prev;
	if (vcc->dev && vcc->dev != hold_dev && !vcc->dev->vccs &&
	    test_bit(ATM_DF_CLOSE,&vcc->dev->flags))
		shutdown_atm_dev(vcc->dev);
}


void free_atm_vcc_sk(struct sock *sk)
{
	unlink_vcc(atm_sk(sk), NULL);
	sk_free(sk);
}

void bind_vcc(struct atm_vcc *vcc,struct atm_dev *dev)
{
	unlink_vcc(vcc,dev);
	vcc->dev = dev;
	if (dev) {
		vcc->next = NULL;
		vcc->prev = dev->last;
		if (dev->vccs)
			dev->last->next = vcc;
		else
			dev->vccs = vcc;
		dev->last = vcc;
	} else {
		if (nodev_vccs)
			nodev_vccs->prev = vcc;
		vcc->next = nodev_vccs;
		vcc->prev = NULL;
		nodev_vccs = vcc;
	}
}

EXPORT_SYMBOL(atm_dev_register);
EXPORT_SYMBOL(atm_dev_deregister);
EXPORT_SYMBOL(atm_find_dev);
EXPORT_SYMBOL(shutdown_atm_dev);
EXPORT_SYMBOL(bind_vcc);
