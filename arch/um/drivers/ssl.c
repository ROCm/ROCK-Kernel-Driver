/* 
 * Copyright (C) 2000, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/config.h"
#include "linux/fs.h"
#include "linux/tty.h"
#include "linux/tty_driver.h"
#include "linux/major.h"
#include "linux/mm.h"
#include "linux/init.h"
#include "asm/termbits.h"
#include "asm/irq.h"
#include "line.h"
#include "ssl.h"
#include "chan_kern.h"
#include "user_util.h"
#include "kern_util.h"
#include "kern.h"
#include "init.h"
#include "irq_user.h"
#include "2_5compat.h"

static int ssl_version = 1;

static struct tty_driver ssl_driver;

static int ssl_refcount = 0;

#define NR_PORTS 64

void ssl_announce(char *dev_name, int dev)
{
	printk(KERN_INFO "Serial line %d assigned device '%s'\n", dev,
	       dev_name);
}

static struct chan_opts opts = {
	announce: 	ssl_announce,
	xterm_title:	"Serial Line #%d",
	raw:		1,
	tramp_stack :	0,
};

static struct line_driver driver = {
	name :			"UML serial line",
	devfs_name :		"tts/%d",
	major :			TTYAUX_MAJOR,
	minor_start :		64,
	type :		 	TTY_DRIVER_TYPE_SERIAL,
	subtype :	 	0,
	read_irq :		SSL_IRQ,
	read_irq_name :		"ssl",
	write_irq :		SSL_WRITE_IRQ,
	write_irq_name :	"ssl-write",
	symlink_from :		"serial",
	symlink_to :		"tts",
};

static struct line serial_lines[NR_PORTS] = 
	{ [0 ... NR_PORTS - 1] = LINE_INIT(CONFIG_SSL_CHAN, &driver) };

static struct lines lines = LINES_INIT(NR_PORTS);

int ssl_open(struct tty_struct *tty, struct file *filp)
{
	return(line_open(serial_lines, tty, &opts));
}

static void ssl_close(struct tty_struct *tty, struct file * filp)
{
	line_close(serial_lines, tty);
}

static int ssl_write(struct tty_struct * tty, int from_user,
		     const unsigned char *buf, int count)
{
	return(line_write(serial_lines, tty, buf, count));
}

static void ssl_put_char(struct tty_struct *tty, unsigned char ch)
{
	line_write(serial_lines, tty, &ch, sizeof(ch));
}

static void ssl_flush_chars(struct tty_struct *tty)
{
	return;
}

static int ssl_chars_in_buffer(struct tty_struct *tty)
{
	return(0);
}

static void ssl_flush_buffer(struct tty_struct *tty)
{
	return;
}

static int ssl_ioctl(struct tty_struct *tty, struct file * file,
		     unsigned int cmd, unsigned long arg)
{
	int ret;

	ret = 0;
	switch(cmd){
	case TCGETS:
	case TCSETS:
	case TCFLSH:
	case TCSETSF:
	case TCSETSW:
	case TCGETA:
	case TIOCMGET:
		ret = -ENOIOCTLCMD;
		break;
	default:
		printk(KERN_ERR 
		       "Unimplemented ioctl in ssl_ioctl : 0x%x\n", cmd);
		ret = -ENOIOCTLCMD;
		break;
	}
	return(ret);
}

static void ssl_throttle(struct tty_struct * tty)
{
	printk(KERN_ERR "Someone should implement ssl_throttle\n");
}

static void ssl_unthrottle(struct tty_struct * tty)
{
	printk(KERN_ERR "Someone should implement ssl_unthrottle\n");
}

static void ssl_set_termios(struct tty_struct *tty, 
			    struct termios *old_termios)
{
}

static void ssl_stop(struct tty_struct *tty)
{
	printk(KERN_ERR "Someone should implement ssl_stop\n");
}

static void ssl_start(struct tty_struct *tty)
{
	printk(KERN_ERR "Someone should implement ssl_start\n");
}

void ssl_hangup(struct tty_struct *tty)
{
}

static int ssl_init_done = 0;

int ssl_init(void)
{
	char *new_title;

	printk(KERN_INFO "Initializing software serial port version %d\n", 
	       ssl_version);

	ssl_driver = ((struct tty_driver)
		      {
			      refcount :	&ssl_refcount,
			      open :	 	ssl_open,
			      close :	 	ssl_close,
			      write :	 	ssl_write,
			      put_char :	ssl_put_char,
			      flush_chars :	ssl_flush_chars,
			      chars_in_buffer :	ssl_chars_in_buffer,
			      flush_buffer :	ssl_flush_buffer,
			      ioctl :	 	ssl_ioctl,
			      throttle :	ssl_throttle,
			      unthrottle :	ssl_unthrottle,
			      set_termios :	ssl_set_termios,
			      stop :	 	ssl_stop,
			      start :	 	ssl_start,
			      hangup :	 	ssl_hangup
		      });

	line_register_devfs(&lines, &driver, &ssl_driver, serial_lines, 
			    sizeof(serial_lines)/sizeof(serial_lines[0]));

	lines_init(serial_lines, sizeof(serial_lines)/sizeof(serial_lines[0]));

	new_title = add_xterm_umid(opts.xterm_title);
	if(new_title != NULL) opts.xterm_title = new_title;

	ssl_init_done = 1;
	return(0);
}

__initcall(ssl_init);

static int ssl_chan_setup(char *str)
{
	line_setup(serial_lines, sizeof(serial_lines)/sizeof(serial_lines[0]),
		   str);
	return(1);
}

__setup("ssl", ssl_chan_setup);
__channel_help(ssl_chan_setup, "ssl");

static void ssl_exit(void)
{
	if(!ssl_init_done) return;
	close_lines(serial_lines, 
		    sizeof(serial_lines)/sizeof(serial_lines[0]));
}

__uml_exitcall(ssl_exit);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
