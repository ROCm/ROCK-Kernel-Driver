/*
 * device driver for philips saa7134 based TV cards
 * card-specific stuff.
 *
 * (c) 2001-04 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>

#include "saa7134-reg.h"
#include "saa7134.h"

/* commly used strings */
static char name_mute[]    = "mute";
static char name_radio[]   = "Radio";
static char name_tv[]      = "Television";
static char name_tv_mono[] = "TV (mono only)";
static char name_comp1[]   = "Composite1";
static char name_comp2[]   = "Composite2";
static char name_comp3[]   = "Composite3";
static char name_comp4[]   = "Composite4";
static char name_svideo[]  = "S-Video";

/* ------------------------------------------------------------------ */
/* board config info                                                  */

struct saa7134_board saa7134_boards[] = {
	[SAA7134_BOARD_UNKNOWN] = {
		.name		= "UNKNOWN/GENERIC",
		.audio_clock	= 0x00187de7,
		.tuner_type	= TUNER_ABSENT,
		.inputs         = {{
			.name = "default",
			.vmux = 0,
			.amux = LINE1,
		}},
	},
	[SAA7134_BOARD_PROTEUS_PRO] = {
		/* /me */
		.name		= "Proteus Pro [philips reference design]",
		.audio_clock	= 0x00187de7,
		.tuner_type	= TUNER_PHILIPS_PAL,
		.inputs         = {{
			.name = name_comp1,
			.vmux = 0,
			.amux = LINE1,
		},{
			.name = name_tv,
			.vmux = 1,
			.amux = TV,
			.tv   = 1,
		},{
			.name = name_tv_mono,
			.vmux = 1,
			.amux = LINE2,
			.tv   = 1,
		}},
		.radio = {
			.name = name_radio,
			.amux = LINE2,
		},
	},
	[SAA7134_BOARD_FLYVIDEO3000] = {
		/* "Marco d'Itri" <md@Linux.IT> */
		.name		= "LifeView FlyVIDEO3000",
		.audio_clock	= 0x00200000,
		.tuner_type	= TUNER_PHILIPS_PAL,
		.gpiomask       = 0xe000,
		.inputs         = {{
			.name = name_tv,
			.vmux = 1,
			.amux = TV,
			.gpio = 0x8000,
			.tv   = 1,
                },{
			.name = name_tv_mono,
			.vmux = 1,
			.amux = LINE2,
			.gpio = 0x0000,
			.tv   = 1,
		},{
			.name = name_comp1,
			.vmux = 0,
			.amux = LINE2,
			.gpio = 0x4000,
		},{
			.name = name_comp2,
			.vmux = 3,
			.amux = LINE2,
			.gpio = 0x4000,
		},{
			.name = name_svideo,
			.vmux = 8,
			.amux = LINE2,
			.gpio = 0x4000,
		}},
		.radio = {
			.name = name_radio,
			.amux = LINE2,
			.gpio = 0x2000,
		},
	},
	[SAA7134_BOARD_FLYVIDEO2000] = {
		/* "TC Wan" <tcwan@cs.usm.my> */
		.name           = "LifeView FlyVIDEO2000",
		.audio_clock    = 0x00200000,
		.tuner_type     = TUNER_LG_PAL_NEW_TAPC,
		.gpiomask       = 0xe000,
		.inputs         = {{
			.name = name_tv,
			.vmux = 1,
			.amux = LINE2,
			.gpio = 0x0000,
			.tv   = 1,
		},{
			.name = name_comp1,
			.vmux = 0,
			.amux = LINE2,
			.gpio = 0x4000,
		},{
			.name = name_comp2,
			.vmux = 3,
			.amux = LINE2,
			.gpio = 0x4000,
		},{
			.name = name_svideo,
			.vmux = 8,
			.amux = LINE2,
			.gpio = 0x4000,
		}},
                .radio = {
                        .name = name_radio,
                        .amux = LINE2,
			.gpio = 0x2000,
                },
		.mute = {
			.name = name_mute,
                        .amux = LINE2,
			.gpio = 0x8000,
		},
	},
	[SAA7134_BOARD_EMPRESS] = {
		/* "Gert Vervoort" <gert.vervoort@philips.com> */
		.name		= "EMPRESS",
		.audio_clock	= 0x00187de7,
		.tuner_type	= TUNER_PHILIPS_PAL,
		.inputs         = {{
			.name = name_comp1,
			.vmux = 0,
			.amux = LINE1,
		},{
			.name = name_svideo,
			.vmux = 8,
			.amux = LINE1,
		},{
			.name = name_tv,
			.vmux = 1,
			.amux = LINE2,
			.tv   = 1,
		}},
		.radio = {
			.name = name_radio,
			.amux = LINE2,
		},
		.i2s_rate  = 48000,
		.has_ts    = 1,
		.video_out = CCIR656,
	},
	[SAA7134_BOARD_MONSTERTV] = {
               /* "K.Ohta" <alpha292@bremen.or.jp> */
               .name           = "SKNet Monster TV",
               .audio_clock    = 0x00187de7,
               .tuner_type     = TUNER_PHILIPS_NTSC_M,
               .inputs         = {{
                       .name = name_tv,
                       .vmux = 1,
                       .amux = TV,
                       .tv   = 1,
               },{
                       .name = name_comp1,
                       .vmux = 0,
                       .amux = LINE1,
               },{
                       .name = name_svideo,
                       .vmux = 8,
                       .amux = LINE1,
               }},
               .radio = {
                       .name = name_radio,
                       .amux = LINE2,
               },
	},
	[SAA7134_BOARD_MD9717] = {
		.name		= "Tevion MD 9717",
		.audio_clock	= 0x00200000,
		.tuner_type	= TUNER_PHILIPS_PAL,
		.inputs         = {{
			.name = name_tv,
			.vmux = 1,
			.amux = TV,
			.tv   = 1,
		},{
			/* workaround for problems with normal TV sound */
			.name = name_tv_mono,
			.vmux = 1,
			.amux = LINE2,
			.tv   = 1,
		},{
			.name = name_comp1,
			.vmux = 2,
			.amux = LINE1,
		},{
			.name = name_comp2,
			.vmux = 3,
			.amux = LINE1,
		},{
			.name = name_svideo,
			.vmux = 8,
			.amux = LINE1,
		}},
		.radio = {
			.name = name_radio,
			.amux = LINE2,
		},
	},
	[SAA7134_BOARD_TVSTATION_RDS] = {
                /* Typhoon TV Tuner RDS: Art.Nr. 50694 */
		.name		= "KNC One TV-Station RDS / Typhoon TV Tuner RDS",
		.audio_clock	= 0x00200000,
		.tuner_type	= TUNER_PHILIPS_FM1216ME_MK3,
		.need_tda9887   = 1,
		.inputs         = {{
			.name = name_tv,
			.vmux = 1,
			.amux = TV,
			.tv   = 1,
		},{
			.name = name_tv_mono,
                        .vmux = 1,
                        .amux   = LINE2,
                        .tv   = 1,
                },{

			.name = name_svideo,
			.vmux = 8,
			.amux = LINE1,
		},{
			.name = name_comp1,
			.vmux = 3,
			.amux = LINE1,
		},{

                        .name = "CVid over SVid",
                        .vmux = 0,
                        .amux = LINE1,
                }},
		.radio = {
			.name = name_radio,
			.amux = LINE2,
		},
	},
	[SAA7134_BOARD_TVSTATION_DVR] = {
		.name		= "KNC One TV-Station DVR",
		.audio_clock	= 0x00200000,
		.tuner_type	= TUNER_PHILIPS_FM1216ME_MK3,
		.need_tda9887	= 1,
		.gpiomask	= 0x820000,
		.inputs		= {{
			.name = name_tv,
			.vmux = 1,
			.amux = LINE2,
			.tv   = 1,
			.gpio = 0x20000,
		},{
			.name = name_svideo,
			.vmux = 8,
			.amux = LINE1,
			.gpio = 0x20000,
		},{
			.name = name_comp1,
			.vmux = 3,
			.amux = LINE1,
			.gpio = 0x20000,
		}},
		.radio		= {
			.name = name_radio,
			.amux = LINE2,
			.gpio = 0x20000,
		},
		.i2s_rate	= 48000,
		.has_ts		= 1,
		.video_out	= CCIR656,
	},
	[SAA7134_BOARD_CINERGY400] = {
                .name           = "Terratec Cinergy 400 TV",
                .audio_clock    = 0x00200000,
                .tuner_type     = TUNER_PHILIPS_PAL,
                .inputs         = {{
                        .name = name_tv,
                        .vmux = 1,
                        .amux = TV,
                        .tv   = 1,
                },{
                        .name = name_comp1,
                        .vmux = 4,
                        .amux = LINE1,
                },{
                        .name = name_svideo,
                        .vmux = 8,
                        .amux = LINE1,
                },{
                        .name = name_comp2, // CVideo over SVideo Connector
                        .vmux = 0,
                        .amux = LINE1,
                }}
        },
	[SAA7134_BOARD_MD5044] = {
		.name           = "Medion 5044",
		.audio_clock    = 0x00187de7, // was: 0x00200000,
		.tuner_type     = TUNER_PHILIPS_FM1216ME_MK3,
		.need_tda9887   = 1,
		.inputs         = {{
			.name = name_tv,
			.vmux = 1,
			.amux = TV,
			.tv   = 1,
		},{
			/* workaround for problems with normal TV sound */
			.name = name_tv_mono,
			.vmux = 1,
			.amux = LINE2,
			.tv   = 1,
		},{
			.name = name_comp1,
			.vmux = 0,
			.amux = LINE2,
		},{
			.name = name_comp2,
			.vmux = 3,
			.amux = LINE2,
		},{
			.name = name_svideo,
			.vmux = 8,
			.amux = LINE2,
		}},
		.radio = {
			.name = name_radio,
			.amux = LINE2,
		},
	},
	[SAA7134_BOARD_KWORLD] = {
                .name           = "Kworld/KuroutoShikou SAA7130-TVPCI",
		.audio_clock    = 0x00187de7,
		.tuner_type     = TUNER_PHILIPS_NTSC_M,
                .inputs         = {{
                        .name = name_svideo,
                        .vmux = 8,
                        .amux = LINE1,
                },{
                        .name = name_comp1,
                        .vmux = 3,
                        .amux = LINE1,
                },{
                        .name = name_tv,
                        .vmux = 1,
                        .amux = LINE2,
                        .tv   = 1,
                }},
        },
	[SAA7134_BOARD_CINERGY600] = {
                .name           = "Terratec Cinergy 600 TV",
                .audio_clock    = 0x00200000,
                .tuner_type     = TUNER_PHILIPS_PAL,
                .inputs         = {{
                        .name = name_tv,
                        .vmux = 1,
                        .amux = TV,
                        .tv   = 1,
                },{
                        .name = name_comp1,
                        .vmux = 4,
                        .amux = LINE1,
                },{
                        .name = name_svideo,
                        .vmux = 8,
                        .amux = LINE1,
                },{
                        .name = name_comp2, // CVideo over SVideo Connector
                        .vmux = 0,
                        .amux = LINE1,
                }},
		.radio = {
			.name = name_radio,
			.amux = LINE2,
               },
        },
	[SAA7134_BOARD_MD7134] = {
		.name           = "Medion 7134",
		//.audio_clock    = 0x00200000,
		.audio_clock    = 0x00187de7,
		.tuner_type     = TUNER_PHILIPS_FM1216ME_MK3,
		.need_tda9887   = 1,
		.inputs = {{
			.name   = name_tv,
			.vmux   = 1,
			.amux   = TV,
			.tv     = 1,
#if 0
		},{
			.name   = name_comp1,
			.vmux   = 0,
			.amux   = LINE2,
		},{
			.name   = name_comp2,
			.vmux   = 3,
			.amux   = LINE2,
		},{
			.name   = name_svideo,
			.vmux   = 8,
			.amux   = LINE2,
#endif
		}},
		.radio = {
			.name   = name_radio,
			.amux   = LINE2,
		},
	},
	[SAA7134_BOARD_TYPHOON_90031] = {
		/* aka Typhoon "TV+Radio", Art.Nr 90031 */
		/* Tom Zoerner <tomzo at users sourceforge net> */
		.name           = "Typhoon TV+Radio 90031",
		.audio_clock    = 0x00200000,
		.tuner_type     = TUNER_PHILIPS_PAL,
		.need_tda9887   = 1,
		.inputs         = {{
			.name   = name_tv,
			.vmux   = 1,
			.amux   = TV,
			.tv     = 1,
		},{
			.name   = name_comp1,
			.vmux   = 3,
			.amux   = LINE1,
		},{
			.name   = name_svideo,
			.vmux   = 8,
			.amux   = LINE1,
		}},
		.radio = {
			.name   = name_radio,
			.amux   = LINE2,
		},
        },
	[SAA7134_BOARD_ELSA] = {
		.name           = "ELSA EX-VISION 300TV",
		.audio_clock    = 0x00187de7,
		.tuner_type     = TUNER_HITACHI_NTSC,
		.inputs         = {{
			.name = name_svideo,
			.vmux = 8,
			.amux = LINE1,
		},{
			.name   = name_comp1,
			.vmux   = 0,
			.amux   = LINE1,
		},{
			.name = name_tv,
			.vmux = 4,
			.amux = LINE2,
			.tv   = 1,
		}},
        },
	[SAA7134_BOARD_ELSA_500TV] = {
		.name           = "ELSA EX-VISION 500TV",
		.audio_clock    = 0x00187de7,
		.tuner_type     = TUNER_HITACHI_NTSC,
		.inputs         = {{
			.name = name_svideo,
			.vmux = 7,
			.amux = LINE1,
		},{
			.name = name_tv,
			.vmux = 8,
			.amux = TV,
			.tv   = 1,
		},{
			.name = name_tv_mono,
			.vmux = 8,
			.amux = LINE2,
			.tv   = 1,
		}},
        },
	[SAA7134_BOARD_ASUSTeK_TVFM7134] = {
                .name           = "ASUS TV-FM 7134",
                .audio_clock    = 0x00187de7,
                .tuner_type     = TUNER_PHILIPS_FM1216ME_MK3,
                .need_tda9887   = 1,
                .inputs         = {{
                        .name = name_tv,
                        .vmux = 1,
                        .amux = TV,
                        .tv   = 1,
#if 0 /* untested */
                },{
                        .name = name_comp1,
                        .vmux = 4,
                        .amux = LINE2,
                },{
                        .name = name_comp2,
                        .vmux = 2,
                        .amux = LINE2,
                },{
                        .name = name_svideo,
                        .vmux = 6,
                        .amux = LINE2,
                },{
                        .name = "S-Video2",
                        .vmux = 7,
                        .amux = LINE2,
#endif
                }},
                .radio = {
                        .name = name_radio,
                        .amux = LINE1,
                },
	},
	[SAA7134_BOARD_VA1000POWER] = {
                .name           = "AOPEN VA1000 POWER",
		.audio_clock    = 0x00187de7,
		.tuner_type     = TUNER_PHILIPS_NTSC,
                .inputs         = {{
                        .name = name_svideo,
                        .vmux = 8,
                        .amux = LINE1,
                },{
                        .name = name_comp1,
                        .vmux = 3,
                        .amux = LINE1,
                },{
                        .name = name_tv,
                        .vmux = 1,
                        .amux = LINE2,
                        .tv   = 1,
                }},
	},
	[SAA7134_BOARD_10MOONSTVMASTER] = {
		/* "lilicheng" <llc@linuxfans.org> */
		.name           = "10MOONS PCI TV CAPTURE CARD",
		.audio_clock    = 0x00200000,
		.tuner_type     = TUNER_LG_PAL_NEW_TAPC,
		.gpiomask       = 0xe000,
		.inputs         = {{
			.name = name_tv,
			.vmux = 1,
			.amux = LINE2,
			.gpio = 0x0000,
			.tv   = 1,
		},{
			.name = name_comp1,
			.vmux = 0,
			.amux = LINE2,
			.gpio = 0x4000,
		},{
			.name = name_comp2,
			.vmux = 3,
			.amux = LINE2,
			.gpio = 0x4000,
		},{
			.name = name_svideo,
			.vmux = 8,
			.amux = LINE2,
			.gpio = 0x4000,
		}},
                .radio = {
                        .name = name_radio,
                        .amux = LINE2,
			.gpio = 0x2000,
                },
		.mute = {
			.name = name_mute,
                        .amux = LINE2,
			.gpio = 0x8000,
		},
	},
	[SAA7134_BOARD_BMK_MPEX_NOTUNER] = {
		/* "Andrew de Quincey" <adq@lidskialf.net> */
		.name		= "BMK MPEX No Tuner",
		.audio_clock	= 0x200000,
		.tuner_type	= TUNER_ABSENT,
		.inputs         = {{
			.name = name_comp1,
			.vmux = 4,
			.amux = LINE1,
		},{
			.name = name_comp2,
			.vmux = 3,
			.amux = LINE1,
		},{
			.name = name_comp3,
			.vmux = 0,
			.amux = LINE1,
		},{
			.name = name_comp4,
			.vmux = 1,
			.amux = LINE1,
		},{
			.name = name_svideo,
			.vmux = 8,
			.amux = LINE1,
		}},
		.i2s_rate  = 48000,
		.has_ts    = 1,
		.video_out = CCIR656,
	},
	[SAA7134_BOARD_VIDEOMATE_TV] = {
		.name           = "Compro VideoMate TV",
		.audio_clock    = 0x00187de7,
		.tuner_type     = TUNER_PHILIPS_NTSC_M,
                .inputs         = {{
                        .name = name_svideo,
                        .vmux = 8,
                        .amux = LINE1,
                },{
                        .name = name_comp1,
                        .vmux = 3,
                        .amux = LINE1,
                },{
                        .name = name_tv,
                        .vmux = 1,
                        .amux = LINE2,
                        .tv   = 1,
                }},
        },
	[SAA7134_BOARD_CRONOS_PLUS] = {
		/* gpio pins:
		   0  .. 3   BASE_ID
		   4  .. 7   PROTECT_ID
		   8  .. 11  USER_OUT
		   12 .. 13  USER_IN
		   14 .. 15  VIDIN_SEL */
		.name           = "Matrox CronosPlus",
		.tuner_type     = TUNER_ABSENT,
		.gpiomask       = 0xcf00,
                .inputs         = {{
                        .name = name_comp1,
                        .vmux = 0,
			.gpio = 2 << 14,
		},{
                        .name = name_comp2,
                        .vmux = 0,
			.gpio = 1 << 14,
		},{
                        .name = name_comp3,
                        .vmux = 0,
			.gpio = 0 << 14,
		},{
                        .name = name_comp4,
                        .vmux = 0,
			.gpio = 3 << 14,
		},{
			.name = name_svideo,
			.vmux = 8,
			.gpio = 2 << 14,
                }},
        },
	[SAA7134_BOARD_MD2819] = {
		.name           = "Medion 2819/ AverMedia M156",
		.audio_clock    = 0x00187de7,
		.tuner_type     = TUNER_PHILIPS_FM1216ME_MK3,
		.need_tda9887   = 1,
		.inputs         = {{
			.name = name_tv,
			.vmux = 1,
			.amux = TV,
			.tv   = 1,
		},{
			.name = name_comp1,
			.vmux = 0,
			.amux = LINE2,
		},{
			.name = name_comp2,
			.vmux = 3,
			.amux = LINE2,
		},{
			.name = name_svideo,
			.vmux = 8,
			.amux = LINE2,
		}},
		.radio = {
			.name = name_radio,
			.amux = LINE2,
		},
	},
	[SAA7134_BOARD_BMK_MPEX_TUNER] = {
		/* "Greg Wickham <greg.wickham@grangenet.net> */
		.name           = "BMK MPEX Tuner",
		.audio_clock    = 0x200000,
		.tuner_type     = TUNER_PHILIPS_PAL,
		.inputs         = {{
			.name = name_comp1,
			.vmux = 1,
			.amux = LINE1,
		},{
			.name = name_svideo,
			.vmux = 8,
			.amux = LINE1,
		},{
			.name = name_tv,
			.vmux = 3,
			.amux = LINE2,
			.tv   = 1,
		}},
		.i2s_rate  = 48000,
		.has_ts    = 1,
		.video_out = CCIR656,
        },
        [SAA7134_BOARD_ASUSTEK_TVFM7133] = {
                .name           = "ASUS TV-FM 7133",
                .audio_clock    = 0x00187de7,
		// probably wrong, the 7133 one is the NTSC version ...
		// .tuner_type     = TUNER_PHILIPS_FM1236_MK3
                .tuner_type     = TUNER_LG_NTSC_NEW_TAPC,
                .need_tda9887   = 1,
                .inputs         = {{
                        .name = name_tv,
                        .vmux = 1,
                        .amux = TV,
                        .tv   = 1,
		},{
                        .name = name_comp1,
                        .vmux = 4,
                        .amux = LINE2,
                },{
                        .name = name_svideo,
                        .vmux = 6,
                        .amux = LINE2,
                }},
                .radio = {
                        .name = name_radio,
                        .amux = LINE1,
                },
        },
	[SAA7134_BOARD_PINNACLE_PCTV_STEREO] = {
                .name           = "Pinnacle PCTV Stereo (saa7134)",
                .audio_clock    = 0x00187de7,
                .tuner_type     = TUNER_MT2032,
                .need_tda9887   = 1,
                .inputs         = {{
                        .name = name_tv,
                        .vmux = 3,
                        .amux = TV,
                        .tv   = 1,
                },{
                        .name = name_comp1,
                        .vmux = 0,
                        .amux = LINE2,
                },{
                        .name = name_comp2,
                        .vmux = 1,
                        .amux = LINE2,
                },{
                        .name = name_svideo,
                        .vmux = 8,
                        .amux = LINE2,
                }},
        },
	[SAA7134_BOARD_MANLI_MTV002] = {
		/* Ognjen Nastic <ognjen@logosoft.ba> */
		.name           = "Manli MuchTV M-TV002",
		.audio_clock    = 0x00200000,
		.tuner_type     = TUNER_PHILIPS_PAL,
		.inputs         = {{
			.name = name_svideo,
			.vmux = 8,
			.amux = LINE1,
		},{
			.name   = name_comp1,
			.vmux   = 1,
			.amux   = LINE1,
		},{
			.name = name_tv,
			.vmux = 3,
			.amux = LINE2,
			.tv   = 1,
		}},
		.radio = {
			.name = name_radio,
			.amux = LINE2,
		},
	},
	[SAA7134_BOARD_MANLI_MTV001] = {
		/* Ognjen Nastic <ognjen@logosoft.ba> UNTESTED */
		.name           = "Manli MuchTV M-TV001",
		.audio_clock    = 0x00200000,
		.tuner_type     = TUNER_PHILIPS_PAL,
		.inputs         = {{
			.name = name_svideo,
			.vmux = 8,
			.amux = LINE1,
		},{			
			.name = name_comp1,
			.vmux = 1,
			.amux = LINE1,
		},{
			.name = name_tv,
			.vmux = 3,
			.amux = LINE2,
			.tv   = 1,
		}},
        },
	[SAA7134_BOARD_TG3000TV] = {
		/* TransGear 3000TV */
		.name           = "Nagase Sangyo TransGear 3000TV",
		.audio_clock    = 0x00187de7,
		.tuner_type     = TUNER_PHILIPS_NTSC_M,
		.inputs         = {{
			.name = name_tv,
			.vmux = 1,
			.amux = LINE2,
			.tv   = 1,
		},{
			.name = name_comp1,
			.vmux = 3,
			.amux = LINE2,
		},{
			.name = name_svideo,
			.vmux = 8,
			.amux = LINE2,
		}},
	},
        [SAA7134_BOARD_ECS_TVP3XP] = {
                .name           = "Elitegroup ECS TVP3XP FM1216 Tuner Card(PAL-BG,FM) ",
                .audio_clock    = 0x187de7,  // xtal 32.1 MHz
                .tuner_type     = TUNER_PHILIPS_PAL,
                .inputs         = {{
                        .name   = name_tv,
                        .vmux   = 1,
                        .amux   = TV,
                        .tv     = 1,
                },{
                        .name   = name_tv_mono,
                        .vmux   = 1,
                        .amux   = LINE2,
                        .tv     = 1,
                },{
                        .name   = name_comp1,
                        .vmux   = 3,
                        .amux   = LINE1,
                },{
                        .name   = name_svideo,
                        .vmux   = 8,
                        .amux   = LINE1,
		},{
			.name   = "CVid over SVid",
			.vmux   = 0,
			.amux   = LINE1,
		}},
                .radio = {
                        .name   = name_radio,
                        .amux   = LINE2,
                },
        },
        [SAA7134_BOARD_ECS_TVP3XP_4CB5] = {
                .name           = "Elitegroup ECS TVP3XP FM1236 Tuner Card (NTSC,FM)",
                .audio_clock    = 0x187de7,
                .tuner_type     = TUNER_PHILIPS_NTSC,
                .inputs         = {{
                        .name   = name_tv,
                        .vmux   = 1,
                        .amux   = TV,
                        .tv     = 1,
                },{
                        .name   = name_tv_mono,
                        .vmux   = 1,
                        .amux   = LINE2,
                        .tv     = 1,
                },{
                        .name   = name_comp1,
                        .vmux   = 3,
                        .amux   = LINE1,
                },{
                        .name   = name_svideo,
                        .vmux   = 8,
                        .amux   = LINE1,
                },{
                        .name   = "CVid over SVid",
                        .vmux   = 0,
                        .amux   = LINE1,
                }},
                .radio = {
                        .name   = name_radio,
                        .amux   = LINE2,
                },
        },
	[SAA7134_BOARD_AVACSSMARTTV] = {
		/* Roman Pszonczenko <romka@kolos.math.uni.lodz.pl> */
		.name           = "AVACS SmartTV",
		.audio_clock    = 0x00187de7,
		.tuner_type     = TUNER_PHILIPS_PAL,
		.inputs         = {{
			.name = name_tv,
			.vmux = 1,
			.amux = TV,
			.tv   = 1,
                },{
			.name = name_tv_mono,
			.vmux = 1,
			.amux = LINE2,
			.tv   = 1,
		},{
			.name = name_comp1,
			.vmux = 0,
			.amux = LINE2,
		},{
			.name = name_comp2,
			.vmux = 3,
			.amux = LINE2,
		},{
			.name = name_svideo,
			.vmux = 8,
			.amux = LINE2,
		}},
		.radio = {
			.name = name_radio,
			.amux = LINE2,
			.gpio = 0x200000,
		},
	},
	[SAA7134_BOARD_AVERMEDIA_DVD_EZMAKER] = {
		/* Michael Smith <msmith@cbnco.com> */
		.name           = "AVerMedia DVD EZMaker",
		.audio_clock    = 0x00187de7,
		.tuner_type     = TUNER_ABSENT,
		.inputs         = {{
			.name = name_comp1,
			.vmux = 3,
		}},
	},
};
const unsigned int saa7134_bcount = ARRAY_SIZE(saa7134_boards);

/* ------------------------------------------------------------------ */
/* PCI ids + subsystem IDs                                            */

struct pci_device_id saa7134_pci_tbl[] = {
	{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
		.subvendor    = PCI_VENDOR_ID_PHILIPS,
		.subdevice    = 0x2001,
		.driver_data  = SAA7134_BOARD_PROTEUS_PRO,
        },{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7133,
		.subvendor    = PCI_VENDOR_ID_PHILIPS,
		.subdevice    = 0x2001,
		.driver_data  = SAA7134_BOARD_PROTEUS_PRO,
        },{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
		.subvendor    = PCI_VENDOR_ID_PHILIPS,
		.subdevice    = 0x6752,
		.driver_data  = SAA7134_BOARD_EMPRESS,
	},{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
                .subvendor    = 0x1131,
                .subdevice    = 0x4e85,
		.driver_data  = SAA7134_BOARD_MONSTERTV,
        },{
                .vendor       = PCI_VENDOR_ID_PHILIPS,
                .device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
                .subvendor    = 0x153B,
                .subdevice    = 0x1142,
                .driver_data  = SAA7134_BOARD_CINERGY400,
        },{
                .vendor       = PCI_VENDOR_ID_PHILIPS,
                .device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
                .subvendor    = 0x153B,
                .subdevice    = 0x1143,
                .driver_data  = SAA7134_BOARD_CINERGY600,
        },{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
		.subvendor    = 0x5168,
		.subdevice    = 0x0138,
		.driver_data  = SAA7134_BOARD_FLYVIDEO3000,
        },{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
                .device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
                .subvendor    = 0x4e42,				//"Typhoon PCI Capture TV Card" Art.No. 50673
                .subdevice    = 0x0138,
                .driver_data  = SAA7134_BOARD_FLYVIDEO3000,
        },{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7130,
		.subvendor    = 0x5168,
		.subdevice    = 0x0138,
		.driver_data  = SAA7134_BOARD_FLYVIDEO2000,
        },{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
		.subvendor    = 0x16be,
		.subdevice    = 0x0003,
		.driver_data  = SAA7134_BOARD_MD7134,
	},{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7130,
		.subvendor    = 0x1048,
		.subdevice    = 0x226b,
		.driver_data  = SAA7134_BOARD_ELSA,
	},{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7130,
		.subvendor    = 0x1048,
		.subdevice    = 0x226b,
		.driver_data  = SAA7134_BOARD_ELSA_500TV,
	},{
                .vendor       = PCI_VENDOR_ID_PHILIPS,
                .device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
                .subvendor    = PCI_VENDOR_ID_ASUSTEK,
                .subdevice    = 0x4842,
                .driver_data  = SAA7134_BOARD_ASUSTeK_TVFM7134,
	},{
                .vendor       = PCI_VENDOR_ID_PHILIPS,
                .device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
                .subvendor    = PCI_VENDOR_ID_ASUSTEK,
                .subdevice    = 0x4830,
                .driver_data  = SAA7134_BOARD_ASUSTeK_TVFM7134,
        },{
                .vendor       = PCI_VENDOR_ID_PHILIPS,
                .device       = PCI_DEVICE_ID_PHILIPS_SAA7133,
                .subvendor    = PCI_VENDOR_ID_ASUSTEK,
                .subdevice    = 0x4843,
                .driver_data  = SAA7134_BOARD_ASUSTEK_TVFM7133,
	},{
                .vendor       = PCI_VENDOR_ID_PHILIPS,
                .device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
                .subvendor    = PCI_VENDOR_ID_ASUSTEK,
                .subdevice    = 0x4840,
                .driver_data  = SAA7134_BOARD_ASUSTeK_TVFM7134,
        },{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
		.subvendor    = PCI_VENDOR_ID_PHILIPS,
		.subdevice    = 0xfe01,
		.driver_data  = SAA7134_BOARD_TVSTATION_RDS,
	},{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
		.subvendor    = 0x1894,
		.subdevice    = 0xfe01,
		.driver_data  = SAA7134_BOARD_TVSTATION_RDS,
	},{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
		.subvendor    = 0x1894,
		.subdevice    = 0xa006,
		.driver_data  = SAA7134_BOARD_TVSTATION_DVR,
	},{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
                .subvendor    = 0x1131,
                .subdevice    = 0x7133,
		.driver_data  = SAA7134_BOARD_VA1000POWER,
        },{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7130,
                .subvendor    = PCI_VENDOR_ID_PHILIPS,
                .subdevice    = 0x2001,
		.driver_data  = SAA7134_BOARD_10MOONSTVMASTER,
        },{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7133,
                .subvendor    = 0x185b,
                .subdevice    = 0xc100,
		.driver_data  = SAA7134_BOARD_VIDEOMATE_TV,
        },{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7130,
                .subvendor    = PCI_VENDOR_ID_MATROX,
                .subdevice    = 0x48d0,
		.driver_data  = SAA7134_BOARD_CRONOS_PLUS,
	},{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
                .subvendor    = 0x1461, /* Avermedia Technologies Inc */
                .subdevice    = 0xa70b,
		.driver_data  = SAA7134_BOARD_MD2819,
	},{
		/* AverMedia Studio 305, using AverMedia M156 entry for now */
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
                .subvendor    = 0x1461, /* Avermedia Technologies Inc */
                .subdevice    = 0x2115,
		.driver_data  = SAA7134_BOARD_MD2819,
	},{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7130,
                .subvendor    = 0x1461, /* Avermedia Technologies Inc */
                .subdevice    = 0x10ff,
		.driver_data  = SAA7134_BOARD_AVERMEDIA_DVD_EZMAKER,
        },{
		/* TransGear 3000TV */
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7130,
                .subvendor    = 0x1461, /* Avermedia Technologies Inc */
                .subdevice    = 0x050c,
		.driver_data  = SAA7134_BOARD_TG3000TV,
	},{
                .vendor       = PCI_VENDOR_ID_PHILIPS,
                .device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
                .subvendor    = 0x11bd,
                .subdevice    = 0x002b,
                .driver_data  = SAA7134_BOARD_PINNACLE_PCTV_STEREO,
        },{
                .vendor       = PCI_VENDOR_ID_PHILIPS,
                .device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
                .subvendor    = 0x1019,
                .subdevice    = 0x4cb4,
                .driver_data  = SAA7134_BOARD_ECS_TVP3XP,
        },{
                .vendor       = PCI_VENDOR_ID_PHILIPS,
                .device       = PCI_DEVICE_ID_PHILIPS_SAA7133,
                .subvendor    = 0x1019,
                .subdevice    = 0x4cb5,
                .driver_data  = SAA7134_BOARD_ECS_TVP3XP_4CB5,
        },{
		
		/* --- boards without eeprom + subsystem ID --- */
                .vendor       = PCI_VENDOR_ID_PHILIPS,
                .device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
                .subvendor    = PCI_VENDOR_ID_PHILIPS,
		.subdevice    = 0,
		.driver_data  = SAA7134_BOARD_NOAUTO,
        },{
                .vendor       = PCI_VENDOR_ID_PHILIPS,
                .device       = PCI_DEVICE_ID_PHILIPS_SAA7130,
                .subvendor    = PCI_VENDOR_ID_PHILIPS,
		.subdevice    = 0,
		.driver_data  = SAA7134_BOARD_NOAUTO,
	},{
		
		/* --- default catch --- */
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7130,
                .subvendor    = PCI_ANY_ID,
                .subdevice    = PCI_ANY_ID,
		.driver_data  = SAA7134_BOARD_UNKNOWN,
        },{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7133,
                .subvendor    = PCI_ANY_ID,
                .subdevice    = PCI_ANY_ID,
		.driver_data  = SAA7134_BOARD_UNKNOWN,
        },{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7134,
                .subvendor    = PCI_ANY_ID,
                .subdevice    = PCI_ANY_ID,
		.driver_data  = SAA7134_BOARD_UNKNOWN,
        },{
		.vendor       = PCI_VENDOR_ID_PHILIPS,
		.device       = PCI_DEVICE_ID_PHILIPS_SAA7135,
                .subvendor    = PCI_ANY_ID,
                .subdevice    = PCI_ANY_ID,
		.driver_data  = SAA7134_BOARD_UNKNOWN,
	},{
		/* --- end of list --- */
	}
};
MODULE_DEVICE_TABLE(pci, saa7134_pci_tbl);

/* ----------------------------------------------------------- */
/* flyvideo tweaks                                             */

#if 0
static struct {
	char  *model;
	int   tuner_type;
} fly_list[0x20] = {
	/* default catch ... */
	[ 0 ... 0x1f ] = {
		.model      = "UNKNOWN",
		.tuner_type = TUNER_ABSENT,
	},
	/* ... the ones known so far */
	[ 0x05 ] = {
		.model      = "PAL-BG",
		.tuner_type = TUNER_LG_PAL_NEW_TAPC,
	},
	[ 0x10 ] = {
		.model      = "PAL-BG / PAL-DK",
		.tuner_type = TUNER_PHILIPS_PAL,
	},
	[ 0x15 ] = {
		.model      = "NTSC",
		.tuner_type = TUNER_ABSENT /* FIXME */,
	},
};
#endif

static void board_flyvideo(struct saa7134_dev *dev)
{
#if 0
	u32 value;
	int index;

	value = dev->gpio_value;
	index = (value & 0x1f00) >> 8;
	printk(KERN_INFO "%s: flyvideo: gpio is 0x%x [model=%s,tuner=%d]\n",
	       dev->name, value, fly_list[index].model,
	       fly_list[index].tuner_type);
	dev->tuner_type = fly_list[index].tuner_type;
#endif
}

/* ----------------------------------------------------------- */

int saa7134_board_init(struct saa7134_dev *dev)
{
	// Always print gpio, often manufacturers encode tuner type and other info.
	saa_writel(SAA7134_GPIO_GPMODE0 >> 2, 0);
	dev->gpio_value = saa_readl(SAA7134_GPIO_GPSTATUS0 >> 2);
	printk(KERN_INFO "%s: board init: gpio is %x\n", dev->name, dev->gpio_value);

	switch (dev->board) {
	case SAA7134_BOARD_FLYVIDEO2000:
	case SAA7134_BOARD_FLYVIDEO3000:
		board_flyvideo(dev);
		dev->has_remote = 1;
		break;
	case SAA7134_BOARD_CINERGY400:
	case SAA7134_BOARD_CINERGY600:
	case SAA7134_BOARD_ECS_TVP3XP:
	case SAA7134_BOARD_ECS_TVP3XP_4CB5:
		dev->has_remote = 1;
		break;
	}
	return 0;
}

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
