/*======================================================================

    PC Card Driver Services
    
    ds.c 1.112 2001/10/13 00:08:28
    
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
    terms of the GNU General Public License version 2 (the "GPL"), in
    which case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.
    
======================================================================*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/timer.h>
#include <linux/ioctl.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/workqueue.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <pcmcia/ss.h>

/*====================================================================*/

/* Module parameters */

MODULE_AUTHOR("David Hinds <dahinds@users.sourceforge.net>");
MODULE_DESCRIPTION("PCMCIA Driver Services");
MODULE_LICENSE("Dual MPL/GPL");

#define INT_MODULE_PARM(n, v) static int n = v; MODULE_PARM(n, "i")

#ifdef PCMCIA_DEBUG
INT_MODULE_PARM(pc_debug, PCMCIA_DEBUG);
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
#else
#define DEBUG(n, args...)
#endif

/*====================================================================*/

typedef struct socket_bind_t {
    struct pcmcia_driver	*driver;
    u_char		function;
    dev_link_t		*instance;
    struct socket_bind_t *next;
} socket_bind_t;

/* Device user information */
#define MAX_EVENTS	32
#define USER_MAGIC	0x7ea4
#define CHECK_USER(u) \
    (((u) == NULL) || ((u)->user_magic != USER_MAGIC))
typedef struct user_info_t {
    u_int		user_magic;
    int			event_head, event_tail;
    event_t		event[MAX_EVENTS];
    struct user_info_t	*next;
} user_info_t;

/* Socket state information */
struct pcmcia_bus_socket {
	client_handle_t		handle;
	int			state;
	user_info_t		*user;
	int			req_pending, req_result;
	wait_queue_head_t	queue, request;
	struct work_struct	removal;
	socket_bind_t		*bind;
	struct device		*socket_dev;
	struct pcmcia_socket	*parent;
};

#define SOCKET_PRESENT		0x01
#define SOCKET_BUSY		0x02
#define SOCKET_REMOVAL_PENDING	0x10

/*====================================================================*/

/* Device driver ID passed to Card Services */
static dev_info_t dev_info = "Driver Services";

static int major_dev = -1;

extern struct proc_dir_entry *proc_pccard;

/*====================================================================*/

void cs_error(client_handle_t handle, int func, int ret)
{
	error_info_t err = { func, ret };
	pcmcia_report_error(handle, &err);
}
EXPORT_SYMBOL(cs_error);

/*======================================================================*/

static struct pcmcia_driver * get_pcmcia_driver (dev_info_t *dev_info);
static struct pcmcia_bus_socket * get_socket_info_by_nr(unsigned int nr);

/**
 * pcmcia_register_driver - register a PCMCIA driver with the bus core
 *
 * Registers a PCMCIA driver with the PCMCIA bus core.
 */
int pcmcia_register_driver(struct pcmcia_driver *driver)
{
	if (!driver)
		return -EINVAL;

 	driver->use_count = 0;
	driver->drv.bus = &pcmcia_bus_type;

	return driver_register(&driver->drv);
}
EXPORT_SYMBOL(pcmcia_register_driver);

/**
 * pcmcia_unregister_driver - unregister a PCMCIA driver with the bus core
 */
void pcmcia_unregister_driver(struct pcmcia_driver *driver)
{
	driver_unregister(&driver->drv);
}
EXPORT_SYMBOL(pcmcia_unregister_driver);

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *proc_pccard = NULL;

static int proc_read_drivers_callback(struct device_driver *driver, void *d)
{
	char **p = d;
	struct pcmcia_driver *p_dev = container_of(driver, 
						   struct pcmcia_driver, drv);

	*p += sprintf(*p, "%-24.24s 1 %d\n", driver->name, p_dev->use_count);
	d = (void *) p;

	return 0;
}

static int proc_read_drivers(char *buf, char **start, off_t pos,
			     int count, int *eof, void *data)
{
	char *p = buf;

	bus_for_each_drv(&pcmcia_bus_type, NULL, 
			 (void *) &p, proc_read_drivers_callback);

	return (p - buf);
}
#endif

/*======================================================================

    These manage a ring buffer of events pending for one user process
    
======================================================================*/

static int queue_empty(user_info_t *user)
{
    return (user->event_head == user->event_tail);
}

static event_t get_queued_event(user_info_t *user)
{
    user->event_tail = (user->event_tail+1) % MAX_EVENTS;
    return user->event[user->event_tail];
}

static void queue_event(user_info_t *user, event_t event)
{
    user->event_head = (user->event_head+1) % MAX_EVENTS;
    if (user->event_head == user->event_tail)
	user->event_tail = (user->event_tail+1) % MAX_EVENTS;
    user->event[user->event_head] = event;
}

static void handle_event(struct pcmcia_bus_socket *s, event_t event)
{
    user_info_t *user;
    for (user = s->user; user; user = user->next)
	queue_event(user, event);
    wake_up_interruptible(&s->queue);
}

static int handle_request(struct pcmcia_bus_socket *s, event_t event)
{
    if (s->req_pending != 0)
	return CS_IN_USE;
    if (s->state & SOCKET_BUSY)
	s->req_pending = 1;
    handle_event(s, event);
    if (s->req_pending > 0) {
	interruptible_sleep_on(&s->request);
	if (signal_pending(current))
	    return CS_IN_USE;
	else
	    return s->req_result;
    }
    return CS_SUCCESS;
}

static void handle_removal(void *data)
{
    struct pcmcia_bus_socket *s = data;
    handle_event(s, CS_EVENT_CARD_REMOVAL);
    s->state &= ~SOCKET_REMOVAL_PENDING;
}

/*======================================================================

    The card status event handler.
    
======================================================================*/

static int ds_event(event_t event, int priority,
		    event_callback_args_t *args)
{
    struct pcmcia_bus_socket *s;

    DEBUG(1, "ds: ds_event(0x%06x, %d, 0x%p)\n",
	  event, priority, args->client_handle);
    s = args->client_data;
    
    switch (event) {
	
    case CS_EVENT_CARD_REMOVAL:
	s->state &= ~SOCKET_PRESENT;
	if (!(s->state & SOCKET_REMOVAL_PENDING)) {
		s->state |= SOCKET_REMOVAL_PENDING;
		schedule_delayed_work(&s->removal,  HZ/10);
	}
	break;
	
    case CS_EVENT_CARD_INSERTION:
	s->state |= SOCKET_PRESENT;
	handle_event(s, event);
	break;

    case CS_EVENT_EJECTION_REQUEST:
	return handle_request(s, event);
	break;
	
    default:
	handle_event(s, event);
	break;
    }

    return 0;
} /* ds_event */

/*======================================================================

    bind_mtd() connects a memory region with an MTD client.
    
======================================================================*/

static int bind_mtd(struct pcmcia_bus_socket *bus_sock, mtd_info_t *mtd_info)
{
    mtd_bind_t bind_req;
    int ret;

    bind_req.dev_info = &mtd_info->dev_info;
    bind_req.Attributes = mtd_info->Attributes;
    bind_req.Socket = bus_sock->parent;
    bind_req.CardOffset = mtd_info->CardOffset;
    ret = pcmcia_bind_mtd(&bind_req);
    if (ret != CS_SUCCESS) {
	cs_error(NULL, BindMTD, ret);
	printk(KERN_NOTICE "ds: unable to bind MTD '%s' to socket %d"
	       " offset 0x%x\n",
	       (char *)bind_req.dev_info, bus_sock->parent->sock, bind_req.CardOffset);
	return -ENODEV;
    }
    return 0;
} /* bind_mtd */

/*======================================================================

    bind_request() connects a socket to a particular client driver.
    It looks up the specified device ID in the list of registered
    drivers, binds it to the socket, and tries to create an instance
    of the device.  unbind_request() deletes a driver instance.
    
======================================================================*/

static int bind_request(struct pcmcia_bus_socket *s, bind_info_t *bind_info)
{
    struct pcmcia_driver *driver;
    socket_bind_t *b;
    bind_req_t bind_req;
    int ret;

    if (!s)
	    return -EINVAL;

    DEBUG(2, "bind_request(%d, '%s')\n", s->parent->sock,
	  (char *)bind_info->dev_info);
    driver = get_pcmcia_driver(&bind_info->dev_info);
    if (!driver)
	    return -EINVAL;

    for (b = s->bind; b; b = b->next)
	if ((driver == b->driver) &&
	    (bind_info->function == b->function))
	    break;
    if (b != NULL) {
	bind_info->instance = b->instance;
	return -EBUSY;
    }

    if (!try_module_get(driver->owner))
	    return -EINVAL;

    bind_req.Socket = s->parent;
    bind_req.Function = bind_info->function;
    bind_req.dev_info = (dev_info_t *) driver->drv.name;
    ret = pcmcia_bind_device(&bind_req);
    if (ret != CS_SUCCESS) {
	cs_error(NULL, BindDevice, ret);
	printk(KERN_NOTICE "ds: unable to bind '%s' to socket %d\n",
	       (char *)dev_info, s->parent->sock);
	module_put(driver->owner);
	return -ENODEV;
    }

    /* Add binding to list for this socket */
    driver->use_count++;
    b = kmalloc(sizeof(socket_bind_t), GFP_KERNEL);
    if (!b) 
    {
    	driver->use_count--;
	module_put(driver->owner);
	return -ENOMEM;    
    }
    b->driver = driver;
    b->function = bind_info->function;
    b->instance = NULL;
    b->next = s->bind;
    s->bind = b;
    
    if (driver->attach) {
	b->instance = driver->attach();
	if (b->instance == NULL) {
	    printk(KERN_NOTICE "ds: unable to create instance "
		   "of '%s'!\n", (char *)bind_info->dev_info);
	    module_put(driver->owner);
	    return -ENODEV;
	}
    }
    
    return 0;
} /* bind_request */

/*====================================================================*/

static int get_device_info(struct pcmcia_bus_socket *s, bind_info_t *bind_info, int first)
{
    socket_bind_t *b;
    dev_node_t *node;

#ifdef CONFIG_CARDBUS
    /*
     * Some unbelievably ugly code to associate the PCI cardbus
     * device and its driver with the PCMCIA "bind" information.
     */
    {
	struct pci_bus *bus;

	bus = pcmcia_lookup_bus(s->handle);
	if (bus) {
	    	struct list_head *list;
		struct pci_dev *dev = NULL;
		
		list = bus->devices.next;
		while (list != &bus->devices) {
			struct pci_dev *pdev = pci_dev_b(list);
			list = list->next;

			if (first) {
				dev = pdev;
				break;
			}

			/* Try to handle "next" here some way? */
		}
		if (dev && dev->driver) {
			strlcpy(bind_info->name, dev->driver->name, DEV_NAME_LEN);
			bind_info->major = 0;
			bind_info->minor = 0;
			bind_info->next = NULL;
			return 0;
		}
	}
    }
#endif

    for (b = s->bind; b; b = b->next)
	if ((strcmp((char *)b->driver->drv.name,
		    (char *)bind_info->dev_info) == 0) &&
	    (b->function == bind_info->function))
	    break;
    if (b == NULL) return -ENODEV;
    if ((b->instance == NULL) ||
	(b->instance->state & DEV_CONFIG_PENDING))
	return -EAGAIN;
    if (first)
	node = b->instance->dev;
    else
	for (node = b->instance->dev; node; node = node->next)
	    if (node == bind_info->next) break;
    if (node == NULL) return -ENODEV;

    strlcpy(bind_info->name, node->dev_name, DEV_NAME_LEN);
    bind_info->major = node->major;
    bind_info->minor = node->minor;
    bind_info->next = node->next;
    
    return 0;
} /* get_device_info */

/*====================================================================*/

static int unbind_request(struct pcmcia_bus_socket *s, bind_info_t *bind_info)
{
    socket_bind_t **b, *c;

    DEBUG(2, "unbind_request(%d, '%s')\n", s->parent->sock,
	  (char *)bind_info->dev_info);
    for (b = &s->bind; *b; b = &(*b)->next)
	if ((strcmp((char *)(*b)->driver->drv.name,
		    (char *)bind_info->dev_info) == 0) &&
	    ((*b)->function == bind_info->function))
	    break;
    if (*b == NULL)
	return -ENODEV;
    
    c = *b;
    c->driver->use_count--;
    if (c->driver->detach) {
	if (c->instance)
	    c->driver->detach(c->instance);
    }
    module_put(c->driver->owner);
    *b = c->next;
    kfree(c);
    return 0;
} /* unbind_request */

/*======================================================================

    The user-mode PC Card device interface

======================================================================*/

static int ds_open(struct inode *inode, struct file *file)
{
    socket_t i = iminor(inode);
    struct pcmcia_bus_socket *s;
    user_info_t *user;

    DEBUG(0, "ds_open(socket %d)\n", i);

    s = get_socket_info_by_nr(i);
    if (!s)
	    return -ENODEV;

    if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
	if (s->state & SOCKET_BUSY)
	    return -EBUSY;
	else
	    s->state |= SOCKET_BUSY;
    }
    
    user = kmalloc(sizeof(user_info_t), GFP_KERNEL);
    if (!user) return -ENOMEM;
    user->event_tail = user->event_head = 0;
    user->next = s->user;
    user->user_magic = USER_MAGIC;
    s->user = user;
    file->private_data = user;
    
    if (s->state & SOCKET_PRESENT)
	queue_event(user, CS_EVENT_CARD_INSERTION);
    return 0;
} /* ds_open */

/*====================================================================*/

static int ds_release(struct inode *inode, struct file *file)
{
    socket_t i = iminor(inode);
    struct pcmcia_bus_socket *s;
    user_info_t *user, **link;

    DEBUG(0, "ds_release(socket %d)\n", i);

    s = get_socket_info_by_nr(i);
    if (!s)
	    return 0;

    user = file->private_data;
    if (CHECK_USER(user))
	goto out;

    /* Unlink user data structure */
    if ((file->f_flags & O_ACCMODE) != O_RDONLY)
	s->state &= ~SOCKET_BUSY;
    file->private_data = NULL;
    for (link = &s->user; *link; link = &(*link)->next)
	if (*link == user) break;
    if (link == NULL)
	goto out;
    *link = user->next;
    user->user_magic = 0;
    kfree(user);
out:
    return 0;
} /* ds_release */

/*====================================================================*/

static ssize_t ds_read(struct file *file, char *buf,
		       size_t count, loff_t *ppos)
{
    socket_t i = iminor(file->f_dentry->d_inode);
    struct pcmcia_bus_socket *s;
    user_info_t *user;

    DEBUG(2, "ds_read(socket %d)\n", i);
    
    if (count < 4)
	return -EINVAL;

    s = get_socket_info_by_nr(i);
    if (!s)
	    return -ENODEV;

    user = file->private_data;
    if (CHECK_USER(user))
	return -EIO;
    
    if (queue_empty(user)) {
	interruptible_sleep_on(&s->queue);
	if (signal_pending(current))
	    return -EINTR;
    }

    return put_user(get_queued_event(user), (int *)buf) ? -EFAULT : 4;
} /* ds_read */

/*====================================================================*/

static ssize_t ds_write(struct file *file, const char *buf,
			size_t count, loff_t *ppos)
{
    socket_t i = iminor(file->f_dentry->d_inode);
    struct pcmcia_bus_socket *s;
    user_info_t *user;

    DEBUG(2, "ds_write(socket %d)\n", i);
    
    if (count != 4)
	return -EINVAL;
    if ((file->f_flags & O_ACCMODE) == O_RDONLY)
	return -EBADF;

    s = get_socket_info_by_nr(i);
    if (!s)
	    return -ENODEV;

    user = file->private_data;
    if (CHECK_USER(user))
	return -EIO;

    if (s->req_pending) {
	s->req_pending--;
	get_user(s->req_result, (int *)buf);
	if ((s->req_result != 0) || (s->req_pending == 0))
	    wake_up_interruptible(&s->request);
    } else
	return -EIO;

    return 4;
} /* ds_write */

/*====================================================================*/

/* No kernel lock - fine */
static u_int ds_poll(struct file *file, poll_table *wait)
{
    socket_t i = iminor(file->f_dentry->d_inode);
    struct pcmcia_bus_socket *s;
    user_info_t *user;

    DEBUG(2, "ds_poll(socket %d)\n", i);
    
    s = get_socket_info_by_nr(i);
    if (!s)
	    return POLLERR;

    user = file->private_data;
    if (CHECK_USER(user))
	return POLLERR;
    poll_wait(file, &s->queue, wait);
    if (!queue_empty(user))
	return POLLIN | POLLRDNORM;
    return 0;
} /* ds_poll */

/*====================================================================*/

static int ds_ioctl(struct inode * inode, struct file * file,
		    u_int cmd, u_long arg)
{
    socket_t i = iminor(inode);
    struct pcmcia_bus_socket *s;
    u_int size;
    int ret, err;
    ds_ioctl_arg_t buf;

    DEBUG(2, "ds_ioctl(socket %d, %#x, %#lx)\n", i, cmd, arg);
    
    s = get_socket_info_by_nr(i);
    if (!s)
	    return -ENODEV;
    
    size = (cmd & IOCSIZE_MASK) >> IOCSIZE_SHIFT;
    if (size > sizeof(ds_ioctl_arg_t)) return -EINVAL;

    /* Permission check */
    if (!(cmd & IOC_OUT) && !capable(CAP_SYS_ADMIN))
	return -EPERM;
	
    if (cmd & IOC_IN) {
	err = verify_area(VERIFY_READ, (char *)arg, size);
	if (err) {
	    DEBUG(3, "ds_ioctl(): verify_read = %d\n", err);
	    return err;
	}
    }
    if (cmd & IOC_OUT) {
	err = verify_area(VERIFY_WRITE, (char *)arg, size);
	if (err) {
	    DEBUG(3, "ds_ioctl(): verify_write = %d\n", err);
	    return err;
	}
    }
    
    err = ret = 0;
    
    if (cmd & IOC_IN) __copy_from_user((char *)&buf, (char *)arg, size);
    
    switch (cmd) {
    case DS_ADJUST_RESOURCE_INFO:
	ret = pcmcia_adjust_resource_info(s->handle, &buf.adjust);
	break;
    case DS_GET_CARD_SERVICES_INFO:
	ret = pcmcia_get_card_services_info(&buf.servinfo);
	break;
    case DS_GET_CONFIGURATION_INFO:
	ret = pcmcia_get_configuration_info(s->handle, &buf.config);
	break;
    case DS_GET_FIRST_TUPLE:
	ret = pcmcia_get_first_tuple(s->handle, &buf.tuple);
	break;
    case DS_GET_NEXT_TUPLE:
	ret = pcmcia_get_next_tuple(s->handle, &buf.tuple);
	break;
    case DS_GET_TUPLE_DATA:
	buf.tuple.TupleData = buf.tuple_parse.data;
	buf.tuple.TupleDataMax = sizeof(buf.tuple_parse.data);
	ret = pcmcia_get_tuple_data(s->handle, &buf.tuple);
	break;
    case DS_PARSE_TUPLE:
	buf.tuple.TupleData = buf.tuple_parse.data;
	ret = pcmcia_parse_tuple(s->handle, &buf.tuple, &buf.tuple_parse.parse);
	break;
    case DS_RESET_CARD:
	ret = pcmcia_reset_card(s->handle, NULL);
	break;
    case DS_GET_STATUS:
	ret = pcmcia_get_status(s->handle, &buf.status);
	break;
    case DS_VALIDATE_CIS:
	ret = pcmcia_validate_cis(s->handle, &buf.cisinfo);
	break;
    case DS_SUSPEND_CARD:
	ret = pcmcia_suspend_card(s->handle, NULL);
	break;
    case DS_RESUME_CARD:
	ret = pcmcia_resume_card(s->handle, NULL);
	break;
    case DS_EJECT_CARD:
	ret = pcmcia_eject_card(s->handle, NULL);
	break;
    case DS_INSERT_CARD:
	ret = pcmcia_insert_card(s->handle, NULL);
	break;
    case DS_ACCESS_CONFIGURATION_REGISTER:
	if ((buf.conf_reg.Action == CS_WRITE) && !capable(CAP_SYS_ADMIN))
	    return -EPERM;
	ret = pcmcia_access_configuration_register(s->handle, &buf.conf_reg);
	break;
    case DS_GET_FIRST_REGION:
        ret = pcmcia_get_first_region(s->handle, &buf.region);
	break;
    case DS_GET_NEXT_REGION:
	ret = pcmcia_get_next_region(s->handle, &buf.region);
	break;
    case DS_GET_FIRST_WINDOW:
	buf.win_info.handle = (window_handle_t)s->handle;
	ret = pcmcia_get_first_window(&buf.win_info.handle, &buf.win_info.window);
	break;
    case DS_GET_NEXT_WINDOW:
	ret = pcmcia_get_next_window(&buf.win_info.handle, &buf.win_info.window);
	break;
    case DS_GET_MEM_PAGE:
	ret = pcmcia_get_mem_page(buf.win_info.handle,
			   &buf.win_info.map);
	break;
    case DS_REPLACE_CIS:
	ret = pcmcia_replace_cis(s->handle, &buf.cisdump);
	break;
    case DS_BIND_REQUEST:
	if (!capable(CAP_SYS_ADMIN)) return -EPERM;
	err = bind_request(s, &buf.bind_info);
	break;
    case DS_GET_DEVICE_INFO:
	err = get_device_info(s, &buf.bind_info, 1);
	break;
    case DS_GET_NEXT_DEVICE:
	err = get_device_info(s, &buf.bind_info, 0);
	break;
    case DS_UNBIND_REQUEST:
	err = unbind_request(s, &buf.bind_info);
	break;
    case DS_BIND_MTD:
	if (!capable(CAP_SYS_ADMIN)) return -EPERM;
	err = bind_mtd(s, &buf.mtd_info);
	break;
    default:
	err = -EINVAL;
    }
    
    if ((err == 0) && (ret != CS_SUCCESS)) {
	DEBUG(2, "ds_ioctl: ret = %d\n", ret);
	switch (ret) {
	case CS_BAD_SOCKET: case CS_NO_CARD:
	    err = -ENODEV; break;
	case CS_BAD_ARGS: case CS_BAD_ATTRIBUTE: case CS_BAD_IRQ:
	case CS_BAD_TUPLE:
	    err = -EINVAL; break;
	case CS_IN_USE:
	    err = -EBUSY; break;
	case CS_OUT_OF_RESOURCE:
	    err = -ENOSPC; break;
	case CS_NO_MORE_ITEMS:
	    err = -ENODATA; break;
	case CS_UNSUPPORTED_FUNCTION:
	    err = -ENOSYS; break;
	default:
	    err = -EIO; break;
	}
    }

    if (cmd & IOC_OUT) __copy_to_user((char *)arg, (char *)&buf, size);

    return err;
} /* ds_ioctl */

/*====================================================================*/

static struct file_operations ds_fops = {
	.owner		= THIS_MODULE,
	.open		= ds_open,
	.release	= ds_release,
	.ioctl		= ds_ioctl,
	.read		= ds_read,
	.write		= ds_write,
	.poll		= ds_poll,
};

static int __devinit pcmcia_bus_add_socket(struct class_device *class_dev)
{
	struct pcmcia_socket *socket = class_dev->class_data;
	client_reg_t client_reg;
	bind_req_t bind;
	struct pcmcia_bus_socket *s;
	int ret;

	s = kmalloc(sizeof(struct pcmcia_bus_socket), GFP_KERNEL);
	if(!s)
		return -ENOMEM;
	memset(s, 0, sizeof(struct pcmcia_bus_socket));
    
	/*
	 * Ugly. But we want to wait for the socket threads to have started up.
	 * We really should let the drivers themselves drive some of this..
	 */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(HZ/4);

	init_waitqueue_head(&s->queue);
	init_waitqueue_head(&s->request);

	/* initialize data */
	s->socket_dev = socket->dev.dev;
	INIT_WORK(&s->removal, handle_removal, s);
	s->parent = socket;

	/* Set up hotline to Card Services */
	client_reg.dev_info = bind.dev_info = &dev_info;

	bind.Socket = socket;
	bind.Function = BIND_FN_ALL;
	ret = pcmcia_bind_device(&bind);
	if (ret != CS_SUCCESS) {
		cs_error(NULL, BindDevice, ret);
		kfree(s);
		return -EINVAL;
	}

	client_reg.Attributes = INFO_MASTER_CLIENT;
	client_reg.EventMask =
		CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL |
		CS_EVENT_RESET_PHYSICAL | CS_EVENT_CARD_RESET |
		CS_EVENT_EJECTION_REQUEST | CS_EVENT_INSERTION_REQUEST |
		CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
	client_reg.event_handler = &ds_event;
	client_reg.Version = 0x0210;
	client_reg.event_callback_args.client_data = s;
	ret = pcmcia_register_client(&s->handle, &client_reg);
	if (ret != CS_SUCCESS) {
		cs_error(NULL, RegisterClient, ret);
		kfree(s);
		return -EINVAL;
	}

	socket->pcmcia = s;

	return 0;
}


static void pcmcia_bus_remove_socket(struct class_device *class_dev)
{
	struct pcmcia_socket *socket = class_dev->class_data;

	if (!socket || !socket->pcmcia)
		return;

	flush_scheduled_work();

	pcmcia_deregister_client(socket->pcmcia->handle);

	kfree(socket->pcmcia);
	socket->pcmcia = NULL;

	return;
}


/* the pcmcia_bus_interface is used to handle pcmcia socket devices */
static struct class_interface pcmcia_bus_interface = {
	.class = &pcmcia_socket_class,
	.add = &pcmcia_bus_add_socket,
	.remove = &pcmcia_bus_remove_socket,
};


struct bus_type pcmcia_bus_type = {
	.name = "pcmcia",
};
EXPORT_SYMBOL(pcmcia_bus_type);


static int __init init_pcmcia_bus(void)
{
	int i;

	bus_register(&pcmcia_bus_type);
	class_interface_register(&pcmcia_bus_interface);

	/* Set up character device for user mode clients */
	i = register_chrdev(0, "pcmcia", &ds_fops);
	if (i == -EBUSY)
		printk(KERN_NOTICE "unable to find a free device # for "
		       "Driver Services\n");
	else
		major_dev = i;

#ifdef CONFIG_PROC_FS
	proc_pccard = proc_mkdir("pccard", proc_bus);
	if (proc_pccard)
		create_proc_read_entry("drivers",0,proc_pccard,proc_read_drivers,NULL);
#endif

	return 0;
}
fs_initcall(init_pcmcia_bus); /* one level after subsys_initcall so that 
			       * pcmcia_socket_class is already registered */


static void __exit exit_pcmcia_bus(void)
{
	class_interface_unregister(&pcmcia_bus_interface);

#ifdef CONFIG_PROC_FS
	if (proc_pccard) {
		remove_proc_entry("drivers", proc_pccard);
		remove_proc_entry("pccard", proc_bus);
	}
#endif
	if (major_dev != -1)
		unregister_chrdev(major_dev, "pcmcia");

	bus_unregister(&pcmcia_bus_type);
}
module_exit(exit_pcmcia_bus);



/* helpers for backwards-compatible functions */

static struct pcmcia_bus_socket * get_socket_info_by_nr(unsigned int nr)
{
	struct pcmcia_socket * s = pcmcia_get_socket_by_nr(nr);
	if (s && s->pcmcia)
		return s->pcmcia;
	else
		return NULL;
}

/* backwards-compatible accessing of driver --- by name! */

struct cmp_data {
	void *dev_info;
	struct pcmcia_driver *drv;
};

static int cmp_drv_callback(struct device_driver *drv, void *data)
{
	struct cmp_data *cmp = data;
	if (strncmp((char *)cmp->dev_info, (char *)drv->name,
		    DEV_NAME_LEN) == 0) {
		cmp->drv = container_of(drv, struct pcmcia_driver, drv);
		return -EINVAL;
	}
	return 0;
}

static struct pcmcia_driver * get_pcmcia_driver (dev_info_t *dev_info)
{
	int ret;
	struct cmp_data cmp = {
		.dev_info = dev_info,
	};
	
	ret = bus_for_each_drv(&pcmcia_bus_type, NULL, &cmp, cmp_drv_callback);
	if (ret)
		return cmp.drv;
	return NULL;
}
