/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Alchemy Db1000 board setup.
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
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/mc146818rtc.h>
#include <linux/delay.h>

#include <asm/cpu.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/keyboard.h>
#include <asm/mipsregs.h>
#include <asm/reboot.h>
#include <asm/pgtable.h>
#include <asm/au1000.h>
#include <asm/db1x00.h>

#ifdef CONFIG_BLK_DEV_INITRD
extern unsigned long initrd_start, initrd_end;
extern void * __rd_start, * __rd_end;
#endif

#ifdef CONFIG_BLK_DEV_IDE
extern struct ide_ops std_ide_ops;
extern struct ide_ops *ide_ops;
#endif

extern struct rtc_ops no_rtc_ops;
extern char * __init prom_getcmdline(void);
extern void au1000_restart(char *);
extern void au1000_halt(void);
extern void au1000_power_off(void);
extern struct resource ioport_resource;
extern struct resource iomem_resource;
#if defined(CONFIG_64BIT_PHYS_ADDR) && defined(CONFIG_SOC_AU1500)
extern phys_t (*fixup_bigphys_addr)(phys_t phys_addr, phys_t size);
static phys_t db_fixup_bigphys_addr(phys_t phys_addr, phys_t size);
#endif

void __init au1x00_setup(void)
{
	char *argptr;
	u32 pin_func, static_cfg0;
	u32 sys_freqctrl, sys_clksrc;
	u32 prid = read_c0_prid();

	argptr = prom_getcmdline();

	/* Various early Au1000 Errata corrected by this */
	set_c0_config(1<<19); /* Config[OD] */

#ifdef CONFIG_AU1X00_SERIAL_CONSOLE
	if ((argptr = strstr(argptr, "console=")) == NULL) {
		argptr = prom_getcmdline();
		strcat(argptr, " console=ttyS0,115200");
	}
#endif	  

#ifdef CONFIG_FB_AU1100
    if ((argptr = strstr(argptr, "video=")) == NULL) {
        argptr = prom_getcmdline();
        /* default panel */
        //strcat(argptr, " video=au1100fb:panel:Sharp_320x240_16");
        strcat(argptr, " video=au1100fb:panel:s10,nohwcursor");
    }
#endif

#if defined(CONFIG_SOUND_AU1X00) && !defined(CONFIG_SOC_AU1000)
	// au1000 does not support vra, au1500 and au1100 do
	strcat(argptr, " au1000_audio=vra");
	argptr = prom_getcmdline();
#endif

	rtc_ops = &no_rtc_ops;
	_machine_restart = au1000_restart;
	_machine_halt = au1000_halt;
	_machine_power_off = au1000_power_off;
#if defined(CONFIG_64BIT_PHYS_ADDR) && defined(CONFIG_SOC_AU1500)
	fixup_bigphys_addr = db_fixup_bigphys_addr;
#endif

	// IO/MEM resources. 
	set_io_port_base(0);
#ifdef CONFIG_SOC_AU1500
	ioport_resource.start = 0x00000000;
#else
	/* don't allow any legacy ports probing */
	ioport_resource.start = 0x10000000;
#endif
	ioport_resource.end = 0xffffffff;
	iomem_resource.start = 0x10000000;
	iomem_resource.end = 0xffffffff;

#ifdef CONFIG_BLK_DEV_INITRD
	ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0);
	initrd_start = (unsigned long)&__rd_start;
	initrd_end = (unsigned long)&__rd_end;
#endif

	//
	// NOTE:
	//
	// YAMON (specifically reset_db1500.s) enables 32khz osc
	// YAMON (specifically reset_db1x00.s) setups all clocking and GPIOs
	// YAMON (specifically reset_db1500.s) setups all PCI
	//

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

#ifdef CONFIG_USB_OHCI
	// enable host controller and wait for reset done
	au_writel(0x08, USB_HOST_CONFIG);
	udelay(1000);
	au_writel(0x0E, USB_HOST_CONFIG);
	udelay(1000);
	au_readl(USB_HOST_CONFIG); // throw away first read
	while (!(au_readl(USB_HOST_CONFIG) & 0x10))
		au_readl(USB_HOST_CONFIG);
#endif

#ifdef CONFIG_AU1X00_USB_DEVICE
	// 2nd USB port is USB device
	pin_func = au_readl(SYS_PINFUNC) & (u32)(~0x8000);
	au_writel(pin_func, SYS_PINFUNC);
#endif

#endif // defined (CONFIG_USB_OHCI) || defined (CONFIG_AU1X00_USB_DEVICE)

#ifdef CONFIG_FB
	// Needed if PCI video card in use
	conswitchp = &dummy_con;
#endif

#ifndef CONFIG_SERIAL_NONSTANDARD
	/* don't touch the default serial console */
        au_writel(0, UART_ADDR + UART_CLK);
#endif
	//au_writel(0, UART3_ADDR + UART_CLK);

#ifdef CONFIG_BLK_DEV_IDE
	ide_ops = &std_ide_ops;
#endif

#if 0
	//// FIX!!! must be valid for au1000, au1500 and au1100
	/* Enable Au1000 BCLK switching */
	switch (prid & 0x000000FF)
	{
	case 0x00: /* DA */
	case 0x01: /* HA */
	case 0x02: /* HB */
		break;
	default:  /* HC and newer */
		au_writel(0x00000060, 0xb190003c);
		break;
	}
#endif

	au_writel(0, 0xAE000010); /* turn off pcmcia power */

#ifdef CONFIG_MIPS_DB1000
    printk("AMD Alchemy Au1000/Db1000 Board\n");
#endif
#ifdef CONFIG_MIPS_DB1500
    printk("AMD Alchemy Au1500/Db1500 Board\n");
#endif
#ifdef CONFIG_MIPS_DB1100
    printk("AMD Alchemy Au1100/Db1100 Board\n");
#endif
}

#if defined(CONFIG_64BIT_PHYS_ADDR) && defined(CONFIG_SOC_AU1500)
static phys_t db_fixup_bigphys_addr(phys_t phys_addr, phys_t size)
{
	u32 pci_start = (u32)Au1500_PCI_MEM_START;
	u32 pci_end = (u32)Au1500_PCI_MEM_END;

	/* Don't fixup 36 bit addresses */
	if ((phys_addr >> 32) != 0) return phys_addr;

	/* check for pci memory window */
	if ((phys_addr >= pci_start) && ((phys_addr + size) < pci_end)) {
		return (phys_t)((phys_addr - pci_start) +
				     Au1500_PCI_MEM_START);
	}
	else 
		return phys_addr;
}
#endif
