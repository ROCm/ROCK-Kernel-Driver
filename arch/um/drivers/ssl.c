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
#include "linux/console.h"
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
#include "mconsole_kern.h"
#include "2_5compat.h"

static int ssl_version = 1;

/* Referenced only by tty_driver below - presumably it's locked correctly
 * by the tty driver.
 */

static struct tty_driver *ssl_driver;

#define NR_PORTS 64

void ssl_announce(char *dev_name, int dev)
{
	printk(KERN_INFO "Serial line %d assigned device '%s'\n", dev,
	       dev_name);
}

static struct chan_opts opts = {
	.announce 	= ssl_announce,
	.xterm_title	= "Serial Line #%d",
	.raw		= 1,
	.tramp_stack 	= 0,
	.in_kernel 	= 1,
};

static int ssl_config(char *str);
static int ssl_get_config(char *dev, char *str, int size, char **error_out);
static int ssl_remove(char *str);

static struct line_driver driver = {
	.name 			= "UML serial line",
	.device_name 		= "ttS",
	.devfs_name 		= "tts/",
	.major 			= TTY_MAJOR,
	.minor_start 		= 64,
	.type 		 	= TTY_DRIVER_TYPE_SERIAL,
	.subtype 	 	= 0,
	.read_irq 		= SSL_IRQ,
	.read_irq_name 		= "ssl",
	.write_irq 		= SSL_WRITE_IRQ,
	.write_irq_name 	= "ssl-write",
	.symlink_from 		= "serial",
	.symlink_to 		= "tts",
	.mc  = {
		.name  		= "ssl",
		.config 	= ssl_config,
		.get_config 	= ssl_get_config,
		.remove 	= ssl_remove,
	},
};

/* The array is initialized by line_init, which is an initcall.  The 
 * individual elements are protected by individual semaphores.
 */
static struct line serial_lines[NR_PORTS] =
	{ [0 ... NR_PORTS - 1] = LINE_INIT(CONFIG_SSL_CHAN, &driver) };

static struct lines lines = LINES_INIT(NR_PORTS);

static int ssl_config(char *str)
{
	return(line_config(serial_lines, 
			   sizeof(serial_lines)/sizeof(serial_lines[0]), str));
}

static int ssl_get_config(char *dev, char *str, int size, char **error_out)
{
	return(line_get_config(dev, serial_lines, 
			       sizeof(serial_lines)/sizeof(serial_lines[0]), 
			       str, size, error_out));
}

static int ssl_remove(char *str)
{
	return(line_remove(serial_lines, 
			   sizeof(serial_lines)/sizeof(serial_lines[0]), str));
}

int ssl_open(struct tty_struct *tty, struct file *filp)
{
	return(line_open(serial_lines, tty, &opts));
}

static void ssl_close(struct tty_struct *tty, struct file * filp)
{
	line_close(serial_lines, tty);
}

static int ssl_write(struct tty_struct * tty,
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
	case TCSBRK:
	case TCSBRKP:
	case TIOCMSET:
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

static struct tty_operations ssl_ops = {
	.open 	 		= ssl_open,
	.close 	 		= ssl_close,
	.write 	 		= ssl_write,
	.put_char 		= ssl_put_char,
	.flush_chars 		= ssl_flush_chars,
	.chars_in_buffer 	= ssl_chars_in_buffer,
	.flush_buffer 		= ssl_flush_buffer,
	.ioctl 	 		= ssl_ioctl,
	.throttle 		= ssl_throttle,
	.unthrottle 		= ssl_unthrottle,
	.set_termios 		= ssl_set_termios,
	.stop 	 		= ssl_stop,
	.start 	 		= ssl_start,
	.hangup 	 	= ssl_hangup,
	.write_room		= line_write_room,
};

/* Changed by ssl_init and referenced by ssl_exit, which are both serialized
 * by being an initcall and exitcall, respectively.
 */
static int ssl_init_done = 0;

static void ssl_console_write(struct console *c, const char *string,
			      unsigned len)
{
	struct line *line = &serial_lines[c->index];
	if(ssl_init_done)
		down(&line->sem);
	console_write_chan(&line->chan_list, string, len);
	if(ssl_init_done)
		up(&line->sem);
}

static struct tty_driver *ssl_console_device(struct console *c, int *index)
{
	*index = c->index;
	return ssl_driver;
}

static int ssl_console_setup(struct console *co, char *options)
{
	return(0);
}

static struct console ssl_cons = {
	name:		"ttyS",
	write:		ssl_console_write,
	device:		ssl_console_device,
	setup:		ssl_console_setup,
	flags:		CON_PRINTBUFFER,
	index:		-1,
};

int ssl_init(void)
{
	char *new_title;

	printk(KERN_INFO "Initializing software serial port version %d\n", 
	       ssl_version);

	ssl_driver = line_register_devfs(&lines, &driver, &ssl_ops,
		serial_lines, sizeof(serial_lines)/sizeof(serial_lines[0]));

	lines_init(serial_lines, sizeof(serial_lines)/sizeof(serial_lines[0]));

	new_title = add_xterm_umid(opts.xterm_title);
	if(new_title != NULL) opts.xterm_title = new_title;

	register_console(&ssl_cons);
	ssl_init_done = 1;
	return(0);
}

late_initcall(ssl_init);

static int ssl_chan_setup(char *str)
{
	return(line_setup(serial_lines,
			  sizeof(serial_lines)/sizeof(serial_lines[0]),
			  str, 1));
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
