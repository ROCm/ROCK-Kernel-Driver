/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Au1000-based board setup.
 *
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/mc146818rtc.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>
#include <asm/au1000.h>

#if defined(CONFIG_AU1000_SERIAL_CONSOLE)
extern void console_setup(char *, int *);
char serial_console[20];
#endif

void (*__wbflush) (void);
extern struct rtc_ops no_rtc_ops;
extern char * __init prom_getcmdline(void);
extern void au1000_restart(void);
extern void au1000_halt(void);
extern void au1000_power_off(void);

struct {
    struct resource ram;
    struct resource io;
    struct resource sram;
    struct resource flash;
    struct resource boot;
    struct resource pcmcia;
    struct resource lcd;
} au1000_resources = {
    { "RAM",           0,          0x3FFFFFF,  IORESOURCE_MEM },
    { "I/O",           0x10000000, 0x119FFFFF                 },
    { "SRAM",          0x1e000000, 0x1E03FFFF                 },
    { "System Flash",  0x1F800000, 0x1FBFFFFF                 },
    { "Boot ROM",      0x1FC00000, 0x1FFFFFFF                 },
    { "PCMCIA",        0x20000000, 0x27FFFFFF                 },
    { "LCD",           0x60000000, 0x603FFFFF                 },
};

void au1000_wbflush(void)
{
	__asm__ volatile ("sync");
}

void __init au1000_setup(void)
{
	char *argptr;

	argptr = prom_getcmdline();

#ifdef CONFIG_AU1000_SERIAL_CONSOLE
	if ((argptr = strstr(argptr, "console=ttyS0")) == NULL) {
		argptr = prom_getcmdline();
		strcat(argptr, " console=ttyS0,115200");
	}
#endif	  

	//set_cp0_status(ST0_FR,0);
	rtc_ops = &no_rtc_ops;
        __wbflush = au1000_wbflush;
	_machine_restart = au1000_restart;
	_machine_halt = au1000_halt;
	_machine_power_off = au1000_power_off;

	/*
	 * IO/MEM resources. 
	 */
	mips_io_port_base = KSEG1;
	ioport_resource.start = au1000_resources.io.start;
	ioport_resource.end = au1000_resources.lcd.end;

#ifdef CONFIG_BLK_DEV_INITRD
	ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0);
#endif

	outl(PC_CNTRL_E0 | PC_CNTRL_EN0 | PC_CNTRL_EN0, PC_COUNTER_CNTRL);
	while (inl(PC_COUNTER_CNTRL) & PC_CNTRL_T0S);
	outl(0x8000-1, PC0_TRIM);

	printk("Alchemy Semi PB1000 Board\n");
	printk("Au1000/PB1000 port (C) 2001 MontaVista Software, Inc. (source@mvista.com)\n");
}

