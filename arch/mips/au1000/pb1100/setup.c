/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Alchemy Pb1100 board setup.
 *
 * Copyright 2002 MontaVista Software Inc.
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
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/mc146818rtc.h>
#include <linux/delay.h>
#include <linux/major.h>
#include <linux/kdev_t.h>
#include <linux/root_dev.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>
#include <asm/pgtable.h>
#include <asm/au1000.h>
#include <asm/pb1100.h>

#ifdef CONFIG_USB_OHCI
// Enable the workaround for the OHCI DoneHead
// register corruption problem.
#define CONFIG_AU1000_OHCI_FIX
#endif

#if defined(CONFIG_AU1X00_SERIAL_CONSOLE)
extern void console_setup(char *, int *);
char serial_console[20];
#endif

#ifdef CONFIG_BLK_DEV_INITRD
extern unsigned long initrd_start, initrd_end;
extern void * __rd_start, * __rd_end;
#endif

#ifdef CONFIG_BLK_DEV_IDE
extern struct ide_ops std_ide_ops;
extern struct ide_ops *ide_ops;
#endif

#ifdef CONFIG_RTC
extern struct rtc_ops pb1500_rtc_ops;
#endif

extern char * __init prom_getcmdline(void);
extern void au1000_restart(char *);
extern void au1000_halt(void);
extern void au1000_power_off(void);
extern struct resource ioport_resource;
extern struct resource iomem_resource;


void __init au1x00_setup(void)
{
	char *argptr;
	u32 pin_func, static_cfg0;
	u32 sys_freqctrl, sys_clksrc;

	argptr = prom_getcmdline();

	/* NOTE: The memory map is established by YAMON 2.08+ */

	/* Various early Au1000 Errata corrected by this */
	set_c0_config(1<<19); /* Config[OD] */

#ifdef CONFIG_AU1X00_SERIAL_CONSOLE
	if ((argptr = strstr(argptr, "console=")) == NULL) {
		argptr = prom_getcmdline();
		strcat(argptr, " console=ttyS0,115200");
	}
#endif

#ifdef CONFIG_SOUND_AU1X00
	strcat(argptr, " au1000_audio=vra");
	argptr = prom_getcmdline();
#endif

	_machine_restart = au1000_restart;
	_machine_halt = au1000_halt;
	_machine_power_off = au1000_power_off;

	// IO/MEM resources.
	set_io_port_base(0);
	ioport_resource.start = 0x10000000;
	ioport_resource.end = 0xffffffff;
	iomem_resource.start = 0x10000000;
	iomem_resource.end = 0xffffffff;

#ifdef CONFIG_BLK_DEV_INITRD
	ROOT_DEV = Root_RAM0;
	initrd_start = (unsigned long)&__rd_start;
	initrd_end = (unsigned long)&__rd_end;
#endif

	// set AUX clock to 12MHz * 8 = 96 MHz
	au_writel(8, SYS_AUXPLL);
	au_writel(0, SYS_PININPUTEN);
	udelay(100);

#if defined (CONFIG_USB_OHCI) || defined (CONFIG_AU1X00_USB_DEVICE)
#ifdef CONFIG_USB_OHCI
	if ((argptr = strstr(argptr, "usb_ohci=")) == NULL) {
	        char usb_args[80];
		argptr = prom_getcmdline();
		memset(usb_args, 0, sizeof(usb_args));
		sprintf(usb_args, " usb_ohci=base:0x%x,len:0x%x,irq:%d",
			USB_OHCI_BASE, USB_OHCI_LEN, AU1000_USB_HOST_INT);
		strcat(argptr, usb_args);
	}
#endif
	// configure pins GPIO[14:9] as GPIO
	pin_func = au_readl(SYS_PINFUNC) & (u32)(~0x80);

	/* zero and disable FREQ2 */
	sys_freqctrl = au_readl(SYS_FREQCTRL0);
	sys_freqctrl &= ~0xFFF00000;
	au_writel(sys_freqctrl, SYS_FREQCTRL0);

	/* zero and disable USBH/USBD/IrDA clock */
	sys_clksrc = au_readl(SYS_CLKSRC);
	sys_clksrc &= ~0x0000001F;
	au_writel(sys_clksrc, SYS_CLKSRC);

	sys_freqctrl = au_readl(SYS_FREQCTRL0);
	sys_freqctrl &= ~0xFFF00000;

	sys_clksrc = au_readl(SYS_CLKSRC);
	sys_clksrc &= ~0x0000001F;

	// FREQ2 = aux/2 = 48 MHz
	sys_freqctrl |= ((0<<22) | (1<<21) | (1<<20));
	au_writel(sys_freqctrl, SYS_FREQCTRL0);

	/*
	 * Route 48MHz FREQ2 into USBH/USBD/IrDA
	 */
	sys_clksrc |= ((4<<2) | (0<<1) | 0 );
	au_writel(sys_clksrc, SYS_CLKSRC);

	/* setup the static bus controller */
	au_writel(0x00000002, MEM_STCFG3);  /* type = PCMCIA */
	au_writel(0x280E3D07, MEM_STTIME3); /* 250ns cycle time */
	au_writel(0x10000000, MEM_STADDR3); /* any PCMCIA select */

	// get USB Functionality pin state (device vs host drive pins)
	pin_func = au_readl(SYS_PINFUNC) & (u32)(~0x8000);
#ifndef CONFIG_AU1X00_USB_DEVICE
	// 2nd USB port is USB host
	pin_func |= 0x8000;
#endif
	au_writel(pin_func, SYS_PINFUNC);
#endif // defined (CONFIG_USB_OHCI) || defined (CONFIG_AU1X00_USB_DEVICE)

#ifdef CONFIG_USB_OHCI
	// enable host controller and wait for reset done
	au_writel(0x08, USB_HOST_CONFIG);
	udelay(1000);
	au_writel(0x0c, USB_HOST_CONFIG);
	udelay(1000);
	au_readl(USB_HOST_CONFIG);
	while (!(au_readl(USB_HOST_CONFIG) & 0x10))
	    ;
	au_readl(USB_HOST_CONFIG);
#endif

#ifdef CONFIG_FB
	conswitchp = &dummy_con;
#endif

#ifdef CONFIG_FB_AU1100
	if ((argptr = strstr(argptr, "video=")) == NULL) {
		argptr = prom_getcmdline();
		/* default panel */
		strcat(argptr, " video=au1100fb:panel:Sharp_320x240_16");
	}
#endif

#ifdef CONFIG_FB_E1356
	if ((argptr = strstr(argptr, "video=")) == NULL) {
		argptr = prom_getcmdline();
		strcat(argptr, " video=e1356fb:system:pb1500");
	}
#endif

#ifndef CONFIG_SERIAL_NONSTANDARD
	/* don't touch the default serial console */
	au_writel(0, UART0_ADDR + UART_CLK);
#endif
	au_writel(0, UART1_ADDR + UART_CLK);
	au_writel(0, UART3_ADDR + UART_CLK);

#ifdef CONFIG_BLK_DEV_IDE
	ide_ops = &std_ide_ops;
#endif

	while (au_readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_E0S);
	au_writel(SYS_CNTRL_E0 | SYS_CNTRL_EN0, SYS_COUNTER_CNTRL);
	au_sync();
	while (au_readl(SYS_COUNTER_CNTRL) & SYS_CNTRL_T0S);
	au_writel(0, SYS_TOYTRIM);

	au_writel(0x00000060, 0xb190003c);

#ifdef CONFIG_RTC
	rtc_ops = &pb1500_rtc_ops;
	// Enable the RTC if not already enabled
	if (!(readb(0xac000028) & 0x20)) {
		writeb(readb(0xac000028) | 0x20, 0xac000028);
		au_sync();
	}
	// Put the clock in BCD mode
	if (readb(0xac00002C) & 0x4) { /* reg B */
		writeb(readb(0xac00002c) & ~0x4, 0xac00002c);
		au_sync();
	}
#endif
}
