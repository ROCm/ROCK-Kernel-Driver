/*
 * Wrap-around code for a console using the
 * SGI PROM io-routines.
 *
 * Copyright (c) 1999 Ulf Carlsson
 *
 * Derived from DECstation promcon.c
 * Copyright (c) 1998 Harald Koerfgen 
 */

#include <linux/tty.h>
#include <linux/major.h>
#include <linux/ptrace.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/fs.h>

#include <asm/sgialib.h>

static void prom_console_write(struct console *co, const char *s,
			       unsigned count)
{
    unsigned i;

    /*
     *    Now, do each character
     */
    for (i = 0; i < count; i++) {
	if (*s == 10)
	    prom_printf("%c", 13);
	prom_printf("%c", *s++);
    }
}

static int prom_console_wait_key(struct console *co)
{
    return prom_getchar();
}

static int __init prom_console_setup(struct console *co, char *options)
{
    return 0;
}

static kdev_t prom_console_device(struct console *c)
{
    return MKDEV(TTY_MAJOR, 64 + c->index);
}

static struct console sercons =
{
    name:	"ttyS",
    write:	prom_console_write,
    device:	prom_console_device,
    wait_key:	prom_console_wait_key,
    setup:	prom_console_setup,
    flags:	CON_PRINTBUFFER,
    index:	-1,
};

/*
 *    Register console.
 */

void __init sgi_prom_console_init(void )
{
    register_console(&sercons);
}
