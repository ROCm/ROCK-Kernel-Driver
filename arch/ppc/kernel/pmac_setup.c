/*
 *  linux/arch/ppc/kernel/setup.c
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 *  Derived from "arch/alpha/kernel/setup.c"
 *    Copyright (C) 1995 Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

/*
 * bootup setup stuff..
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/major.h>
#include <linux/blk.h>
#include <linux/vt_kern.h>
#include <linux/console.h>
#include <linux/ide.h>
#include <linux/pci.h>
#include <linux/adb.h>
#include <linux/cuda.h>
#include <linux/pmu.h>

#include <asm/init.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <asm/ohare.h>
#include <asm/mediabay.h>
#include <asm/feature.h>
#include <asm/machdep.h>
#include <asm/keyboard.h>
#include <asm/dma.h>
#include <asm/bootx.h>

#include <asm/time.h>
#include "local_irq.h"
#include "pmac_pic.h"

#undef SHOW_GATWICK_IRQS

extern long pmac_time_init(void);
extern unsigned long pmac_get_rtc_time(void);
extern int pmac_set_rtc_time(unsigned long nowtime);
extern void pmac_read_rtc_time(void);
extern void pmac_calibrate_decr(void);
extern void pmac_setup_pci_ptrs(void);

extern int mackbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int mackbd_getkeycode(unsigned int scancode);
extern int mackbd_translate(unsigned char keycode, unsigned char *keycodep,
		     char raw_mode);
extern int mackbd_unexpected_up(unsigned char keycode);
extern void mackbd_leds(unsigned char leds);
extern void __init mackbd_init_hw(void);
extern int mac_hid_kbd_translate(unsigned char scancode, unsigned char *keycode,
				 char raw_mode);
extern char mac_hid_kbd_unexpected_up(unsigned char keycode);
extern void mac_hid_init_hw(void);
#ifdef CONFIG_MAGIC_SYSRQ
extern unsigned char mac_hid_kbd_sysrq_xlate[128];
extern unsigned char pckbd_sysrq_xlate[128];
extern unsigned char mackbd_sysrq_xlate[128];
#endif /* CONFIG_MAGIC_SYSRQ */
extern int pckbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int pckbd_getkeycode(unsigned int scancode);
extern int pckbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char pckbd_unexpected_up(unsigned char keycode);
extern int keyboard_sends_linux_keycodes;
extern void pmac_nvram_update(void);

extern void *pmac_pci_dev_io_base(unsigned char bus, unsigned char devfn, int physical);
extern void *pmac_pci_dev_mem_base(unsigned char bus, unsigned char devfn);
extern int pmac_pci_dev_root_bridge(unsigned char bus, unsigned char devfn);

unsigned char drive_info;

int ppc_override_l2cr = 0;
int ppc_override_l2cr_value;
int has_l2cache = 0;

static int current_root_goodness = -1;

extern char saved_command_line[];

extern int pmac_newworld;

#define DEFAULT_ROOT_DEVICE 0x0801	/* sda1 - slightly silly choice */

extern void zs_kgdb_hook(int tty_num);
static void ohare_init(void);
static void init_p2pbridge(void);
#ifdef CONFIG_BOOTX_TEXT
void pmac_progress(char *s, unsigned short hex);
#endif

sys_ctrler_t sys_ctrler = SYS_CTRLER_UNKNOWN;

__pmac
int
pmac_get_cpuinfo(char *buffer)
{
	int len;
	struct device_node *np;
	char *pp;
	int plen;

	/* find motherboard type */
	len = sprintf(buffer, "machine\t\t: ");
	np = find_devices("device-tree");
	if (np != NULL) {
		pp = (char *) get_property(np, "model", NULL);
		if (pp != NULL)
			len += sprintf(buffer+len, "%s\n", pp);
		else
			len += sprintf(buffer+len, "PowerMac\n");
		pp = (char *) get_property(np, "compatible", &plen);
		if (pp != NULL) {
			len += sprintf(buffer+len, "motherboard\t:");
			while (plen > 0) {
				int l = strlen(pp) + 1;
				len += sprintf(buffer+len, " %s", pp);
				plen -= l;
				pp += l;
			}
			buffer[len++] = '\n';
		}
	} else
		len += sprintf(buffer+len, "PowerMac\n");

	/* find l2 cache info */
	np = find_devices("l2-cache");
	if (np == 0)
		np = find_type_devices("cache");
	if (np != 0) {
		unsigned int *ic = (unsigned int *)
			get_property(np, "i-cache-size", NULL);
		unsigned int *dc = (unsigned int *)
			get_property(np, "d-cache-size", NULL);
		len += sprintf(buffer+len, "L2 cache\t:");
		has_l2cache = 1;
		if (get_property(np, "cache-unified", NULL) != 0 && dc) {
			len += sprintf(buffer+len, " %dK unified", *dc / 1024);
		} else {
			if (ic)
				len += sprintf(buffer+len, " %dK instruction",
					       *ic / 1024);
			if (dc)
				len += sprintf(buffer+len, "%s %dK data",
					       (ic? " +": ""), *dc / 1024);
		}
		pp = get_property(np, "ram-type", NULL);
		if (pp)
			len += sprintf(buffer+len, " %s", pp);
		buffer[len++] = '\n';
	}

	/* find ram info */
	np = find_devices("memory");
	if (np != 0) {
		int n;
		struct reg_property *reg = (struct reg_property *)
			get_property(np, "reg", &n);
		
		if (reg != 0) {
			unsigned long total = 0;

			for (n /= sizeof(struct reg_property); n > 0; --n)
				total += (reg++)->size;
			len += sprintf(buffer+len, "memory\t\t: %luMB\n",
				       total >> 20);
		}
	}

	/* Checks "l2cr-value" property in the registry */
	np = find_devices("cpus");		
	if (np == 0)
		np = find_type_devices("cpu");		
	if (np != 0) {
		unsigned int *l2cr = (unsigned int *)
			get_property(np, "l2cr-value", NULL);
		if (l2cr != 0) {
			len += sprintf(buffer+len, "l2cr override\t: 0x%x\n", *l2cr);
		}
	}
	
	/* Indicate newworld/oldworld */
	len += sprintf(buffer+len, "pmac-generation\t: %s\n",
		pmac_newworld ? "NewWorld" : "OldWorld");		
	

	return len;
}

#ifdef CONFIG_SCSI
/* Find the device number for the disk (if any) at target tgt
   on host adaptor host.  We just need to get the prototype from
   sd.h */
#include <linux/blkdev.h>
#include "../../../drivers/scsi/scsi.h"
#include "../../../drivers/scsi/sd.h"

#endif

/*
 * Dummy mksound function that does nothing.
 * The real one is in the dmasound driver.
 */
__pmac
static void
pmac_mksound(unsigned int hz, unsigned int ticks)
{
}

static volatile u32 *sysctrl_regs;

void __init
pmac_setup_arch(void)
{
	struct device_node *cpu;
	int *fp;

	/* Set loops_per_sec to a half-way reasonable value,
	   for use until calibrate_delay gets called. */
	cpu = find_type_devices("cpu");
	if (cpu != 0) {
		fp = (int *) get_property(cpu, "clock-frequency", NULL);
		if (fp != 0) {
			switch (_get_PVR() >> 16) {
			case 4:		/* 604 */
			case 8:		/* G3 */
			case 9:		/* 604e */
			case 10:	/* mach V (604ev5) */
			case 12:	/* G4 */
			case 20:	/* 620 */
				loops_per_sec = *fp;
				break;
			default:	/* 601, 603, etc. */
				loops_per_sec = *fp / 2;
			}
		} else
			loops_per_sec = 50000000;
	}

	/* this area has the CPU identification register
	   and some registers used by smp boards */
	sysctrl_regs = (volatile u32 *) ioremap(0xf8000000, 0x1000);
	__ioremap(0xffc00000, 0x400000, pgprot_val(PAGE_READONLY));
	ohare_init();

	pmac_find_bridges();
	init_p2pbridge();
	
	/* Checks "l2cr-value" property in the registry */
	if ( (_get_PVR() >> 16) == 8 || (_get_PVR() >> 16) == 12 ) {
		struct device_node *np = find_devices("cpus");		
		if (np == 0)
			np = find_type_devices("cpu");		
		if (np != 0) {
			unsigned int *l2cr = (unsigned int *)
				get_property(np, "l2cr-value", NULL);
			if (l2cr != 0) {
				ppc_override_l2cr = 1;
				ppc_override_l2cr_value = *l2cr;
				_set_L2CR(0);
				_set_L2CR(ppc_override_l2cr_value);
			}
		}
	}

	if (ppc_override_l2cr)
		printk(KERN_INFO "L2CR overriden (0x%x), backside cache is %s\n",
			ppc_override_l2cr_value, (ppc_override_l2cr_value & 0x80000000)
				? "enabled" : "disabled");

#ifdef CONFIG_KGDB
	zs_kgdb_hook(0);
#endif

#ifdef CONFIG_ADB_CUDA
	find_via_cuda();
#endif	
#ifdef CONFIG_ADB_PMU
	find_via_pmu();
#endif	
#ifdef CONFIG_NVRAM
	pmac_nvram_init();
#endif
#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	kd_mksound = pmac_mksound;

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0);
	else
#endif
		ROOT_DEV = to_kdev_t(DEFAULT_ROOT_DEVICE);
}

/*
 * Tweak the PCI-PCI bridge chip on the blue & white G3s.
 */
static void __init init_p2pbridge(void)
{
	struct device_node *p2pbridge;
	unsigned char bus, devfn;
	unsigned short val;

	/* XXX it would be better here to identify the specific
	   PCI-PCI bridge chip we have. */
	if ((p2pbridge = find_devices("pci-bridge")) == 0
	    || p2pbridge->parent == NULL
	    || strcmp(p2pbridge->parent->name, "pci") != 0)
		return;
	if (pci_device_loc(p2pbridge, &bus, &devfn) < 0)
		return;
	if (ppc_md.pcibios_read_config_word(bus, devfn, PCI_BRIDGE_CONTROL, &val) < 0) {
		printk(KERN_ERR "init_p2pbridge: couldn't read bridge control\n");
		return;
	}
	val &= ~PCI_BRIDGE_CTL_MASTER_ABORT;
	ppc_md.pcibios_write_config_word(bus, devfn, PCI_BRIDGE_CONTROL, val);
	ppc_md.pcibios_read_config_word(bus, devfn, PCI_BRIDGE_CONTROL, &val);
}

static void __init ohare_init(void)
{
	/*
	 * Turn on the L2 cache.
	 * We assume that we have a PSX memory controller iff
	 * we have an ohare I/O controller.
	 */
	if (find_devices("ohare") != NULL) {
		if (((sysctrl_regs[2] >> 24) & 0xf) >= 3) {
			if (sysctrl_regs[4] & 0x10)
				sysctrl_regs[4] |= 0x04000020;
			else
				sysctrl_regs[4] |= 0x04000000;
			if(has_l2cache)
				printk(KERN_INFO "Level 2 cache enabled\n");
		}
	}
}

extern char *bootpath;
extern char *bootdevice;
void *boot_host;
int boot_target;
int boot_part;
kdev_t boot_dev;

void __init
pmac_init2(void)
{
#ifdef CONFIG_ADB_PMU
	via_pmu_start();
#endif
#ifdef CONFIG_ADB_CUDA
	via_cuda_start();
#endif
#ifdef CONFIG_PMAC_PBOOK
	media_bay_init();
#endif	
}

#ifdef CONFIG_SCSI
void __init
note_scsi_host(struct device_node *node, void *host)
{
	int l;
	char *p;

	l = strlen(node->full_name);
	if (bootpath != NULL && bootdevice != NULL
	    && strncmp(node->full_name, bootdevice, l) == 0
	    && (bootdevice[l] == '/' || bootdevice[l] == 0)) {
		boot_host = host;
		/*
		 * There's a bug in OF 1.0.5.  (Why am I not surprised.)
		 * If you pass a path like scsi/sd@1:0 to canon, it returns
		 * something like /bandit@F2000000/gc@10/53c94@10000/sd@0,0
		 * That is, the scsi target number doesn't get preserved.
		 * So we pick the target number out of bootpath and use that.
		 */
		p = strstr(bootpath, "/sd@");
		if (p != NULL) {
			p += 4;
			boot_target = simple_strtoul(p, NULL, 10);
			p = strchr(p, ':');
			if (p != NULL)
				boot_part = simple_strtoul(p + 1, NULL, 10);
		}
	}
}
#endif

#if defined(CONFIG_BLK_DEV_IDE) && defined(CONFIG_BLK_DEV_IDE_PMAC)

kdev_t __init find_ide_boot(void)
{
	char *p;
	int n;
	kdev_t __init pmac_find_ide_boot(char *bootdevice, int n);

	if (bootdevice == NULL)
		return 0;
	p = strrchr(bootdevice, '/');
	if (p == NULL)
		return 0;
	n = p - bootdevice;

	return pmac_find_ide_boot(bootdevice, n);
}
#endif /* CONFIG_BLK_DEV_IDE && CONFIG_BLK_DEV_IDE_PMAC */

void __init find_boot_device(void)
{
#ifdef CONFIG_SCSI
	if (boot_host != NULL) {
		boot_dev = sd_find_target(boot_host, boot_target);
		if (boot_dev != 0)
			return;
	}
#endif
#if defined(CONFIG_BLK_DEV_IDE) && defined(CONFIG_BLK_DEV_IDE_PMAC)
	boot_dev = find_ide_boot();
#endif
}

/* can't be __init - can be called whenever a disk is first accessed */
__pmac
void note_bootable_part(kdev_t dev, int part, int goodness)
{
	static int found_boot = 0;
	char *p;

	/* Do nothing if the root has been set already. */
	if ((goodness < current_root_goodness) &&
		(ROOT_DEV != to_kdev_t(DEFAULT_ROOT_DEVICE)))
		return;
	p = strstr(saved_command_line, "root=");
	if (p != NULL && (p == saved_command_line || p[-1] == ' '))
		return;

	if (!found_boot) {
		find_boot_device();
		found_boot = 1;
	}
	if (boot_dev == 0 || dev == boot_dev) {
		ROOT_DEV = MKDEV(MAJOR(dev), MINOR(dev) + part);
		boot_dev = NODEV;
		current_root_goodness = goodness;
	}
}

void
pmac_restart(char *cmd)
{
#ifdef CONFIG_ADB_CUDA
	struct adb_request req;
#endif /* CONFIG_ADB_CUDA */

	pmac_nvram_update();
	
	switch (sys_ctrler) {
#ifdef CONFIG_ADB_CUDA
	case SYS_CTRLER_CUDA:
		cuda_request(&req, NULL, 2, CUDA_PACKET,
			     CUDA_RESET_SYSTEM);
		for (;;)
			cuda_poll();
		break;
#endif /* CONFIG_ADB_CUDA */
#ifdef CONFIG_ADB_PMU		
	case SYS_CTRLER_PMU:
		pmu_restart();
		break;
#endif /* CONFIG_ADB_PMU */		
	default:
	}
}

void
pmac_power_off(void)
{
#ifdef CONFIG_ADB_CUDA
	struct adb_request req;
#endif /* CONFIG_ADB_CUDA */

	pmac_nvram_update();
	
	switch (sys_ctrler) {
#ifdef CONFIG_ADB_CUDA
	case SYS_CTRLER_CUDA:
		cuda_request(&req, NULL, 2, CUDA_PACKET,
			     CUDA_POWERDOWN);
		for (;;)
			cuda_poll();
		break;
#endif /* CONFIG_ADB_CUDA */
#ifdef CONFIG_ADB_PMU
	case SYS_CTRLER_PMU:
		pmu_shutdown();
		break;
#endif /* CONFIG_ADB_PMU */
	default:
	}
}

void
pmac_halt(void)
{
   pmac_power_off();
}


#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
/*
 * IDE stuff.
 */
void
pmac_ide_insw(ide_ioreg_t port, void *buf, int ns)
{
	_insw_ns((unsigned short *)(port+_IO_BASE), buf, ns);
}

void
pmac_ide_outsw(ide_ioreg_t port, void *buf, int ns)
{
	_outsw_ns((unsigned short *)(port+_IO_BASE), buf, ns);
}

int
pmac_ide_default_irq(ide_ioreg_t base)
{
        return 0;
}

#if defined(CONFIG_BLK_DEV_IDE) && defined(CONFIG_BLK_DEV_IDE_PMAC)
extern ide_ioreg_t pmac_ide_get_base(int index);
#endif

ide_ioreg_t
pmac_ide_default_io_base(int index)
{
#if defined(CONFIG_BLK_DEV_IDE) && defined(CONFIG_BLK_DEV_IDE_PMAC)
        return pmac_ide_get_base(index);
#else
	return 0;
#endif
}

int
pmac_ide_check_region(ide_ioreg_t from, unsigned int extent)
{
        return 0;
}

void
pmac_ide_request_region(ide_ioreg_t from,
			unsigned int extent,
			const char *name)
{
}

void
pmac_ide_release_region(ide_ioreg_t from,
			unsigned int extent)
{
}

/* Convert the shorts/longs in hd_driveid from little to big endian;
 * chars are endian independant, of course, but strings need to be flipped.
 * (Despite what it says in drivers/block/ide.h, they come up as little
 * endian...)
 *
 * Changes to linux/hdreg.h may require changes here. */
void
pmac_ide_fix_driveid(struct hd_driveid *id)
{
        ppc_generic_ide_fix_driveid(id);
}

#if defined(CONFIG_BLK_DEV_IDE) && defined(CONFIG_BLK_DEV_IDE_PMAC)
/* This is declared in drivers/block/ide-pmac.c */
void pmac_ide_init_hwif_ports (hw_regs_t *hw, ide_ioreg_t data_port, ide_ioreg_t ctrl_port, int *irq);
#else
/*
 * This registers the standard ports for this architecture with the IDE
 * driver.
 */
void pmac_ide_init_hwif_ports(hw_regs_t *hw, ide_ioreg_t data_port, ide_ioreg_t ctrl_port, int *irq)
{
}
#endif
#endif

void __init
pmac_init(unsigned long r3, unsigned long r4, unsigned long r5,
	  unsigned long r6, unsigned long r7)
{
	pmac_setup_pci_ptrs();

	/* isa_io_base gets set in pmac_find_bridges */
	isa_mem_base = PMAC_ISA_MEM_BASE;
	pci_dram_offset = PMAC_PCI_DRAM_OFFSET;
	ISA_DMA_THRESHOLD = ~0L;
	DMA_MODE_READ = 1;
	DMA_MODE_WRITE = 2;

	ppc_md.setup_arch     = pmac_setup_arch;
	ppc_md.setup_residual = NULL;
	ppc_md.get_cpuinfo    = pmac_get_cpuinfo;
	ppc_md.irq_cannonicalize = NULL;
	ppc_md.init_IRQ       = pmac_pic_init;
	ppc_md.get_irq        = pmac_get_irq;
	ppc_md.init           = pmac_init2;

	ppc_md.restart        = pmac_restart;
	ppc_md.power_off      = pmac_power_off;
	ppc_md.halt           = pmac_halt;

	ppc_md.time_init      = pmac_time_init;
	ppc_md.set_rtc_time   = pmac_set_rtc_time;
	ppc_md.get_rtc_time   = pmac_get_rtc_time;
	ppc_md.calibrate_decr = pmac_calibrate_decr;

	ppc_md.pci_dev_io_base          = pmac_pci_dev_io_base;
	ppc_md.pci_dev_mem_base         = pmac_pci_dev_mem_base;
	ppc_md.pci_dev_root_bridge      = pmac_pci_dev_root_bridge;

#ifdef CONFIG_VT
#ifdef CONFIG_INPUT_ADBHID
	ppc_md.kbd_init_hw       = mac_hid_init_hw;
	ppc_md.kbd_translate     = mac_hid_kbd_translate;
	ppc_md.kbd_unexpected_up = mac_hid_kbd_unexpected_up;
	ppc_md.kbd_setkeycode    = 0;
	ppc_md.kbd_getkeycode    = 0;
#ifdef CONFIG_MAGIC_SYSRQ
#ifdef CONFIG_MAC_ADBKEYCODES
	if (!keyboard_sends_linux_keycodes) {
		ppc_md.ppc_kbd_sysrq_xlate = mac_hid_kbd_sysrq_xlate;
		SYSRQ_KEY = 0x69;
	} else
#endif /* CONFIG_MAC_ADBKEYCODES */
	{
		ppc_md.ppc_kbd_sysrq_xlate = pckbd_sysrq_xlate;
		SYSRQ_KEY = 0x54;
	}
#endif /* CONFIG_MAGIC_SYSRQ */
#elif defined(CONFIG_ADB_KEYBOARD)
	ppc_md.kbd_setkeycode    = mackbd_setkeycode;
	ppc_md.kbd_getkeycode    = mackbd_getkeycode;
	ppc_md.kbd_translate     = mackbd_translate;
	ppc_md.kbd_unexpected_up = mackbd_unexpected_up;
	ppc_md.kbd_leds          = mackbd_leds;
	ppc_md.kbd_init_hw       = mackbd_init_hw;
#ifdef CONFIG_MAGIC_SYSRQ
	ppc_md.ppc_kbd_sysrq_xlate       = mackbd_sysrq_xlate;
	SYSRQ_KEY = 0x69;
#endif /* CONFIG_MAGIC_SYSRQ */
#endif /* CONFIG_INPUT_ADBHID/CONFIG_ADB_KEYBOARD */
#endif /* CONFIG_VT */

#if defined(CONFIG_BLK_DEV_IDE) && defined(CONFIG_BLK_DEV_IDE_PMAC)
        ppc_ide_md.insw = pmac_ide_insw;
        ppc_ide_md.outsw = pmac_ide_outsw;
        ppc_ide_md.default_irq = pmac_ide_default_irq;
        ppc_ide_md.default_io_base = pmac_ide_default_io_base;
        ppc_ide_md.ide_check_region = pmac_ide_check_region;
        ppc_ide_md.ide_request_region = pmac_ide_request_region;
        ppc_ide_md.ide_release_region = pmac_ide_release_region;
        ppc_ide_md.fix_driveid = pmac_ide_fix_driveid;
        ppc_ide_md.ide_init_hwif = pmac_ide_init_hwif_ports;

        ppc_ide_md.io_base = _IO_BASE;	/* actually too early for this :-( */
#endif
#ifdef CONFIG_BOOTX_TEXT
	ppc_md.progress = pmac_progress;
#endif
	if (ppc_md.progress) ppc_md.progress("pmac_init(): exit", 0);
	
}

#ifdef CONFIG_BOOTX_TEXT
extern void drawchar(char c);
extern void drawstring(const char *c);
extern boot_infos_t *disp_bi;
void
pmac_progress(char *s, unsigned short hex)
{
	if (disp_bi == 0)
		return;
	prom_drawstring(s);
	prom_drawchar('\n');
}
#endif CONFIG_BOOTX_TEXT

