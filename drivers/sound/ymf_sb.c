/*
  Legacy audio driver for YMF724, 740, 744, 754 series.
  Copyright 2000 Daisuke Nagano <breeze.nagano@nifty.ne.jp>

  Based on the VIA 82Cxxx driver by Jeff Garzik <jgarzik@pobox.com>
  And ported to 2.3.x by Jeff Garzik too :)  My it is a small world.

  Distribued under the GNU PUBLIC LICENSE (GPL) Version 2.
  See the "COPYING" file distributed with kernel source tree for more info.

  -------------------------------------------------------------------------

  It only supports SBPro compatible function of YMF7xx series s.t.
    * 22.05kHz, 8-bit and stereo sample
    * OPL3-compatible FM synthesizer
    * MPU-401 compatible "external" MIDI interface

  -------------------------------------------------------------------------

  Revision history

   Tue May 14 19:00:00 2000   0.0.1
   * initial release

   Tue May 16 19:29:29 2000   0.0.2

   * add a little delays for reset devices.
   * fixed addressing bug.

   Sun May 21 15:14:37 2000   0.0.3

   * Add 'master_vol' module parameter to change 'PCM out Vol' of AC'97.
   * remove native UART401 support. External MIDI port should be supported 
     by sb_midi driver.
   * add support for SPDIF OUT. Module parameter 'spdif_out' is now available.

   Wed May 31 00:13:57 2000   0.0.4

   * remove entries in Hwmcode.h. Now YMF744 / YMF754 sets instructions 
     in 724hwmcode.h.
   * fixed wrong legacy_io setting on YMF744/YMF754 .

   Thu Sep 21 05:32:51 BRT 2000 0.0.5
   * got rid of attach_uart401 and attach_sbmpu
     Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ac97_codec.h>

#include <asm/io.h>

#include "sound_config.h"
#include "sb.h"

#include "724hwmcode.h"

#undef YMF_DEBUG
#define SUPPORT_UART401_MIDI 1

/* ---------------------------------------------------------------------- */

#ifndef PCI_VENDOR_ID_YAMAHA
#define PCI_VENDOR_ID_YAMAHA  0x1073
#endif
#ifndef PCI_DEVICE_ID_YMF724
#define PCI_DEVICE_ID_YMF724  0x0004
#endif
#ifndef PCI_DEVICE_ID_YMF740
#define PCI_DEVICE_ID_YMF740  0x000A
#endif
#ifndef PCI_DEVICE_ID_YMF740C
#define PCI_DEVICE_ID_YMF740C 0x000C
#endif
#ifndef PCI_DEVICE_ID_YMF724F
#define PCI_DEVICE_ID_YMF724F 0x000D
#endif
#ifndef PCI_DEVICE_ID_YMF744
#define PCI_DEVICE_ID_YMF744  0x0010
#endif
#ifndef PCI_DEVICE_ID_YMF754
#define PCI_DEVICE_ID_YMF754  0x0012
#endif

/* ---------------------------------------------------------------------- */

#define YMFSB_RESET_DELAY               5

#define YMFSB_REGSIZE                   0x8000

#define YMFSB_AC97TIMEOUT               2000

#define	YMFSB_WORKBITTIMEOUT            250000

#define	YMFSB_DSPLENGTH                 0x0080
#define	YMFSB_CTRLLENGTH                0x3000

#define YMFSB_PCIR_VENDORID             0x00
#define YMFSB_PCIR_DEVICEID             0x02
#define YMFSB_PCIR_CMD                  0x04
#define YMFSB_PCIR_REVISIONID           0x08
#define YMFSB_PCIR_BASEADDR             0x10
#define YMFSB_PCIR_IRQ                  0x3c

#define	YMFSB_PCIR_LEGCTRL              0x40
#define	YMFSB_PCIR_ELEGCTRL             0x42
#define	YMFSB_PCIR_DSXGCTRL             0x48
#define YMFSB_PCIR_OPLADR               0x60
#define YMFSB_PCIR_SBADR                0x62
#define YMFSB_PCIR_MPUADR               0x64

#define	YMFSB_INTFLAG                   0x0004
#define	YMFSB_ACTIVITY                  0x0006
#define	YMFSB_GLOBALCTRL                0x0008
#define	YMFSB_ZVCTRL                    0x000A
#define	YMFSB_TIMERCTRL                 0x0010
#define	YMFSB_TIMERCOUNT                0x0012
#define	YMFSB_SPDIFOUTCTRL              0x0018
#define	YMFSB_SPDIFOUTSTATUS            0x001C
#define	YMFSB_EEPROMCTRL                0x0020
#define	YMFSB_SPDIFINCTRL               0x0034
#define	YMFSB_SPDIFINSTATUS             0x0038
#define	YMFSB_DSPPROGRAMDL              0x0048
#define	YMFSB_DLCNTRL                   0x004C
#define	YMFSB_GPIOININTFLAG             0x0050
#define	YMFSB_GPIOININTENABLE           0x0052
#define	YMFSB_GPIOINSTATUS              0x0054
#define	YMFSB_GPIOOUTCTRL               0x0056
#define	YMFSB_GPIOFUNCENABLE            0x0058
#define	YMFSB_GPIOTYPECONFIG            0x005A
#define	YMFSB_AC97CMDDATA               0x0060
#define	YMFSB_AC97CMDADR                0x0062
#define	YMFSB_PRISTATUSDATA             0x0064
#define	YMFSB_PRISTATUSADR              0x0066
#define	YMFSB_SECSTATUSDATA             0x0068
#define	YMFSB_SECSTATUSADR              0x006A
#define	YMFSB_SECCONFIG                 0x0070
#define	YMFSB_LEGACYOUTVOL              0x0080
#define	YMFSB_LEGACYOUTVOLL             0x0080
#define	YMFSB_LEGACYOUTVOLR             0x0082
#define	YMFSB_NATIVEDACOUTVOL           0x0084
#define	YMFSB_NATIVEDACOUTVOLL          0x0084
#define	YMFSB_NATIVEDACOUTVOLR          0x0086
#define	YMFSB_SPDIFOUTVOL               0x0088
#define	YMFSB_SPDIFOUTVOLL              0x0088
#define	YMFSB_SPDIFOUTVOLR              0x008A
#define	YMFSB_AC3OUTVOL                 0x008C
#define	YMFSB_AC3OUTVOLL                0x008C
#define	YMFSB_AC3OUTVOLR                0x008E
#define	YMFSB_PRIADCOUTVOL              0x0090
#define	YMFSB_PRIADCOUTVOLL             0x0090
#define	YMFSB_PRIADCOUTVOLR             0x0092
#define	YMFSB_LEGACYLOOPVOL             0x0094
#define	YMFSB_LEGACYLOOPVOLL            0x0094
#define	YMFSB_LEGACYLOOPVOLR            0x0096
#define	YMFSB_NATIVEDACLOOPVOL          0x0098
#define	YMFSB_NATIVEDACLOOPVOLL         0x0098
#define	YMFSB_NATIVEDACLOOPVOLR         0x009A
#define	YMFSB_SPDIFLOOPVOL              0x009C
#define	YMFSB_SPDIFLOOPVOLL             0x009E
#define	YMFSB_SPDIFLOOPVOLR             0x009E
#define	YMFSB_AC3LOOPVOL                0x00A0
#define	YMFSB_AC3LOOPVOLL               0x00A0
#define	YMFSB_AC3LOOPVOLR               0x00A2
#define	YMFSB_PRIADCLOOPVOL             0x00A4
#define	YMFSB_PRIADCLOOPVOLL            0x00A4
#define	YMFSB_PRIADCLOOPVOLR            0x00A6
#define	YMFSB_NATIVEADCINVOL            0x00A8
#define	YMFSB_NATIVEADCINVOLL           0x00A8
#define	YMFSB_NATIVEADCINVOLR           0x00AA
#define	YMFSB_NATIVEDACINVOL            0x00AC
#define	YMFSB_NATIVEDACINVOLL           0x00AC
#define	YMFSB_NATIVEDACINVOLR           0x00AE
#define	YMFSB_BUF441OUTVOL              0x00B0
#define	YMFSB_BUF441OUTVOLL             0x00B0
#define	YMFSB_BUF441OUTVOLR             0x00B2
#define	YMFSB_BUF441LOOPVOL             0x00B4
#define	YMFSB_BUF441LOOPVOLL            0x00B4
#define	YMFSB_BUF441LOOPVOLR            0x00B6
#define	YMFSB_SPDIFOUTVOL2              0x00B8
#define	YMFSB_SPDIFOUTVOL2L             0x00B8
#define	YMFSB_SPDIFOUTVOL2R             0x00BA
#define	YMFSB_SPDIFLOOPVOL2             0x00BC
#define	YMFSB_SPDIFLOOPVOL2L            0x00BC
#define	YMFSB_SPDIFLOOPVOL2R            0x00BE
#define	YMFSB_ADCSLOTSR                 0x00C0
#define	YMFSB_RECSLOTSR                 0x00C4
#define	YMFSB_ADCFORMAT                 0x00C8
#define	YMFSB_RECFORMAT                 0x00CC
#define	YMFSB_P44SLOTSR                 0x00D0
#define	YMFSB_STATUS                    0x0100
#define	YMFSB_CTRLSELECT                0x0104
#define	YMFSB_MODE                      0x0108
#define	YMFSB_SAMPLECOUNT               0x010C
#define	YMFSB_NUMOFSAMPLES              0x0110
#define	YMFSB_CONFIG                    0x0114
#define	YMFSB_PLAYCTRLSIZE              0x0140
#define	YMFSB_RECCTRLSIZE               0x0144
#define	YMFSB_EFFCTRLSIZE               0x0148
#define	YMFSB_WORKSIZE                  0x014C
#define	YMFSB_MAPOFREC                  0x0150
#define	YMFSB_MAPOFEFFECT               0x0154
#define	YMFSB_PLAYCTRLBASE              0x0158
#define	YMFSB_RECCTRLBASE               0x015C
#define	YMFSB_EFFCTRLBASE               0x0160
#define	YMFSB_WORKBASE                  0x0164
#define	YMFSB_DSPINSTRAM                0x1000
#define	YMFSB_CTRLINSTRAM               0x4000


/* ---------------------------------------------------------------------- */

#define MAX_CARDS	4

#define PFX		"ymf_sb: "

#define YMFSB_VERSION	"0.0.6"
#define YMFSB_CARD_NAME	"YMF7xx Legacy Audio driver " YMFSB_VERSION

#ifdef SUPPORT_UART401_MIDI
#if 0
# define ymf7xxsb_probe_midi probe_uart401
# define ymf7xxsb_unload_midi unload_uart401
#else
# define ymf7xxsb_probe_midi probe_sbmpu
# define ymf7xxsb_unload_midi unload_sbmpu
#endif
#endif

/* ---------------------------------------------------------------------- */

static struct address_info	sb_data[MAX_CARDS];
static struct address_info	opl3_data[MAX_CARDS];
#ifdef SUPPORT_UART401_MIDI
static struct address_info	mpu_data[MAX_CARDS];
#endif
static unsigned			cards = 0;
static unsigned short          *ymfbase[MAX_CARDS];

/* ---------------------------------------------------------------------- */

#ifdef MODULE
#ifdef SUPPORT_UART401_MIDI
static int mpu_io   = 0;
#endif
static int synth_io = 0;
static int io       = 0;
static int dma      = 0;
static int master_vol = -1;
static int spdif_out = 0;
#ifdef SUPPORT_UART401_MIDI
MODULE_PARM(mpu_io, "i");
#endif
MODULE_PARM(synth_io, "i");
MODULE_PARM(io,"i");
MODULE_PARM(dma,"i");
MODULE_PARM(master_vol,"i");
MODULE_PARM(spdif_out,"i");
#else
#ifdef SUPPORT_UART401_MIDI
static int mpu_io     = 0x330;
#endif
static int synth_io   = 0x388;
static int io         = 0x220;
static int dma        = 1;
static int master_vol = 80;
static int spdif_out  = 0;
#endif

/* ---------------------------------------------------------------------- */

static int readRegWord( int adr ) {

	if (ymfbase[cards]==NULL) return 0;

	return readw(ymfbase[cards]+adr/2);
}

static void writeRegWord( int adr, int val ) {

	if (ymfbase[cards]==NULL) return;

	writew((unsigned short)(val&0xffff), ymfbase[cards] + adr/2);

	return;
}

static int readRegDWord( int adr ) {

	if (ymfbase[cards]==NULL) return 0;

	return (readl(ymfbase[cards]+adr/2));
}

static void writeRegDWord( int adr, int val ) {

	if (ymfbase[cards]==NULL) return;

	writel((unsigned int)(val&0xffffffff), ymfbase[cards]+adr/2);

	return;
}

/* ---------------------------------------------------------------------- */

static int checkPrimaryBusy( void )
{
	int timeout=0;

	while ( timeout++ < YMFSB_AC97TIMEOUT )
	{
		if ( (readRegWord(YMFSB_PRISTATUSADR) & 0x8000) == 0x0000 )
			return 0;
	}
	return -1;
}

static int writeAc97( int adr, unsigned short val )
{

	if ( adr > 0x7f || adr < 0x00 ) return -1;

	if ( checkPrimaryBusy() ) return -1;

#ifdef YMF_DEBUG
	printk(KERN_INFO PFX "AC97 0x%0x = 0x%0x\n",adr,val);
#endif

	writeRegWord( YMFSB_AC97CMDADR,  0x0000 | adr );
	writeRegWord( YMFSB_AC97CMDDATA, val );

	return 0;
}

static int __init checkCodec( struct pci_dev *pcidev )
{
	u8 tmp8;

	pci_read_config_byte(pcidev, YMFSB_PCIR_DSXGCTRL, &tmp8);
	if ( tmp8 & 0x03 ) {
		pci_write_config_byte(pcidev, YMFSB_PCIR_DSXGCTRL, tmp8&0xfc);
		mdelay(YMFSB_RESET_DELAY);
		pci_write_config_byte(pcidev, YMFSB_PCIR_DSXGCTRL, tmp8|0x03);
		mdelay(YMFSB_RESET_DELAY);
		pci_write_config_byte(pcidev, YMFSB_PCIR_DSXGCTRL, tmp8&0xfc);
		mdelay(YMFSB_RESET_DELAY);
	}

	if ( checkPrimaryBusy() ) return -1;

	return 0;
}

static int __init setupLegacyIO( struct pci_dev *pcidev )
{
	int v;
	int sbio=0,mpuio=0,oplio=0,dma=0;

	switch(sb_data[cards].io_base) {
	case 0x220:
		sbio = 0;
		break;
	case 0x240:
		sbio = 1;
		break;
	case 0x260:
		sbio = 2;
		break;
	case 0x280:
		sbio = 3;
		break;
	default:
		return -1;
		break;
	}
#ifdef YMF_DEBUG
	printk(PFX "set SBPro I/O at 0x%x\n",sb_data[cards].io_base);
#endif

#ifdef SUPPORT_UART401_MIDI
	switch(mpu_data[cards].io_base) {
	case 0x330:
		mpuio = 0;
		break;
	case 0x300:
		mpuio = 1;
		break;
	case 0x332:
		mpuio = 2;
		break;
	case 0x334:
		mpuio = 3;
		break;
	default:
		mpuio = 0;
		break;
	}
# ifdef YMF_DEBUG
	printk(PFX "set MPU401 I/O at 0x%x\n",mpu_data[cards].io_base);
# endif
#endif

	switch(opl3_data[cards].io_base) {
	case 0x388:
		oplio = 0;
		break;
	case 0x398:
		oplio = 1;
		break;
	case 0x3a0:
		oplio = 2;
		break;
	case 0x3a8:
		oplio = 3;
		break;
	default:
		return -1;
		break;
	}
#ifdef YMF_DEBUG
	printk(PFX "set OPL3 I/O at 0x%x\n",opl3_data[cards].io_base);
#endif

	dma = sb_data[cards].dma;
#ifdef YMF_DEBUG
	printk(PFX "set DMA address at 0x%x\n",sb_data[cards].dma);
#endif

	v = 0x0000 | ((dma & 0x03) << 6) | 0x003f;
	pci_write_config_word(pcidev, YMFSB_PCIR_LEGCTRL, v);
#ifdef YMF_DEBUG
	printk(PFX "LEGCTRL: 0x%x\n",v);
#endif
	switch( pcidev->device ) {
	case PCI_DEVICE_ID_YMF724:
	case PCI_DEVICE_ID_YMF740:
	case PCI_DEVICE_ID_YMF724F:
	case PCI_DEVICE_ID_YMF740C:
		v = 0x8800 | ((mpuio & 0x03) << 4)
			   | ((sbio & 0x03) << 2)
			   | (oplio & 0x03);
		pci_write_config_word(pcidev, YMFSB_PCIR_ELEGCTRL, v);
#ifdef YMF_DEBUG
		printk(PFX "ELEGCTRL: 0x%x\n",v);
#endif
		break;

	case PCI_DEVICE_ID_YMF744:
	case PCI_DEVICE_ID_YMF754:
		v = 0x8800;
		pci_write_config_word(pcidev, YMFSB_PCIR_ELEGCTRL, v);
#ifdef YMF_DEBUG
		printk(PFX "ELEGCTRL: 0x%x\n",v);
#endif
		pci_write_config_word(pcidev, YMFSB_PCIR_OPLADR, opl3_data[cards].io_base);
		pci_write_config_word(pcidev, YMFSB_PCIR_SBADR,  sb_data[cards].
io_base);
#ifdef SUPPORT_UART401_MIDI
		pci_write_config_word(pcidev, YMFSB_PCIR_MPUADR, mpu_data[cards].io_base);
#endif
		break;

	default:
		printk(KERN_ERR PFX "Invalid device ID: %d\n",pcidev->device);
		return -1;
		break;
	}

	return 0;
}

/* ---------------------------------------------------------------------- */

static void enableDSP( void )
{
	writeRegDWord( YMFSB_CONFIG, 0x00000001 );
	return;
}

static void disableDSP( void )
{
	int val;
	int i;

	val = readRegDWord( YMFSB_CONFIG );
	if ( val ) {
		writeRegDWord( YMFSB_CONFIG, 0 );
	}

	i=0;
	while( ++i < YMFSB_WORKBITTIMEOUT ) {
		val = readRegDWord(YMFSB_STATUS);
		if ( (val & 0x00000002) == 0x00000000 ) break;
	}

	return;
}

static int __init setupInstruction( struct pci_dev *pcidev )
{
	int i;
	int val;

	writeRegDWord( YMFSB_NATIVEDACOUTVOL, 0 ); /* mute dac */
	disableDSP();

	writeRegDWord( YMFSB_MODE, 0x00010000 );

	/* DS-XG Software Reset */
	writeRegDWord( YMFSB_MODE,         0x00000000 );
	writeRegDWord( YMFSB_MAPOFREC,     0x00000000 );
	writeRegDWord( YMFSB_MAPOFEFFECT,  0x00000000 );
	writeRegDWord( YMFSB_PLAYCTRLBASE, 0x00000000 );
	writeRegDWord( YMFSB_RECCTRLBASE,  0x00000000 );
	writeRegDWord( YMFSB_EFFCTRLBASE,  0x00000000 );

	val = readRegWord( YMFSB_GLOBALCTRL );
	writeRegWord( YMFSB_GLOBALCTRL, (val&~0x0007) );

	/* setup DSP instruction code */
	for ( i=0 ; i<YMFSB_DSPLENGTH ; i+=4 ) {
	  writeRegDWord( YMFSB_DSPINSTRAM+i, DspInst[i>>2] );
	}

	switch( pcidev->device ) {
	case PCI_DEVICE_ID_YMF724:
	case PCI_DEVICE_ID_YMF740:
		/* setup Control instruction code */
		for ( i=0 ; i<YMFSB_CTRLLENGTH ; i+=4 ) {
			writeRegDWord( YMFSB_CTRLINSTRAM+i, CntrlInst[i>>2] );
		}
		break;

	case PCI_DEVICE_ID_YMF724F:
	case PCI_DEVICE_ID_YMF740C:
	case PCI_DEVICE_ID_YMF744:
	case PCI_DEVICE_ID_YMF754:
		/* setup Control instruction code */
		for ( i=0 ; i<YMFSB_CTRLLENGTH ; i+=4 ) {
			writeRegDWord( YMFSB_CTRLINSTRAM+i, CntrlInst1E[i>>2] );
		}
		break;

	default:
		return -1;
	}

	enableDSP();

	return 0;
}

/* ---------------------------------------------------------------------- */

static int __init ymf7xx_init(struct pci_dev *pcidev)
{
	unsigned short v;

	/* Read hardware information */
#ifdef YMF_DEBUG
	unsigned int   dv;
	pci_read_config_word(pcidev, YMFSB_PCIR_VENDORID, &v);
	printk(KERN_INFO PFX "Vendor ID = 0x%x\n",v);
	pci_read_config_word(pcidev, YMFSB_PCIR_DEVICEID, &v);
	printk(KERN_INFO PFX "Device ID = 0x%x\n",v);
	pci_read_config_word(pcidev, YMFSB_PCIR_REVISIONID, &v);
	printk(KERN_INFO PFX "Revision ID = 0x%x\n",v&0xff);
	pci_read_config_dword(pcidev, YMFSB_PCIR_BASEADDR, &dv);
	printk(KERN_INFO PFX "Base address = 0x%x\n",dv);
	pci_read_config_word(pcidev, YMFSB_PCIR_IRQ, &v);
	printk(KERN_INFO PFX "IRQ line = 0x%x\n",v&0xff);
#endif

	/* enables memory space access / bus mastering */
	pci_read_config_word(pcidev, YMFSB_PCIR_CMD, &v);
	pci_write_config_word(pcidev, YMFSB_PCIR_CMD, v|0x06);

	/* check codec */
#ifdef YMF_DEBUG
	printk(KERN_INFO PFX "check codec...\n");
#endif
	if (checkCodec(pcidev)) return -1;

	/* setup legacy I/O */
#ifdef YMF_DEBUG
	printk(KERN_INFO PFX "setup legacy I/O...\n");
#endif
	if (setupLegacyIO(pcidev)) return -1;
	
	/* setup instruction code */	
#ifdef YMF_DEBUG
	printk(KERN_INFO PFX "setup instructions...\n");
#endif
	if (setupInstruction(pcidev)) return -1;

	/* AC'97 setup */	
#ifdef YMF_DEBUG
	printk(KERN_INFO PFX "setup AC'97...\n");
#endif
	if ( writeAc97(AC97_RESET            ,0x0000) )  /* Reset */
		return -1;
	if ( writeAc97(AC97_MASTER_VOL_STEREO,0x0000) )  /* Master Volume */
		return -1;

	v = 31*(100-master_vol)/100;
	v = (v<<8 | v)&0x7fff;
	if ( writeAc97(AC97_PCMOUT_VOL       ,v     ) )  /* PCM out Volume */
		return -1;

#ifdef YMF_DEBUG
	printk(KERN_INFO PFX "setup Legacy Volume...\n");
#endif
	/* Legacy Audio Output Volume L & R ch */
	writeRegDWord( YMFSB_LEGACYOUTVOL, 0x3fff3fff );

#ifdef YMF_DEBUG
	printk(KERN_INFO PFX "setup SPDIF output control...\n");
#endif
	/* SPDIF Output control */
	v = spdif_out != 0 ? 0x0001 : 0x0000;
	writeRegWord( YMFSB_SPDIFOUTCTRL, v );
	/* no copyright protection, 
	   sample-rate converted,
	   re-recorded software comercially available (the 1st generation),
	   original */
	writeRegWord( YMFSB_SPDIFOUTSTATUS, 0x9a04 );

	return 0;
}

/* ---------------------------------------------------------------------- */

static void __init ymf7xxsb_attach_sb(struct address_info *hw_config)
{
	if(!sb_dsp_init(hw_config, THIS_MODULE))
		hw_config->slots[0] = -1;
}

static int __init ymf7xxsb_probe_sb(struct address_info *hw_config)
{
	if (check_region(hw_config->io_base, 16))
	{
		printk(KERN_DEBUG PFX "SBPro port 0x%x is already in use\n",
		       hw_config->io_base);
		return 0;
	}
	return sb_dsp_detect(hw_config, SB_PCI_YAMAHA, 0, NULL);
}


static void ymf7xxsb_unload_sb(struct address_info *hw_config, int unload_mpu)
{
	if(hw_config->slots[0]!=-1)
		sb_dsp_unload(hw_config, unload_mpu);
}

/* ---------------------------------------------------------------------- */

enum chip_types {
	CH_YMF724 = 0,
	CH_YMF724F,
	CH_YMF740,
	CH_YMF740C,
	CH_YMF744,
	CH_YMF754,
};

/* directly indexed by chip_types enum above */
/* note we keep this a struct to ease adding
 * other per-board or per-chip info here */
struct {
	const char           *devicename;
} devicetable[] __initdata = 
{
	{ "YMF724A-E" },
	{ "YMF724F" },
	{ "YMF740A-B" },
	{ "YMF740C" },
	{ "YMF744" },
	{ "YMF754" },
};

static struct pci_device_id ymf7xxsb_pci_tbl[] __initdata = {
	{ PCI_VENDOR_ID_YAMAHA, PCI_DEVICE_ID_YMF724,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_YMF724 },
	{ PCI_VENDOR_ID_YAMAHA, PCI_DEVICE_ID_YMF724F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_YMF724F },
	{ PCI_VENDOR_ID_YAMAHA, PCI_DEVICE_ID_YMF740,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_YMF740 },
	{ PCI_VENDOR_ID_YAMAHA, PCI_DEVICE_ID_YMF740C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_YMF740C },
	{ PCI_VENDOR_ID_YAMAHA, PCI_DEVICE_ID_YMF744,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_YMF744 },
	{ PCI_VENDOR_ID_YAMAHA, PCI_DEVICE_ID_YMF754,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_YMF754 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ymf7xxsb_pci_tbl);

static int __init ymf7xxsb_init_one (struct pci_dev *pcidev, const struct pci_device_id *ent)
{

	const char	*devicename;
	unsigned long   iobase;

	if (cards == MAX_CARDS) {
	  	printk (KERN_DEBUG PFX "maximum number of cards reached\n");
		return -ENODEV;
	}

	if ( pcidev->irq == 0 ) return -ENODEV;
	iobase = pci_resource_start (pcidev, 0);
	if ( iobase == 0x00000000 ) return -ENODEV;

	devicename = devicetable[ent->driver_data].devicename;

	/* remap memory mapped I/O onto kernel virtual memory */
	if ( (ymfbase[cards] = ioremap_nocache(iobase, YMFSB_REGSIZE)) == 0 )
	{
		printk(KERN_ERR PFX "ioremap (0x%lx) returns zero\n", iobase);
		return -ENODEV;
	}
	printk(KERN_INFO PFX "found %s at 0x%lx\n", devicename, iobase);
#ifdef YMF_DEBUG
	printk(KERN_INFO PFX "remappling to 0x%p\n", ymfbase[cards]);
#endif

	memset (&sb_data[cards], 0, sizeof (struct address_info));
	memset (&opl3_data[cards], 0, sizeof (struct address_info));
#ifdef SUPPORT_UART401_MIDI
	memset (&mpu_data[cards], 0, sizeof (struct address_info));
#endif

	sb_data[cards].name   = YMFSB_CARD_NAME;
	opl3_data[cards].name = YMFSB_CARD_NAME;
#ifdef SUPPORT_UART401_MIDI
	mpu_data[cards].name  = YMFSB_CARD_NAME;
#endif

	sb_data[cards].card_subtype = MDL_YMPCI;

	if ( io == 0 ) io      = 0x220;
	sb_data[cards].io_base = io;
	sb_data[cards].irq     = pcidev->irq;
	sb_data[cards].dma     = dma;

	if ( synth_io == 0 ) synth_io = 0x388;
	opl3_data[cards].io_base = synth_io;
	opl3_data[cards].irq     = -1;

#ifdef SUPPORT_UART401_MIDI
	if ( mpu_io == 0 ) mpu_io = 0x330;
	mpu_data[cards].io_base = mpu_io;
	mpu_data[cards].irq     = -1;
#endif

	if ( ymf7xx_init(pcidev) ) {
		printk (KERN_ERR PFX
			"Cannot initialize %s, aborting\n",
			devicename);
		return -ENODEV;
	}

	/* register legacy SoundBlaster Pro */
	if (!ymf7xxsb_probe_sb(&sb_data[cards])) {
		printk (KERN_ERR PFX
			"SB probe at 0x%X failed, aborting\n",
			io);
		return -ENODEV;
	}
	ymf7xxsb_attach_sb (&sb_data[cards]);

#ifdef SUPPORT_UART401_MIDI
	/* register legacy MIDI */
	if ( mpu_io > 0 && 0)
	{
		if (!ymf7xxsb_probe_midi (&mpu_data[cards], THIS_MODULE)) {
			printk (KERN_ERR PFX
				"MIDI probe @ 0x%X failed, aborting\n",
				mpu_io);
			ymf7xxsb_unload_sb (&sb_data[cards], 0);
			return -ENODEV;
		}
	}
#endif

	/* register legacy OPL3 */

	cards++;	
	return 0;
}

static struct pci_driver ymf7xxsb_driver = {
	name:		"ymf7xxsb",
	id_table:	ymf7xxsb_pci_tbl,
	probe:		ymf7xxsb_init_one,
};

static int __init init_ymf7xxsb_module(void)
{
	int i;

	if ( master_vol < 0 ) master_vol  = 50;
	if ( master_vol > 100 ) master_vol = 100;

	for (i=0 ; i<MAX_CARDS ; i++ )
		ymfbase[i] = NULL;

	i = pci_module_init (&ymf7xxsb_driver);
	if (i < 0)
		return i;

	printk (KERN_INFO PFX YMFSB_CARD_NAME " loaded\n");
	
	return 0;
}

static void __exit free_iomaps( void )
{
	int i;

	for ( i=0 ; i<MAX_CARDS ; i++ ) {
		if ( ymfbase[i]!=NULL )
			iounmap(ymfbase[i]);
	}

	return;
}

static void __exit cleanup_ymf7xxsb_module(void)
{
	int i;
	
	for (i = 0; i < cards; i++) {
#ifdef SUPPORT_UART401_MIDI
		ymf7xxsb_unload_sb (&sb_data[i], 0);
		ymf7xxsb_unload_midi (&mpu_data[i]);
#else
		ymf7xxsb_unload_sb (&sb_data[i], 1);
#endif
	}

	free_iomaps();
	pci_unregister_driver(&ymf7xxsb_driver);

}

MODULE_AUTHOR("Daisuke Nagano, breeze.nagano@nifty.ne.jp");
MODULE_DESCRIPTION("YMF7xx Legacy Audio Driver");

module_init(init_ymf7xxsb_module);
module_exit(cleanup_ymf7xxsb_module);

