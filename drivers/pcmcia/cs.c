/*======================================================================

    Kernel Card Services -- core services

    cs.c 1.271 2000/10/02 20:27:49
    
    The contents of this file are subject to the Mozilla Public
    License Version 1.1 (the "License"); you may not use this file
    except in compliance with the License. You may obtain a copy of
    the License at http://www.mozilla.org/MPL/

    Software distributed under the License is distributed on an "AS
    IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
    implied. See the License for the specific language governing
    rights and limitations under the License.

    The initial developer of the original code is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

    Alternatively, the contents of this file may be used under the
    terms of the GNU General Public License version 2 (the "GPL"), in which
    case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.
    
======================================================================*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/string.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/suspend.h>
#include <asm/system.h>
#include <asm/irq.h>

#define IN_CARD_SERVICES
#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include "cs_internal.h"

#ifdef CONFIG_PCI
#define PCI_OPT " [pci]"
#else
#define PCI_OPT ""
#endif
#ifdef CONFIG_CARDBUS
#define CB_OPT " [cardbus]"
#else
#define CB_OPT ""
#endif
#ifdef CONFIG_PM
#define PM_OPT " [pm]"
#else
#define PM_OPT ""
#endif
#if !defined(CONFIG_CARDBUS) && !defined(CONFIG_PCI) && !defined(CONFIG_PM)
#define OPTIONS " none"
#else
#define OPTIONS PCI_OPT CB_OPT PM_OPT
#endif

static const char *release = "Linux Kernel Card Services";
static const char *options = "options: " OPTIONS;

/*====================================================================*/

/* Module parameters */

MODULE_AUTHOR("David Hinds <dahinds@users.sourceforge.net>");
MODULE_DESCRIPTION("Linux Kernel Card Services\noptions:" OPTIONS);
MODULE_LICENSE("Dual MPL/GPL");	  

#define INT_MODULE_PARM(n, v) static int n = v; module_param(n, int, 0444)

INT_MODULE_PARM(setup_delay,	10);		/* centiseconds */
INT_MODULE_PARM(resume_delay,	20);		/* centiseconds */
INT_MODULE_PARM(shutdown_delay,	3);		/* centiseconds */
INT_MODULE_PARM(vcc_settle,	40);		/* centiseconds */
INT_MODULE_PARM(reset_time,	10);		/* usecs */
INT_MODULE_PARM(unreset_delay,	10);		/* centiseconds */
INT_MODULE_PARM(unreset_check,	10);		/* centiseconds */
INT_MODULE_PARM(unreset_limit,	30);		/* unreset_check's */

/* Access speed for attribute memory windows */
INT_MODULE_PARM(cis_speed,	300);		/* ns */

/* Access speed for IO windows */
INT_MODULE_PARM(io_speed,	0);		/* ns */

#ifdef DEBUG
static int pc_debug;

module_param(pc_debug, int, 0644);

int cs_debug_level(int level)
{
	return pc_debug > level;
}
#endif

/*====================================================================*/

socket_state_t dead_socket = {
	.csc_mask	= SS_DETECT,
};


/* List of all sockets, protected by a rwsem */
LIST_HEAD(pcmcia_socket_list);
DECLARE_RWSEM(pcmcia_socket_list_rwsem);


/*====================================================================

    Low-level PC Card interface drivers need to register with Card
    Services using these calls.
    
======================================================================*/

/**
 * socket drivers are expected to use the following callbacks in their 
 * .drv struct:
 *  - pcmcia_socket_dev_suspend
 *  - pcmcia_socket_dev_resume
 * These functions check for the appropriate struct pcmcia_soket arrays,
 * and pass them to the low-level functions pcmcia_{suspend,resume}_socket
 */
static int socket_resume(struct pcmcia_socket *skt);
static int socket_suspend(struct pcmcia_socket *skt);

int pcmcia_socket_dev_suspend(struct device *dev, u32 state)
{
	struct pcmcia_socket *socket;

	down_read(&pcmcia_socket_list_rwsem);
	list_for_each_entry(socket, &pcmcia_socket_list, socket_list) {
		if (socket->dev.dev != dev)
			continue;
		down(&socket->skt_sem);
		socket_suspend(socket);
		up(&socket->skt_sem);
	}
	up_read(&pcmcia_socket_list_rwsem);

	return 0;
}
EXPORT_SYMBOL(pcmcia_socket_dev_suspend);

int pcmcia_socket_dev_resume(struct device *dev)
{
	struct pcmcia_socket *socket;

	down_read(&pcmcia_socket_list_rwsem);
	list_for_each_entry(socket, &pcmcia_socket_list, socket_list) {
		if (socket->dev.dev != dev)
			continue;
		down(&socket->skt_sem);
		socket_resume(socket);
		up(&socket->skt_sem);
	}
	up_read(&pcmcia_socket_list_rwsem);

	return 0;
}
EXPORT_SYMBOL(pcmcia_socket_dev_resume);


static void pcmcia_release_socket(struct class_device *class_dev)
{
	struct pcmcia_socket *socket = class_get_devdata(class_dev);
	client_t *client;

	while (socket->clients) {
		client = socket->clients;
		socket->clients = socket->clients->next;
		kfree(client);
	}

	complete(&socket->socket_released);
}

static int pccardd(void *__skt);

/**
 * pcmcia_register_socket - add a new pcmcia socket device
 */
int pcmcia_register_socket(struct pcmcia_socket *socket)
{
	int ret;

	if (!socket || !socket->ops || !socket->dev.dev)
		return -EINVAL;

	cs_dbg(socket, 0, "pcmcia_register_socket(0x%p)\n", socket->ops);

	/* try to obtain a socket number [yes, it gets ugly if we
	 * register more than 2^sizeof(unsigned int) pcmcia 
	 * sockets... but the socket number is deprecated 
	 * anyways, so I don't care] */
	down_write(&pcmcia_socket_list_rwsem);
	if (list_empty(&pcmcia_socket_list))
		socket->sock = 0;
	else {
		unsigned int found, i = 1;
		struct pcmcia_socket *tmp;
		do {
			found = 1;
			list_for_each_entry(tmp, &pcmcia_socket_list, socket_list) {
				if (tmp->sock == i)
					found = 0;
			}
			i++;
		} while (!found);
		socket->sock = i - 1;
	}
	list_add_tail(&socket->socket_list, &pcmcia_socket_list);
	up_write(&pcmcia_socket_list_rwsem);


	/* set proper values in socket->dev */
	socket->dev.class_data = socket;
	socket->dev.class = &pcmcia_socket_class;
	snprintf(socket->dev.class_id, BUS_ID_SIZE, "pcmcia_socket%u", socket->sock);

	/* base address = 0, map = 0 */
	socket->cis_mem.flags = 0;
	socket->cis_mem.speed = cis_speed;

	INIT_LIST_HEAD(&socket->cis_cache);
	spin_lock_init(&socket->lock);

	init_completion(&socket->socket_released);
	init_completion(&socket->thread_done);
	init_waitqueue_head(&socket->thread_wait);
	init_MUTEX(&socket->skt_sem);
	spin_lock_init(&socket->thread_lock);

	ret = kernel_thread(pccardd, socket, CLONE_KERNEL);
	if (ret < 0)
		goto err;

	wait_for_completion(&socket->thread_done);
	if(!socket->thread) {
		printk(KERN_WARNING "PCMCIA: warning: socket thread for socket %p did not start\n", socket);
		return -EIO;
	}
	pcmcia_parse_events(socket, SS_DETECT);

	return 0;

 err:
	down_write(&pcmcia_socket_list_rwsem);
	list_del(&socket->socket_list);
	up_write(&pcmcia_socket_list_rwsem);
	return ret;
} /* pcmcia_register_socket */
EXPORT_SYMBOL(pcmcia_register_socket);


/**
 * pcmcia_unregister_socket - remove a pcmcia socket device
 */
void pcmcia_unregister_socket(struct pcmcia_socket *socket)
{
	if (!socket)
		return;

	cs_dbg(socket, 0, "pcmcia_unregister_socket(0x%p)\n", socket->ops);

	if (socket->thread) {
		init_completion(&socket->thread_done);
		socket->thread = NULL;
		wake_up(&socket->thread_wait);
		wait_for_completion(&socket->thread_done);
	}
	release_cis_mem(socket);

	/* remove from our own list */
	down_write(&pcmcia_socket_list_rwsem);
	list_del(&socket->socket_list);
	up_write(&pcmcia_socket_list_rwsem);

	/* wait for sysfs to drop all references */
	wait_for_completion(&socket->socket_released);
} /* pcmcia_unregister_socket */
EXPORT_SYMBOL(pcmcia_unregister_socket);


struct pcmcia_socket * pcmcia_get_socket_by_nr(unsigned int nr)
{
	struct pcmcia_socket *s;

	down_read(&pcmcia_socket_list_rwsem);
	list_for_each_entry(s, &pcmcia_socket_list, socket_list)
		if (s->sock == nr) {
			up_read(&pcmcia_socket_list_rwsem);
			return s;
		}
	up_read(&pcmcia_socket_list_rwsem);

	return NULL;

}
EXPORT_SYMBOL(pcmcia_get_socket_by_nr);


/*======================================================================

    Shutdown_Socket() and setup_socket() are scheduled using add_timer
    calls by the main event handler when card insertion and removal
    events are received.  Shutdown_Socket() unconfigures a socket and
    turns off socket power.  Setup_socket() turns on socket power
    and resets the socket, in two stages.

======================================================================*/

static void free_regions(memory_handle_t *list)
{
    memory_handle_t tmp;
    while (*list != NULL) {
	tmp = *list;
	*list = tmp->info.next;
	tmp->region_magic = 0;
	kfree(tmp);
    }
}

static int send_event(struct pcmcia_socket *s, event_t event, int priority);

static void shutdown_socket(struct pcmcia_socket *s)
{
    client_t **c;
    
    cs_dbg(s, 1, "shutdown_socket\n");

    /* Blank out the socket state */
    s->socket = dead_socket;
    s->ops->init(s);
    s->ops->set_socket(s, &s->socket);
    s->irq.AssignedIRQ = s->irq.Config = 0;
    s->lock_count = 0;
    destroy_cis_cache(s);
#ifdef CONFIG_CARDBUS
    cb_free(s);
#endif
    s->functions = 0;
    if (s->config) {
	kfree(s->config);
	s->config = NULL;
    }
    for (c = &s->clients; *c; ) {
	if ((*c)->state & CLIENT_UNBOUND) {
	    client_t *d = *c;
	    *c = (*c)->next;
	    kfree(d);
	} else {
	    c = &((*c)->next);
	}
    }
    free_regions(&s->a_region);
    free_regions(&s->c_region);

    {
	int status;
	s->ops->get_status(s, &status);
	if (status & SS_POWERON) {
		printk(KERN_ERR "PCMCIA: socket %p: *** DANGER *** unable to remove socket power\n", s);
	}
    }
} /* shutdown_socket */

/*======================================================================

    The central event handler.  Send_event() sends an event to all
    valid clients.  Parse_events() interprets the event bits from
    a card status change report.  Do_shutdown() handles the high
    priority stuff associated with a card removal.
    
======================================================================*/

static int send_event(struct pcmcia_socket *s, event_t event, int priority)
{
    client_t *client = s->clients;
    int ret;
    cs_dbg(s, 1, "send_event(event %d, pri %d)\n",
	   event, priority);
    ret = 0;
    if (s->state & SOCKET_CARDBUS)
	    return 0;
    for (; client; client = client->next) { 
	if (client->state & (CLIENT_UNBOUND|CLIENT_STALE))
	    continue;
	if (client->EventMask & event) {
	    ret = EVENT(client, event, priority);
	    if (ret != 0)
		return ret;
	}
    }
    return ret;
} /* send_event */

static void socket_remove_drivers(struct pcmcia_socket *skt)
{
	client_t *client;

	cs_dbg(skt, 4, "remove_drivers\n");

	send_event(skt, CS_EVENT_CARD_REMOVAL, CS_EVENT_PRI_HIGH);

	for (client = skt->clients; client; client = client->next)
		if (!(client->Attributes & INFO_MASTER_CLIENT))
			client->state |= CLIENT_STALE;
}

static void socket_shutdown(struct pcmcia_socket *skt)
{
	cs_dbg(skt, 4, "shutdown\n");

	socket_remove_drivers(skt);
	skt->state &= SOCKET_INUSE|SOCKET_PRESENT;
	msleep(shutdown_delay * 10);
	skt->state &= SOCKET_INUSE;
	shutdown_socket(skt);
}

static int socket_reset(struct pcmcia_socket *skt)
{
	int status, i;

	cs_dbg(skt, 4, "reset\n");

	skt->socket.flags |= SS_OUTPUT_ENA | SS_RESET;
	skt->ops->set_socket(skt, &skt->socket);
	udelay((long)reset_time);

	skt->socket.flags &= ~SS_RESET;
	skt->ops->set_socket(skt, &skt->socket);

	msleep(unreset_delay * 10);
	for (i = 0; i < unreset_limit; i++) {
		skt->ops->get_status(skt, &status);

		if (!(status & SS_DETECT))
			return CS_NO_CARD;

		if (status & SS_READY)
			return CS_SUCCESS;

		msleep(unreset_check * 10);
	}

	cs_err(skt, "time out after reset.\n");
	return CS_GENERAL_FAILURE;
}

static int socket_setup(struct pcmcia_socket *skt, int initial_delay)
{
	int status, i;

	cs_dbg(skt, 4, "setup\n");

	skt->ops->get_status(skt, &status);
	if (!(status & SS_DETECT))
		return CS_NO_CARD;

	msleep(initial_delay * 10);

	for (i = 0; i < 100; i++) {
		skt->ops->get_status(skt, &status);
		if (!(status & SS_DETECT))
			return CS_NO_CARD;

		if (!(status & SS_PENDING))
			break;

		msleep(100);
	}

	if (status & SS_PENDING) {
		cs_err(skt, "voltage interrogation timed out.\n");
		return CS_GENERAL_FAILURE;
	}

	if (status & SS_CARDBUS) {
		skt->state |= SOCKET_CARDBUS;
#ifndef CONFIG_CARDBUS
		cs_err(skt, "cardbus cards are not supported.\n");
		return CS_BAD_TYPE;
#endif
	}

	/*
	 * Decode the card voltage requirements, and apply power to the card.
	 */
	if (status & SS_3VCARD)
		skt->socket.Vcc = skt->socket.Vpp = 33;
	else if (!(status & SS_XVCARD))
		skt->socket.Vcc = skt->socket.Vpp = 50;
	else {
		cs_err(skt, "unsupported voltage key.\n");
		return CS_BAD_TYPE;
	}
	skt->socket.flags = 0;
	skt->ops->set_socket(skt, &skt->socket);

	/*
	 * Wait "vcc_settle" for the supply to stabilise.
	 */
	msleep(vcc_settle * 10);

	skt->ops->get_status(skt, &status);
	if (!(status & SS_POWERON)) {
		cs_err(skt, "unable to apply power.\n");
		return CS_BAD_TYPE;
	}

	return socket_reset(skt);
}

/*
 * Handle card insertion.  Setup the socket, reset the card,
 * and then tell the rest of PCMCIA that a card is present.
 */
static int socket_insert(struct pcmcia_socket *skt)
{
	int ret;

	cs_dbg(skt, 4, "insert\n");

	if (!cs_socket_get(skt))
		return CS_NO_CARD;

	ret = socket_setup(skt, setup_delay);
	if (ret == CS_SUCCESS) {
		skt->state |= SOCKET_PRESENT;
#ifdef CONFIG_CARDBUS
		if (skt->state & SOCKET_CARDBUS) {
			cb_alloc(skt);
			skt->state |= SOCKET_CARDBUS_CONFIG;
		}
#endif
		cs_dbg(skt, 4, "insert done\n");

		send_event(skt, CS_EVENT_CARD_INSERTION, CS_EVENT_PRI_LOW);
	} else {
		socket_shutdown(skt);
		cs_socket_put(skt);
	}

	return ret;
}

static int socket_suspend(struct pcmcia_socket *skt)
{
	if (skt->state & SOCKET_SUSPEND)
		return CS_IN_USE;

	send_event(skt, CS_EVENT_PM_SUSPEND, CS_EVENT_PRI_LOW);
	skt->socket = dead_socket;
	skt->ops->suspend(skt);
	skt->state |= SOCKET_SUSPEND;

	return CS_SUCCESS;
}

/*
 * Resume a socket.  If a card is present, verify its CIS against
 * our cached copy.  If they are different, the card has been
 * replaced, and we need to tell the drivers.
 */
static int socket_resume(struct pcmcia_socket *skt)
{
	int ret;

	if (!(skt->state & SOCKET_SUSPEND))
		return CS_IN_USE;

	skt->socket = dead_socket;
	skt->ops->init(skt);
	skt->ops->set_socket(skt, &skt->socket);

	ret = socket_setup(skt, resume_delay);
	if (ret == CS_SUCCESS) {
		/*
		 * FIXME: need a better check here for cardbus cards.
		 */
		if (verify_cis_cache(skt) != 0) {
			cs_dbg(skt, 4, "cis mismatch - different card\n");
			socket_remove_drivers(skt);
			destroy_cis_cache(skt);
			/*
			 * Workaround: give DS time to schedule removal.
			 * Remove me once the 100ms delay is eliminated
			 * in ds.c
			 */
			msleep(200);
			send_event(skt, CS_EVENT_CARD_INSERTION, CS_EVENT_PRI_LOW);
		} else {
			cs_dbg(skt, 4, "cis matches cache\n");
			send_event(skt, CS_EVENT_PM_RESUME, CS_EVENT_PRI_LOW);
		}
	} else {
		socket_shutdown(skt);
		cs_socket_put(skt);
	}

	skt->state &= ~SOCKET_SUSPEND;

	return CS_SUCCESS;
}

static void socket_remove(struct pcmcia_socket *skt)
{
	socket_shutdown(skt);
	cs_socket_put(skt);
}

/*
 * Process a socket card detect status change.
 *
 * If we don't have a card already present, delay the detect event for
 * about 20ms (to be on the safe side) before reading the socket status.
 *
 * Some i82365-based systems send multiple SS_DETECT events during card
 * insertion, and the "card present" status bit seems to bounce.  This
 * will probably be true with GPIO-based card detection systems after
 * the product has aged.
 */
static void socket_detect_change(struct pcmcia_socket *skt)
{
	if (!(skt->state & SOCKET_SUSPEND)) {
		int status;

		if (!(skt->state & SOCKET_PRESENT))
			msleep(20);

		skt->ops->get_status(skt, &status);
		if ((skt->state & SOCKET_PRESENT) &&
		     !(status & SS_DETECT))
			socket_remove(skt);
		if (!(skt->state & SOCKET_PRESENT) &&
		    (status & SS_DETECT))
			socket_insert(skt);
	}
}

static int pccardd(void *__skt)
{
	struct pcmcia_socket *skt = __skt;
	DECLARE_WAITQUEUE(wait, current);
	int ret;

	daemonize("pccardd");

	skt->thread = current;
	skt->socket = dead_socket;
	skt->ops->init(skt);
	skt->ops->set_socket(skt, &skt->socket);

	/* register with the device core */
	ret = class_device_register(&skt->dev);
	if (ret) {
		printk(KERN_WARNING "PCMCIA: unable to register socket 0x%p\n",
			skt);
		skt->thread = NULL;
		complete_and_exit(&skt->thread_done, 0);
	}
	complete(&skt->thread_done);

	add_wait_queue(&skt->thread_wait, &wait);
	for (;;) {
		unsigned long flags;
		unsigned int events;

		set_current_state(TASK_INTERRUPTIBLE);

		spin_lock_irqsave(&skt->thread_lock, flags);
		events = skt->thread_events;
		skt->thread_events = 0;
		spin_unlock_irqrestore(&skt->thread_lock, flags);

		if (events) {
			down(&skt->skt_sem);
			if (events & SS_DETECT)
				socket_detect_change(skt);
			if (events & SS_BATDEAD)
				send_event(skt, CS_EVENT_BATTERY_DEAD, CS_EVENT_PRI_LOW);
			if (events & SS_BATWARN)
				send_event(skt, CS_EVENT_BATTERY_LOW, CS_EVENT_PRI_LOW);
			if (events & SS_READY)
				send_event(skt, CS_EVENT_READY_CHANGE, CS_EVENT_PRI_LOW);
			up(&skt->skt_sem);
			continue;
		}

		schedule();
		if (current->flags & PF_FREEZE)
			refrigerator(PF_FREEZE);

		if (!skt->thread)
			break;
	}
	remove_wait_queue(&skt->thread_wait, &wait);

	/* remove from the device core */
	class_device_unregister(&skt->dev);

	complete_and_exit(&skt->thread_done, 0);
}

/*
 * Yenta (at least) probes interrupts before registering the socket and
 * starting the handler thread.
 */
void pcmcia_parse_events(struct pcmcia_socket *s, u_int events)
{
	cs_dbg(s, 4, "parse_events: events %08x\n", events);
	if (s->thread) {
		spin_lock(&s->thread_lock);
		s->thread_events |= events;
		spin_unlock(&s->thread_lock);

		wake_up(&s->thread_wait);
	}
} /* pcmcia_parse_events */


/*======================================================================

    Special stuff for managing IO windows, because they are scarce.
    
======================================================================*/

static int alloc_io_space(struct pcmcia_socket *s, u_int attr, ioaddr_t *base,
			  ioaddr_t num, u_int lines, char *name)
{
    int i;
    ioaddr_t try, align;

    align = (*base) ? (lines ? 1<<lines : 0) : 1;
    if (align && (align < num)) {
	if (*base) {
	    cs_dbg(s, 0, "odd IO request: num %04x align %04x\n",
		   num, align);
	    align = 0;
	} else
	    while (align && (align < num)) align <<= 1;
    }
    if (*base & ~(align-1)) {
	cs_dbg(s, 0, "odd IO request: base %04x align %04x\n",
	       *base, align);
	align = 0;
    }
    if ((s->features & SS_CAP_STATIC_MAP) && s->io_offset) {
	*base = s->io_offset | (*base & 0x0fff);
	return 0;
    }
    /* Check for an already-allocated window that must conflict with
       what was asked for.  It is a hack because it does not catch all
       potential conflicts, just the most obvious ones. */
    for (i = 0; i < MAX_IO_WIN; i++)
	if ((s->io[i].NumPorts != 0) &&
	    ((s->io[i].BasePort & (align-1)) == *base))
	    return 1;
    for (i = 0; i < MAX_IO_WIN; i++) {
	if (s->io[i].NumPorts == 0) {
	    s->io[i].res = find_io_region(*base, num, align, name, s);
	    if (s->io[i].res) {
		s->io[i].Attributes = attr;
		s->io[i].BasePort = *base = s->io[i].res->start;
		s->io[i].NumPorts = s->io[i].InUse = num;
		break;
	    } else
		return 1;
	} else if (s->io[i].Attributes != attr)
	    continue;
	/* Try to extend top of window */
	try = s->io[i].BasePort + s->io[i].NumPorts;
	if ((*base == 0) || (*base == try))
	    if (adjust_io_region(s->io[i].res, s->io[i].res->start,
				 s->io[i].res->end + num, s) == 0) {
		*base = try;
		s->io[i].NumPorts += num;
		s->io[i].InUse += num;
		break;
	    }
	/* Try to extend bottom of window */
	try = s->io[i].BasePort - num;
	if ((*base == 0) || (*base == try))
	    if (adjust_io_region(s->io[i].res, s->io[i].res->start - num,
				 s->io[i].res->end, s) == 0) {
		s->io[i].BasePort = *base = try;
		s->io[i].NumPorts += num;
		s->io[i].InUse += num;
		break;
	    }
    }
    return (i == MAX_IO_WIN);
} /* alloc_io_space */

static void release_io_space(struct pcmcia_socket *s, ioaddr_t base,
			     ioaddr_t num)
{
    int i;

    for (i = 0; i < MAX_IO_WIN; i++) {
	if ((s->io[i].BasePort <= base) &&
	    (s->io[i].BasePort+s->io[i].NumPorts >= base+num)) {
	    s->io[i].InUse -= num;
	    /* Free the window if no one else is using it */
	    if (s->io[i].InUse == 0) {
		s->io[i].NumPorts = 0;
		release_resource(s->io[i].res);
		kfree(s->io[i].res);
		s->io[i].res = NULL;
	    }
	}
    }
}

/*======================================================================

    Access_configuration_register() reads and writes configuration
    registers in attribute memory.  Memory window 0 is reserved for
    this and the tuple reading services.
    
======================================================================*/

int pccard_access_configuration_register(struct pcmcia_socket *s,
					 unsigned int function,
					 conf_reg_t *reg)
{
    config_t *c;
    int addr;
    u_char val;

    if (!s || !s->config)
	return CS_NO_CARD;    

    c = &s->config[function];

    if (c == NULL)
	return CS_NO_CARD;

    if (!(c->state & CONFIG_LOCKED))
	return CS_CONFIGURATION_LOCKED;

    addr = (c->ConfigBase + reg->Offset) >> 1;
    
    switch (reg->Action) {
    case CS_READ:
	read_cis_mem(s, 1, addr, 1, &val);
	reg->Value = val;
	break;
    case CS_WRITE:
	val = reg->Value;
	write_cis_mem(s, 1, addr, 1, &val);
	break;
    default:
	return CS_BAD_ARGS;
	break;
    }
    return CS_SUCCESS;
} /* access_configuration_register */
EXPORT_SYMBOL(pccard_access_configuration_register);

/*====================================================================*/

int pcmcia_deregister_client(client_handle_t handle)
{
    client_t **client;
    struct pcmcia_socket *s;
    u_long flags;
    int i;
    
    if (CHECK_HANDLE(handle))
	return CS_BAD_HANDLE;

    s = SOCKET(handle);
    cs_dbg(s, 1, "deregister_client(%p)\n", handle);

    if (handle->state &
	(CLIENT_IRQ_REQ|CLIENT_IO_REQ|CLIENT_CONFIG_LOCKED))
	return CS_IN_USE;
    for (i = 0; i < MAX_WIN; i++)
	if (handle->state & CLIENT_WIN_REQ(i))
	    return CS_IN_USE;

    if ((handle->state & CLIENT_STALE) ||
	(handle->Attributes & INFO_MASTER_CLIENT)) {
	spin_lock_irqsave(&s->lock, flags);
	client = &s->clients;
	while ((*client) && ((*client) != handle))
	    client = &(*client)->next;
	if (*client == NULL) {
	    spin_unlock_irqrestore(&s->lock, flags);
	    return CS_BAD_HANDLE;
	}
	*client = handle->next;
	handle->client_magic = 0;
	kfree(handle);
	spin_unlock_irqrestore(&s->lock, flags);
    } else {
	handle->state = CLIENT_UNBOUND;
	handle->event_handler = NULL;
    }

    return CS_SUCCESS;
} /* deregister_client */

/*====================================================================*/

int pccard_get_configuration_info(struct pcmcia_socket *s,
				  unsigned int function,
				  config_info_t *config)
{
    config_t *c;
    
    if (!(s->state & SOCKET_PRESENT))
	return CS_NO_CARD;

    config->Function = function;

#ifdef CONFIG_CARDBUS
    if (s->state & SOCKET_CARDBUS) {
	memset(config, 0, sizeof(config_info_t));
	config->Vcc = s->socket.Vcc;
	config->Vpp1 = config->Vpp2 = s->socket.Vpp;
	config->Option = s->cb_dev->subordinate->number;
	if (s->state & SOCKET_CARDBUS_CONFIG) {
	    config->Attributes = CONF_VALID_CLIENT;
	    config->IntType = INT_CARDBUS;
	    config->AssignedIRQ = s->irq.AssignedIRQ;
	    if (config->AssignedIRQ)
		config->Attributes |= CONF_ENABLE_IRQ;
	    config->BasePort1 = s->io[0].BasePort;
	    config->NumPorts1 = s->io[0].NumPorts;
	}
	return CS_SUCCESS;
    }
#endif
    
    c = (s->config != NULL) ? &s->config[function] : NULL;
    
    if ((c == NULL) || !(c->state & CONFIG_LOCKED)) {
	config->Attributes = 0;
	config->Vcc = s->socket.Vcc;
	config->Vpp1 = config->Vpp2 = s->socket.Vpp;
	return CS_SUCCESS;
    }
    
    /* !!! This is a hack !!! */
    memcpy(&config->Attributes, &c->Attributes, sizeof(config_t));
    config->Attributes |= CONF_VALID_CLIENT;
    config->CardValues = c->CardValues;
    config->IRQAttributes = c->irq.Attributes;
    config->AssignedIRQ = s->irq.AssignedIRQ;
    config->BasePort1 = c->io.BasePort1;
    config->NumPorts1 = c->io.NumPorts1;
    config->Attributes1 = c->io.Attributes1;
    config->BasePort2 = c->io.BasePort2;
    config->NumPorts2 = c->io.NumPorts2;
    config->Attributes2 = c->io.Attributes2;
    config->IOAddrLines = c->io.IOAddrLines;
    
    return CS_SUCCESS;
} /* get_configuration_info */
EXPORT_SYMBOL(pccard_get_configuration_info);

/*======================================================================

    Return information about this version of Card Services.
    
======================================================================*/

int pcmcia_get_card_services_info(servinfo_t *info)
{
    unsigned int socket_count = 0;
    struct list_head *tmp;
    info->Signature[0] = 'C';
    info->Signature[1] = 'S';
    down_read(&pcmcia_socket_list_rwsem);
    list_for_each(tmp, &pcmcia_socket_list)
	    socket_count++;
    up_read(&pcmcia_socket_list_rwsem);
    info->Count = socket_count;
    info->Revision = CS_RELEASE_CODE;
    info->CSLevel = 0x0210;
    info->VendorString = (char *)release;
    return CS_SUCCESS;
} /* get_card_services_info */

#ifdef CONFIG_PCMCIA_OBSOLETE

/*======================================================================

    Note that get_first_client() *does* recognize the Socket field
    in the request structure.
    
======================================================================*/

int pcmcia_get_first_client(client_handle_t *handle, client_req_t *req)
{
    socket_t s;
    struct pcmcia_socket *socket;
    if (req->Attributes & CLIENT_THIS_SOCKET)
	s = req->Socket;
    else
	s = 0;
    socket = pcmcia_get_socket_by_nr(s);
    if (!socket)
	return CS_BAD_SOCKET;
    if (socket->clients == NULL)
	return CS_NO_MORE_ITEMS;
    *handle = socket->clients;
    return CS_SUCCESS;
} /* get_first_client */
EXPORT_SYMBOL(pcmcia_get_first_client);

/*====================================================================*/

int pcmcia_get_next_client(client_handle_t *handle, client_req_t *req)
{
    struct pcmcia_socket *s;
    if ((handle == NULL) || CHECK_HANDLE(*handle))
	return CS_BAD_HANDLE;
    if ((*handle)->next == NULL) {
	if (req->Attributes & CLIENT_THIS_SOCKET)
	    return CS_NO_MORE_ITEMS;
	s = (*handle)->Socket;
	if (s->clients == NULL)
	    return CS_NO_MORE_ITEMS;
	*handle = s->clients;
    } else
	*handle = (*handle)->next;
    return CS_SUCCESS;
} /* get_next_client */
EXPORT_SYMBOL(pcmcia_get_next_client);
#endif /* CONFIG_PCMCIA_OBSOLETE */

/*====================================================================*/

int pcmcia_get_window(struct pcmcia_socket *s, window_handle_t *handle, int idx, win_req_t *req)
{
    window_t *win;
    int w;

    if (!s || !(s->state & SOCKET_PRESENT))
	return CS_NO_CARD;
    for (w = idx; w < MAX_WIN; w++)
	if (s->state & SOCKET_WIN_REQ(w)) break;
    if (w == MAX_WIN)
	return CS_NO_MORE_ITEMS;
    win = &s->win[w];
    req->Base = win->ctl.res->start;
    req->Size = win->ctl.res->end - win->ctl.res->start + 1;
    req->AccessSpeed = win->ctl.speed;
    req->Attributes = 0;
    if (win->ctl.flags & MAP_ATTRIB)
	req->Attributes |= WIN_MEMORY_TYPE_AM;
    if (win->ctl.flags & MAP_ACTIVE)
	req->Attributes |= WIN_ENABLE;
    if (win->ctl.flags & MAP_16BIT)
	req->Attributes |= WIN_DATA_WIDTH_16;
    if (win->ctl.flags & MAP_USE_WAIT)
	req->Attributes |= WIN_USE_WAIT;
    *handle = win;
    return CS_SUCCESS;
} /* get_window */
EXPORT_SYMBOL(pcmcia_get_window);

/*=====================================================================

    Return the PCI device associated with a card..

======================================================================*/

#ifdef CONFIG_CARDBUS

struct pci_bus *pcmcia_lookup_bus(struct pcmcia_socket *s)
{
	if (!s || !(s->state & SOCKET_CARDBUS))
		return NULL;

	return s->cb_dev->subordinate;
}

EXPORT_SYMBOL(pcmcia_lookup_bus);

#endif

/*======================================================================

    Get the current socket state bits.  We don't support the latched
    SocketState yet: I haven't seen any point for it.
    
======================================================================*/

int pccard_get_status(struct pcmcia_socket *s, unsigned int function, cs_status_t *status)
{
    config_t *c;
    int val;
    
    s->ops->get_status(s, &val);
    status->CardState = status->SocketState = 0;
    status->CardState |= (val & SS_DETECT) ? CS_EVENT_CARD_DETECT : 0;
    status->CardState |= (val & SS_CARDBUS) ? CS_EVENT_CB_DETECT : 0;
    status->CardState |= (val & SS_3VCARD) ? CS_EVENT_3VCARD : 0;
    status->CardState |= (val & SS_XVCARD) ? CS_EVENT_XVCARD : 0;
    if (s->state & SOCKET_SUSPEND)
	status->CardState |= CS_EVENT_PM_SUSPEND;
    if (!(s->state & SOCKET_PRESENT))
	return CS_NO_CARD;
    
    c = (s->config != NULL) ? &s->config[function] : NULL;
    if ((c != NULL) && (c->state & CONFIG_LOCKED) &&
	(c->IntType & (INT_MEMORY_AND_IO | INT_ZOOMED_VIDEO))) {
	u_char reg;
	if (c->Present & PRESENT_PIN_REPLACE) {
	    read_cis_mem(s, 1, (c->ConfigBase+CISREG_PRR)>>1, 1, &reg);
	    status->CardState |=
		(reg & PRR_WP_STATUS) ? CS_EVENT_WRITE_PROTECT : 0;
	    status->CardState |=
		(reg & PRR_READY_STATUS) ? CS_EVENT_READY_CHANGE : 0;
	    status->CardState |=
		(reg & PRR_BVD2_STATUS) ? CS_EVENT_BATTERY_LOW : 0;
	    status->CardState |=
		(reg & PRR_BVD1_STATUS) ? CS_EVENT_BATTERY_DEAD : 0;
	} else {
	    /* No PRR?  Then assume we're always ready */
	    status->CardState |= CS_EVENT_READY_CHANGE;
	}
	if (c->Present & PRESENT_EXT_STATUS) {
	    read_cis_mem(s, 1, (c->ConfigBase+CISREG_ESR)>>1, 1, &reg);
	    status->CardState |=
		(reg & ESR_REQ_ATTN) ? CS_EVENT_REQUEST_ATTENTION : 0;
	}
	return CS_SUCCESS;
    }
    status->CardState |=
	(val & SS_WRPROT) ? CS_EVENT_WRITE_PROTECT : 0;
    status->CardState |=
	(val & SS_BATDEAD) ? CS_EVENT_BATTERY_DEAD : 0;
    status->CardState |=
	(val & SS_BATWARN) ? CS_EVENT_BATTERY_LOW : 0;
    status->CardState |=
	(val & SS_READY) ? CS_EVENT_READY_CHANGE : 0;
    return CS_SUCCESS;
} /* get_status */
EXPORT_SYMBOL(pccard_get_status);

/*======================================================================

    Change the card address of an already open memory window.
    
======================================================================*/

int pcmcia_get_mem_page(window_handle_t win, memreq_t *req)
{
    if ((win == NULL) || (win->magic != WINDOW_MAGIC))
	return CS_BAD_HANDLE;
    req->Page = 0;
    req->CardOffset = win->ctl.card_start;
    return CS_SUCCESS;
} /* get_mem_page */

int pcmcia_map_mem_page(window_handle_t win, memreq_t *req)
{
    struct pcmcia_socket *s;
    if ((win == NULL) || (win->magic != WINDOW_MAGIC))
	return CS_BAD_HANDLE;
    if (req->Page != 0)
	return CS_BAD_PAGE;
    s = win->sock;
    win->ctl.card_start = req->CardOffset;
    if (s->ops->set_mem_map(s, &win->ctl) != 0)
	return CS_BAD_OFFSET;
    return CS_SUCCESS;
} /* map_mem_page */

/*======================================================================

    Modify a locked socket configuration
    
======================================================================*/

int pcmcia_modify_configuration(client_handle_t handle,
				modconf_t *mod)
{
    struct pcmcia_socket *s;
    config_t *c;
    
    if (CHECK_HANDLE(handle))
	return CS_BAD_HANDLE;
    s = SOCKET(handle); c = CONFIG(handle);
    if (!(s->state & SOCKET_PRESENT))
	return CS_NO_CARD;
    if (!(c->state & CONFIG_LOCKED))
	return CS_CONFIGURATION_LOCKED;
    
    if (mod->Attributes & CONF_IRQ_CHANGE_VALID) {
	if (mod->Attributes & CONF_ENABLE_IRQ) {
	    c->Attributes |= CONF_ENABLE_IRQ;
	    s->socket.io_irq = s->irq.AssignedIRQ;
	} else {
	    c->Attributes &= ~CONF_ENABLE_IRQ;
	    s->socket.io_irq = 0;
	}
	s->ops->set_socket(s, &s->socket);
    }

    if (mod->Attributes & CONF_VCC_CHANGE_VALID)
	return CS_BAD_VCC;

    /* We only allow changing Vpp1 and Vpp2 to the same value */
    if ((mod->Attributes & CONF_VPP1_CHANGE_VALID) &&
	(mod->Attributes & CONF_VPP2_CHANGE_VALID)) {
	if (mod->Vpp1 != mod->Vpp2)
	    return CS_BAD_VPP;
	c->Vpp1 = c->Vpp2 = s->socket.Vpp = mod->Vpp1;
	if (s->ops->set_socket(s, &s->socket))
	    return CS_BAD_VPP;
    } else if ((mod->Attributes & CONF_VPP1_CHANGE_VALID) ||
	       (mod->Attributes & CONF_VPP2_CHANGE_VALID))
	return CS_BAD_VPP;

    return CS_SUCCESS;
} /* modify_configuration */

#ifdef CONFIG_PCMCIA_OBSOLETE

/*======================================================================

    Modify the attributes of a window returned by RequestWindow.

======================================================================*/

int pcmcia_modify_window(window_handle_t win, modwin_t *req)
{
    if ((win == NULL) || (win->magic != WINDOW_MAGIC))
	return CS_BAD_HANDLE;

    win->ctl.flags &= ~(MAP_ATTRIB|MAP_ACTIVE);
    if (req->Attributes & WIN_MEMORY_TYPE)
	win->ctl.flags |= MAP_ATTRIB;
    if (req->Attributes & WIN_ENABLE)
	win->ctl.flags |= MAP_ACTIVE;
    if (req->Attributes & WIN_DATA_WIDTH_16)
	win->ctl.flags |= MAP_16BIT;
    if (req->Attributes & WIN_USE_WAIT)
	win->ctl.flags |= MAP_USE_WAIT;
    win->ctl.speed = req->AccessSpeed;
    win->sock->ops->set_mem_map(win->sock, &win->ctl);
    
    return CS_SUCCESS;
} /* modify_window */
EXPORT_SYMBOL(pcmcia_modify_window);

#endif /* CONFIG_PCMCIA_OBSOLETE */


/*======================================================================

    Register_client() uses the dev_info_t handle to match the
    caller with a socket.  The driver must have already been bound
    to a socket with bind_device() -- in fact, bind_device()
    allocates the client structure that will be used.
    
======================================================================*/

int pcmcia_register_client(client_handle_t *handle, client_reg_t *req)
{
    client_t *client = NULL;
    struct pcmcia_socket *s;
    
    /* Look for unbound client with matching dev_info */
    down_read(&pcmcia_socket_list_rwsem);
    list_for_each_entry(s, &pcmcia_socket_list, socket_list) {
	client = s->clients;
	while (client != NULL) {
	    if ((strcmp(client->dev_info, (char *)req->dev_info) == 0)
		&& (client->state & CLIENT_UNBOUND)) break;
	    client = client->next;
	}
	if (client != NULL) break;
    }
    up_read(&pcmcia_socket_list_rwsem);
    if (client == NULL)
	return CS_OUT_OF_RESOURCE;

    /*
     * Prevent this racing with a card insertion.
     */
    down(&s->skt_sem);
    *handle = client;
    client->state &= ~CLIENT_UNBOUND;
    client->Socket = s;
    client->Attributes = req->Attributes;
    client->EventMask = req->EventMask;
    client->event_handler = req->event_handler;
    client->event_callback_args = req->event_callback_args;
    client->event_callback_args.client_handle = client;

    if (s->state & SOCKET_CARDBUS)
	client->state |= CLIENT_CARDBUS;
    
    if ((!(s->state & SOCKET_CARDBUS)) && (s->functions == 0) &&
	(client->Function != BIND_FN_ALL)) {
	cistpl_longlink_mfc_t mfc;
	if (pccard_read_tuple(s, client->Function, CISTPL_LONGLINK_MFC, &mfc)
	    == CS_SUCCESS)
	    s->functions = mfc.nfn;
	else
	    s->functions = 1;
	s->config = kmalloc(sizeof(config_t) * s->functions,
			    GFP_KERNEL);
	if (!s->config)
		goto out_no_resource;
	memset(s->config, 0, sizeof(config_t) * s->functions);
    }
    
    cs_dbg(s, 1, "register_client(): client 0x%p, dev %s\n",
	   client, client->dev_info);
    if (client->EventMask & CS_EVENT_REGISTRATION_COMPLETE)
	EVENT(client, CS_EVENT_REGISTRATION_COMPLETE, CS_EVENT_PRI_LOW);

    if ((s->state & (SOCKET_PRESENT|SOCKET_CARDBUS)) == SOCKET_PRESENT) {
	if (client->EventMask & CS_EVENT_CARD_INSERTION)
	    EVENT(client, CS_EVENT_CARD_INSERTION, CS_EVENT_PRI_LOW);
	else
	    client->PendingEvents |= CS_EVENT_CARD_INSERTION;
    }

    up(&s->skt_sem);
    return CS_SUCCESS;

 out_no_resource:
    up(&s->skt_sem);
    return CS_OUT_OF_RESOURCE;
} /* register_client */

/*====================================================================*/

int pcmcia_release_configuration(client_handle_t handle)
{
    pccard_io_map io = { 0, 0, 0, 0, 1 };
    struct pcmcia_socket *s;
    int i;
    
    if (CHECK_HANDLE(handle) ||
	!(handle->state & CLIENT_CONFIG_LOCKED))
	return CS_BAD_HANDLE;
    handle->state &= ~CLIENT_CONFIG_LOCKED;
    s = SOCKET(handle);
    
#ifdef CONFIG_CARDBUS
    if (handle->state & CLIENT_CARDBUS)
	return CS_SUCCESS;
#endif
    
    if (!(handle->state & CLIENT_STALE)) {
	config_t *c = CONFIG(handle);
	if (--(s->lock_count) == 0) {
	    s->socket.flags = SS_OUTPUT_ENA;   /* Is this correct? */
	    s->socket.Vpp = 0;
	    s->socket.io_irq = 0;
	    s->ops->set_socket(s, &s->socket);
	}
	if (c->state & CONFIG_IO_REQ)
	    for (i = 0; i < MAX_IO_WIN; i++) {
		if (s->io[i].NumPorts == 0)
		    continue;
		s->io[i].Config--;
		if (s->io[i].Config != 0)
		    continue;
		io.map = i;
		s->ops->set_io_map(s, &io);
	    }
	c->state &= ~CONFIG_LOCKED;
    }
    
    return CS_SUCCESS;
} /* release_configuration */

/*======================================================================

    Release_io() releases the I/O ranges allocated by a client.  This
    may be invoked some time after a card ejection has already dumped
    the actual socket configuration, so if the client is "stale", we
    don't bother checking the port ranges against the current socket
    values.
    
======================================================================*/

int pcmcia_release_io(client_handle_t handle, io_req_t *req)
{
    struct pcmcia_socket *s;
    
    if (CHECK_HANDLE(handle) || !(handle->state & CLIENT_IO_REQ))
	return CS_BAD_HANDLE;
    handle->state &= ~CLIENT_IO_REQ;
    s = SOCKET(handle);
    
#ifdef CONFIG_CARDBUS
    if (handle->state & CLIENT_CARDBUS)
	return CS_SUCCESS;
#endif
    
    if (!(handle->state & CLIENT_STALE)) {
	config_t *c = CONFIG(handle);
	if (c->state & CONFIG_LOCKED)
	    return CS_CONFIGURATION_LOCKED;
	if ((c->io.BasePort1 != req->BasePort1) ||
	    (c->io.NumPorts1 != req->NumPorts1) ||
	    (c->io.BasePort2 != req->BasePort2) ||
	    (c->io.NumPorts2 != req->NumPorts2))
	    return CS_BAD_ARGS;
	c->state &= ~CONFIG_IO_REQ;
    }

    release_io_space(s, req->BasePort1, req->NumPorts1);
    if (req->NumPorts2)
	release_io_space(s, req->BasePort2, req->NumPorts2);
    
    return CS_SUCCESS;
} /* release_io */

/*====================================================================*/

int pcmcia_release_irq(client_handle_t handle, irq_req_t *req)
{
    struct pcmcia_socket *s;
    if (CHECK_HANDLE(handle) || !(handle->state & CLIENT_IRQ_REQ))
	return CS_BAD_HANDLE;
    handle->state &= ~CLIENT_IRQ_REQ;
    s = SOCKET(handle);
    
    if (!(handle->state & CLIENT_STALE)) {
	config_t *c = CONFIG(handle);
	if (c->state & CONFIG_LOCKED)
	    return CS_CONFIGURATION_LOCKED;
	if (c->irq.Attributes != req->Attributes)
	    return CS_BAD_ATTRIBUTE;
	if (s->irq.AssignedIRQ != req->AssignedIRQ)
	    return CS_BAD_IRQ;
	if (--s->irq.Config == 0) {
	    c->state &= ~CONFIG_IRQ_REQ;
	    s->irq.AssignedIRQ = 0;
	}
    }
    
    if (req->Attributes & IRQ_HANDLE_PRESENT) {
	free_irq(req->AssignedIRQ, req->Instance);
    }

#ifdef CONFIG_PCMCIA_PROBE
    if (req->AssignedIRQ != s->pci_irq)
	undo_irq(req->Attributes, req->AssignedIRQ);
#endif
    
    return CS_SUCCESS;
} /* cs_release_irq */

/*====================================================================*/

int pcmcia_release_window(window_handle_t win)
{
    struct pcmcia_socket *s;
    
    if ((win == NULL) || (win->magic != WINDOW_MAGIC))
	return CS_BAD_HANDLE;
    s = win->sock;
    if (!(win->handle->state & CLIENT_WIN_REQ(win->index)))
	return CS_BAD_HANDLE;

    /* Shut down memory window */
    win->ctl.flags &= ~MAP_ACTIVE;
    s->ops->set_mem_map(s, &win->ctl);
    s->state &= ~SOCKET_WIN_REQ(win->index);

    /* Release system memory */
    if (win->ctl.res) {
	release_resource(win->ctl.res);
	kfree(win->ctl.res);
	win->ctl.res = NULL;
    }
    win->handle->state &= ~CLIENT_WIN_REQ(win->index);

    win->magic = 0;
    
    return CS_SUCCESS;
} /* release_window */

/*====================================================================*/

int pcmcia_request_configuration(client_handle_t handle,
				 config_req_t *req)
{
    int i;
    u_int base;
    struct pcmcia_socket *s;
    config_t *c;
    pccard_io_map iomap;
    
    if (CHECK_HANDLE(handle))
	return CS_BAD_HANDLE;
    s = SOCKET(handle);
    if (!(s->state & SOCKET_PRESENT))
	return CS_NO_CARD;
    
#ifdef CONFIG_CARDBUS
    if (handle->state & CLIENT_CARDBUS)
	return CS_UNSUPPORTED_MODE;
#endif
    
    if (req->IntType & INT_CARDBUS)
	return CS_UNSUPPORTED_MODE;
    c = CONFIG(handle);
    if (c->state & CONFIG_LOCKED)
	return CS_CONFIGURATION_LOCKED;

    /* Do power control.  We don't allow changes in Vcc. */
    if (s->socket.Vcc != req->Vcc)
	return CS_BAD_VCC;
    if (req->Vpp1 != req->Vpp2)
	return CS_BAD_VPP;
    s->socket.Vpp = req->Vpp1;
    if (s->ops->set_socket(s, &s->socket))
	return CS_BAD_VPP;
    
    c->Vcc = req->Vcc; c->Vpp1 = c->Vpp2 = req->Vpp1;
    
    /* Pick memory or I/O card, DMA mode, interrupt */
    c->IntType = req->IntType;
    c->Attributes = req->Attributes;
    if (req->IntType & INT_MEMORY_AND_IO)
	s->socket.flags |= SS_IOCARD;
    if (req->IntType & INT_ZOOMED_VIDEO)
	s->socket.flags |= SS_ZVCARD | SS_IOCARD;
    if (req->Attributes & CONF_ENABLE_DMA)
	s->socket.flags |= SS_DMA_MODE;
    if (req->Attributes & CONF_ENABLE_SPKR)
	s->socket.flags |= SS_SPKR_ENA;
    if (req->Attributes & CONF_ENABLE_IRQ)
	s->socket.io_irq = s->irq.AssignedIRQ;
    else
	s->socket.io_irq = 0;
    s->ops->set_socket(s, &s->socket);
    s->lock_count++;
    
    /* Set up CIS configuration registers */
    base = c->ConfigBase = req->ConfigBase;
    c->Present = c->CardValues = req->Present;
    if (req->Present & PRESENT_COPY) {
	c->Copy = req->Copy;
	write_cis_mem(s, 1, (base + CISREG_SCR)>>1, 1, &c->Copy);
    }
    if (req->Present & PRESENT_OPTION) {
	if (s->functions == 1) {
	    c->Option = req->ConfigIndex & COR_CONFIG_MASK;
	} else {
	    c->Option = req->ConfigIndex & COR_MFC_CONFIG_MASK;
	    c->Option |= COR_FUNC_ENA|COR_IREQ_ENA;
	    if (req->Present & PRESENT_IOBASE_0)
		c->Option |= COR_ADDR_DECODE;
	}
	if (c->state & CONFIG_IRQ_REQ)
	    if (!(c->irq.Attributes & IRQ_FORCED_PULSE))
		c->Option |= COR_LEVEL_REQ;
	write_cis_mem(s, 1, (base + CISREG_COR)>>1, 1, &c->Option);
	mdelay(40);
    }
    if (req->Present & PRESENT_STATUS) {
	c->Status = req->Status;
	write_cis_mem(s, 1, (base + CISREG_CCSR)>>1, 1, &c->Status);
    }
    if (req->Present & PRESENT_PIN_REPLACE) {
	c->Pin = req->Pin;
	write_cis_mem(s, 1, (base + CISREG_PRR)>>1, 1, &c->Pin);
    }
    if (req->Present & PRESENT_EXT_STATUS) {
	c->ExtStatus = req->ExtStatus;
	write_cis_mem(s, 1, (base + CISREG_ESR)>>1, 1, &c->ExtStatus);
    }
    if (req->Present & PRESENT_IOBASE_0) {
	u_char b = c->io.BasePort1 & 0xff;
	write_cis_mem(s, 1, (base + CISREG_IOBASE_0)>>1, 1, &b);
	b = (c->io.BasePort1 >> 8) & 0xff;
	write_cis_mem(s, 1, (base + CISREG_IOBASE_1)>>1, 1, &b);
    }
    if (req->Present & PRESENT_IOSIZE) {
	u_char b = c->io.NumPorts1 + c->io.NumPorts2 - 1;
	write_cis_mem(s, 1, (base + CISREG_IOSIZE)>>1, 1, &b);
    }
    
    /* Configure I/O windows */
    if (c->state & CONFIG_IO_REQ) {
	iomap.speed = io_speed;
	for (i = 0; i < MAX_IO_WIN; i++)
	    if (s->io[i].NumPorts != 0) {
		iomap.map = i;
		iomap.flags = MAP_ACTIVE;
		switch (s->io[i].Attributes & IO_DATA_PATH_WIDTH) {
		case IO_DATA_PATH_WIDTH_16:
		    iomap.flags |= MAP_16BIT; break;
		case IO_DATA_PATH_WIDTH_AUTO:
		    iomap.flags |= MAP_AUTOSZ; break;
		default:
		    break;
		}
		iomap.start = s->io[i].BasePort;
		iomap.stop = iomap.start + s->io[i].NumPorts - 1;
		s->ops->set_io_map(s, &iomap);
		s->io[i].Config++;
	    }
    }
    
    c->state |= CONFIG_LOCKED;
    handle->state |= CLIENT_CONFIG_LOCKED;
    return CS_SUCCESS;
} /* request_configuration */

/*======================================================================
  
    Request_io() reserves ranges of port addresses for a socket.
    I have not implemented range sharing or alias addressing.
    
======================================================================*/

int pcmcia_request_io(client_handle_t handle, io_req_t *req)
{
    struct pcmcia_socket *s;
    config_t *c;
    
    if (CHECK_HANDLE(handle))
	return CS_BAD_HANDLE;
    s = SOCKET(handle);
    if (!(s->state & SOCKET_PRESENT))
	return CS_NO_CARD;

    if (handle->state & CLIENT_CARDBUS) {
#ifdef CONFIG_CARDBUS
	handle->state |= CLIENT_IO_REQ;
	return CS_SUCCESS;
#else
	return CS_UNSUPPORTED_FUNCTION;
#endif
    }

    if (!req)
	return CS_UNSUPPORTED_MODE;
    c = CONFIG(handle);
    if (c->state & CONFIG_LOCKED)
	return CS_CONFIGURATION_LOCKED;
    if (c->state & CONFIG_IO_REQ)
	return CS_IN_USE;
    if (req->Attributes1 & (IO_SHARED | IO_FORCE_ALIAS_ACCESS))
	return CS_BAD_ATTRIBUTE;
    if ((req->NumPorts2 > 0) &&
	(req->Attributes2 & (IO_SHARED | IO_FORCE_ALIAS_ACCESS)))
	return CS_BAD_ATTRIBUTE;

    if (alloc_io_space(s, req->Attributes1, &req->BasePort1,
		       req->NumPorts1, req->IOAddrLines,
		       handle->dev_info))
	return CS_IN_USE;

    if (req->NumPorts2) {
	if (alloc_io_space(s, req->Attributes2, &req->BasePort2,
			   req->NumPorts2, req->IOAddrLines,
			   handle->dev_info)) {
	    release_io_space(s, req->BasePort1, req->NumPorts1);
	    return CS_IN_USE;
	}
    }

    c->io = *req;
    c->state |= CONFIG_IO_REQ;
    handle->state |= CLIENT_IO_REQ;
    return CS_SUCCESS;
} /* request_io */

/*======================================================================

    Request_irq() reserves an irq for this client.

    Also, since Linux only reserves irq's when they are actually
    hooked, we don't guarantee that an irq will still be available
    when the configuration is locked.  Now that I think about it,
    there might be a way to fix this using a dummy handler.
    
======================================================================*/

int pcmcia_request_irq(client_handle_t handle, irq_req_t *req)
{
    struct pcmcia_socket *s;
    config_t *c;
    int ret = CS_IN_USE, irq = 0;
    
    if (CHECK_HANDLE(handle))
	return CS_BAD_HANDLE;
    s = SOCKET(handle);
    if (!(s->state & SOCKET_PRESENT))
	return CS_NO_CARD;
    c = CONFIG(handle);
    if (c->state & CONFIG_LOCKED)
	return CS_CONFIGURATION_LOCKED;
    if (c->state & CONFIG_IRQ_REQ)
	return CS_IN_USE;

#ifdef CONFIG_PCMCIA_PROBE
    if (s->irq.AssignedIRQ != 0) {
	/* If the interrupt is already assigned, it must match */
	irq = s->irq.AssignedIRQ;
	if (req->IRQInfo1 & IRQ_INFO2_VALID) {
	    u_int mask = req->IRQInfo2 & s->irq_mask;
	    ret = ((mask >> irq) & 1) ? 0 : CS_BAD_ARGS;
	} else
	    ret = ((req->IRQInfo1&IRQ_MASK) == irq) ? 0 : CS_BAD_ARGS;
    } else {
	if (req->IRQInfo1 & IRQ_INFO2_VALID) {
	    u_int try, mask = req->IRQInfo2 & s->irq_mask;
	    for (try = 0; try < 2; try++) {
		for (irq = 0; irq < 32; irq++)
		    if ((mask >> irq) & 1) {
			ret = try_irq(req->Attributes, irq, try);
			if (ret == 0) break;
		    }
		if (ret == 0) break;
	    }
	} else {
	    irq = req->IRQInfo1 & IRQ_MASK;
	    ret = try_irq(req->Attributes, irq, 1);
	}
    }
#endif
    if (ret != 0) {
	if (!s->pci_irq)
	    return ret;
	irq = s->pci_irq;
    }

    if (req->Attributes & IRQ_HANDLE_PRESENT) {
	if (request_irq(irq, req->Handler,
			    ((req->Attributes & IRQ_TYPE_DYNAMIC_SHARING) || 
			     (s->functions > 1) ||
			     (irq == s->pci_irq)) ? SA_SHIRQ : 0,
			    handle->dev_info, req->Instance))
	    return CS_IN_USE;
    }

    c->irq.Attributes = req->Attributes;
    s->irq.AssignedIRQ = req->AssignedIRQ = irq;
    s->irq.Config++;
    
    c->state |= CONFIG_IRQ_REQ;
    handle->state |= CLIENT_IRQ_REQ;
    return CS_SUCCESS;
} /* pcmcia_request_irq */

/*======================================================================

    Request_window() establishes a mapping between card memory space
    and system memory space.

======================================================================*/

int pcmcia_request_window(client_handle_t *handle, win_req_t *req, window_handle_t *wh)
{
    struct pcmcia_socket *s;
    window_t *win;
    u_long align;
    int w;
    
    if (CHECK_HANDLE(*handle))
	return CS_BAD_HANDLE;
    s = (*handle)->Socket;
    if (!(s->state & SOCKET_PRESENT))
	return CS_NO_CARD;
    if (req->Attributes & (WIN_PAGED | WIN_SHARED))
	return CS_BAD_ATTRIBUTE;

    /* Window size defaults to smallest available */
    if (req->Size == 0)
	req->Size = s->map_size;
    align = (((s->features & SS_CAP_MEM_ALIGN) ||
	      (req->Attributes & WIN_STRICT_ALIGN)) ?
	     req->Size : s->map_size);
    if (req->Size & (s->map_size-1))
	return CS_BAD_SIZE;
    if ((req->Base && (s->features & SS_CAP_STATIC_MAP)) ||
	(req->Base & (align-1)))
	return CS_BAD_BASE;
    if (req->Base)
	align = 0;

    /* Allocate system memory window */
    for (w = 0; w < MAX_WIN; w++)
	if (!(s->state & SOCKET_WIN_REQ(w))) break;
    if (w == MAX_WIN)
	return CS_OUT_OF_RESOURCE;

    win = &s->win[w];
    win->magic = WINDOW_MAGIC;
    win->index = w;
    win->handle = *handle;
    win->sock = s;

    if (!(s->features & SS_CAP_STATIC_MAP)) {
	win->ctl.res = find_mem_region(req->Base, req->Size, align,
				       (req->Attributes & WIN_MAP_BELOW_1MB),
				       (*handle)->dev_info, s);
	if (!win->ctl.res)
	    return CS_IN_USE;
    }
    (*handle)->state |= CLIENT_WIN_REQ(w);

    /* Configure the socket controller */
    win->ctl.map = w+1;
    win->ctl.flags = 0;
    win->ctl.speed = req->AccessSpeed;
    if (req->Attributes & WIN_MEMORY_TYPE)
	win->ctl.flags |= MAP_ATTRIB;
    if (req->Attributes & WIN_ENABLE)
	win->ctl.flags |= MAP_ACTIVE;
    if (req->Attributes & WIN_DATA_WIDTH_16)
	win->ctl.flags |= MAP_16BIT;
    if (req->Attributes & WIN_USE_WAIT)
	win->ctl.flags |= MAP_USE_WAIT;
    win->ctl.card_start = 0;
    if (s->ops->set_mem_map(s, &win->ctl) != 0)
	return CS_BAD_ARGS;
    s->state |= SOCKET_WIN_REQ(w);

    /* Return window handle */
    if (s->features & SS_CAP_STATIC_MAP) {
	req->Base = win->ctl.static_start;
    } else {
	req->Base = win->ctl.res->start;
    }
    *wh = win;
    
    return CS_SUCCESS;
} /* request_window */

/*======================================================================

    I'm not sure which "reset" function this is supposed to use,
    but for now, it uses the low-level interface's reset, not the
    CIS register.
    
======================================================================*/

int pccard_reset_card(struct pcmcia_socket *skt)
{
	int ret;
    
	cs_dbg(skt, 1, "resetting socket\n");

	down(&skt->skt_sem);
	do {
		if (!(skt->state & SOCKET_PRESENT)) {
			ret = CS_NO_CARD;
			break;
		}
		if (skt->state & SOCKET_SUSPEND) {
			ret = CS_IN_USE;
			break;
		}
		if (skt->state & SOCKET_CARDBUS) {
			ret = CS_UNSUPPORTED_FUNCTION;
			break;
		}

		ret = send_event(skt, CS_EVENT_RESET_REQUEST, CS_EVENT_PRI_LOW);
		if (ret == 0) {
			send_event(skt, CS_EVENT_RESET_PHYSICAL, CS_EVENT_PRI_LOW);
			if (socket_reset(skt) == CS_SUCCESS)
				send_event(skt, CS_EVENT_CARD_RESET, CS_EVENT_PRI_LOW);
		}

		ret = CS_SUCCESS;
	} while (0);
	up(&skt->skt_sem);

	return ret;
} /* reset_card */
EXPORT_SYMBOL(pccard_reset_card);

/*======================================================================

    These shut down or wake up a socket.  They are sort of user
    initiated versions of the APM suspend and resume actions.
    
======================================================================*/

int pcmcia_suspend_card(struct pcmcia_socket *skt)
{
	int ret;
    
	cs_dbg(skt, 1, "suspending socket\n");

	down(&skt->skt_sem);
	do {
		if (!(skt->state & SOCKET_PRESENT)) {
			ret = CS_NO_CARD;
			break;
		}
		if (skt->state & SOCKET_CARDBUS) {
			ret = CS_UNSUPPORTED_FUNCTION;
			break;
		}
		ret = socket_suspend(skt);
	} while (0);
	up(&skt->skt_sem);

	return ret;
} /* suspend_card */

int pcmcia_resume_card(struct pcmcia_socket *skt)
{
	int ret;
    
	cs_dbg(skt, 1, "waking up socket\n");

	down(&skt->skt_sem);
	do {
		if (!(skt->state & SOCKET_PRESENT)) {
			ret = CS_NO_CARD;
			break;
		}
		if (skt->state & SOCKET_CARDBUS) {
			ret = CS_UNSUPPORTED_FUNCTION;
			break;
		}
		ret = socket_resume(skt);
	} while (0);
	up(&skt->skt_sem);

	return ret;
} /* resume_card */

/*======================================================================

    These handle user requests to eject or insert a card.
    
======================================================================*/

int pcmcia_eject_card(struct pcmcia_socket *skt)
{
	int ret;
    
	cs_dbg(skt, 1, "user eject request\n");

	down(&skt->skt_sem);
	do {
		if (!(skt->state & SOCKET_PRESENT)) {
			ret = -ENODEV;
			break;
		}

		ret = send_event(skt, CS_EVENT_EJECTION_REQUEST, CS_EVENT_PRI_LOW);
		if (ret != 0) {
			ret = -EINVAL;
			break;
		}

		socket_remove(skt);
		ret = 0;
	} while (0);
	up(&skt->skt_sem);

	return ret;
} /* eject_card */

int pcmcia_insert_card(struct pcmcia_socket *skt)
{
	int ret;

	cs_dbg(skt, 1, "user insert request\n");

	down(&skt->skt_sem);
	do {
		if (skt->state & SOCKET_PRESENT) {
			ret = -EBUSY;
			break;
		}
		if (socket_insert(skt) == CS_NO_CARD) {
			ret = -ENODEV;
			break;
		}
		ret = 0;
	} while (0);
	up(&skt->skt_sem);

	return ret;
} /* insert_card */

/*======================================================================

    Maybe this should send a CS_EVENT_CARD_INSERTION event if we
    haven't sent one to this client yet?
    
======================================================================*/

#ifdef CONFIG_PCMCIA_OBSOLETE
int pcmcia_set_event_mask(client_handle_t handle, eventmask_t *mask)
{
    u_int events, bit;
    if (CHECK_HANDLE(handle))
	return CS_BAD_HANDLE;
    if (handle->Attributes & CONF_EVENT_MASK_VALID)
	return CS_BAD_SOCKET;
    handle->EventMask = mask->EventMask;
    events = handle->PendingEvents & handle->EventMask;
    handle->PendingEvents -= events;
    while (events != 0) {
	bit = ((events ^ (events-1)) + 1) >> 1;
	EVENT(handle, bit, CS_EVENT_PRI_LOW);
	events -= bit;
    }
    return CS_SUCCESS;
} /* set_event_mask */
EXPORT_SYMBOL(pcmcia_set_event_mask);

#endif /* CONFIG_PCMCIA_OBSOLETE */

/*======================================================================

    OS-specific module glue goes here
    
======================================================================*/
/* in alpha order */
EXPORT_SYMBOL(pcmcia_deregister_client);
EXPORT_SYMBOL(pcmcia_eject_card);
EXPORT_SYMBOL(pcmcia_get_card_services_info);
EXPORT_SYMBOL(pcmcia_get_mem_page);
EXPORT_SYMBOL(pcmcia_insert_card);
EXPORT_SYMBOL(pcmcia_map_mem_page);
EXPORT_SYMBOL(pcmcia_modify_configuration);
EXPORT_SYMBOL(pcmcia_register_client);
EXPORT_SYMBOL(pcmcia_release_configuration);
EXPORT_SYMBOL(pcmcia_release_io);
EXPORT_SYMBOL(pcmcia_release_irq);
EXPORT_SYMBOL(pcmcia_release_window);
EXPORT_SYMBOL(pcmcia_replace_cis);
EXPORT_SYMBOL(pcmcia_request_configuration);
EXPORT_SYMBOL(pcmcia_request_io);
EXPORT_SYMBOL(pcmcia_request_irq);
EXPORT_SYMBOL(pcmcia_request_window);
EXPORT_SYMBOL(pcmcia_resume_card);
EXPORT_SYMBOL(pcmcia_suspend_card);

EXPORT_SYMBOL(dead_socket);
EXPORT_SYMBOL(pcmcia_parse_events);

struct class pcmcia_socket_class = {
	.name = "pcmcia_socket",
	.release = pcmcia_release_socket,
};
EXPORT_SYMBOL(pcmcia_socket_class);


static int __init init_pcmcia_cs(void)
{
	int ret;
	printk(KERN_INFO "%s\n", release);
	printk(KERN_INFO "  %s\n", options);

	ret = class_register(&pcmcia_socket_class);
	if (ret)
		return (ret);
	return class_interface_register(&pccard_sysfs_interface);
}

static void __exit exit_pcmcia_cs(void)
{
    printk(KERN_INFO "unloading Kernel Card Services\n");
    release_resource_db();
    class_interface_unregister(&pccard_sysfs_interface);
    class_unregister(&pcmcia_socket_class);
}

subsys_initcall(init_pcmcia_cs);
module_exit(exit_pcmcia_cs);

/*====================================================================*/

