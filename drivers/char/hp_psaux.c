/*
 *      LASI PS/2 keyboard/psaux driver for HP-PARISC workstations
 *      
 *      (c) Copyright 1999 The Puffin Group Inc.
 *      by Alex deVries <adevries@thepuffingroup.com>
 *	Copyright 1999, 2000 Philipp Rumpf <prumpf@tux.org>
 *
 *	2000/10/26	Debacker Xavier (debackex@esiee.fr)
 *			Marteau Thomas (marteaut@esiee.fr)
 * 			Djoudi Malek (djoudim@esiee.fr)
 *	fixed leds control
 *	implemented the psaux and controlled the mouse scancode based on pc_keyb.c
 */

#include <linux/config.h>

#include <asm/hardware.h>
#include <asm/keyboard.h>
#include <asm/gsc.h>

#include <linux/types.h>
#include <linux/ptrace.h>	/* interrupt.h wants struct pt_regs defined */
#include <linux/interrupt.h>
#include <linux/sched.h>	/* for request_irq/free_irq */
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/pc_keyb.h>
#include <linux/kbd_kern.h>

/* mouse includes */
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>
#include <linux/poll.h>

/* HP specific LASI PS/2 keyboard and psaux constants */
#define	AUX_REPLY_ACK	0xFA	/* Command byte ACK. */
#define	AUX_RECONNECT	0xAA	/* scancode when ps2 device is plugged (back) in */

#define	LASI_PSAUX_OFFSET 0x0100 /* offset from keyboard to psaux port */

#define	LASI_ID		0x00	/* ID and reset port offsets */
#define	LASI_RESET	0x00
#define	LASI_RCVDATA	0x04	/* receive and transmit port offsets */
#define	LASI_XMTDATA	0x04
#define	LASI_CONTROL	0x08	/* see: control register bits */
#define	LASI_STATUS	0x0C	/* see: status register bits */

/* control register bits */
#define LASI_CTRL_ENBL	0x01	/* enable interface */
#define LASI_CTRL_LPBXR	0x02	/* loopback operation */
#define LASI_CTRL_DIAG	0x20	/* directly control clock/data line */
#define LASI_CTRL_DATDIR 0x40	/* data line direct control */
#define LASI_CTRL_CLKDIR 0x80	/* clock line direct control */

/* status register bits */
#define LASI_STAT_RBNE	0x01
#define LASI_STAT_TBNE	0x02
#define LASI_STAT_TERR	0x04
#define LASI_STAT_PERR	0x08
#define LASI_STAT_CMPINTR 0x10
#define LASI_STAT_DATSHD 0x40
#define LASI_STAT_CLKSHD 0x80

static void *lasikbd_hpa;
static void *lasips2_hpa;


static inline u8 read_input(void *hpa)
{
	return gsc_readb(hpa+LASI_RCVDATA);
}

static inline u8 read_control(void *hpa)
{
        return gsc_readb(hpa+LASI_CONTROL);
}

static inline void write_control(u8 val, void *hpa)
{
	gsc_writeb(val, hpa+LASI_CONTROL);
}

static inline u8 read_status(void *hpa)
{
        return gsc_readb(hpa+LASI_STATUS);
}

static int write_output(u8 val, void *hpa)
{
	int wait = 0;

	while (read_status(hpa) & LASI_STAT_TBNE) {
		wait++;
		if (wait>10000) {
			/* printk(KERN_WARNING "Lasi PS/2 transmit buffer timeout\n"); */
			return 0;
		}
	}

	if (wait)
		printk(KERN_DEBUG "Lasi PS/2 wait %d\n", wait);
	
	gsc_writeb(val, hpa+LASI_XMTDATA);

	return 1;
}

/* This function is the PA-RISC adaptation of i386 source */

static inline int aux_write_ack(u8 val)
{
      return write_output(val, lasikbd_hpa+LASI_PSAUX_OFFSET);
}

static void lasikbd_leds(unsigned char leds)
{
	write_output(KBD_CMD_SET_LEDS, lasikbd_hpa);
	write_output(leds, lasikbd_hpa);
	write_output(KBD_CMD_ENABLE, lasikbd_hpa);
}

#if 0
/* this might become useful again at some point.  not now  -prumpf */
int lasi_ps2_test(void *hpa)
{
	u8 control,c;
	int i, ret = 0;

	control = read_control(hpa);
	write_control(control | LASI_CTRL_LPBXR | LASI_CTRL_ENBL, hpa);

	for (i=0; i<256; i++) {
		write_output(i, hpa);

		while (!(read_status(hpa) & LASI_STAT_RBNE))
		    /* just wait */;
		    
		c = read_input(hpa);
		if (c != i)
			ret--;
	}

	write_control(control, hpa);

	return ret;
}
#endif 

static int __init lasi_ps2_reset(void *hpa, int id)
{
	u8 control;
	int ret = 1;

	/* reset the interface */
	gsc_writeb(0xff, hpa+LASI_RESET);
	gsc_writeb(0x0 , hpa+LASI_RESET);		

	/* enable it */
	control = read_control(hpa);
	write_control(control | LASI_CTRL_ENBL, hpa);

        /* initializes the leds at the default state */
        if (id==0) {
           write_output(KBD_CMD_SET_LEDS, hpa);
	   write_output(0, hpa);
	   ret = write_output(KBD_CMD_ENABLE, hpa);
	}

	return ret;
}

static int inited;

static void lasi_ps2_init_hw(void)
{
	++inited;
}


/* Greatly inspired by pc_keyb.c */

/*
 * Wait for keyboard controller input buffer to drain.
 *
 * Don't use 'jiffies' so that we don't depend on
 * interrupts..
 *
 * Quote from PS/2 System Reference Manual:
 *
 * "Address hex 0060 and address hex 0064 should be written only when
 * the input-buffer-full bit and output-buffer-full bit in the
 * Controller Status register are set 0."
 */
#ifdef CONFIG_PSMOUSE

static struct aux_queue	*queue;
static spinlock_t	kbd_controller_lock = SPIN_LOCK_UNLOCKED;
static unsigned char	mouse_reply_expected;
static int 		aux_count;

static int fasync_aux(int fd, struct file *filp, int on)
{
	int retval;
	
	retval = fasync_helper(fd, filp, on, &queue->fasync);
	if (retval < 0)
		return retval;
	
	return 0;
}



static inline void handle_mouse_scancode(unsigned char scancode)
{
	if (mouse_reply_expected) {
		if (scancode == AUX_REPLY_ACK) {
			mouse_reply_expected--;
			return;
		}
		mouse_reply_expected = 0;
	}
	else if (scancode == AUX_RECONNECT) {
		queue->head = queue->tail = 0;  /* Flush input queue */
		return;
	}

	add_mouse_randomness(scancode);
	if (aux_count) {
		int head = queue->head;
				
		queue->buf[head] = scancode;
		head = (head + 1) & (AUX_BUF_SIZE-1);
		
		if (head != queue->tail) {
			queue->head = head;
			kill_fasync(&queue->fasync, SIGIO, POLL_IN);
			wake_up_interruptible(&queue->proc_list);
		}
	}
}

static inline int queue_empty(void)
{
	return queue->head == queue->tail;
}

static unsigned char get_from_queue(void)
{
	unsigned char result;
	unsigned long flags;

	spin_lock_irqsave(&kbd_controller_lock, flags);
	result = queue->buf[queue->tail];
	queue->tail = (queue->tail + 1) & (AUX_BUF_SIZE-1);
	spin_unlock_irqrestore(&kbd_controller_lock, flags);

	return result;
}


/*
 * Write to the aux device.
 */

static ssize_t write_aux(struct file * file, const char * buffer,
			 size_t count, loff_t *ppos)
{
	ssize_t retval = 0;

	if (count) {
		ssize_t written = 0;

		if (count > 32)
			count = 32; /* Limit to 32 bytes. */
		do {
			char c;
			get_user(c, buffer++);
			written++;
		} while (--count);
		retval = -EIO;
		if (written) {
			retval = written;
			file->f_dentry->d_inode->i_mtime = CURRENT_TIME;
		}
	}

	return retval;
}



static ssize_t read_aux(struct file * file, char * buffer,
			size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	ssize_t i = count;
	unsigned char c;

	if (queue_empty()) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		add_wait_queue(&queue->proc_list, &wait);
repeat:
		set_current_state(TASK_INTERRUPTIBLE);
		if (queue_empty() && !signal_pending(current)) {
			schedule();
			goto repeat;
		}
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&queue->proc_list, &wait);
	}
	while (i > 0 && !queue_empty()) {
		c = get_from_queue();
		put_user(c, buffer++);
		i--;
	}
	if (count-i) {
		file->f_dentry->d_inode->i_atime = CURRENT_TIME;
		return count-i;
	}
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}


static int open_aux(struct inode * inode, struct file * file)
{
	if (aux_count++) 
		return 0;

	queue->head = queue->tail = 0;	/* Flush input queue */
	aux_count = 1;
	aux_write_ack(AUX_ENABLE_DEV);	/* Enable aux device */
	
	return 0;
}


/* No kernel lock held - fine */
static unsigned int aux_poll(struct file *file, poll_table * wait)
{

	poll_wait(file, &queue->proc_list, wait);
	if (!queue_empty())
		return POLLIN | POLLRDNORM;
	return 0;
}


static int release_aux(struct inode * inode, struct file * file)
{
	lock_kernel();
	fasync_aux(-1, file, 0);
	if (--aux_count) {
	   unlock_kernel();
		return 0;
	}
	unlock_kernel();
	return 0;
}

static struct file_operations psaux_fops = {
	read:		read_aux,
	write:		write_aux,
	poll:		aux_poll,
	open:		open_aux,
	release:	release_aux,
	fasync:		fasync_aux,
};

static struct miscdevice psaux_mouse = {
	minor:		PSMOUSE_MINOR,
	name:		"psaux",
	fops:		&psaux_fops,
};

#endif /* CONFIG_PSMOUSE */


/* This function is looking at the PS2 controller and empty the two buffers */

static u8 handle_lasikbd_event(void *hpa)
{
        u8 status_keyb,status_mouse,scancode,id;
        extern void handle_at_scancode(int); /* in drivers/char/keyb_at.c */
        
        /* Mask to get the base address of the PS/2 controller */
        id = gsc_readb(hpa+LASI_ID) & 0x0f;
        
        if (id==1) 
           hpa -= LASI_PSAUX_OFFSET; 
        lasikbd_hpa = hpa;
        

        status_keyb = read_status(hpa);
        status_mouse = read_status(hpa+LASI_PSAUX_OFFSET);

        while ((status_keyb|status_mouse) & LASI_STAT_RBNE){
           
           while (status_keyb & LASI_STAT_RBNE) {
	      
              scancode = read_input(hpa);

	      /* XXX don't know if this is a valid fix, but filtering
	       * 0xfa avoids 'unknown scancode' errors on, eg, capslock
	       * on some keyboards.
	       */
	      if (inited && scancode != 0xfa)
		 handle_at_scancode(scancode); 
	      
	      status_keyb =read_status(hpa);
           }
	   
#ifdef CONFIG_PSMOUSE
           while (status_mouse & LASI_STAT_RBNE) {
	      scancode = read_input(hpa+LASI_PSAUX_OFFSET);
	      handle_mouse_scancode(scancode);
              status_mouse = read_status(hpa+LASI_PSAUX_OFFSET);
	   }
           status_mouse = read_status(hpa+LASI_PSAUX_OFFSET);
#endif /* CONFIG_PSMOUSE */
           status_keyb = read_status(hpa);
        }

        tasklet_schedule(&keyboard_tasklet);
        return (status_keyb|status_mouse);
}



	
extern struct pt_regs *kbd_pt_regs;

static void lasikbd_interrupt(int irq, void *dev, struct pt_regs *regs)
{
	lasips2_hpa = dev; /* save "hpa" for lasikbd_leds() */
	kbd_pt_regs = regs;
	handle_lasikbd_event(lasips2_hpa);
}


extern int pckbd_translate(unsigned char, unsigned char *, char);

static struct kbd_ops gsc_ps2_kbd_ops = {
	translate:	pckbd_translate,
	init_hw:	lasi_ps2_init_hw,
	leds:		lasikbd_leds,
#ifdef CONFIG_MAGIC_SYSRQ
	sysrq_key:	0x54,
	sysrq_xlate:	hp_ps2kbd_sysrq_xlate,
#endif
};

static int __init
lasi_ps2_register(struct hp_device *d, struct pa_iodc_driver *dri)
{
	void *hpa = (void *) d->hpa;
	unsigned int irq;
	char *name;
	int device_found;
	u8 id;

	id = gsc_readb(hpa+LASI_ID) & 0x0f;

	switch (id) {
	case 0:
		name = "keyboard";
		lasikbd_hpa = hpa;
		break;
	case 1:
		name = "psaux";
		break;
	default:
		printk(KERN_WARNING "%s: Unknown PS/2 port (id=%d) - ignored.\n",
			__FUNCTION__, id );
		return 0;
	}
	
	/* reset the PS/2 port */
	device_found = lasi_ps2_reset(hpa,id);

	/* allocate the irq and memory region for that device */
	if (!(irq = busdevice_alloc_irq(d)))
		return -ENODEV;
		    
	if (request_irq(irq, lasikbd_interrupt, 0, name, hpa))
		return -ENODEV;
	
	if (!request_mem_region((unsigned long)hpa, LASI_STATUS + 4, name))
		return -ENODEV;

	switch (id) {
	case 0:	
		register_kbd_ops(&gsc_ps2_kbd_ops);
		break;
	case 1:
#ifdef CONFIG_PSMOUSE
		queue = (struct aux_queue *) kmalloc(sizeof(*queue), GFP_KERNEL);
		if (!queue)
			return -ENOMEM;

		memset(queue, 0, sizeof(*queue));
		queue->head = queue->tail = 0;
		init_waitqueue_head(&queue->proc_list);
		
		misc_register(&psaux_mouse);

		aux_write_ack(AUX_ENABLE_DEV);
		/* try it a second time, this will give status if the device is
		 * available */
		device_found = aux_write_ack(AUX_ENABLE_DEV);
		break;
#else
		/* return without printing any unnecessary and misleading info */
		return 0;	
#endif
	} /* of case */
	
	printk(KERN_INFO "PS/2 %s controller at 0x%08lx (irq %d) found, "
			 "%sdevice attached.\n",
			name, (unsigned long)hpa, irq,
			device_found ? "":"no ");

	return 0;
}


static struct pa_iodc_driver lasi_psaux_drivers_for[] __initdata = {
	{HPHW_FIO, 0x0, 0,0x00084, 0, 0,
		DRIVER_CHECK_HWTYPE + DRIVER_CHECK_SVERSION,
		"Lasi psaux", "generic", (void *) lasi_ps2_register},
	{ 0, }
};

static int __init gsc_ps2_init(void) 
{
	return pdc_register_driver(lasi_psaux_drivers_for);
}

module_init(gsc_ps2_init);

