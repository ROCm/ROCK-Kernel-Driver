/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/console.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <asm/sn/addrs.h>
#include <asm/sn/sn0/hub.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/ioc3.h>
#include <asm/sgialib.h>
#include <asm/sn/sn_private.h>

void prom_putchar(char c)
{
	struct ioc3 *ioc3;
	struct ioc3_uartregs *uart;

	ioc3 = (struct ioc3 *)KL_CONFIG_CH_CONS_INFO(master_nasid)->memory_base;
	uart = &ioc3->sregs.uarta;

	while ((uart->iu_lsr & 0x20) == 0);
	uart->iu_thr = c;
}

char __init prom_getchar(void)
{
	return 0;
}

static void
ip27prom_console_write(struct console *con, const char *s, unsigned n)
{
	prom_printf("%s", s);
}

static kdev_t 
ip27prom_console_dev(struct console *c)
{
	return MKDEV(TTY_MAJOR, 64 + c->index);
}

static struct console ip27_prom_console = {
    name:	"prom",
    write:	ip27prom_console_write,
    device:	ip27prom_console_dev,
    flags:	CON_PRINTBUFFER,
    index:	-1,
};

__init void ip27_setup_console(void)
{
	register_console(&ip27_prom_console);
}
