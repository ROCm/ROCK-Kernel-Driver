/*
    bttv-cards.c

    this file has configuration informations - card-specific stuff
    like the big tvcards array for the most part

    Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
                           & Marcus Metzler (mocm@thp.uni-koeln.de)
    (c) 1999,2000 Gerd Knorr <kraxel@goldbach.in-berlin.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
    
*/

#define __NO_VERSION__ 1

#include <linux/version.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/io.h>

#include "bttvp.h"
#include "tuner.h"

/* fwd decl */
static void hauppauge_eeprom(struct bttv *btv);
static void init_PXC200(struct bttv *btv);
#if 0
static void init_tea5757(struct bttv *btv);
#endif

static void winview_audio(struct bttv *btv, struct video_audio *v, int set);
static void avermedia_tvphone_audio(struct bttv *btv, struct video_audio *v,
				    int set);
static void terratv_audio(struct bttv *btv, struct video_audio *v, int set);
static void gvbctv3pci_audio(struct bttv *btv, struct video_audio *v, int set);

/* config variables */
static int triton1=0;
static unsigned int card[4]  = { -1, -1, -1, -1 };
static unsigned int pll[4]   = { -1, -1, -1, -1 };
static unsigned int tuner[4] = { -1, -1, -1, -1 };
#ifdef MODULE
static unsigned int autoload = 1;
#else
static unsigned int autoload = 0;
#endif
static unsigned int gpiomask = -1;
static unsigned int audioall = -1;
static unsigned int audiomux[5] = { -1, -1, -1, -1, -1 };

/* insmod options */
MODULE_PARM(triton1,"i");
MODULE_PARM(card,"1-4i");
MODULE_PARM_DESC(card,"specify TV/grabber card model, see CARDLIST file for a list");
MODULE_PARM(pll,"1-4i");
MODULE_PARM_DESC(pll,"specify installed crystal (0=none, 28=28 MHz, 35=35 MHz)");
MODULE_PARM(tuner,"1-4i");
MODULE_PARM_DESC(tuner,"specify installed tuner type");
MODULE_PARM(autoload,"i");
MODULE_PARM_DESC(autoload,"automatically load i2c modules like tuner.o, default is 1 (yes)");
MODULE_PARM(gpiomask,"i");
MODULE_PARM(audioall,"i");
MODULE_PARM(audiomux,"1-5i");

/* kernel args */
#ifndef MODULE
static int __init p_card(char *str)  { return bttv_parse(str,BTTV_MAX,card);  }
static int __init p_pll(char *str)   { return bttv_parse(str,BTTV_MAX,pll);   }
static int __init p_tuner(char *str) { return bttv_parse(str,BTTV_MAX,tuner); }
__setup("bttv.card=",  p_card);
__setup("bttv.pll=",   p_pll);
__setup("bttv.tuner=", p_tuner);

int __init bttv_parse(char *str, int max, int *vals)
{
	int i,number,res = 2;
	
	for (i = 0; res == 2 && i < max; i++) {
		res = get_option(&str,&number);
		if (res)
			vals[i] = number;
	}
	return 1;
}
#endif

/* ----------------------------------------------------------------------- */
/* list of card IDs for bt878+ cards                                       */

static struct CARD {
	unsigned id;
	int cardnr;
	char *name;
} cards[] __devinitdata = {
	{ 0x00011002, BTTV_HAUPPAUGE878,  "ATI TV Wonder" },
	{ 0x00011461, BTTV_AVPHONE98,     "AVerMedia TVPhone98" },
	{ 0x00021461, BTTV_AVERMEDIA98,   "Avermedia TVCapture 98" },
	{ 0x00031002, BTTV_HAUPPAUGE878,  "ATI TV Wonder/VE" },
	{ 0x00031461, BTTV_AVPHONE98,     "AVerMedia TVPhone98" },
	{ 0x00041461, BTTV_AVERMEDIA98,   "AVerMedia TVCapture 98" },
	{ 0x10b42636, BTTV_HAUPPAUGE878,  "STB ???" },
	{ 0x1118153b, BTTV_TERRATVALUE,   "Terratec TV Value" },
	{ 0x1123153b, BTTV_TERRATVRADIO,  "Terratec TV/Radio+" },
	{ 0x1200bd11, BTTV_PINNACLERAVE,  "Pinnacle PCTV Rave" },
	{ 0x13eb0070, BTTV_HAUPPAUGE878,  "Hauppauge WinTV" },
	{ 0x18501851, BTTV_CHRONOS_VS2,   "Chronos Video Shuttle II" },
	{ 0x18521852, BTTV_TYPHOON_TVIEW, "Typhoon TView TV/FM Tuner" },
	{ 0x217d6606, BTTV_WINFAST2000,   "Leadtek WinFast TV 2000" },
	{ 0x263610b4, BTTV_STB2,          "STB TV PCI FM, P/N 6000704" },
	{ 0x3000121a, 0/* no entry yet */,"VoodooTV 200" },
	{ 0x3000144f, BTTV_MAGICTVIEW063, "TView 99 (CPH063)" },
	{ 0x300014ff, BTTV_MAGICTVIEW061, "TView 99 (CPH061)" },
	{ 0x3002144f, BTTV_MAGICTVIEW061, "Askey Magic TView" },
	{ 0x300214ff, BTTV_PHOEBE_TVMAS,  "Phoebe TV Master" },
	{ 0x39000070, BTTV_HAUPPAUGE878,  "Hauppauge WinTV-D" },
	{ 0x400a15b0, BTTV_ZOLTRIX_GENIE, "Zoltrix Genie TV" },
	{ 0x401015b0, BTTV_ZOLTRIX_GENIE, "Zoltrix Genie TV / Radio" },
 	{ 0x402010fc, BTTV_GVBCTV3PCI,    "I-O Data Co. GV-BCV3/PCI" },
	{ 0xff000070, BTTV_HAUPPAUGE878,  "Osprey-100" },
	{ 0xff010070, BTTV_HAUPPAUGE878,  "Osprey-200" },
#if 0 /* probably wrong */
	{ 0x14610002, BTTV_AVERMEDIA98,   "Avermedia TVCapture 98" },
	{ 0x6606217d, BTTV_WINFAST2000,   "Leadtek WinFast TV 2000" },
#endif
	{ 0, -1, NULL }
};

/* ----------------------------------------------------------------------- */
/* array with description for bt848 / bt878 tv/grabber cards               */

struct tvcard bttv_tvcards[] = {
{
/* ---- card 0x00 ---------------------------------- */
	name:		" *** UNKNOWN *** ",
	video_inputs:	4,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	muxsel:		{ 2, 3, 1, 0},
	tuner_type:	-1,
},{
	name:		"MIRO PCTV",
	video_inputs:	4,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	15,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 2, 0, 0, 0, 10},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"Hauppauge old",
	video_inputs:	4,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	7,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 0, 1, 2, 3, 4},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"STB",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	7,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 4, 0, 2, 3, 1},
	no_msp34xx:	1,
	needs_tvaudio:	1,
	tuner_type:	-1,
},{

/* ---- card 0x04 ---------------------------------- */
	name:		"Intel",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		-1,
	gpiomask:	7,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 0, 1, 2, 3, 4},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"Diamond DTV2000",
	video_inputs:	4,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	3,
	muxsel:		{ 2, 3, 1, 0},
	audiomux:	{ 0, 1, 0, 1, 3},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"AVerMedia TVPhone",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		3,
	gpiomask:	15,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{12, 4,11,11, 0},
	needs_tvaudio:	1,
	pll:		PLL_28,
	tuner_type:	-1,
},{
	name:		"MATRIX-Vision MV-Delta",
	video_inputs:	5,
	audio_inputs:	1,
	tuner:		-1,
	svhs:		3,
	gpiomask:	0,
	muxsel:		{ 2, 3, 1, 0, 0},
	audiomux:	{0 },
	needs_tvaudio:	1,
	tuner_type:	-1,
},{

/* ---- card 0x08 ---------------------------------- */
	name:		"Fly Video II",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0xc00,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 0, 0xc00, 0x800, 0x400, 0xc00, 0},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"TurboTV",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	3,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 1, 1, 2, 3, 0},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"Hauppauge new (bt878)",
	video_inputs:	4,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	7,
	muxsel:		{ 2, 0, 1, 1},
	audiomux:	{ 0, 1, 2, 3, 4},
	needs_tvaudio:	1,
	pll:		PLL_28,
	tuner_type:	-1,
},{
	name:		"MIRO PCTV pro",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	65551,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{1,65537, 0, 0,10},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{

/* ---- card 0x0c ---------------------------------- */
	name:		"ADS Technologies Channel Surfer TV",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	15,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 13, 14, 11, 7, 0, 0},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"AVerMedia TVCapture 98",
	video_inputs:	3,
	audio_inputs:	4,
	tuner:		0,
	svhs:		2,
	gpiomask:	15,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 13, 14, 11, 7, 0, 0},
	needs_tvaudio:	1,
	pll:		PLL_28,
	tuner_type:	5,
},{
	name:		"Aimslab VHX",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	7,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 0, 1, 2, 3, 4},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"Zoltrix TV-Max",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	15,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{0 , 0, 1 , 0, 10},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{

/* ---- card 0x10 ---------------------------------- */
	name:		"Pixelview PlayTV (bt878)",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0x01fe00,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 0x01c000, 0, 0x018000, 0x014000, 0x002000, 0 },
	needs_tvaudio:	1,
	pll:		PLL_28,
	tuner_type:	-1,
},{
	name:		"Leadtek WinView 601",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0x8300f8,
	muxsel:		{ 2, 3, 1, 1,0},
	audiomux:	{ 0x4fa007,0xcfa007,0xcfa007,0xcfa007,0xcfa007,0xcfa007},
	needs_tvaudio:	1,
	tuner_type:	-1,
	audio_hook:	winview_audio,
},{
	name:		"AVEC Intercapture",
	video_inputs:	3,
	audio_inputs:	2,
	tuner:		0,
	svhs:		2,
	gpiomask:	0,
	muxsel:		{2, 3, 1, 1},
	audiomux:	{1, 0, 0, 0, 0},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"LifeView FlyKit w/o Tuner",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		-1,
	svhs:		-1,
	gpiomask:	0x8dff00,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 0 },
	no_msp34xx:	1,
	tuner_type:	-1,
},{

/* ---- card 0x14 ---------------------------------- */
	name:		"CEI Raffles Card",
	video_inputs:	3,
	audio_inputs:	3,
	tuner:		0,
	svhs:		2,
	muxsel:		{2, 3, 1, 1},
	tuner_type:	-1,
},{
	name:		"Lucky Star Image World ConferenceTV",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0x00fffe07,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 131072, 1, 1638400, 3, 4},
	needs_tvaudio:	1,
	pll:		PLL_28,
	tuner_type:	TUNER_PHILIPS_PAL_I,
},{
	name:		"Phoebe Tv Master + FM",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0xc00,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{0, 1, 0x800, 0x400, 0xc00, 0},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"Modular Technology MM205 PCTV, bt878",
	video_inputs:	2,
	audio_inputs:	1,
	tuner:		0,
	svhs:		-1,
	gpiomask:	7,
	muxsel:		{ 2, 3 },
	audiomux:	{ 0, 0, 0, 0, 0 },
	no_msp34xx:	1,
	needs_tvaudio:	1,
	tuner_type:	-1,
},{

/* ---- card 0x18 ---------------------------------- */
	name:		"Askey/Typhoon/Anubis Magic TView CPH051/061 (bt878)",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0xe00,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{0x400, 0x400, 0x400, 0x400, 0},
	needs_tvaudio:	1,
	pll:		PLL_28,
	tuner_type:	-1,
},{
	name:		"Terratec/Vobis TV-Boostar",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	16777215,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 131072, 1, 1638400, 3,4},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"Newer Hauppauge WinCam (bt878)",
	video_inputs:	4,
	audio_inputs:	1,
	tuner:		0,
	svhs:		3,
	gpiomask:	7,
	muxsel:		{ 2, 0, 1, 1},
	audiomux:	{ 0, 1, 2, 3, 4},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"MAXI TV Video PCI2",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0xffff,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 0, 1, 2, 3, 0xc00},
	needs_tvaudio:	1,
	tuner_type:	TUNER_PHILIPS_SECAM,
},{

/* ---- card 0x1c ---------------------------------- */
	name:		"Terratec TerraTV+",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0x70000,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 0x20000, 0x30000, 0x00000, 0x10000, 0x40000},
	needs_tvaudio:	1,
	tuner_type:	-1,
	audio_hook:	terratv_audio,
},{
	name:		"Imagenation PXC200",
	video_inputs:	5,
	audio_inputs:	1,
	tuner:		-1,
	svhs:		4,
	gpiomask:	0,
	muxsel:		{ 2, 3, 1, 0, 0},
	audiomux:	{ 0 },
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"FlyVideo 98",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0x8dfe00,
	muxsel:		{2, 3, 1, 1},
	audiomux:	{ 0, 0x8dff00, 0x8df700, 0x8de700, 0x8dff00, 0 },
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"iProTV",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	1,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 1, 0, 0, 0, 0 },
	tuner_type:	-1,
},{

/* ---- card 0x20 ---------------------------------- */
	name:		"Intel Create and Share PCI",
	video_inputs:	4,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	7,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 4, 4, 4, 4, 4},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"Terratec TerraTValue",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0xffff00,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 0x500, 0, 0x300, 0x900, 0x900},
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"Leadtek WinFast 2000",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0xfff000,
	muxsel:		{ 2, 3, 1, 1,0},
	audiomux:	{ 0x621000,0x6ddf07,0x621100,0x620000,0xE210000,0x620000},
	needs_tvaudio:	1,
	pll:		PLL_28,
	tuner_type:	-1,
},{
	name:		"Chronos Video Shuttle II",
	video_inputs:	3,
	audio_inputs:	3,
	tuner:		0,
	svhs:		2,
	gpiomask:	0x1800,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 0, 0, 0x1000, 0x1000, 0x0800},
	needs_tvaudio:	1,
	pll:		PLL_28,
	tuner_type:	-1,
},{

/* ---- card 0x24 ---------------------------------- */
	name:		"Typhoon TView TV/FM Tuner",
	video_inputs:	3,
	audio_inputs:	3,
	tuner:		0,
	svhs:		2,
	gpiomask:	0x1800,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 0, 0x800, 0, 0, 0x1800, 0 },
	needs_tvaudio:	1,
	pll:		PLL_28,
	tuner_type:	-1,
},{
	name:		"PixelView PlayTV pro",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0xff,
	muxsel:		{ 2, 3, 1, 1 },
	audiomux:	{ 0x21, 0x20, 0x24, 0x2c, 0x29, 0x29 },
	no_msp34xx:	1,
	pll:		PLL_28,
	tuner_type:	-1,
},{
	name:		"TView99 CPH063",
	video_inputs:	4,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0x551e00,
	muxsel:		{ 2, 3, 1, 0},
	audiomux:	{ 0x551400, 0x551200, 0, 0, 0, 0x551200 },
	needs_tvaudio:	1,
	pll:		PLL_28,
	tuner_type:	-1,
},{
	name:		"Pinnacle PCTV Rave",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0x03000F,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 2, 0, 0, 0, 1},
	needs_tvaudio:	1,
	pll:		PLL_28,
	tuner_type:	-1,
},{

/* ---- card 0x28 ---------------------------------- */
	name:		"STB2",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	7,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 4, 0, 2, 3, 1},
	no_msp34xx:	1,
	needs_tvaudio:	1,
	tuner_type:	-1,
},{
	name:		"AVerMedia TVPhone 98",
	video_inputs:	3,
	audio_inputs:	4,
	tuner:		0,
	svhs:		2,
	gpiomask:	12,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 13, 14, 11, 7, 0, 0},
	needs_tvaudio:	1,
	pll:		PLL_28,
	tuner_type:	5,
	audio_hook:	avermedia_tvphone_audio,
},{
	name:		"ProVideo PV951", /* pic16c54 */
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 0, 0, 0, 0, 0},
	no_msp34xx:	1,
	pll:		PLL_28,
	tuner_type:	1,
},{
	name:		"Little OnAir TV",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0xe00b,
	muxsel:		{2, 3, 1, 1},
	audiomux:	{0xff9ff6, 0xff9ff6, 0xff1ff7, 0, 0xff3ffc},
	no_msp34xx:	1,
	tuner_type:	-1,
},{

/* ---- card 0x2c ---------------------------------- */
	name:		"Sigma TVII-FM",
	video_inputs:	2,
	audio_inputs:	1,
	tuner:		0,
	svhs:		-1,
	gpiomask:	3,
	muxsel:		{2, 3, 1, 1},
	audiomux:	{1, 1, 0, 2, 3},
	no_msp34xx:	1,
	pll:		PLL_NONE,
	tuner_type:	-1,
},{
	name:		"MATRIX-Vision MV-Delta 2",
	video_inputs:	5,
	audio_inputs:	1,
	tuner:		-1,
	svhs:		3,
	gpiomask:	0,
	muxsel:		{ 2, 3, 1, 0, 0},
	audiomux:	{0 },
	no_msp34xx:	1,
	pll:		PLL_28,
	tuner_type:	-1,
},{
	name:		"Zoltrix Genie TV",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0xbcf03f,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 0xbc803f, 0, 0xbcb03f, 0, 0xbcb03f},
	no_msp34xx:	1,
	pll:		PLL_28,
	tuner_type:	5,
},{
	name:		"Terratec TV/Radio+",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0x1f0000,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 0xe2ffff, 0xebffff, 0, 0, 0xe0ffff, 0xe2ffff },
	no_msp34xx:	1,
	pll:		PLL_35,
	tuner_type:	1,
},{

/* ---- card 0x30 ---------------------------------- */
	name:		"Dynalink Magic TView ",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	15,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{2,0,0,0,1},
	needs_tvaudio:	1,
	pll:		PLL_28,
	tuner_type:	-1,
},{
	name:		"GV-BCTV3",
	video_inputs:	3,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0x010f00,
	muxsel:		{2, 3, 0, 0},
	audiomux:	{0x10000, 0, 0x10000, 0, 0, 0},
	no_msp34xx:	1,
	pll:		PLL_28,
	tuner_type:	TUNER_ALPS_TSHC6_NTSC,
	audio_hook:	gvbctv3pci_audio,
},{
	name:		"Prolink PV-BT878P+4E (PixelView PlayTV PAK)",
	video_inputs:	4,
	audio_inputs:	1,
	tuner:		0,
	svhs:		2,
	gpiomask:	0xAA0000,
	muxsel:		{ 2,3,1,1 },
	audiomux:	{ 0x20000, 0, 0x80000, 0x80000, 0xa8000, 0x46000  },
	no_msp34xx:	1,
	pll:		PLL_28,
	tuner_type:	TUNER_PHILIPS_PAL_I,
},{
	name:           "Eagle Wireless Capricorn2 (bt878A)",
	video_inputs:   4,
	audio_inputs:   1,
	tuner:          0,
	svhs:           2,
	gpiomask:       7,
	muxsel:         { 2, 0, 1, 1},
	audiomux:       { 0, 1, 2, 3, 4},
	pll:            PLL_28,
	tuner_type:     -1 /* TUNER_ALPS_TMDH2_NTSC */,
},{

/* ---- card 0x34 ---------------------------------- */
	name:           "Pinnacle Studio PCTV Pro", /* David Härdeman <david@2gen.com> */
	video_inputs:   3,
	audio_inputs:   1,
	tuner:          0,
	svhs:           2,
	gpiomask:       0x03000F,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 1, 65537, 0, 0, 10},
	needs_tvaudio:  1,
	pll:            PLL_28,
	tuner_type:     -1,
},{
	name:		"Typhoon TView RDS", /* Claas Langbehn <claas@bigfoot.com> */
	video_inputs:	3,
	audio_inputs:	3,
	tuner:		0,
	svhs:		2,
	gpiomask:	0xffff,
	muxsel:		{ 2, 3, 1, 1},
	audiomux:	{ 0xb002, 0, 0x12, 0x12, 0x3007 },
	needs_tvaudio:	1,
	pll:		PLL_28,
	tuner_type:	TUNER_PHILIPS_PAL_I,
}};

const int bttv_num_tvcards = (sizeof(bttv_tvcards)/sizeof(struct tvcard));

/* ----------------------------------------------------------------------- */

static unsigned char eeprom_data[256];

/*
 * identify card
 */
void __devinit bttv_idcard(struct bttv *btv)
{
	unsigned int gpiobits;
	int i,type;
	unsigned short tmp;

	/* read PCI subsystem ID */
	pci_read_config_word(btv->dev, PCI_SUBSYSTEM_ID, &tmp);
	btv->cardid = tmp << 16;
	pci_read_config_word(btv->dev, PCI_SUBSYSTEM_VENDOR_ID, &tmp);
	btv->cardid |= tmp;

	if (0 != btv->cardid && 0xffffffff != btv->cardid) {
		/* look for the card */
		for (type = -1, i = 0; cards[i].id != 0; i++)
			if (cards[i].id  == btv->cardid)
				type = i;
		
		if (type != -1) {
			/* found it */
			printk(KERN_INFO "bttv%d: subsystem: %04x:%04x  =>  %s  =>  card=%d\n",
			       btv->nr, btv->cardid & 0xffff, btv->cardid >> 16,
			       cards[type].name,cards[type].cardnr);
			btv->type = cards[type].cardnr;
		} else {
			/* 404 */
			printk(KERN_INFO "bttv%d: subsystem: %04x:%04x (UNKNOWN)\n",
			       btv->nr, btv->cardid&0xffff, btv->cardid>>16);
			printk(KERN_DEBUG "please mail id, board name and "
			       "the correct card= insmod option to "
			       "kraxel@goldbach.in-berlin.de\n");
		}
	}

	/* let the user override the autodetected type */
	if (card[btv->nr] >= 0 && card[btv->nr] < bttv_num_tvcards)
		btv->type=card[btv->nr];
	
	/* print which card config we are using */
	sprintf(btv->video_dev.name,"BT%d%s(%.23s)",
		btv->id,
		(btv->id==848 && btv->revision==0x12) ? "A" : "",
		bttv_tvcards[btv->type].name);
	printk(KERN_INFO "bttv%d: model: %s [%s]\n",btv->nr,btv->video_dev.name,
	       (card[btv->nr] >= 0 && card[btv->nr] < bttv_num_tvcards) ?
	       "insmod option" : "autodetected");

	/* overwrite gpio stuff ?? */
	if (-1 == audioall && -1 == audiomux[0])
		return;

	if (-1 != audiomux[0]) {
		gpiobits = 0;
		for (i = 0; i < 5; i++) {
			bttv_tvcards[btv->type].audiomux[i] = audiomux[i];
			gpiobits |= audiomux[i];
		}
	} else {
		gpiobits = audioall;
		for (i = 0; i < 5; i++) {
			bttv_tvcards[btv->type].audiomux[i] = audioall;
		}
	}
	bttv_tvcards[btv->type].gpiomask = (-1 != gpiomask) ? gpiomask : gpiobits;
	printk(KERN_INFO "bttv%d: gpio config override: mask=0x%x, mux=",
	       btv->nr,bttv_tvcards[btv->type].gpiomask);
	for (i = 0; i < 5; i++) {
		printk("%s0x%x", i ? "," : "", bttv_tvcards[btv->type].audiomux[i]);
	}
	printk("\n");
}

/*
 * (most) board specific initialisations goes here
 */
void __devinit bttv_init_card(struct bttv *btv)
{
	int eeprom = 0;

        if (btv->type == BTTV_MIRO || btv->type == BTTV_MIROPRO) {
                /* auto detect tuner for MIRO cards */
                btv->tuner_type=((btread(BT848_GPIO_DATA)>>10)-1)&7;
#if 0
		if (btv->type == BTTV_MIROPRO) {
			if (bttv_verbose)
				printk(KERN_INFO "Initializing TEA5757...\n");
			init_tea5757(btv);
		}
#endif
        }
        if (btv->type == BTTV_HAUPPAUGE || btv->type == BTTV_HAUPPAUGE878) {
		/* pick up some config infos from the eeprom */
		if (0xa0 != eeprom) {
			eeprom = 0xa0;
			bttv_readee(btv,eeprom_data,0xa0);
		}
                hauppauge_eeprom(btv);
        }
 	if (btv->type == BTTV_PXC200)
		init_PXC200(btv);

	/* pll configuration */
        if (!(btv->id==848 && btv->revision==0x11)) {
		/* defaults from card list */
		if (PLL_28 == bttv_tvcards[btv->type].pll) {
			btv->pll.pll_ifreq=28636363;
			btv->pll.pll_crystal=BT848_IFORM_XT0;
		}
		if (PLL_35 == bttv_tvcards[btv->type].pll) {
			btv->pll.pll_ifreq=35468950;
			btv->pll.pll_crystal=BT848_IFORM_XT1;
		}
		/* insmod options can override */
                switch (pll[btv->nr]) {
                case 0: /* none */
			btv->pll.pll_crystal = 0;
			btv->pll.pll_ifreq   = 0;
			btv->pll.pll_ofreq   = 0;
                        break;
                case 1: /* 28 MHz */
		case 28:
                        btv->pll.pll_ifreq   = 28636363;
			btv->pll.pll_ofreq   = 0;
                        btv->pll.pll_crystal = BT848_IFORM_XT0;
                        break;
                case 2: /* 35 MHz */
		case 35:
                        btv->pll.pll_ifreq   = 35468950;
			btv->pll.pll_ofreq   = 0;
                        btv->pll.pll_crystal = BT848_IFORM_XT1;
                        break;
                }
        }

	/* tuner configuration (from card list / insmod option) */
 	if (-1 != bttv_tvcards[btv->type].tuner_type)
                btv->tuner_type = bttv_tvcards[btv->type].tuner_type;
	if (-1 != tuner[btv->nr])
		btv->tuner_type = tuner[btv->nr];
	if (btv->tuner_type != -1)
		bttv_call_i2c_clients(btv,TUNER_SET_TYPE,&btv->tuner_type);

	/* try to detect audio/fader chips */
	if (!bttv_tvcards[btv->type].no_msp34xx &&
	    bttv_I2CRead(btv, I2C_MSP3400, "MSP34xx") >=0) {
		if (autoload)
			request_module("msp3400");
	}

	if (bttv_I2CRead(btv, I2C_TDA9875, "TDA9875") >=0) {
		if (autoload)
			request_module("tda9875");
	}

	if (bttv_I2CRead(btv, I2C_TDA7432, "TDA7432") >=0) {
		if (autoload)
			request_module("tda7432");
	}

	if (bttv_tvcards[btv->type].needs_tvaudio) {
		if (autoload)
			request_module("tvaudio");
	}

	if (bttv_tvcards[btv->type].tuner != -1) {
		if (autoload)
			request_module("tuner");
	}
}


/* ----------------------------------------------------------------------- */
/* some hauppauge specific stuff                                           */

static struct HAUPPAUGE_TUNER 
{
        int  id;
        char *name;
} 
hauppauge_tuner[] __devinitdata = 
{
        { TUNER_ABSENT,        "" },
        { TUNER_ABSENT,        "External" },
        { TUNER_ABSENT,        "Unspecified" },
        { TUNER_ABSENT,        "Philips FI1216" },
        { TUNER_PHILIPS_SECAM, "Philips FI1216MF" },
        { TUNER_PHILIPS_NTSC,  "Philips FI1236" },
        { TUNER_ABSENT,        "Philips FI1246" },
        { TUNER_ABSENT,        "Philips FI1256" },
        { TUNER_PHILIPS_PAL,   "Philips FI1216 MK2" },
        { TUNER_PHILIPS_SECAM, "Philips FI1216MF MK2" },
        { TUNER_PHILIPS_NTSC,  "Philips FI1236 MK2" },
        { TUNER_PHILIPS_PAL_I, "Philips FI1246 MK2" },
        { TUNER_ABSENT,        "Philips FI1256 MK2" },
        { TUNER_ABSENT,        "Temic 4032FY5" },
        { TUNER_TEMIC_PAL,     "Temic 4002FH5" },
        { TUNER_TEMIC_PAL_I,   "Temic 4062FY5" },
        { TUNER_ABSENT,        "Philips FR1216 MK2" },
        { TUNER_PHILIPS_SECAM, "Philips FR1216MF MK2" },
        { TUNER_PHILIPS_NTSC,  "Philips FR1236 MK2" },
        { TUNER_PHILIPS_PAL_I, "Philips FR1246 MK2" },
        { TUNER_ABSENT,        "Philips FR1256 MK2" },
        { TUNER_PHILIPS_PAL,   "Philips FM1216" },
        { TUNER_ABSENT,        "Philips FM1216MF" },
        { TUNER_PHILIPS_NTSC,  "Philips FM1236" },
        { TUNER_PHILIPS_PAL_I, "Philips FM1246" },
        { TUNER_ABSENT,        "Philips FM1256" },
        { TUNER_TEMIC_4036FY5_NTSC,  "Temic 4036FY5" },
        { TUNER_ABSENT,        "Samsung TCPN9082D" },
        { TUNER_ABSENT,        "Samsung TCPM9092P" },
        { TUNER_TEMIC_PAL,     "Temic 4006FH5" },
        { TUNER_ABSENT,        "Samsung TCPN9085D" },
        { TUNER_ABSENT,        "Samsung TCPB9085P" },
        { TUNER_ABSENT,        "Samsung TCPL9091P" },
        { TUNER_ABSENT,        "Temic 4039FR5" },
        { TUNER_ABSENT,        "Philips FQ1216 ME" },
        { TUNER_TEMIC_PAL_I,   "Temic 4066FY5" },
        { TUNER_ABSENT,        "Philips TD1536" },
        { TUNER_ABSENT,        "Philips TD1536D" },
        { TUNER_ABSENT,        "Philips FMR1236" },
        { TUNER_ABSENT,        "Philips FI1256MP" },
        { TUNER_ABSENT,        "Samsung TCPQ9091P" },
        { TUNER_ABSENT,        "Temic 4006FN5" },
        { TUNER_ABSENT,        "Temic 4009FR5" },
        { TUNER_ABSENT,        "Temic 4046FM5" },
};

static void __devinit hauppauge_eeprom(struct bttv *btv)
{
        if (eeprom_data[9] < sizeof(hauppauge_tuner)/sizeof(struct HAUPPAUGE_TUNER)) 
        {
                btv->tuner_type = hauppauge_tuner[eeprom_data[9]].id;
		if (bttv_verbose)
			printk("bttv%d: Hauppauge eeprom: model=%d, tuner=%s (%d)\n",btv->nr,
			       eeprom_data[12] << 8 | eeprom_data[11],
			       hauppauge_tuner[eeprom_data[9]].name,btv->tuner_type);
        }
}

void __devinit bttv_hauppauge_boot_msp34xx(struct bttv *btv)
{
        /* reset/enable the MSP on some Hauppauge cards */
        /* Thanks to Kyösti Mälkki (kmalkki@cc.hut.fi)! */
        btaor(32, ~32, BT848_GPIO_OUT_EN);
        btaor(0, ~32, BT848_GPIO_DATA);
        udelay(2500);
        btaor(32, ~32, BT848_GPIO_DATA);
	if (bttv_gpio)
		bttv_gpio_tracking(btv,"msp34xx");

	if (bttv_verbose)
		printk("bttv%d: Hauppauge msp34xx: reset line init\n",btv->nr);
}


/* ----------------------------------------------------------------------- */
/*  Imagenation L-Model PXC200 Framegrabber */
/*  This is basically the same procedure as 
 *  used by Alessandro Rubini in his pxc200 
 *  driver, but using BTTV functions */

static void __devinit init_PXC200(struct bttv *btv)
{
	static const int vals[] = { 0x08, 0x09, 0x0a, 0x0b, 0x0d, 0x0d,
				    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
				    0x00 };
	int i,tmp;

	/* Initialise GPIO-connevted stuff */
	btwrite(1<<13,BT848_GPIO_OUT_EN); /* Reset pin only */
	btwrite(0,BT848_GPIO_DATA);
	udelay(3);
	btwrite(1<<13,BT848_GPIO_DATA);
	/* GPIO inputs are pulled up, so no need to drive 
	 * reset pin any longer */
	btwrite(0,BT848_GPIO_OUT_EN);
	if (bttv_gpio)
		bttv_gpio_tracking(btv,"pxc200");

	/*  we could/should try and reset/control the AD pots? but
	    right now  we simply  turned off the crushing.  Without
	    this the AGC drifts drifts
	    remember the EN is reverse logic -->
	    setting BT848_ADC_AGC_EN disable the AGC
	    tboult@eecs.lehigh.edu
	*/
	btwrite(BT848_ADC_RESERVED|BT848_ADC_AGC_EN, BT848_ADC);
	
	/*	Initialise MAX517 DAC */
	printk(KERN_INFO "Setting DAC reference voltage level ...\n");
	bttv_I2CWrite(btv,0x5E,0,0x80,1);
	
	/*	Initialise 12C508 PIC */
	/*	The I2CWrite and I2CRead commmands are actually to the 
	 *	same chips - but the R/W bit is included in the address
	 *	argument so the numbers are different */
	
	printk(KERN_INFO "Initialising 12C508 PIC chip ...\n");

	for (i = 0; i < sizeof(vals)/sizeof(int); i++) {
		tmp=bttv_I2CWrite(btv,0x1E,vals[i],0,1);
		printk(KERN_INFO "I2C Write(0x08) = %i\nI2C Read () = %x\n\n",
		       tmp,bttv_I2CRead(btv,0x1F,NULL));
	}
	printk(KERN_INFO "PXC200 Initialised.\n");
}

/* ----------------------------------------------------------------------- */
/* Miro Pro radio stuff -- the tea5757 is connected to some GPIO ports     */
/*
 * Copyright (c) 1999 Csaba Halasz <qgehali@uni-miskolc.hu>
 * This code is placed under the terms of the GNU General Public License
 *
 * Brutally hacked by Dan Sheridan <dan.sheridan@contact.org.uk> djs52 8/3/00
 */

/* bus bits on the GPIO port */
#define TEA_WE			6
#define TEA_DATA		9
#define TEA_CLK			8
#define TEA_MOST		7

#define BUS_LOW(bit) 	btand(~(1<<TEA_##bit), BT848_GPIO_DATA)
#define BUS_HIGH(bit)	btor((1<<TEA_##bit), BT848_GPIO_DATA)
#define BUS_IN(bit)	((btread(BT848_GPIO_DATA) >> TEA_##bit) & 1)

/* TEA5757 register bits */
#define TEA_FREQ		0:14
#define TEA_BUFFER		15:15

#define TEA_SIGNAL_STRENGTH	16:17

#define TEA_PORT1		18:18
#define TEA_PORT0		19:19

#define TEA_BAND		20:21
#define TEA_BAND_FM		0
#define TEA_BAND_MW		1
#define TEA_BAND_LW		2
#define TEA_BAND_SW		3

#define TEA_MONO		22:22
#define TEA_ALLOW_STEREO	0
#define TEA_FORCE_MONO		1

#define TEA_SEARCH_DIRECTION	23:23
#define TEA_SEARCH_DOWN		0
#define TEA_SEARCH_UP		1

#define TEA_STATUS		24:24
#define TEA_STATUS_TUNED	0
#define TEA_STATUS_SEARCHING	1

/* Low-level stuff */
static int tea_read(struct bttv *btv)
{
	int value = 0;
	long timeout;
	int i;
	
	/* better safe than sorry */
	btaor((1<<TEA_CLK) | (1<<TEA_WE), ~((1<<TEA_CLK) | (1<<TEA_DATA) | (1<<TEA_WE) | (1<<TEA_MOST)), BT848_GPIO_OUT_EN);
	if (bttv_gpio)
		bttv_gpio_tracking(btv,"miro tea read");
	
	BUS_LOW(WE);
	BUS_LOW(CLK);
	
	udelay(10);
	for(timeout = jiffies + 10 * HZ;
	    BUS_IN(DATA) == 1 && time_before(jiffies, timeout);
	    schedule());	/* 10 s */
	if (BUS_IN(DATA) == 1) {
		printk("tea5757: read timeout\n");
		return -1;
	}
	for(timeout = jiffies + HZ/5;
	    BUS_IN(MOST) == 1 && time_before(jiffies, timeout);
	    schedule());	/* 0.2 s */
	if (bttv_debug) printk("tea5757:");
	for(i = 0; i < 24; i++)
	{
		udelay(10);
		BUS_HIGH(CLK);
		udelay(10);
		if (bttv_debug) printk("%c", (BUS_IN(MOST) == 0)?'T':'-');
		BUS_LOW(CLK);
		value <<= 1;					
		value |= (BUS_IN(DATA) == 0)?0:1;	/* MSB first */
		if (bttv_debug) printk("%c", (BUS_IN(MOST) == 0)?'S':'M');
	}
	if (bttv_debug) printk("\ntea5757: read 0x%X\n", value);
	return value;
}

static int tea_write(struct bttv *btv, int value)
{
	int i;
	int reg = value;
	
	btaor((1<<TEA_CLK) | (1<<TEA_WE) | (1<<TEA_DATA), ~((1<<TEA_CLK) | (1<<TEA_DATA) | (1<<TEA_WE) | (1<<TEA_MOST)), BT848_GPIO_OUT_EN);
	if (bttv_gpio)
		bttv_gpio_tracking(btv,"miro tea write");
	if (bttv_debug)
		printk("tea5757: write 0x%X\n", value);
	BUS_LOW(CLK);
	BUS_HIGH(WE);
	for(i = 0; i < 25; i++)
	{
		if (reg & 0x1000000)
			BUS_HIGH(DATA);
		else
			BUS_LOW(DATA);
		reg <<= 1;
		BUS_HIGH(CLK);
		udelay(10);
		BUS_LOW(CLK);
		udelay(10);
	}
	BUS_LOW(WE);	/* unmute !!! */
	return 0;
}

void tea5757_set_freq(struct bttv *btv, unsigned short freq)
{
	tea_write(btv, 5 * freq + 0x358); /* add 10.7MHz (see docs) */
	if (bttv_debug) tea_read(btv);
}

#if 0
void init_tea5757(struct bttv *btv)
{
	BUS_LOW(CLK);
	BUS_LOW(WE); /* just to be on the safe side... */

	/* software CLK (unused) */
	btaor(0, BT848_GPIO_DMA_CTL_GPCLKMODE, BT848_GPIO_DMA_CTL);
	/* normal mode for GPIO */
	btaor(0, BT848_GPIO_DMA_CTL_GPIOMODE, BT848_GPIO_DMA_CTL);
}
#endif

/* ----------------------------------------------------------------------- */
/* winview                                                                 */

void winview_audio(struct bttv *btv, struct video_audio *v, int set)
{
	/* PT2254A programming Jon Tombs, jon@gte.esi.us.es */
	int bits_out, loops, vol, data;

	if (!set) {
		v->mode |= VIDEO_AUDIO_VOLUME;
		return;
	}
	
	/* 32 levels logarithmic */
	vol = 32 - ((v->volume>>11));
	/* units */
	bits_out = (PT2254_DBS_IN_2>>(vol%5));
	/* tens */
	bits_out |= (PT2254_DBS_IN_10>>(vol/5));
	bits_out |= PT2254_L_CHANEL | PT2254_R_CHANEL;
	data = btread(BT848_GPIO_DATA);
	data &= ~(WINVIEW_PT2254_CLK| WINVIEW_PT2254_DATA|
		  WINVIEW_PT2254_STROBE);
	for (loops = 17; loops >= 0 ; loops--) {
		if (bits_out & (1<<loops))
			data |=  WINVIEW_PT2254_DATA;
		else
			data &= ~WINVIEW_PT2254_DATA;
		btwrite(data, BT848_GPIO_DATA);
		udelay(5);
		data |= WINVIEW_PT2254_CLK;
		btwrite(data, BT848_GPIO_DATA);
		udelay(5);
		data &= ~WINVIEW_PT2254_CLK;
		btwrite(data, BT848_GPIO_DATA);
	}
	data |=  WINVIEW_PT2254_STROBE;
	data &= ~WINVIEW_PT2254_DATA;
	btwrite(data, BT848_GPIO_DATA);
	udelay(10);                     
	data &= ~WINVIEW_PT2254_STROBE;
	btwrite(data, BT848_GPIO_DATA);
}

/* ----------------------------------------------------------------------- */
/* mono/stereo control for various cards (which don't use i2c chips but    */
/* connect something to the GPIO pins                                      */

static void
gvbctv3pci_audio(struct bttv *btv, struct video_audio *v, int set)
{
	unsigned int con = 0;

	if (set) {
		btor(0x100, BT848_GPIO_OUT_EN);
		if (v->mode & VIDEO_SOUND_LANG1)
			con = 0x000;
		if (v->mode & VIDEO_SOUND_LANG2)
			con = 0x300;
		if (v->mode & VIDEO_SOUND_STEREO)
			con = 0x200;
//		if (v->mode & VIDEO_SOUND_MONO)
//			con = 0x100;
		btaor(con, ~0x300, BT848_GPIO_DATA);
	} else {
		v->mode = VIDEO_SOUND_STEREO |
			  VIDEO_SOUND_LANG1  | VIDEO_SOUND_LANG2;
	}
}

/*
 * Mario Medina Nussbaum <medisoft@alohabbs.org.mx>
 *  I discover that on BT848_GPIO_DATA address a byte 0xcce enable stereo,
 *  0xdde enables mono and 0xccd enables sap
 *
 * Petr Vandrovec <VANDROVE@vc.cvut.cz>
 *  P.S.: At least mask in line above is wrong - GPIO pins 3,2 select
 *  input/output sound connection, so both must be set for output mode.
 *
 */
static void
avermedia_tvphone_audio(struct bttv *btv, struct video_audio *v, int set)
{
#if 0 /* needs more testing -- might be we need two versions for PAL/NTSC */
	int val = 0;

	if (set) {
		if (v->mode & VIDEO_SOUND_LANG1)   /* SAP */
			val = 0xce;
		if (v->mode & VIDEO_SOUND_STEREO)
			val = 0xcd;
		if (val) {
			btaor(val, 0xff, BT848_GPIO_OUT_EN);
			btaor(val, 0xff, BT848_GPIO_DATA);
			if (bttv_gpio)
				bttv_gpio_tracking(btv,"avermedia");
		}
	} else {
		v->mode = VIDEO_SOUND_MONO | VIDEO_SOUND_STEREO |
			VIDEO_SOUND_LANG1;
		return;
	}
#endif
}

static void
terratv_audio(struct bttv *btv, struct video_audio *v, int set)
{
	unsigned int con = 0;

	if (set) {
		btor(0x180000, BT848_GPIO_OUT_EN);
		if (v->mode & VIDEO_SOUND_LANG2)
			con = 0x080000;
		if (v->mode & VIDEO_SOUND_STEREO)
			con = 0x180000;
		btaor(con, ~0x180000, BT848_GPIO_DATA);
		if (bttv_gpio)
			bttv_gpio_tracking(btv,"terratv");
	} else {
		v->mode = VIDEO_SOUND_MONO | VIDEO_SOUND_STEREO |
			VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
	}
}


/* ----------------------------------------------------------------------- */
/* motherboard chipset specific stuff                                      */

static struct {
	char            *name;
	unsigned short  vendor;
	unsigned short  device;
} needs_etbf[] __devinitdata = {
	{ "Intel 82437FX [Triton PIIX]",
	  PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82437  },
	{ "VIA VT82C597 [Apollo VP3]",
	  PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C597_0 },
	{ NULL, 0, 0 }
};

void __devinit bttv_check_chipset(void)
{
	int i;
	struct pci_dev *dev = NULL;

	if(pci_pci_problems & PCIPCI_FAIL)
		printk(KERN_WARNING "BT848 and your chipset may not work together.\n");

        while ((dev = pci_find_device(PCI_VENDOR_ID_INTEL,
				      PCI_DEVICE_ID_INTEL_82441, dev))) {
                unsigned char b;
                pci_read_config_byte(dev, 0x53, &b);
		if (bttv_debug)
			printk(KERN_INFO "bttv: Host bridge: 82441FX Natoma, "
			       "bufcon=0x%02x\n",b);
	}

	if(pci_pci_problems & (PCIPCI_TRITON|PCIPCI_VIAETBF))
	{
		printk(KERN_INFO "bttv: Host bridge needs ETBF enabled.\n");
		triton1=1;
	}
}

int __devinit bttv_handle_chipset(struct bttv *btv)
{
 	unsigned char command;

	if (!triton1)
		return 0;

	if (bttv_verbose)
		printk("bttv%d: enabling 430FX/VP3 compatibilty\n",btv->nr);

	if (btv->id < 878) {
		/* bt848 (mis)uses a bit in the irq mask */
		btv->triton1 = BT848_INT_ETBF;
	} else {
		/* bt878 has a bit in the pci config space for it */
                pci_read_config_byte(btv->dev, BT878_DEVCTRL, &command);
                command |= BT878_EN_TBFX;
                pci_write_config_byte(btv->dev, BT878_DEVCTRL, command);
                pci_read_config_byte(btv->dev, BT878_DEVCTRL, &command);
                if (!(command&BT878_EN_TBFX)) {
                        printk("bttv: 430FX compatibility could not be enabled\n");
			return -1;
                }
        }
	return 0;
}


#ifndef MODULE

static int __init bttv_card_setup(char *str)
{
	int i,number,res = 2;

	for (i = 0; res == 2 && i < BTTV_MAX; i++) {
		res = get_option(&str,&number);
		if (res)
			card[i] = number;
	}
	return 1;
}

__setup("bttv_card=", bttv_card_setup);

#endif /* not MODULE */

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
