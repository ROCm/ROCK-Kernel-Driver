/*
 * IBM eServer Hypervisor Virtual Console Server Device Driver
 * Copyright (C) 2003, 2004 IBM Corp.
 *  Ryan S. Arnold (rsa@us.ibm.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Author(s) :  Ryan S. Arnold <rsa@us.ibm.com>
 *
 * This is the device driver for the IBM Hypervisor Virtual Console Server,
 * "hvcs".  The IBM hvcs provides a tty driver interface to allow Linux
 * user space applications access to the system consoles of logically
 * partitioned operating systems, e.g. Linux, running on the same partitioned
 * Power5 ppc64 system.  Physical hardware consoles per partition are not
 * practical on this hardware so system consoles are accessed by this driver
 * using inter-partition firmware interfaces to virtual terminal devices.
 *
 * For direction on installation and usage of this driver please reference
 * Documentation/powerpc/hvcs.txt.
 *
 * For an architectural overview of this driver please reference
 * Documentation/powerpc/hvcs_arch.txt
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/major.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <linux/stat.h>
#include <linux/kobject.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/device.h>
#include <asm/vio.h>
#include <linux/list.h>
#include <asm/hvconsole.h>

MODULE_AUTHOR("Ryan S. Arnold <rsa@us.ibm.com>");
MODULE_DESCRIPTION("IBM hvcs (Hypervisor Virtual Console Server) Driver");
MODULE_LICENSE("GPL");

/* Since the Linux TTY code does not currently (2-04-2004) support
 * dynamic addition of tty derived devices and we shouldn't
 * allocate thousands of tty_device pointers when the number of
 * vty-server & vty partner connections will most often be much
 * lower than this, we'll arbitrarily allocate HVCS_DEFAULT_SERVER_ADAPTERS
 * tty_structs and cdev's by default when we register the tty_driver.
 * This can be overridden using an insmod parameter.
 */
#define HVCS_DEFAULT_SERVER_ADAPTERS	64

/* The user can't insmod with more than HVCS_MAX_SERVER_ADAPTERS
 * hvcs device nodes as a sanity check.  Theoretically there can be
 * over 1 Billion vty-server & vty partner connections.
 */
#define HVCS_MAX_SERVER_ADAPTERS	1024

/* We let Linux assign us a major number and we start the minors at zero.
 * There is no intuitive mapping between minor number and the target
 * partition.  The mapping of minor number is related to the order the
 * vty-servers are exposed to this driver via the hvcs_probe function.
 */
#define HVCS_MINOR_START	0

#define __ALIGNED__	__attribute__((__aligned__(8)))

/* Converged location code string length + 1 null terminator */
#define CLC_LENGTH		80

/* How much data can firmware send with each hvterm_put_chars()?
 * Maybe this should be moved into an architecture specific area. */
#define HVCS_BUFF_LEN	16

/* This is the maximum amount of data we'll let the user send us
 * (hvcs_write) at once in a chunk as a sanity check. */
#define HVCS_MAX_FROM_USER	4096

/* Be careful when adding flags to this line discipline.  Don't add
 * anything that will cause echoing or we'll go into recursive loop
 * echoing chars back and forth with the console drivers. */
static struct termios hvcs_tty_termios = {
	.c_iflag = IGNBRK | IGNPAR,
	.c_oflag = OPOST,
	.c_cflag = B38400 | CS8 | CREAD | HUPCL,
	.c_cc = INIT_C_CC
};

/* This value is used to take the place of a command line parameter
 * when the module is inserted.  It starts as -1 and stays as such if
 * the user doesn't specify a module insmod parameter.  If they DO
 * specify one then it is set to the value of the integer passed in.
 */
static int hvcs_parm_num_devs = -1;
module_param(hvcs_parm_num_devs, int, 0);

static char hvcs_driver_name[] = "hvcs";
static const char hvcs_device_node[] = "hvcs";
static const char hvcs_driver_string[]
	= "IBM hvcs (Hypervisor Virtual Console Server) Driver";

/* Status of partner info rescan triggered via sysfs. */
static int hvcs_rescan_status = 0;

static struct tty_driver *hvcs_tty_driver;

/* This is used to associate a vty-server, as it is exposed to
 * this driver, with a preallocated tty_struct.index.  The dev node
 * and hvcs index numbers are not re-used after device removal
 * otherwise removing and adding a new one would link a /dev/hvcs*
 * entry to a different vty-server than it did before the removal.
 * This means that a newly exposed vty-server will always map to
 * an incrementally higher /dev/hvcs* entry than last exposed
 * vty-server.
 */
static int hvcs_struct_count = -1;

/* One vty-server per hvcs_struct */
struct hvcs_struct {
	struct list_head next; /* list management */
	struct vio_dev *vdev;
	struct tty_struct *tty;
	unsigned int open_count;
	/* this index identifies this hvcs device as the
	 * complement to a tty index. */
	unsigned int index;
	unsigned int p_unit_address; /* partner unit address */
	unsigned int p_partition_ID; /* partner partition ID */
	char p_location_code[CLC_LENGTH];
	char name[32];
	int enabled; /* there are tty's open against this device */
	struct kobject kobj; /* ref count & hvcs_struct lifetime */
	struct work_struct read_work;
};

/* Required to back map a kobject to its containing object */
#define from_kobj(kobj) container_of(kobj, struct hvcs_struct, kobj)

static struct list_head hvcs_structs = LIST_HEAD_INIT(hvcs_structs);

static void hvcs_read_task(void *data);
static void hvcs_unthrottle(struct tty_struct *tty);
static void hvcs_throttle(struct tty_struct *tty);
static irqreturn_t hvcs_handle_interrupt(int irq, void *dev_instance, struct pt_regs *regs);

static int hvcs_write(struct tty_struct *tty, int from_user, const unsigned char *buf, int count);
static int hvcs_write_room(struct tty_struct *tty);
static int hvcs_chars_in_buffer(struct tty_struct *tty);

static int hvcs_has_pi(struct hvcs_struct *hvcsd);
static void hvcs_set_pi(struct hvcs_partner_info *pi, struct hvcs_struct *hvcsd);
static int hvcs_get_pi(struct hvcs_struct *hvcsd);
static int hvcs_rescan_devices_list(void);

static int hvcs_partner_connect(struct hvcs_struct *hvcsd);
static void hvcs_partner_free(struct hvcs_struct *hvcsd);

static int hvcs_enable_device(struct hvcs_struct *hvcsd);
static void hvcs_disable_device(struct hvcs_struct *hvcsd);

static void destroy_hvcs_struct(struct kobject *kobj);
static int hvcs_open(struct tty_struct *tty, struct file *filp);
static void hvcs_close(struct tty_struct *tty, struct file *filp);
static void hvcs_hangup(struct tty_struct * tty);

static void hvcs_create_device_attrs(struct hvcs_struct *hvcsd);
static void hvcs_remove_device_attrs(struct hvcs_struct *hvcsd);
static void hvcs_create_driver_attrs(void);
static void hvcs_remove_driver_attrs(void);

static int __devinit hvcs_probe(struct vio_dev *dev, const struct vio_device_id *id);
static int __devexit hvcs_remove(struct vio_dev *dev);
static int __init hvcs_module_init(void);
static void __exit hvcs_module_exit(void);

/* This task is scheduled to execute out of the read data interrupt
 * handler, the hvcs_unthrottle, and it can be rescheduled out of itself.
 * This task reschedules itself because every flip_buffer_push may cause a
 * throttle and we want to be able to reschedule ourselves to run AFTER a
 * push is scheduled so that we know when the tty is properly throttled.
 */
static void hvcs_read_task(void *data)
{
	struct hvcs_struct *hvcsd = (struct hvcs_struct *)data;
	unsigned int unit_address = hvcsd->vdev->unit_address;
	struct tty_struct *tty = hvcsd->tty;
	char buf[HVCS_BUFF_LEN] __ALIGNED__;
	int got;
	int i;

	/* Check the tty since it may go away on us at any time. */
	if (hvcsd->enabled && tty && !test_bit(TTY_THROTTLED, &tty->flags)) {
		if ((tty->flip.count + HVCS_BUFF_LEN) < TTY_FLIPBUF_SIZE) {
			got = hvterm_get_chars(unit_address,
					&buf[0],
					HVCS_BUFF_LEN);
			if (!got) {
				if (tty->flip.count)
					tty_flip_buffer_push(tty);
				vio_enable_interrupts(hvcsd->vdev);
				return;
			}
			for (i=0;i<got;i++)
				tty_insert_flip_char(tty, buf[i], TTY_NORMAL);
		}
		if (tty->flip.count)
			tty_flip_buffer_push(tty);
		/* We reschedule this task after every hvterm_get_chars
		 * because we don't want the TTY to throttle on us between
		 * flip_buffer_push calls. */
		schedule_delayed_work(&hvcsd->read_work, 1);
	}
	/* If control drops straight here then it means that we are throttled
	 * and this task will be rescheduled when the TTY is unthrottled. */
	return;
}

/* This is the callback from the tty layer that tells us that the flip
 * buffer has more space.
 */
static void hvcs_unthrottle(struct tty_struct *tty)
{
	struct hvcs_struct *hvcsd = (struct hvcs_struct *)tty->driver_data;

	schedule_delayed_work(&hvcsd->read_work, 1);

	/* Don't enable interrupts here, that will be done in the read task */
}

static void hvcs_throttle(struct tty_struct *tty)
{
	struct hvcs_struct *hvcsd = (struct hvcs_struct *)tty->driver_data;

	vio_disable_interrupts(hvcsd->vdev);

	cancel_delayed_work(&hvcsd->read_work);
}

/* If the device is being removed we don't have to worry about this
 * interrupt handler taking any further interrupts because they are
 * disabled which means the hvcs_struct will always be valid in this
 * handler.
 */
static irqreturn_t hvcs_handle_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct hvcs_struct *hvcsd = (struct hvcs_struct *)dev_instance;

	vio_disable_interrupts(hvcsd->vdev);
	schedule_work(&hvcsd->read_work);

	return IRQ_HANDLED;
}

static struct vio_device_id hvcs_driver_table[] __devinitdata= {
	{"serial-server", "hvterm2"},
	{ 0,}
};
MODULE_DEVICE_TABLE(vio, hvcs_driver_table);

/* callback when the kboject ref count reaches zero */
static void destroy_hvcs_struct(struct kobject *kobj)
{
	struct hvcs_struct *hvcsd = from_kobj(kobj);

	list_del(&(hvcsd->next));

	kfree(hvcsd);
}

static struct kobj_type hvcs_kobj_type = {
	.release = destroy_hvcs_struct,
};

static int __devinit hvcs_probe(
	struct vio_dev *dev,
	const struct vio_device_id *id)
{
	struct hvcs_struct *hvcsd;

	if (!dev || !id) {
		printk(KERN_ERR "HVCS: driver probed with invalid parm.\n");
		return -EPERM;
	}

	printk(KERN_INFO "HVCS: Added vty-server@%X.\n", dev->unit_address);

	hvcsd = kmalloc(sizeof(*hvcsd), GFP_KERNEL);
	if (!hvcsd) {
		return -ENODEV;
	}

	/* hvcsd->tty is zeroed out with the memset */
	memset(hvcsd, 0x00, sizeof(*hvcsd));

	/* Automatically incs the refcount the first time */
	kobject_init(&hvcsd->kobj);
	/* Set up the callback for terminating the hvcs_struct's life */
	hvcsd->kobj.ktype = &hvcs_kobj_type;

	hvcsd->vdev = dev;
	dev->driver_data = hvcsd;
	sprintf(hvcsd->name,"%X",dev->unit_address);

	hvcsd->index = ++hvcs_struct_count;

	INIT_WORK(&hvcsd->read_work, hvcs_read_task, hvcsd);

	hvcsd->enabled = 0;

	/* This will populate the hvcs_struct's partner info fields
	 * for the first time. */
	if(hvcs_get_pi(hvcsd)) {
		printk(KERN_ERR "HVCS: Failed to fetch partner"
			" info for vty-server@%X.\n",
			hvcsd->vdev->unit_address);
	}

	/* If a user app opens a tty that corresponds to this vty-server
	 * before the hvcs_struct has been added to the devices
	 * list then the user app will get -ENODEV.
	 */

	list_add_tail(&(hvcsd->next), &hvcs_structs);

	hvcs_create_device_attrs(hvcsd);

	/* DON'T enable interrupts here because there is no user to receive
	 * the data. */
	return 0;
}

static int __devexit hvcs_remove(struct vio_dev *dev)
{
	struct hvcs_struct *hvcsd = (struct hvcs_struct *)dev->driver_data;

	if (!hvcsd)
		return -ENODEV;

	printk(KERN_INFO "HVCS: Removing vty-server@%X.\n", dev->unit_address);

	/* By this time the vty-server won't be getting any
	 * more interrups but we might get a callback from the tty. */
	cancel_delayed_work(&hvcsd->read_work);
	flush_scheduled_work();

	/* If hvcs_remove is called after a tty_hangup has been issued
	 * (indicating that there are no connections) or before a user
	 * has attempted to open the device then the device will not be
	 * enabled and thus we don't need to do any cleanup.  */
	if (hvcsd->enabled) {
		hvcs_disable_device(hvcsd);
	}

	if (hvcsd->tty) {
		/* This is a scheduled function which will
		 * auto chain call hvcs_hangup. */
		tty_hangup(hvcsd->tty);
	}

	hvcs_remove_device_attrs(hvcsd);

	/* Let the last holder of this object cause it to be removed,
	 * which would probably be tty_hangup.
	 */
	kobject_put (&hvcsd->kobj);
	return 0;
};

static struct vio_driver hvcs_vio_driver = {
	.name		= hvcs_driver_name,
	.id_table	= hvcs_driver_table,
	.probe		= hvcs_probe,
	.remove		= __devexit_p(hvcs_remove),
};

/* Only called from hvcs_get_pi please */
static void hvcs_set_pi(struct hvcs_partner_info *pi, struct hvcs_struct *hvcsd)
{
	int clclength;

	hvcsd->p_unit_address = pi->unit_address;
	hvcsd->p_partition_ID  = pi->partition_ID;
	clclength = strlen(&pi->location_code[0]);
	if(clclength > CLC_LENGTH - 1) {
		clclength = CLC_LENGTH - 1;
	}
	/* copy the null-term char too */
	strncpy(&hvcsd->p_location_code[0],
			&pi->location_code[0], clclength + 1);
}

/* Traverse the list and add the partner info that is found to the
 * hvcs_struct struct entry. NOTE: At this time I know that partner
 * info will return a single entry but in the future there may be
 * multiple partner info entries per vty-server and you'll
 * want to zero out that list and reset it.  If for some reason you
 * have an old version of this driver but there IS more than one
 * partner info then hvcsd->p_* will hold the last partner info
 * data from the firmware query.  A good way to update this code would
 * be to replace the three partner info fields in hvcs_struct with a
 * list of hvcs_partner_infos.
 */
static int hvcs_get_pi(struct hvcs_struct *hvcsd)
{
	/* struct hvcs_partner_info *head_pi = NULL; */
	struct hvcs_partner_info *pi = NULL;
	unsigned int unit_address = hvcsd->vdev->unit_address;
	struct list_head head;
	int retval;

	retval = hvcs_get_partner_info(unit_address, &head);
	if (retval) {
		printk(KERN_ERR "HVCS: Failed to fetch partner"
			" info for vty-server@%x.\n",unit_address);
		return retval;
	}

	/* nixes the values if the partner vty went away */
	hvcsd->p_unit_address = 0;
	hvcsd->p_partition_ID = 0;

	list_for_each_entry(pi, &head, node) {
		hvcs_set_pi(pi,hvcsd);
	}

	hvcs_free_partner_info(&head);
	return 0;
}

/* This function is executed by the driver "rescan" sysfs entry */
static int hvcs_rescan_devices_list(void)
{
	struct hvcs_struct *hvcsd = NULL;

	/* Locking issues? */
	list_for_each_entry(hvcsd, &hvcs_structs, next) {
		hvcs_get_pi(hvcsd);
	}

	return 0;
}

/* Farm this off into its own function because it could be
 * more complex once multiple partners support is added. */
static int hvcs_has_pi(struct hvcs_struct *hvcsd)
{
	if ((!hvcsd->p_unit_address) || (!hvcsd->p_partition_ID))
		return 0;
	return 1;
}

/* NOTE: It is possible that the super admin removed a partner
 * vty and then added a different vty as the new partner.
 */
static int hvcs_partner_connect(struct hvcs_struct *hvcsd)
{
	int retval;
	unsigned int unit_address = hvcsd->vdev->unit_address;

	/* If there wasn't any pi when the device was added it
	 * doesn't meant there isn't any now.  This driver isn't
	 * notified when a new partner vty is added to a
	 * vty-server so we discover changes on our own.
	 * Please see comments in hvcs_register_connection() for
	 * justification of this bizarre code. */
	retval = hvcs_register_connection(unit_address,
			hvcsd->p_partition_ID,
			hvcsd->p_unit_address);
	if (!retval)
		return 0;
	else if (retval != -EINVAL)
		return retval;

	/* As per the spec re-get the pi and try again if -EINVAL
	 * after the first connection attempt. */
	if (hvcs_get_pi(hvcsd))
		return -ENOMEM;

	if (!hvcs_has_pi(hvcsd))
		return -ENODEV;

	retval = hvcs_register_connection(unit_address,
			hvcsd->p_partition_ID,
			hvcsd->p_unit_address);
	if (retval != -EINVAL)
		return retval;

	/* EBUSY is the most likely scenario though the vty could have
	 * been removed or there really could be an hcall error due to the
	 * parameter data but thanks to ambiguous firmware return codes we
	 * can't really tell. */
	printk(KERN_INFO "HVCS: vty-server or partner"
			" vty is busy.  Try again later.\n");
	return -EBUSY;
}

static void hvcs_partner_free(struct hvcs_struct *hvcsd)
{
	int retval;
	do {
		/* it will return -EBUSY if the operation would take too
		 * long to complete synchronously.
		 */
		retval = hvcs_free_connection(hvcsd->vdev->unit_address);
	} while (retval == -EBUSY);
}

static int hvcs_enable_device(struct hvcs_struct *hvcsd)
{
	int retval;
	/* It is possible that the vty-server was removed
	 * between the time that the conn was registered and now.
	 */
	if ((retval = request_irq(hvcsd->vdev->irq,
			&hvcs_handle_interrupt, SA_INTERRUPT,
			"ibmhvcs", hvcsd)) != 0) {
		printk(KERN_ERR "HVCS: irq req failed for"
				" vty-server@%X retval :%d.\n",
			hvcsd->vdev->unit_address, retval);
		hvcs_partner_free(hvcsd);
		return -ENODEV;
	}

	/* It is possible the vty-server was removed
	 * after the irq was requested but before we have time
	 * to enabled interrupts.
	 */
	if (vio_enable_interrupts(hvcsd->vdev) != H_Success) {
		printk(KERN_ERR "HVCS: int enable failed"
				" for vty-server@%X.\n",
			hvcsd->vdev->unit_address);
		/* These can fail but we'll just ignore them for now. */
		free_irq(hvcsd->vdev->irq, hvcsd);
		hvcs_partner_free(hvcsd);
		return -ENODEV;
	}

	return 0;
}

static void hvcs_disable_device(struct hvcs_struct *hvcsd)
{
	if(!hvcsd->enabled)
		return;

	hvcsd->enabled = 0;
	/* Any one of these might fail at any time due to the
	 * vty-server's availability during the call.
	 */
	vio_disable_interrupts(hvcsd->vdev);
	hvcs_partner_free(hvcsd);
	printk(KERN_INFO "HVCS: Closed vty-server@%X and"
			" partner vty@%X:%d connection.\n",
			hvcsd->vdev->unit_address,
			hvcsd->p_unit_address,
			(unsigned int)hvcsd->p_partition_ID);
	free_irq(hvcsd->vdev->irq, hvcsd);
}

/* This always increments the kobject ref count if the call is
 * successful.  Please remember to dec when you are done with
 * the instance. */
struct hvcs_struct *hvcs_get_by_index(int index)
{
	struct hvcs_struct *hvcsd = NULL;
	struct list_head *element;
	struct list_head *safe_temp;
	/* We can immediately discard OOB requests */
	if (index >= 0 && index < HVCS_MAX_SERVER_ADAPTERS) {
		list_for_each_safe(element, safe_temp, &hvcs_structs) {
			hvcsd = list_entry(element, struct hvcs_struct, next);
			if (hvcsd->index == index) {
				kobject_get(&hvcsd->kobj);
				break;
			}
		}
	}
	return hvcsd;
}

/* This is invoked via the tty_open interface when a user app
 * connects to the /dev node.
 */
static int hvcs_open(struct tty_struct *tty, struct file *filp)
{
	struct hvcs_struct *hvcsd = NULL;
	int retval = 0;

	if (tty->driver_data)
		goto fast_open;

	/* Is there a vty-server that shares the same index? */
	if (!(hvcsd = hvcs_get_by_index(tty->index))) return -ENODEV;

	printk(KERN_INFO "HVCS: vty-server@%X opened.\n",
		hvcsd->vdev->unit_address );

	if((retval = hvcs_partner_connect(hvcsd)))
		goto error_release;

	/* This will free the connection if it fails */
	if((retval = hvcs_enable_device(hvcsd)))
		goto error_release;

	hvcsd->open_count = 1;
	hvcsd->enabled = 1;
	hvcsd->tty = tty;
	tty->driver_data = hvcsd;

	goto open_success;

fast_open:
	hvcsd = (struct hvcs_struct *)tty->driver_data;

	if (!kobject_get(&hvcsd->kobj)) {
		printk(KERN_ERR "HVCS: Kobject of open"
			" hvcs doesn't exist.\n");
		return -EFAULT; /* Is this the right return value? */
	}

	hvcsd->open_count++;

open_success:
	return 0;

error_release:
	kobject_put(&hvcsd->kobj);
	return retval;
}

static void hvcs_close(struct tty_struct *tty, struct file *filp)
{
	struct hvcs_struct *hvcsd;

	/* Is someone trying to close the file associated with
	 * this device after we have hung up?  If so
	 * tty->driver_data wouldn't be valid.
	 */
	if (tty_hung_up_p(filp)) {
		return;
	}

	/* No driver_data means that this close was probably
	 * issued after a failed hvcs_open by the tty layer's
	 * release_dev() api and we can just exit cleanly.
	 */
	if (!tty->driver_data) {
		return;
	}

	hvcsd = (struct hvcs_struct *)tty->driver_data;

	if (--hvcsd->open_count == 0) {
		hvcs_disable_device(hvcsd);

		/* This line is important because it tells hvcs_open
		 * that this device needs to be re-configured the
		 * next time hvcs_open is called.
		 */
		hvcsd->tty->driver_data = NULL;
		hvcsd->tty = NULL;
		hvcsd->p_partition_ID = 0;
		hvcsd->p_unit_address = 0;
		memset(&hvcsd->p_location_code[0], 0x00, CLC_LENGTH);
	} else if (hvcsd->open_count < 0) {
		printk(KERN_ERR "HVCS: vty-server@%X open_count: %d"
				" is missmanaged.\n",
			hvcsd->vdev->unit_address, hvcsd->open_count);
	}

	kobject_put(&hvcsd->kobj);
}

static void hvcs_hangup(struct tty_struct * tty)
{
	struct hvcs_struct *hvcsd = (struct hvcs_struct *)tty->driver_data;

	/* If the vty-server disappeared then the device
	 * would already be disabled.  Otherwise the hangup was
	 * indicated from a sighup?
	 */
	hvcs_disable_device(hvcsd);

	/* One big fat close regardless of the open_count */
	hvcsd->tty->driver_data = NULL;
	hvcsd->tty = NULL;
	hvcsd->p_partition_ID = 0;
	hvcsd->p_unit_address = 0;
	memset(&hvcsd->p_location_code[0], 0x00, CLC_LENGTH);

	/* We need to kobject_put() for every open_count we have
	 * since the tty_hangup() function doesn't invoke a close
	 * per open connection on a non-console device. */
	while(hvcsd->open_count) {
		--hvcsd->open_count;
		/* The final put will trigger destruction of the
		 * hvcs_struct */
		kobject_put(&hvcsd->kobj);
	}
}

/*NOTE: This is almost always from_user since user level apps interact
 * with the /dev devices. I'm going out on a limb here and trusting
 * that if hvcs_write gets called and interrupted by hvcs_remove
 * (which removes the target device and executes tty_hangup()) that
 * tty_hangup will allow hvcs_write time to complete execution before
 * it terminates our device. */
static int hvcs_write(struct tty_struct *tty, int from_user, const unsigned char *buf, int count)
{
	struct hvcs_struct *hvcsd = (struct hvcs_struct *)tty->driver_data;
	unsigned int unit_address;
	unsigned char *charbuf;
	int total_sent = 0;
	int tosend = 0;
	int sent = 0;

	/* If they don't check the return code off of their open they
	 * may attempt this even if there is no connected device. */
	if (!hvcsd) {
		return -ENODEV;
	}

	/* Reasonable size to prevent user level flooding */
	if (count > HVCS_MAX_FROM_USER)
		count = HVCS_MAX_FROM_USER;

	unit_address = hvcsd->vdev->unit_address;

	/* Somehow an open succeded but the device was removed or the
	 * connection terminated between the vty-server and
	 * partner vty during the middle of a write
	 * operation? */
	if (!hvcsd->enabled)
		return -ENODEV;

	if (!from_user) {
		charbuf = (unsigned char *)buf;
	} else {
		/* This isn't so important to do if we don't spinlock
		 * around the copy_from_user but we'll leave it here
		 * anyway because there may be locking issues in the
		 * future. */
		charbuf = kmalloc(count, GFP_KERNEL);
		if(!charbuf)
			return -ENOMEM;

		count -= copy_from_user(charbuf,buf,count);
	}

	while (count > 0) {
		tosend = min(count, HVCS_BUFF_LEN);

		/* won't return partial writes */
		sent = hvterm_put_chars(unit_address, &charbuf[total_sent], tosend);
		if (sent <= 0) {
			break;
		}

		total_sent+=sent;
		count-=sent;
	}

	if (from_user)
		kfree(charbuf);

	if (sent == -1)
		return -ENODEV;
	else
		return total_sent;
}

static int hvcs_write_room(struct tty_struct *tty)
{
	struct hvcs_struct *hvcsd = (struct hvcs_struct *)tty->driver_data;

	if (!hvcsd || !hvcsd->enabled)
		return 0;
	else
		return HVCS_MAX_FROM_USER;
}

static int hvcs_chars_in_buffer(struct tty_struct *tty)
{
	return HVCS_MAX_FROM_USER;
}

static struct tty_operations hvcs_ops = {
	.open = hvcs_open,
	.close = hvcs_close,
	.hangup = hvcs_hangup,
	.write = hvcs_write,
	.write_room = hvcs_write_room,
	.chars_in_buffer = hvcs_chars_in_buffer,
	.unthrottle = hvcs_unthrottle,
	.throttle = hvcs_throttle,
};

static int __init hvcs_module_init(void)
{
	int rc;
	int num_ttys_to_alloc;

	printk(KERN_INFO "Initializing %s\n", hvcs_driver_string);

	/* Has the user specified an overload with an insmod param? */
	if( hvcs_parm_num_devs <= 0 ||
		(hvcs_parm_num_devs > HVCS_MAX_SERVER_ADAPTERS)) {
		num_ttys_to_alloc = HVCS_DEFAULT_SERVER_ADAPTERS;
	} else {
		num_ttys_to_alloc = hvcs_parm_num_devs;
	}

	hvcs_tty_driver = alloc_tty_driver(num_ttys_to_alloc);
	if (!hvcs_tty_driver)
		return -ENOMEM;

	hvcs_tty_driver->owner = THIS_MODULE;

	hvcs_tty_driver->driver_name = hvcs_driver_name;
	hvcs_tty_driver->name = hvcs_device_node;

	/* We'll let the system assign us a major number,
	 * indicated by leaving it blank  */

	hvcs_tty_driver->minor_start = HVCS_MINOR_START;
	hvcs_tty_driver->type = TTY_DRIVER_TYPE_SYSTEM;

	/* We role our own so that we DONT ECHO.  We can't echo
	 * because the device we are connecting to already echoes
	 * by default and this would throw us into a horrible
	 * recursive echo-echo-echo loop. */
	hvcs_tty_driver->init_termios = hvcs_tty_termios;
	hvcs_tty_driver->flags = TTY_DRIVER_REAL_RAW;

	tty_set_operations(hvcs_tty_driver, &hvcs_ops);

	/* The following call will result in sysfs entries that denote
	 * the dynamically assigned major and minor numbers for our
	 * deices. */
	if(tty_register_driver(hvcs_tty_driver)) {
		printk(KERN_ERR "HVCS: registration "
			" as a tty driver failed.\n");
		put_tty_driver(hvcs_tty_driver);
		return rc;
	}

	rc = vio_register_driver(&hvcs_vio_driver);

	/* This needs to be done AFTER the vio_register_driver() call
	 * or else the kobjects won't be initialized properly.
	 */
	hvcs_create_driver_attrs();

	printk(KERN_INFO "HVCS: driver module inserted.\n");

	return rc;
}

static void __exit hvcs_module_exit(void)
{
	hvcs_remove_driver_attrs();

	vio_unregister_driver(&hvcs_vio_driver);

	tty_unregister_driver(hvcs_tty_driver);

	put_tty_driver(hvcs_tty_driver);

	printk(KERN_INFO "HVCS: driver module removed.\n");
}

module_init(hvcs_module_init);
module_exit(hvcs_module_exit);

static inline struct hvcs_struct *from_vio_dev(struct vio_dev *viod)
{
	return (struct hvcs_struct *)viod->driver_data;
}
/* The sysfs interface for the driver and devices */

static ssize_t hvcs_partner_vtys_show(struct device *dev, char *buf)
{
	struct vio_dev *viod = to_vio_dev(dev);
	struct hvcs_struct *hvcsd = from_vio_dev(viod);
	return sprintf(buf, "%X\n", hvcsd->p_unit_address);
}
static DEVICE_ATTR(partner_vtys, S_IRUGO, hvcs_partner_vtys_show, NULL);

static ssize_t hvcs_partner_clcs_show(struct device *dev, char *buf)
{
	struct vio_dev *viod = to_vio_dev(dev);
	struct hvcs_struct *hvcsd = from_vio_dev(viod);
	return sprintf(buf, "%s\n", &hvcsd->p_location_code[0]);
}
static DEVICE_ATTR(partner_clcs, S_IRUGO, hvcs_partner_clcs_show, NULL);

static ssize_t hvcs_current_vty_store(struct device *dev, const char * buf, size_t count)
{
	/* Don't need this feature at the present time. */
	printk(KERN_INFO "HVCS: Denied current_vty change: -EPERM.\n");
	return -EPERM;
}

static ssize_t hvcs_current_vty_show(struct device *dev, char *buf)
{
	struct vio_dev *viod = to_vio_dev(dev);
	struct hvcs_struct *hvcsd = from_vio_dev(viod);
	return sprintf(buf, "%s\n", &hvcsd->p_location_code[0]);
}

static DEVICE_ATTR(current_vty,
	S_IRUGO | S_IWUSR, hvcs_current_vty_show, hvcs_current_vty_store);

static struct attribute *hvcs_attrs[] = {
	&dev_attr_partner_vtys.attr,
	&dev_attr_partner_clcs.attr,
	&dev_attr_current_vty.attr,
	NULL,
};

static struct attribute_group hvcs_attr_group = {
	.attrs = hvcs_attrs,
};

static void hvcs_create_device_attrs(struct hvcs_struct *hvcsd)
{
	struct vio_dev *vdev = hvcsd->vdev;
	sysfs_create_group(&vdev->dev.kobj, &hvcs_attr_group);
}

static void hvcs_remove_device_attrs(struct hvcs_struct *hvcsd)
{
	struct vio_dev *vdev = hvcsd->vdev;
	sysfs_remove_group(&vdev->dev.kobj, &hvcs_attr_group);
}

static ssize_t hvcs_rescan_show(struct device_driver *ddp, char *buf)
{
	/* A 1 means it is updating, a 0 means it is done updating */
	return snprintf(buf, PAGE_SIZE, "%d\n", hvcs_rescan_status);
}

static ssize_t hvcs_rescan_store(struct device_driver *ddp, const char * buf, size_t count)
{
	if ((simple_strtol(buf,NULL,0) != 1)
		&& (hvcs_rescan_status != 0))
		return -EINVAL;

	hvcs_rescan_status = 1;
	printk(KERN_INFO "HVCS: rescanning partner info for all"
		" vty-servers.\n");
	hvcs_rescan_devices_list();
	hvcs_rescan_status = 0;
	return count;
}
static DRIVER_ATTR(rescan,
	S_IRUGO | S_IWUSR, hvcs_rescan_show, hvcs_rescan_store);

static void hvcs_create_driver_attrs(void)
{
	struct device_driver *driverfs = &(hvcs_vio_driver.driver);
	driver_create_file(driverfs, &driver_attr_rescan);
}

static void hvcs_remove_driver_attrs(void)
{
	struct device_driver *driverfs = &(hvcs_vio_driver.driver);
	driver_remove_file(driverfs, &driver_attr_rescan);
}
