/* $Id: cmd64x.c,v 1.21 2000/01/30 23:23:16
 *
 * linux/drivers/ide/cmd64x.c		Version 1.22	June 9, 2000
 *
 * cmd64x.c: Enable interrupts at initialization time on Ultra/PCI machines.
 *           Note, this driver is not used at all on other systems because
 *           there the "BIOS" has done all of the following already.
 *           Due to massive hardware bugs, UltraDMA is only supported
 *           on the 646U2 and not on the 646U.
 *
 * Copyright (C) 1998		Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 1998		David S. Miller (davem@redhat.com)
 *
 * Copyright (C) 1999-2002	Andre Hedrick <andre@linux-ide.org>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#include "ide_modes.h"

#ifndef SPLIT_BYTE
#define SPLIT_BYTE(B,H,L)	((H)=(B>>4), (L)=(B-((B>>4)<<4)))
#endif

#define CMD_DEBUG 0

#if CMD_DEBUG
#define cmdprintk(x...)	printk(x)
#else
#define cmdprintk(x...)
#endif

/*
 * CMD64x specific registers definition.
 */

#define CFR		0x50
#define   CFR_INTR_CH0		0x02
#define CNTRL		0x51
#define	  CNTRL_DIS_RA0		0x40
#define   CNTRL_DIS_RA1		0x80
#define	  CNTRL_ENA_2ND		0x08

#define	CMDTIM		0x52
#define	ARTTIM0		0x53
#define	DRWTIM0		0x54
#define ARTTIM1 	0x55
#define DRWTIM1		0x56
#define ARTTIM23	0x57
#define   ARTTIM23_DIS_RA2	0x04
#define   ARTTIM23_DIS_RA3	0x08
#define   ARTTIM23_INTR_CH1	0x10
#define ARTTIM2		0x57
#define ARTTIM3		0x57
#define DRWTIM23	0x58
#define DRWTIM2		0x58
#define BRST		0x59
#define DRWTIM3		0x5b

#define BMIDECR0	0x70
#define MRDMODE		0x71
#define   MRDMODE_INTR_CH0	0x04
#define   MRDMODE_INTR_CH1	0x08
#define   MRDMODE_BLK_CH0	0x10
#define   MRDMODE_BLK_CH1	0x20
#define BMIDESR0	0x72
#define UDIDETCR0	0x73
#define DTPR0		0x74
#define BMIDECR1	0x78
#define BMIDECSR	0x79
#define BMIDESR1	0x7A
#define UDIDETCR1	0x7B
#define DTPR1		0x7C

#define DISPLAY_CMD64X_TIMINGS

#if defined(DISPLAY_CMD64X_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static char * print_cmd64x_get_info(char *, struct pci_dev *, int);
static char * print_sii_get_info(char *, struct pci_dev *, int);
static int cmd64x_get_info(char *, char **, off_t, int);
extern int (*cmd64x_display_info)(char *, char **, off_t, int); /* ide-proc.c */

byte cmd64x_proc = 0;

#define CMD_MAX_DEVS		5

static struct pci_dev *cmd_devs[CMD_MAX_DEVS];
static int n_cmd_devs;

#undef DEBUG_CMD_REGS

static char * print_cmd64x_get_info (char *buf, struct pci_dev *dev, int index)
{
	char *p = buf;

	u8 reg53 = 0, reg54 = 0, reg55 = 0, reg56 = 0;	/* primary */
	u8 reg57 = 0, reg58 = 0, reg5b;			/* secondary */
	u8 reg72 = 0, reg73 = 0;			/* primary */
	u8 reg7a = 0, reg7b = 0;			/* secondary */
	u8 reg50 = 0, reg71 = 0;			/* extra */
#ifdef DEBUG_CMD_REGS
	u8 hi_byte = 0, lo_byte = 0;
#endif /* DEBUG_CMD_REGS */

	p += sprintf(p, "\nController: %d\n", index);
	p += sprintf(p, "CMD%x Chipset.\n", dev->device);
	(void) pci_read_config_byte(dev, CFR,       &reg50);
	(void) pci_read_config_byte(dev, ARTTIM0,   &reg53);
	(void) pci_read_config_byte(dev, DRWTIM0,   &reg54);
	(void) pci_read_config_byte(dev, ARTTIM1,   &reg55);
	(void) pci_read_config_byte(dev, DRWTIM1,   &reg56);
	(void) pci_read_config_byte(dev, ARTTIM2,   &reg57);
	(void) pci_read_config_byte(dev, DRWTIM2,   &reg58);
	(void) pci_read_config_byte(dev, DRWTIM3,   &reg5b);
	(void) pci_read_config_byte(dev, MRDMODE,   &reg71);
	(void) pci_read_config_byte(dev, BMIDESR0,  &reg72);
	(void) pci_read_config_byte(dev, UDIDETCR0, &reg73);
	(void) pci_read_config_byte(dev, BMIDESR1,  &reg7a);
	(void) pci_read_config_byte(dev, UDIDETCR1, &reg7b);

	p += sprintf(p, "--------------- Primary Channel "
			"---------------- Secondary Channel "
			"-------------\n");
	p += sprintf(p, "                %sabled           "
			"              %sabled\n",
		(reg72&0x80)?"dis":" en",
		(reg7a&0x80)?"dis":" en");
	p += sprintf(p, "--------------- drive0 "
		"--------- drive1 -------- drive0 "
		"---------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:    %s              %s"
			"             %s               %s\n",
		(reg72&0x20)?"yes":"no ", (reg72&0x40)?"yes":"no ",
		(reg7a&0x20)?"yes":"no ", (reg7a&0x40)?"yes":"no ");

	p += sprintf(p, "DMA Mode:       %s(%s)          %s(%s)",
		(reg72&0x20)?((reg73&0x01)?"UDMA":" DMA"):" PIO",
		(reg72&0x20)?(
			((reg73&0x30)==0x30)?(((reg73&0x35)==0x35)?"3":"0"):
			((reg73&0x20)==0x20)?(((reg73&0x25)==0x25)?"3":"1"):
			((reg73&0x10)==0x10)?(((reg73&0x15)==0x15)?"4":"2"):
			((reg73&0x00)==0x00)?(((reg73&0x05)==0x05)?"5":"2"):
			"X"):"?",
		(reg72&0x40)?((reg73&0x02)?"UDMA":" DMA"):" PIO",
		(reg72&0x40)?(
			((reg73&0xC0)==0xC0)?(((reg73&0xC5)==0xC5)?"3":"0"):
			((reg73&0x80)==0x80)?(((reg73&0x85)==0x85)?"3":"1"):
			((reg73&0x40)==0x40)?(((reg73&0x4A)==0x4A)?"4":"2"):
			((reg73&0x00)==0x00)?(((reg73&0x0A)==0x0A)?"5":"2"):
			"X"):"?");
	p += sprintf(p, "         %s(%s)           %s(%s)\n",
		(reg7a&0x20)?((reg7b&0x01)?"UDMA":" DMA"):" PIO",
		(reg7a&0x20)?(
			((reg7b&0x30)==0x30)?(((reg7b&0x35)==0x35)?"3":"0"):
			((reg7b&0x20)==0x20)?(((reg7b&0x25)==0x25)?"3":"1"):
			((reg7b&0x10)==0x10)?(((reg7b&0x15)==0x15)?"4":"2"):
			((reg7b&0x00)==0x00)?(((reg7b&0x05)==0x05)?"5":"2"):
			"X"):"?",
		(reg7a&0x40)?((reg7b&0x02)?"UDMA":" DMA"):" PIO",
		(reg7a&0x40)?(
			((reg7b&0xC0)==0xC0)?(((reg7b&0xC5)==0xC5)?"3":"0"):
			((reg7b&0x80)==0x80)?(((reg7b&0x85)==0x85)?"3":"1"):
			((reg7b&0x40)==0x40)?(((reg7b&0x4A)==0x4A)?"4":"2"):
			((reg7b&0x00)==0x00)?(((reg7b&0x0A)==0x0A)?"5":"2"):
			"X"):"?" );
	p += sprintf(p, "PIO Mode:       %s                %s"
			"               %s                 %s\n",
			"?", "?", "?", "?");
	p += sprintf(p, "                %s                     %s\n",
		(reg50 & CFR_INTR_CH0) ? "interrupting" : "polling     ",
		(reg57 & ARTTIM23_INTR_CH1) ? "interrupting" : "polling");
	p += sprintf(p, "                %s                          %s\n",
		(reg71 & MRDMODE_INTR_CH0) ? "pending" : "clear  ",
		(reg71 & MRDMODE_INTR_CH1) ? "pending" : "clear");
	p += sprintf(p, "                %s                          %s\n",
		(reg71 & MRDMODE_BLK_CH0) ? "blocked" : "enabled",
		(reg71 & MRDMODE_BLK_CH1) ? "blocked" : "enabled");

#ifdef DEBUG_CMD_REGS
	SPLIT_BYTE(reg50, hi_byte, lo_byte);
	p += sprintf(p, "CFR       = 0x%02x, HI = 0x%02x, "
			"LOW = 0x%02x\n", reg50, hi_byte, lo_byte);
	SPLIT_BYTE(reg57, hi_byte, lo_byte);
	p += sprintf(p, "ARTTIM23  = 0x%02x, HI = 0x%02x, "
			"LOW = 0x%02x\n", reg57, hi_byte, lo_byte);
	SPLIT_BYTE(reg71, hi_byte, lo_byte);
	p += sprintf(p, "MRDMODE   = 0x%02x, HI = 0x%02x, "
			"LOW = 0x%02x\n", reg71, hi_byte, lo_byte);
#endif /* DEBUG_CMD_REGS */

	return (char *)p;
}

static char * print_sii_get_info (char *buf, struct pci_dev *dev, int index)
{
	char *p = buf;

	p += sprintf(p, "\nController: %d\n", index);
	p += sprintf(p, "SII%x Chipset.\n", dev->device);

	p += sprintf(p, "--------------- Primary Channel "
			"---------------- Secondary Channel "
			"-------------\n");
	p += sprintf(p, "--------------- drive0 --------- drive1 "
			"-------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "PIO Mode:       %s                %s"
			"               %s                 %s\n",
			"?", "?", "?", "?");
	return (char *)p;
}

static int cmd64x_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	int i;

	p += sprintf(p, "\n");
	for (i = 0; i < n_cmd_devs; i++) {
		struct pci_dev *dev	= cmd_devs[i];

		if (dev->device <= PCI_DEVICE_ID_CMD_649)
			p = print_cmd64x_get_info(p, dev, i);
		else
			p = print_sii_get_info(p, dev, i);
	}
	return p-buffer;	/* => must be less than 4k! */
}

#endif	/* defined(DISPLAY_CMD64X_TIMINGS) && defined(CONFIG_PROC_FS) */

/*
 * Registers and masks for easy access by drive index:
 */
#if 0
static byte prefetch_regs[4]  = {CNTRL, CNTRL, ARTTIM23, ARTTIM23};
static byte prefetch_masks[4] = {CNTRL_DIS_RA0, CNTRL_DIS_RA1, ARTTIM23_DIS_RA2, ARTTIM23_DIS_RA3};
#endif

/*
 * This routine writes the prepared setup/active/recovery counts
 * for a drive into the cmd646 chipset registers to active them.
 */
static void program_drive_counts (ide_drive_t *drive, int setup_count, int active_count, int recovery_count)
{
	unsigned long flags;
	struct pci_dev *dev = HWIF(drive)->pci_dev;
	ide_drive_t *drives = HWIF(drive)->drives;
	byte temp_b;
	static const byte setup_counts[] = {0x40, 0x40, 0x40, 0x80, 0, 0xc0};
	static const byte recovery_counts[] =
		{15, 15, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0};
	static const byte arttim_regs[2][2] = {
			{ ARTTIM0, ARTTIM1 },
			{ ARTTIM23, ARTTIM23 }
		};
	static const byte drwtim_regs[2][2] = {
			{ DRWTIM0, DRWTIM1 },
			{ DRWTIM2, DRWTIM3 }
		};
	int channel = (int) HWIF(drive)->channel;
	int slave = (drives != drive);  /* Is this really the best way to determine this?? */

	cmdprintk("program_drive_count parameters = s(%d),a(%d),r(%d),p(%d)\n", setup_count,
		active_count, recovery_count, drive->present);
	/*
	 * Set up address setup count registers.
	 * Primary interface has individual count/timing registers for
	 * each drive.  Secondary interface has one common set of registers,
	 * for address setup so we merge these timings, using the slowest
	 * value.
	 */
	if (channel) {
		drive->drive_data = setup_count;
		setup_count = IDE_MAX(drives[0].drive_data, drives[1].drive_data);
		cmdprintk("Secondary interface, setup_count = %d\n", setup_count);
	}

	/*
	 * Convert values to internal chipset representation
	 */
	setup_count = (setup_count > 5) ? 0xc0 : (int) setup_counts[setup_count];
	active_count &= 0xf; /* Remember, max value is 16 */
	recovery_count = (int) recovery_counts[recovery_count];

	cmdprintk("Final values = %d,%d,%d\n",
		setup_count, active_count, recovery_count);

	/*
	 * Now that everything is ready, program the new timings
	 */
	local_irq_save(flags);
	/*
	 * Program the address_setup clocks into ARTTIM reg,
	 * and then the active/recovery counts into the DRWTIM reg
	 */
	(void) pci_read_config_byte(dev, arttim_regs[channel][slave], &temp_b);
	(void) pci_write_config_byte(dev, arttim_regs[channel][slave],
		((byte) setup_count) | (temp_b & 0x3f));
	(void) pci_write_config_byte(dev, drwtim_regs[channel][slave],
		(byte) ((active_count << 4) | recovery_count));
	cmdprintk ("Write %x to %x\n",
		((byte) setup_count) | (temp_b & 0x3f),
		arttim_regs[channel][slave]);
	cmdprintk ("Write %x to %x\n",
		(byte) ((active_count << 4) | recovery_count),
		drwtim_regs[channel][slave]);
	local_irq_restore(flags);
}

/*
 * Attempts to set the interface PIO mode.
 * The preferred method of selecting PIO modes (e.g. mode 4) is 
 * "echo 'piomode:4' > /proc/ide/hdx/settings".  Special cases are
 * 8: prefetch off, 9: prefetch on, 255: auto-select best mode.
 * Called with 255 at boot time.
 */
static void cmd64x_tuneproc (ide_drive_t *drive, byte mode_wanted)
{
	int setup_time, active_time, recovery_time, clock_time, pio_mode, cycle_time;
	byte recovery_count2, cycle_count;
	int setup_count, active_count, recovery_count;
	int bus_speed = system_bus_clock();
	/*byte b;*/
	ide_pio_data_t  d;

	switch (mode_wanted) {
		case 8: /* set prefetch off */
		case 9: /* set prefetch on */
			mode_wanted &= 1;
			/*set_prefetch_mode(index, mode_wanted);*/
			cmdprintk("%s: %sabled cmd640 prefetch\n",
				drive->name, mode_wanted ? "en" : "dis");
			return;
	}

	mode_wanted = ide_get_best_pio_mode (drive, mode_wanted, 5, &d);
	pio_mode = d.pio_mode;
	cycle_time = d.cycle_time;

	/*
	 * I copied all this complicated stuff from cmd640.c and made a few
	 * minor changes.  For now I am just going to pray that it is correct.
	 */
	if (pio_mode > 5)
		pio_mode = 5;
	setup_time  = ide_pio_timings[pio_mode].setup_time;
	active_time = ide_pio_timings[pio_mode].active_time;
	recovery_time = cycle_time - (setup_time + active_time);
	clock_time = 1000 / bus_speed;
	cycle_count = (cycle_time + clock_time - 1) / clock_time;

	setup_count = (setup_time + clock_time - 1) / clock_time;

	active_count = (active_time + clock_time - 1) / clock_time;

	recovery_count = (recovery_time + clock_time - 1) / clock_time;
	recovery_count2 = cycle_count - (setup_count + active_count);
	if (recovery_count2 > recovery_count)
		recovery_count = recovery_count2;
	if (recovery_count > 16) {
		active_count += recovery_count - 16;
		recovery_count = 16;
	}
	if (active_count > 16)
		active_count = 16; /* maximum allowed by cmd646 */

	/*
	 * In a perfect world, we might set the drive pio mode here
	 * (using WIN_SETFEATURE) before continuing.
	 *
	 * But we do not, because:
	 *	1) this is the wrong place to do it
	 *		(proper is do_special() in ide.c)
	 * 	2) in practice this is rarely, if ever, necessary
	 */
	program_drive_counts (drive, setup_count, active_count, recovery_count);

	cmdprintk("%s: selected cmd646 PIO mode%d : %d (%dns)%s, "
		"clocks=%d/%d/%d\n",
		drive->name, pio_mode, mode_wanted, cycle_time,
		d.overridden ? " (overriding vendor mode)" : "",
		setup_count, active_count, recovery_count);
}

static byte cmd64x_ratemask (ide_drive_t *drive)
{
	struct pci_dev *dev	= HWIF(drive)->pci_dev;
	byte mode		= 0x00;

	switch(dev->device) {
		case PCI_DEVICE_ID_CMD_680:	{ mode |= 0x04; break; }
		case PCI_DEVICE_ID_CMD_649:	{ mode |= 0x03; break; }
		case PCI_DEVICE_ID_CMD_648:	{ mode |= 0x02; break; }
		case PCI_DEVICE_ID_CMD_643:	{ mode |= 0x01; break; }

		case PCI_DEVICE_ID_CMD_646:
		{
			unsigned int class_rev	= 0;
			pci_read_config_dword(dev,
				PCI_CLASS_REVISION, &class_rev);
			class_rev &= 0xff;
		/*
		 * UltraDMA only supported on PCI646U and PCI646U2, which
		 * correspond to revisions 0x03, 0x05 and 0x07 respectively.
		 * Actually, although the CMD tech support people won't
		 * tell me the details, the 0x03 revision cannot support
		 * UDMA correctly without hardware modifications, and even
		 * then it only works with Quantum disks due to some
		 * hold time assumptions in the 646U part which are fixed
		 * in the 646U2.
		 *
		 * So we only do UltraDMA on revision 0x05 and 0x07 chipsets.
		 */
			switch(class_rev) {
				case 0x07:
				case 0x05:	{ mode |= 0x01; break; }
				case 0x03:
				case 0x01:
				default:	{ mode |= 0x00; break; }
			}
		}
	}
	if (!eighty_ninty_three(drive)) {
		mode &= ~0xFE;
		mode |= 0x01;
	}
	return (mode &= ~0xF8);
}

static byte cmd64x_ratefilter (ide_drive_t *drive, byte speed)
{
#ifdef CONFIG_BLK_DEV_IDEDMA
	byte mode = cmd64x_ratemask(drive);

	switch(mode) {
		case 0x04:	while (speed > XFER_UDMA_6) speed--; break;
		case 0x03:	while (speed > XFER_UDMA_5) speed--; break;
		case 0x02:	while (speed > XFER_UDMA_4) speed--; break;
		case 0x01:	while (speed > XFER_UDMA_2) speed--; break;
		case 0x00:
		default:	while (speed > XFER_MW_DMA_2) speed--; break;
			break;
	}
#else
	while (speed > XFER_PIO_4) speed--;
#endif /* CONFIG_BLK_DEV_IDEDMA */
//	printk("%s: mode == %02x speed == %02x\n", drive->name, mode, speed);
	return speed;
}

static byte cmd680_taskfile_timing (ide_hwif_t *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	byte addr_mask		= (hwif->channel) ? 0xB2 : 0xA2;
	unsigned short		timing;

	pci_read_config_word(dev, addr_mask, &timing);

	switch (timing) {
		case 0x10c1:	return 4;
		case 0x10c3:	return 3;
		case 0x1281:	return 2;
		case 0x2283:	return 1;
		case 0x328a:
		default:	return 0;
	}
}

static void cmd680_tuneproc (ide_drive_t *drive, byte mode_wanted)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	byte			drive_pci;
	unsigned short		speedt;

	switch (drive->dn) {
		case 0: drive_pci = 0xA4; break;
		case 1: drive_pci = 0xA6; break;
		case 2: drive_pci = 0xB4; break;
		case 3: drive_pci = 0xB6; break;
		default: return;
        }

	pci_read_config_word(dev, drive_pci, &speedt);

	/* cheat for now and use the docs */
//	switch(cmd680_taskfile_timing(hwif)) {
	switch(mode_wanted) {
		case 4:		speedt = 0x10c1; break;
		case 3:		speedt = 0x10C3; break;
		case 2:		speedt = 0x1104; break;
		case 1:		speedt = 0x2283; break;
		case 0:
		default:	speedt = 0x328A; break;
	}
	pci_write_config_word(dev, drive_pci, speedt);
}

static void config_cmd64x_chipset_for_pio (ide_drive_t *drive, byte set_speed)
{
	byte speed	= 0x00;
	byte set_pio	= ide_get_best_pio_mode(drive, 4, 5, NULL);

	cmd64x_tuneproc(drive, set_pio);
	speed = XFER_PIO_0 + set_pio;
	if (set_speed)
		(void) ide_config_drive_speed(drive, speed);
}

static void config_cmd680_chipset_for_pio (ide_drive_t *drive, byte set_speed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 unit			= (drive->select.b.unit & 0x01);
	u8 addr_mask		= (hwif->channel) ? 0x84 : 0x80;
	u8 speed		= 0x00;
	u8 mode_pci		= 0x00;
	u8 channel_timings	= cmd680_taskfile_timing(hwif);
	u8 set_pio		= ide_get_best_pio_mode(drive, 4, 5, NULL);

	pci_read_config_byte(dev, addr_mask, &mode_pci);
	mode_pci &= ~((unit) ? 0x30 : 0x03);

	/* WARNING PIO timing mess is going to happen b/w devices, argh */
	if ((channel_timings != set_pio) && (set_pio > channel_timings))
		set_pio = channel_timings;

	cmd680_tuneproc(drive, set_pio);
	speed = XFER_PIO_0 + set_pio;
	if (set_speed)
		(void) ide_config_drive_speed(drive, speed);
}

static void config_chipset_for_pio (ide_drive_t *drive, byte set_speed)
{
	switch(HWIF(drive)->pci_dev->device) {
		case PCI_DEVICE_ID_CMD_680:
			config_cmd680_chipset_for_pio(drive, set_speed);
			return;
		default:
			break;
	}
	config_cmd64x_chipset_for_pio(drive, set_speed);
}

static int cmd64x_tune_chipset (ide_drive_t *drive, byte xferspeed)
{
#ifdef CONFIG_BLK_DEV_IDEDMA
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;

	u8 unit			= (drive->select.b.unit & 0x01);
	u8 pciU			= (hwif->channel) ? UDIDETCR1 : UDIDETCR0;
	u8 pciD			= (hwif->channel) ? BMIDESR1 : BMIDESR0;
	u8 regU			= 0;
	u8 regD			= 0;
#endif /* CONFIG_BLK_DEV_IDEDMA */

	u8 speed		= cmd64x_ratefilter(drive, xferspeed);

#ifdef CONFIG_BLK_DEV_IDEDMA
	if ((drive->media != ide_disk) && (speed < XFER_SW_DMA_0))
		return 1;

	(void) pci_read_config_byte(dev, pciD, &regD);
	(void) pci_read_config_byte(dev, pciU, &regU);
	regD &= ~(unit ? 0x40 : 0x20);
	regU &= ~(unit ? 0xCA : 0x35);
	(void) pci_write_config_byte(dev, pciD, regD);
	(void) pci_write_config_byte(dev, pciU, regU);
	(void) pci_read_config_byte(dev, pciD, &regD);
	(void) pci_read_config_byte(dev, pciU, &regU);
#endif /* CONFIG_BLK_DEV_IDEDMA */

	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_UDMA_5:	regU |= (unit ? 0x0A : 0x05); break;
		case XFER_UDMA_4:	regU |= (unit ? 0x4A : 0x15); break;
		case XFER_UDMA_3:	regU |= (unit ? 0x8A : 0x25); break;
		case XFER_UDMA_2:	regU |= (unit ? 0x42 : 0x11); break;
		case XFER_UDMA_1:	regU |= (unit ? 0x82 : 0x21); break;
		case XFER_UDMA_0:	regU |= (unit ? 0xC2 : 0x31); break;
		case XFER_MW_DMA_2:	regD |= (unit ? 0x40 : 0x10); break;
		case XFER_MW_DMA_1:	regD |= (unit ? 0x80 : 0x20); break;
		case XFER_MW_DMA_0:	regD |= (unit ? 0xC0 : 0x30); break;
		case XFER_SW_DMA_2:	regD |= (unit ? 0x40 : 0x10); break;
		case XFER_SW_DMA_1:	regD |= (unit ? 0x80 : 0x20); break;
		case XFER_SW_DMA_0:	regD |= (unit ? 0xC0 : 0x30); break;
#endif /* CONFIG_BLK_DEV_IDEDMA */
		case XFER_PIO_4:	cmd64x_tuneproc(drive, 4); break;
		case XFER_PIO_3:	cmd64x_tuneproc(drive, 3); break;
		case XFER_PIO_2:	cmd64x_tuneproc(drive, 2); break;
		case XFER_PIO_1:	cmd64x_tuneproc(drive, 1); break;
		case XFER_PIO_0:	cmd64x_tuneproc(drive, 0); break;

		default:
			return 1;
	}

#ifdef CONFIG_BLK_DEV_IDEDMA
	(void) pci_write_config_byte(dev, pciU, regU);
	regD |= (unit ? 0x40 : 0x20);
	(void) pci_write_config_byte(dev, pciD, regD);
#endif /* CONFIG_BLK_DEV_IDEDMA */

	return (ide_config_drive_speed(drive, speed));
}

static int cmd680_tune_chipset (ide_drive_t *drive, byte xferspeed)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct pci_dev *dev	= hwif->pci_dev;
	u8 addr_mask		= (hwif->channel) ? 0x84 : 0x80;
	u8 unit			= (drive->select.b.unit & 0x01);
	u8 speed		= cmd64x_ratefilter(drive, xferspeed);
	u8 dma_pci		= 0;
	u8 udma_pci		= 0;
	u8 mode_pci		= 0;
	u8 scsc			= 0;
	u16 ultra		= 0;
	u16 multi		= 0;

        pci_read_config_byte(dev, addr_mask, &mode_pci);
	pci_read_config_byte(dev, 0x8A, &scsc);

        switch (drive->dn) {
		case 0: dma_pci = 0xA8; udma_pci = 0xAC; break;
		case 1: dma_pci = 0xAA; udma_pci = 0xAE; break;
		case 2: dma_pci = 0xB8; udma_pci = 0xBC; break;
		case 3: dma_pci = 0xBA; udma_pci = 0xBE; break;
		default: return 1;
	}

	pci_read_config_byte(dev, addr_mask, &mode_pci);
	mode_pci &= ~((unit) ? 0x30 : 0x03);
	pci_read_config_word(dev, dma_pci, &multi);
	pci_read_config_word(dev, udma_pci, &ultra);

	if ((speed == XFER_UDMA_6) && (scsc & 0x30) == 0x00) {
		pci_write_config_byte(dev, 0x8A, scsc|0x01);
		pci_read_config_byte(dev, 0x8A, &scsc);
#if 0
		/* if 133 clock fails, switch to 2xbus clock */
		if (!(scsc & 0x01))
			pci_write_config_byte(dev, 0x8A, scsc|0x10);
#endif
	}

	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA
		case XFER_UDMA_6:
			if ((scsc & 0x30) == 0x00)
				goto speed_break;
			multi = 0x10C1;
			ultra &= ~0x3F;
			ultra |= 0x01;
			break;
speed_break :
			speed = XFER_UDMA_5;
		case XFER_UDMA_5:
			multi = 0x10C1;
			ultra &= ~0x3F;
			ultra |= (((scsc & 0x30) == 0x00) ? 0x01 : 0x02);
			break;
		case XFER_UDMA_4:
			multi = 0x10C1;
			ultra &= ~0x3F;
			ultra |= (((scsc & 0x30) == 0x00) ? 0x02 : 0x03);
			break;
		case XFER_UDMA_3:
			multi = 0x10C1;
			ultra &= ~0x3F;
			ultra |= (((scsc & 0x30) == 0x00) ? 0x04 : 0x05);
			break;
		case XFER_UDMA_2:
			multi = 0x10C1;
			ultra &= ~0x3F;
			ultra |= (((scsc & 0x30) == 0x00) ? 0x05 : 0x07);
			break;
		case XFER_UDMA_1:
			multi = 0x10C1;
			ultra &= ~0x3F;
			ultra |= (((scsc & 0x30) == 0x00) ? 0x07 : 0x0B);
			break;
		case XFER_UDMA_0:
			multi = 0x10C1;
			ultra &= ~0x3F;
			ultra |= (((scsc & 0x30) == 0x00) ? 0x0C : 0x0F);
			break;
		case XFER_MW_DMA_2:
			multi = 0x10C1;
			break;
		case XFER_MW_DMA_1:
			multi = 0x10C2;
			break;
		case XFER_MW_DMA_0:
			multi = 0x2208;
			break;
#endif /* CONFIG_BLK_DEV_IDEDMA */
		case XFER_PIO_4:	cmd680_tuneproc(drive, 4); break;
		case XFER_PIO_3:	cmd680_tuneproc(drive, 3); break;
		case XFER_PIO_2:	cmd680_tuneproc(drive, 2); break;
		case XFER_PIO_1:	cmd680_tuneproc(drive, 1); break;
		case XFER_PIO_0:	cmd680_tuneproc(drive, 0); break;
		default:
			return 1;
	}
	
	if (speed >= XFER_MW_DMA_0) 
		config_cmd680_chipset_for_pio(drive, 0);

	if (speed >= XFER_UDMA_0)
		mode_pci |= ((unit) ? 0x30 : 0x03);
	else if (speed >= XFER_MW_DMA_0)
		mode_pci |= ((unit) ? 0x20 : 0x02);
	else
		mode_pci |= ((unit) ? 0x10 : 0x01);

	pci_write_config_byte(dev, addr_mask, mode_pci);
	pci_write_config_word(dev, dma_pci, multi);
	pci_write_config_word(dev, udma_pci, ultra);

	return (ide_config_drive_speed(drive, speed));
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int config_chipset_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	ide_hwif_t *hwif	= HWIF(drive);
	byte mode               = cmd64x_ratemask(drive);
	byte speed		= 0x00;
	byte set_pio		= 0x00;
	int rval;

	if (drive->media != ide_disk) {
		cmdprintk("CMD64X: drive->media != ide_disk at double check,"
			" inital check failed!!\n");
		return ((int) ide_dma_off);
	}

	switch(mode) {
		case 0x04:
			if (id->dma_ultra & 0x0040)
				{ speed = XFER_UDMA_6; break; }
		case 0x03:
			if (id->dma_ultra & 0x0020)
				{ speed = XFER_UDMA_5; break; }
		case 0x02:
			if (id->dma_ultra & 0x0010)
				{ speed = XFER_UDMA_4; break; }
			if (id->dma_ultra & 0x0008)
				{ speed = XFER_UDMA_3; break; }
		case 0x01:
			if (id->dma_ultra & 0x0004)
				{ speed = XFER_UDMA_2; break; }
			if (id->dma_ultra & 0x0002)
				{ speed = XFER_UDMA_1; break; }
			if (id->dma_ultra & 0x0001)
				{ speed = XFER_UDMA_0; break; }
		case 0x00:
			if (id->dma_mword & 0x0004)
				{ speed = XFER_MW_DMA_2; break; }
			if (id->dma_mword & 0x0002)
				{ speed = XFER_MW_DMA_1; break; }
			if (id->dma_mword & 0x0001)
				{ speed = XFER_MW_DMA_0; break; }
			if (id->dma_1word & 0x0004)
				{ speed = XFER_SW_DMA_2; break; }
			if (id->dma_1word & 0x0002)
				{ speed = XFER_SW_DMA_1; break; }
			if (id->dma_1word & 0x0001)
				{ speed = XFER_SW_DMA_0; break; }
		default:
			{ set_pio = 1; break; }
	}

	if (!drive->init_speed)
		drive->init_speed = speed;

	config_chipset_for_pio(drive, set_pio);

	if (set_pio)
		return ((int) ide_dma_off_quietly);

	if (hwif->speedproc(drive, speed))
		return ((int) ide_dma_off);

	rval = (int)(	((id->dma_ultra >> 14) & 3) ? ide_dma_on :
			((id->dma_ultra >> 11) & 7) ? ide_dma_on :
			((id->dma_ultra >> 8) & 7) ? ide_dma_on :
			((id->dma_mword >> 8) & 7) ? ide_dma_on :
			((id->dma_1word >> 8) & 7) ? ide_dma_on :
						     ide_dma_off_quietly);

	return rval;
}

static int cmd64x_config_drive_for_dma (ide_drive_t *drive)
{
	struct hd_driveid *id	= drive->id;
	ide_hwif_t *hwif	= HWIF(drive);
	ide_dma_action_t dma_func = ide_dma_on;

	if ((id != NULL) && ((id->capability & 1) != 0) &&
	    hwif->autodma && (drive->media == ide_disk)) {
		/* Consult the list of known "bad" drives */
		if (ide_dmaproc(ide_dma_bad_drive, drive)) {
			dma_func = ide_dma_off;
			goto fast_ata_pio;
		}
		dma_func = ide_dma_off_quietly;
		if ((id->field_valid & 4) && cmd64x_ratemask(drive)) {
			if (id->dma_ultra & 0x007F) {
				/* Force if Capable UltraDMA */
				dma_func = config_chipset_for_dma(drive);
				if ((id->field_valid & 2) &&
				    (dma_func != ide_dma_on))
					goto try_dma_modes;
			}
		} else if (id->field_valid & 2) {
try_dma_modes:
			if ((id->dma_mword & 0x0007) ||
			    (id->dma_1word & 0x0007)) {
				/* Force if Capable regular DMA modes */
				dma_func = config_chipset_for_dma(drive);
				if (dma_func != ide_dma_on)
					goto no_dma_set;
			}
		} else if (ide_dmaproc(ide_dma_good_drive, drive)) {
			if (id->eide_dma_time > 150) {
				goto no_dma_set;
			}
			/* Consult the list of known "good" drives */
			dma_func = config_chipset_for_dma(drive);
			if (dma_func != ide_dma_on)
				goto no_dma_set;
		} else {
			goto fast_ata_pio;
		}
	} else if ((id->capability & 8) || (id->field_valid & 2)) {
fast_ata_pio:
		dma_func = ide_dma_off_quietly;
no_dma_set:
		config_chipset_for_pio(drive, 1);
	}
	return HWIF(drive)->dmaproc(dma_func, drive);
}

static int cmd680_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch (func) {
		case ide_dma_check:
			return cmd64x_config_drive_for_dma(drive);
		default:
			break;
	}
	/* Other cases are done by generic IDE-DMA code. */
        return ide_dmaproc(func, drive);
}

static int cmd64x_alt_dma_status (struct pci_dev *dev)
{
	switch(dev->device) {
		case PCI_DEVICE_ID_CMD_648:
		case PCI_DEVICE_ID_CMD_649:
			return 1;
		default:
			break;
	}
	return 0;
}

static int cmd64x_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	byte dma_stat		= 0;
	byte dma_alt_stat	= 0;
	ide_hwif_t *hwif	= HWIF(drive);
	byte mask		= (hwif->channel) ? MRDMODE_INTR_CH1 : MRDMODE_INTR_CH0;
	unsigned long dma_base	= hwif->dma_base;
	struct pci_dev *dev	= hwif->pci_dev;
	byte alt_dma_stat	= cmd64x_alt_dma_status(dev);

	switch (func) {
		case ide_dma_check:
			return cmd64x_config_drive_for_dma(drive);
		case ide_dma_end: /* returns 1 on error, 0 otherwise */
			drive->waiting_for_dma = 0;
			/* stop DMA */
			OUT_BYTE(IN_BYTE(dma_base)&~1, dma_base);
			/* get DMA status */
			dma_stat = IN_BYTE(dma_base+2);
			/* clear the INTR & ERROR bits */
			OUT_BYTE(dma_stat|6, dma_base+2);
			if (alt_dma_stat) {
				byte dma_intr	= 0;
				byte dma_mask	= (hwif->channel) ? ARTTIM23_INTR_CH1 : CFR_INTR_CH0;
				byte dma_reg	= (hwif->channel) ? ARTTIM2 : CFR;
				(void) pci_read_config_byte(dev, dma_reg, &dma_intr);
				/* clear the INTR bit */
				(void) pci_write_config_byte(dev, dma_reg, dma_intr|dma_mask);
			}
			/* purge DMA mappings */
			ide_destroy_dmatable(drive);
			/* verify good DMA status */
			return (dma_stat & 7) != 4;
		case ide_dma_test_irq:	/* returns 1 if dma irq issued, 0 otherwise */
			dma_stat = IN_BYTE(dma_base+2);
			(void) pci_read_config_byte(dev, MRDMODE, &dma_alt_stat);
#ifdef DEBUG
			printk("%s: dma_stat: 0x%02x dma_alt_stat: "
				"0x%02x mask: 0x%02x\n", drive->name,
				dma_stat, dma_alt_stat, mask);
#endif
			if (!(dma_alt_stat & mask)) {
				return 0;
			}
			/* return 1 if INTR asserted */
			return (dma_stat & 4) == 4;
		default:
			break;
	}
	/* Other cases are done by generic IDE-DMA code. */
	return ide_dmaproc(func, drive);
}

/*
 * ASUS P55T2P4D with CMD646 chipset revision 0x01 requires the old
 * event order for DMA transfers.
 */
static int cmd646_1_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long dma_base = hwif->dma_base;
	byte dma_stat;

	switch (func) {
		case ide_dma_check:
			return cmd64x_config_drive_for_dma(drive);
		case ide_dma_end:
			drive->waiting_for_dma = 0;
			/* get DMA status */
			dma_stat = IN_BYTE(dma_base+2);
			/* stop DMA */
			OUT_BYTE(IN_BYTE(dma_base)&~1, dma_base);
			/* clear the INTR & ERROR bits */
			OUT_BYTE(dma_stat|6, dma_base+2);
			/* and free any DMA resources */
			ide_destroy_dmatable(drive);
			/* verify good DMA status */
			return (dma_stat & 7) != 4;
		default:
			break;
	}

	/* Other cases are done by generic IDE-DMA code. */
	return ide_dmaproc(func, drive);
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

static int cmd680_busproc (ide_drive_t * drive, int state)
{
#if 0
	ide_hwif_t *hwif	= HWIF(drive);
	u8 addr_mask		= (hwif->channel) ? 0xB0 : 0xA0;
	u32 stat_config		= 0;

        pci_read_config_dword(hwif->pci_dev, addr_mask, &stat_config);

	if (!hwif)
		return -EINVAL;

	switch (state) {
		case BUSSTATE_ON:
			hwif->drives[0].failures = 0;
			hwif->drives[1].failures = 0;
			break;
		case BUSSTATE_OFF:
			hwif->drives[0].failures = hwif->drives[0].max_failures + 1;
			hwif->drives[1].failures = hwif->drives[1].max_failures + 1;
			break;
		case BUSSTATE_TRISTATE:
			hwif->drives[0].failures = hwif->drives[0].max_failures + 1;
			hwif->drives[1].failures = hwif->drives[1].max_failures + 1;
			break;
		default:
			return 0;
	}
	hwif->bus_state = state;
#endif
	return 0;
}

void cmd680_reset (ide_drive_t *drive)
{
#if 0
	ide_hwif_t *hwif	= HWIF(drive);
	u8 addr_mask		= (hwif->channel) ? 0xB0 : 0xA0;
	byte reset		= 0;

	pci_read_config_byte(hwif->pci_dev, addr_mask, &reset);
	pci_write_config_byte(hwif->pci_dev, addr_mask, reset|0x03);
#endif
}

unsigned int cmd680_pci_init (struct pci_dev *dev, const char *name)
{
	u8 tmpbyte	= 0;	
	pci_write_config_byte(dev, 0x80, 0x00);
	pci_write_config_byte(dev, 0x84, 0x00);
	pci_read_config_byte(dev, 0x8A, &tmpbyte);
	pci_write_config_byte(dev, 0x8A, tmpbyte|0x01);
#if 0
	/* if 133 clock fails, switch to 2xbus clock */	
	if (!(tmpbyte & 0x01)) {
		pci_read_config_byte(dev, 0x8A, &tmpbyte);
		pci_write_config_byte(dev, 0x8A, tmpbyte|0x10);		
	}
#endif
	pci_write_config_word(dev, 0xA2, 0x328A);
	pci_write_config_dword(dev, 0xA4, 0x328A);
	pci_write_config_dword(dev, 0xA8, 0x4392);
	pci_write_config_dword(dev, 0xAC, 0x4009);
	pci_write_config_word(dev, 0xB2, 0x328A);
	pci_write_config_dword(dev, 0xB4, 0x328A);
	pci_write_config_dword(dev, 0xB8, 0x4392);
	pci_write_config_dword(dev, 0xBC, 0x4009);

	cmd_devs[n_cmd_devs++] = dev;

#if defined(DISPLAY_CMD64X_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!cmd64x_proc) {
		cmd64x_proc = 1;
		cmd64x_display_info = &cmd64x_get_info;
	}
#endif /* DISPLAY_CMD64X_TIMINGS && CONFIG_PROC_FS */

	return 0;
}

unsigned int cmd64x_pci_init (struct pci_dev *dev, const char *name)
{
	unsigned char mrdmode;
	unsigned int class_rev;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

#ifdef __i386__
	if (dev->resource[PCI_ROM_RESOURCE].start) {
		pci_write_config_byte(dev, PCI_ROM_ADDRESS, dev->resource[PCI_ROM_RESOURCE].start | PCI_ROM_ADDRESS_ENABLE);
		printk("%s: ROM enabled at 0x%08lx\n", name, dev->resource[PCI_ROM_RESOURCE].start);
	}
#endif

	switch(dev->device) {
		case PCI_DEVICE_ID_CMD_643:
			break;
		case PCI_DEVICE_ID_CMD_646:
			printk("%s: chipset revision 0x%02X, ", name, class_rev);
			switch(class_rev) {
				case 0x07:
				case 0x05:
					printk("UltraDMA Capable");
					break;
				case 0x03:
					printk("MultiWord DMA Force Limited");
					break;
				case 0x01:
				default:
					printk("MultiWord DMA Limited, IRQ workaround enabled");
					break;
				}
			printk("\n");
                        break;
		case PCI_DEVICE_ID_CMD_648:
		case PCI_DEVICE_ID_CMD_649:
			break;
		default:
			break;
	}

	/* Set a good latency timer and cache line size value. */
	(void) pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
#ifdef __sparc_v9__
	(void) pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 0x10);
#endif


	/* Setup interrupts. */
	(void) pci_read_config_byte(dev, MRDMODE, &mrdmode);
	mrdmode &= ~(0x30);
	(void) pci_write_config_byte(dev, MRDMODE, mrdmode);

	/* Use MEMORY READ LINE for reads.
	 * NOTE: Although not mentioned in the PCI0646U specs,
	 *       these bits are write only and won't be read
	 *       back as set or not.  The PCI0646U2 specs clarify
	 *       this point.
	 */
	(void) pci_write_config_byte(dev, MRDMODE, mrdmode | 0x02);

	/* Set reasonable active/recovery/address-setup values. */
	(void) pci_write_config_byte(dev, ARTTIM0,  0x40);
	(void) pci_write_config_byte(dev, DRWTIM0,  0x3f);
	(void) pci_write_config_byte(dev, ARTTIM1,  0x40);
	(void) pci_write_config_byte(dev, DRWTIM1,  0x3f);
#ifdef __i386__
	(void) pci_write_config_byte(dev, ARTTIM23, 0x1c);
#else
	(void) pci_write_config_byte(dev, ARTTIM23, 0x5c);
#endif
	(void) pci_write_config_byte(dev, DRWTIM23, 0x3f);
	(void) pci_write_config_byte(dev, DRWTIM3,  0x3f);
#ifdef CONFIG_PPC
	(void) pci_write_config_byte(dev, UDIDETCR0, 0xf0);
#endif /* CONFIG_PPC */

	cmd_devs[n_cmd_devs++] = dev;

#if defined(DISPLAY_CMD64X_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!cmd64x_proc) {
		cmd64x_proc = 1;
		cmd64x_display_info = &cmd64x_get_info;
	}
#endif /* DISPLAY_CMD64X_TIMINGS && CONFIG_PROC_FS */

	return 0;
}

unsigned int __init pci_init_cmd64x (struct pci_dev *dev, const char *name)
{
	switch(dev->device) {
		case PCI_DEVICE_ID_CMD_680:
			return cmd680_pci_init (dev, name);
		default:
			break;
	}
	return cmd64x_pci_init (dev, name);
}

unsigned int cmd680_ata66 (ide_hwif_t *hwif)
{
	byte ata66	= 0;
	byte addr_mask	= (hwif->channel) ? 0xB0 : 0xA0;

	pci_read_config_byte(hwif->pci_dev, addr_mask, &ata66);
	return (ata66 & 0x01) ? 1 : 0;
}

unsigned int cmd64x_ata66 (ide_hwif_t *hwif)
{
	byte ata66 = 0;
	byte mask = (hwif->channel) ? 0x02 : 0x01;

	pci_read_config_byte(hwif->pci_dev, BMIDECSR, &ata66);
	return (ata66 & mask) ? 1 : 0;
}

unsigned int __init ata66_cmd64x (ide_hwif_t *hwif)
{
	switch(hwif->pci_dev->device) {
		case PCI_DEVICE_ID_CMD_680:
			return cmd680_ata66(hwif);
		default:
			break;
	}
	return cmd64x_ata66(hwif);
}

void __init ide_init_cmd64x (ide_hwif_t *hwif)
{
	struct pci_dev *dev	= hwif->pci_dev;
	unsigned int class_rev;

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class_rev);
	class_rev &= 0xff;

	hwif->drives[0].autotune = 1;
	hwif->drives[1].autotune = 1;

	switch(dev->device) {
		case PCI_DEVICE_ID_CMD_680:
			hwif->busproc	= &cmd680_busproc;
#ifdef CONFIG_BLK_DEV_IDEDMA
			if (hwif->dma_base)
				hwif->dmaproc	= &cmd680_dmaproc;
#endif /* CONFIG_BLK_DEV_IDEDMA */
			hwif->resetproc = &cmd680_reset;
			hwif->speedproc	= &cmd680_tune_chipset;
			hwif->tuneproc	= &cmd680_tuneproc;
			break;
		case PCI_DEVICE_ID_CMD_649:
		case PCI_DEVICE_ID_CMD_648:
		case PCI_DEVICE_ID_CMD_643:
#ifdef CONFIG_BLK_DEV_IDEDMA
			if (hwif->dma_base)
				hwif->dmaproc	= &cmd64x_dmaproc;
#endif /* CONFIG_BLK_DEV_IDEDMA */
			hwif->tuneproc	= &cmd64x_tuneproc;
			hwif->speedproc = &cmd64x_tune_chipset;
			break;
		case PCI_DEVICE_ID_CMD_646:
			hwif->chipset = ide_cmd646;
#ifdef CONFIG_BLK_DEV_IDEDMA
			if (hwif->dma_base) {
				if (class_rev == 0x01)
					hwif->dmaproc = &cmd646_1_dmaproc;
				else
					hwif->dmaproc = &cmd64x_dmaproc;
			}
#endif /* CONFIG_BLK_DEV_IDEDMA */
			hwif->tuneproc	= &cmd64x_tuneproc;
			hwif->speedproc	= &cmd64x_tune_chipset;
			break;
		default:
			break;
	}

#if defined(CONFIG_BLK_DEV_IDEDMA) && defined(CONFIG_IDEDMA_AUTO)
	if (hwif->dma_base)
		if (!noautodma)
			hwif->autodma = 1;
#endif /* CONFIG_BLK_DEV_IDEDMA && CONFIG_IDEDMA_AUTO*/
}
