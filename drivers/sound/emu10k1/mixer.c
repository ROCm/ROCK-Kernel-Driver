/*
 **********************************************************************
 *     mixer.c - /dev/mixer interface for emu10k1 driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 *     This program uses some code from es1371.c, Copyright 1998-1999
 *     Thomas Sailer
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *     November 2, 1999     Alan Cox        cleaned up stuff
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */

#define __NO_VERSION__		/* Kernel version only defined once */
#include <linux/module.h>
#include <linux/version.h>
#include <linux/bitops.h>
#include <asm/uaccess.h>

#include "hwaccess.h"
#include "8010.h"
#include "recmgr.h"

#define AC97_PESSIMISTIC
#undef OSS_DOCUMENTED_MIXER_SEMANTICS

#define vol_to_hw_5(swvol) (31 - (((swvol) * 31) / 100))
#define vol_to_hw_4(swvol) (15 - (((swvol) * 15) / 100))

#define vol_to_sw_5(hwvol) (((31 - (hwvol)) * 100) / 31)
#define vol_to_sw_4(hwvol) (((15 - (hwvol)) * 100) / 15)

#define DM_MUTE 0x80000000

#ifdef PRIVATE_PCM_VOLUME
struct sblive_pcm_volume_rec sblive_pcm_volume[MAX_PCM_CHANNELS];
u16 pcm_last_mixer = 0x6464;
#endif

/* Mapping arrays */
static const unsigned int recsrc[] = {
	SOUND_MASK_MIC,
	SOUND_MASK_CD,
	SOUND_MASK_VIDEO,
	SOUND_MASK_LINE1,
	SOUND_MASK_LINE,
	SOUND_MASK_VOLUME,
	SOUND_MASK_OGAIN,	/* Used to be PHONEOUT */
	SOUND_MASK_PHONEIN,
#ifdef TONE_CONTROL
	SOUND_MASK_TREBLE,
	SOUND_MASK_BASS,
#endif
};

static const unsigned char volreg[SOUND_MIXER_NRDEVICES] = {
	/* 5 bit stereo */
	[SOUND_MIXER_LINE] = AC97_LINEINVOLUME,
	[SOUND_MIXER_CD] = AC97_CDVOLUME,
	[SOUND_MIXER_VIDEO] = AC97_VIDEOVOLUME,
	[SOUND_MIXER_LINE1] = AC97_AUXVOLUME,

/*	[SOUND_MIXER_PCM] = AC97_PCMOUTVOLUME, */
	/* 5 bit stereo, setting 6th bit equal to maximum attenuation */

/*	[SOUND_MIXER_VOLUME] = AC97_MASTERVOLUME, */
	[SOUND_MIXER_PHONEOUT] = AC97_HEADPHONEVOLUME,
	/* 5 bit mono, setting 6th bit equal to maximum attenuation */
	[SOUND_MIXER_OGAIN] = AC97_MASTERVOLUMEMONO,
	/* 5 bit mono */
	[SOUND_MIXER_PHONEIN] = AC97_PHONEVOLUME,
	/* 4 bit mono but shifted by 1 */
	[SOUND_MIXER_SPEAKER] = AC97_PCBEEPVOLUME,
	/* 5 bit mono, 7th bit = preamp */
	[SOUND_MIXER_MIC] = AC97_MICVOLUME,
	/* 4 bit stereo */
	[SOUND_MIXER_RECLEV] = AC97_RECORDGAIN,
	/* 4 bit mono */
	[SOUND_MIXER_IGAIN] = AC97_RECORDGAINMIC,
	/* test code */
	[SOUND_MIXER_BASS] = AC97_GENERALPURPOSE,
	[SOUND_MIXER_TREBLE] = AC97_MASTERTONE,
	[SOUND_MIXER_LINE2] = AC97_PCMOUTVOLUME,
	[SOUND_MIXER_DIGITAL2] = AC97_MASTERVOLUME
};

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS

#define swab(x) ((((x) >> 8) & 0xff) | (((x) << 8) & 0xff00))

/* FIXME: mixer_rdch() is broken. */

static int mixer_rdch(struct emu10k1_card *card, unsigned int ch, int *arg)
{
	u16 reg;
	int j;
	int nL, nR;

	switch (ch) {
	case SOUND_MIXER_PCM:
	case SOUND_MIXER_VOLUME:
#ifdef TONE_CONTROL
	case SOUND_MIXER_TREBLE:
        case SOUND_MIXER_BASS:
#endif
                return put_user(0x0000, (int *) arg);
	default:
		break;
	}

	if(card->isaps)
		return -EINVAL;

	switch (ch) {
	case SOUND_MIXER_LINE:
	case SOUND_MIXER_CD:
	case SOUND_MIXER_VIDEO:
	case SOUND_MIXER_LINE1:
		sblive_readac97(card, volreg[ch], &reg);
		nL = ((~(reg >> 8) & 0x1f) * 100) / 32;
		nR = (~(reg & 0x1f) * 100) / 32;
		DPD(2, "mixer_rdch: l=%d, r=%d\n", nL, nR);
		return put_user(reg & 0x8000 ? 0 : (nL << 8) | nR, (int *) arg);

	case SOUND_MIXER_OGAIN:
	case SOUND_MIXER_PHONEIN:
		sblive_readac97(card, volreg[ch], &reg);
		return put_user(reg & 0x8000 ? 0 : ~(reg & 0x1f) * 0x64 / 0x20 * 0x101, (int *) arg);

	case SOUND_MIXER_SPEAKER:
		sblive_readac97(card, volreg[ch], &reg);
		return put_user(reg & 0x8000 ? 0 : ~((reg >> 1) & 0xf) * 0x64 / 0x10 * 0x101, (int *) arg);

	case SOUND_MIXER_MIC:
		sblive_readac97(card, volreg[ch], &reg);
		return put_user(reg & 0x8000 ? 0 : ~(reg & 0x1f) * 0x64 / 0x20 * 0x101 + ((reg & 0x40) ? 0x1e1e : 0), (int *) arg);

	case SOUND_MIXER_RECLEV:
		sblive_readac97(card, volreg[ch], &reg);
		nL = ((~(reg >> 8) & 0x1f) * 100) / 16;
		nR = (~(reg & 0x1f) * 100) / 16;
		return put_user(reg & 0x8000 ? 0 : (nL << 8) | nR, (int *) arg);

	default:
		return -EINVAL;
	}
}

#endif				/* OSS_DOCUMENTED_MIXER_SEMANTICS */

static const unsigned char volidx[SOUND_MIXER_NRDEVICES] = {
	/* 5 bit stereo */
	[SOUND_MIXER_LINE] = 1,
	[SOUND_MIXER_CD] = 2,
	[SOUND_MIXER_VIDEO] = 3,
	[SOUND_MIXER_LINE1] = 4,
	[SOUND_MIXER_PCM] = 5,
	/* 6 bit stereo */
	[SOUND_MIXER_VOLUME] = 6,
	[SOUND_MIXER_PHONEOUT] = 7,
	/* 6 bit mono */
	[SOUND_MIXER_OGAIN] = 8,
	[SOUND_MIXER_PHONEIN] = 9,
	/* 4 bit mono but shifted by 1 */
	[SOUND_MIXER_SPEAKER] = 10,
	/* 6 bit mono + preamp */
	[SOUND_MIXER_MIC] = 11,
	/* 4 bit stereo */
	[SOUND_MIXER_RECLEV] = 12,
	/* 4 bit mono */
	[SOUND_MIXER_IGAIN] = 13,
	[SOUND_MIXER_TREBLE] = 14,
	[SOUND_MIXER_BASS] = 15,
	[SOUND_MIXER_LINE2] = 16,
	[SOUND_MIXER_LINE3] = 17,
	[SOUND_MIXER_DIGITAL1] = 18,
	[SOUND_MIXER_DIGITAL2] = 19
};

#ifdef TONE_CONTROL

static const u32 bass_table[41][5] = {
	{ 0x3e4f844f, 0x84ed4cc3, 0x3cc69927, 0x7b03553a, 0xc4da8486 },
	{ 0x3e69a17a, 0x84c280fb, 0x3cd77cd4, 0x7b2f2a6f, 0xc4b08d1d },
	{ 0x3e82ff42, 0x849991d5, 0x3ce7466b, 0x7b5917c6, 0xc48863ee },
	{ 0x3e9bab3c, 0x847267f0, 0x3cf5ffe8, 0x7b813560, 0xc461f22c },
	{ 0x3eb3b275, 0x844ced29, 0x3d03b295, 0x7ba79a1c, 0xc43d223b },
	{ 0x3ecb2174, 0x84290c8b, 0x3d106714, 0x7bcc5ba3, 0xc419dfa5 },
	{ 0x3ee2044b, 0x8406b244, 0x3d1c2561, 0x7bef8e77, 0xc3f8170f },
	{ 0x3ef86698, 0x83e5cb96, 0x3d26f4d8, 0x7c114600, 0xc3d7b625 },
	{ 0x3f0e5390, 0x83c646c9, 0x3d30dc39, 0x7c319498, 0xc3b8ab97 },
	{ 0x3f23d60b, 0x83a81321, 0x3d39e1af, 0x7c508b9c, 0xc39ae704 },
	{ 0x3f38f884, 0x838b20d2, 0x3d420ad2, 0x7c6e3b75, 0xc37e58f1 },
	{ 0x3f4dc52c, 0x836f60ef, 0x3d495cab, 0x7c8ab3a6, 0xc362f2be },
	{ 0x3f6245e8, 0x8354c565, 0x3d4fdbb8, 0x7ca602d6, 0xc348a69b },
	{ 0x3f76845f, 0x833b40ec, 0x3d558bf0, 0x7cc036df, 0xc32f677c },
	{ 0x3f8a8a03, 0x8322c6fb, 0x3d5a70c4, 0x7cd95cd7, 0xc317290b },
	{ 0x3f9e6014, 0x830b4bc3, 0x3d5e8d25, 0x7cf1811a, 0xc2ffdfa5 },
	{ 0x3fb20fae, 0x82f4c420, 0x3d61e37f, 0x7d08af56, 0xc2e9804a },
	{ 0x3fc5a1cc, 0x82df2592, 0x3d6475c3, 0x7d1ef294, 0xc2d40096 },
	{ 0x3fd91f55, 0x82ca6632, 0x3d664564, 0x7d345541, 0xc2bf56b9 },
	{ 0x3fec9120, 0x82b67cac, 0x3d675356, 0x7d48e138, 0xc2ab796e },
	{ 0x40000000, 0x82a36037, 0x3d67a012, 0x7d5c9fc9, 0xc2985fee },
	{ 0x401374c7, 0x8291088a, 0x3d672b93, 0x7d6f99c3, 0xc28601f2 },
	{ 0x4026f857, 0x827f6dd7, 0x3d65f559, 0x7d81d77c, 0xc27457a3 },
	{ 0x403a939f, 0x826e88c5, 0x3d63fc63, 0x7d9360d4, 0xc2635996 },
	{ 0x404e4faf, 0x825e5266, 0x3d613f32, 0x7da43d42, 0xc25300c6 },
	{ 0x406235ba, 0x824ec434, 0x3d5dbbc3, 0x7db473d7, 0xc243468e },
	{ 0x40764f1f, 0x823fd80c, 0x3d596f8f, 0x7dc40b44, 0xc23424a2 },
	{ 0x408aa576, 0x82318824, 0x3d545787, 0x7dd309e2, 0xc2259509 },
	{ 0x409f4296, 0x8223cf0b, 0x3d4e7012, 0x7de175b5, 0xc2179218 },
	{ 0x40b430a0, 0x8216a7a1, 0x3d47b505, 0x7def5475, 0xc20a1670 },
	{ 0x40c97a0a, 0x820a0d12, 0x3d4021a1, 0x7dfcab8d, 0xc1fd1cf5 },
	{ 0x40df29a6, 0x81fdfad6, 0x3d37b08d, 0x7e098028, 0xc1f0a0ca },
	{ 0x40f54ab1, 0x81f26ca9, 0x3d2e5bd1, 0x7e15d72b, 0xc1e49d52 },
	{ 0x410be8da, 0x81e75e89, 0x3d241cce, 0x7e21b544, 0xc1d90e24 },
	{ 0x41231051, 0x81dcccb3, 0x3d18ec37, 0x7e2d1ee6, 0xc1cdef10 },
	{ 0x413acdd0, 0x81d2b39e, 0x3d0cc20a, 0x7e38184e, 0xc1c33c13 },
	{ 0x41532ea7, 0x81c90ffb, 0x3cff9585, 0x7e42a58b, 0xc1b8f15a },
	{ 0x416c40cd, 0x81bfdeb2, 0x3cf15d21, 0x7e4cca7c, 0xc1af0b3f },
	{ 0x418612ea, 0x81b71cdc, 0x3ce20e85, 0x7e568ad3, 0xc1a58640 },
	{ 0x41a0b465, 0x81aec7c5, 0x3cd19e7c, 0x7e5fea1e, 0xc19c5f03 },
	{ 0x41bc3573, 0x81a6dcea, 0x3cc000e9, 0x7e68ebc2, 0xc1939250 }
};

static const u32 treble_table[41][5] = {
	{ 0x0125cba9, 0xfed5debd, 0x00599b6c, 0x0d2506da, 0xfa85b354 },
	{ 0x0142f67e, 0xfeb03163, 0x0066cd0f, 0x0d14c69d, 0xfa914473 },
	{ 0x016328bd, 0xfe860158, 0x0075b7f2, 0x0d03eb27, 0xfa9d32d2 },
	{ 0x0186b438, 0xfe56c982, 0x00869234, 0x0cf27048, 0xfaa97fca },
	{ 0x01adf358, 0xfe21f5fe, 0x00999842, 0x0ce051c2, 0xfab62ca5 },
	{ 0x01d949fa, 0xfde6e287, 0x00af0d8d, 0x0ccd8b4a, 0xfac33aa7 },
	{ 0x02092669, 0xfda4d8bf, 0x00c73d4c, 0x0cba1884, 0xfad0ab07 },
	{ 0x023e0268, 0xfd5b0e4a, 0x00e27b54, 0x0ca5f509, 0xfade7ef2 },
	{ 0x0278645c, 0xfd08a2b0, 0x01012509, 0x0c911c63, 0xfaecb788 },
	{ 0x02b8e091, 0xfcac9d1a, 0x0123a262, 0x0c7b8a14, 0xfafb55df },
	{ 0x03001a9a, 0xfc45e9ce, 0x014a6709, 0x0c65398f, 0xfb0a5aff },
	{ 0x034ec6d7, 0xfbd3576b, 0x0175f397, 0x0c4e2643, 0xfb19c7e4 },
	{ 0x03a5ac15, 0xfb5393ee, 0x01a6d6ed, 0x0c364b94, 0xfb299d7c },
	{ 0x0405a562, 0xfac52968, 0x01ddafae, 0x0c1da4e2, 0xfb39dca5 },
	{ 0x046fa3fe, 0xfa267a66, 0x021b2ddd, 0x0c042d8d, 0xfb4a8631 },
	{ 0x04e4b17f, 0xf975be0f, 0x0260149f, 0x0be9e0f2, 0xfb5b9ae0 },
	{ 0x0565f220, 0xf8b0fbe5, 0x02ad3c29, 0x0bceba73, 0xfb6d1b60 },
	{ 0x05f4a745, 0xf7d60722, 0x030393d4, 0x0bb2b578, 0xfb7f084d },
	{ 0x06923236, 0xf6e279bd, 0x03642465, 0x0b95cd75, 0xfb916233 },
	{ 0x07401713, 0xf5d3aef9, 0x03d01283, 0x0b77fded, 0xfba42984 },
	{ 0x08000000, 0xf4a6bd88, 0x0448a161, 0x0b594278, 0xfbb75e9f },
	{ 0x08d3c097, 0xf3587131, 0x04cf35a4, 0x0b3996c9, 0xfbcb01cb },
	{ 0x09bd59a2, 0xf1e543f9, 0x05655880, 0x0b18f6b2, 0xfbdf1333 },
	{ 0x0abefd0f, 0xf04956ca, 0x060cbb12, 0x0af75e2c, 0xfbf392e8 },
	{ 0x0bdb123e, 0xee806984, 0x06c739fe, 0x0ad4c962, 0xfc0880dd },
	{ 0x0d143a94, 0xec85d287, 0x0796e150, 0x0ab134b0, 0xfc1ddce5 },
	{ 0x0e6d5664, 0xea547598, 0x087df0a0, 0x0a8c9cb6, 0xfc33a6ad },
	{ 0x0fe98a2a, 0xe7e6ba35, 0x097edf83, 0x0a66fe5b, 0xfc49ddc2 },
	{ 0x118c4421, 0xe536813a, 0x0a9c6248, 0x0a4056d7, 0xfc608185 },
	{ 0x1359422e, 0xe23d19eb, 0x0bd96efb, 0x0a18a3bf, 0xfc77912c },
	{ 0x1554982b, 0xdef33645, 0x0d3942bd, 0x09efe312, 0xfc8f0bc1 },
	{ 0x1782b68a, 0xdb50deb1, 0x0ebf676d, 0x09c6133f, 0xfca6f019 },
	{ 0x19e8715d, 0xd74d64fd, 0x106fb999, 0x099b3337, 0xfcbf3cd6 },
	{ 0x1c8b07b8, 0xd2df56ab, 0x124e6ec8, 0x096f4274, 0xfcd7f060 },
	{ 0x1f702b6d, 0xcdfc6e92, 0x14601c10, 0x0942410b, 0xfcf108e5 },
	{ 0x229e0933, 0xc89985cd, 0x16a9bcfa, 0x09142fb5, 0xfd0a8451 },
	{ 0x261b5118, 0xc2aa8409, 0x1930bab6, 0x08e50fdc, 0xfd24604d },
	{ 0x29ef3f5d, 0xbc224f28, 0x1bfaf396, 0x08b4e3aa, 0xfd3e9a3b },
	{ 0x2e21a59b, 0xb4f2ba46, 0x1f0ec2d6, 0x0883ae15, 0xfd592f33 },
	{ 0x32baf44b, 0xad0c7429, 0x227308a3, 0x085172eb, 0xfd741bfd },
	{ 0x37c4448b, 0xa45ef51d, 0x262f3267, 0x081e36dc, 0xfd8f5d14 }
};

static void set_bass(struct emu10k1_card *card, int l, int r)
{
	int i;

	l = (l * 40 + 50) / 100;
	r = (r * 40 + 50) / 100;
	for (i = 0; i < 5; i++) {
		sblive_writeptr(card, FXGPREGBASE + 0x80 + (i * 2), 0, bass_table[l][i]);
		sblive_writeptr(card, FXGPREGBASE + 0x80 + (i * 2) + 1, 0, bass_table[r][i]);
	}
}

static void set_treble(struct emu10k1_card *card, int l, int r)
{
	int i;

	l = (l * 40 + 50) / 100;
	r = (r * 40 + 50) / 100;
	for (i = 0; i < 5; i++) {
		sblive_writeptr(card, FXGPREGBASE + 0x90 + (i * 2), 0, treble_table[l][i]);
		sblive_writeptr(card, FXGPREGBASE + 0x90 + (i * 2) + 1, 0, treble_table[r][i]);
	}
}

#endif

static const u32 db_table[101] = {
	0x00000000, 0x01571f82, 0x01674b41, 0x01783a1b, 0x0189f540,
	0x019c8651, 0x01aff763, 0x01c45306, 0x01d9a446, 0x01eff6b8,
	0x0207567a, 0x021fd03d, 0x0239714c, 0x02544792, 0x027061a1,
	0x028dcebb, 0x02ac9edc, 0x02cce2bf, 0x02eeabe8, 0x03120cb0,
	0x0337184e, 0x035de2df, 0x03868173, 0x03b10a18, 0x03dd93e9,
	0x040c3713, 0x043d0cea, 0x04702ff3, 0x04a5bbf2, 0x04ddcdfb,
	0x0518847f, 0x0555ff62, 0x05966005, 0x05d9c95d, 0x06206005,
	0x066a4a52, 0x06b7b067, 0x0708bc4c, 0x075d9a01, 0x07b6779d,
	0x08138561, 0x0874f5d5, 0x08dafde1, 0x0945d4ed, 0x09b5b4fd,
	0x0a2adad1, 0x0aa58605, 0x0b25f936, 0x0bac7a24, 0x0c3951d8,
	0x0ccccccc, 0x0d673b17, 0x0e08f093, 0x0eb24510, 0x0f639481,
	0x101d3f2d, 0x10dfa9e6, 0x11ab3e3f, 0x12806ac3, 0x135fa333,
	0x144960c5, 0x153e2266, 0x163e6cfe, 0x174acbb7, 0x1863d04d,
	0x198a1357, 0x1abe349f, 0x1c00db77, 0x1d52b712, 0x1eb47ee6,
	0x2026f30f, 0x21aadcb6, 0x23410e7e, 0x24ea64f9, 0x26a7c71d,
	0x287a26c4, 0x2a62812c, 0x2c61df84, 0x2e795779, 0x30aa0bcf,
	0x32f52cfe, 0x355bf9d8, 0x37dfc033, 0x3a81dda4, 0x3d43c038,
	0x4026e73c, 0x432ce40f, 0x46575af8, 0x49a8040f, 0x4d20ac2a,
	0x50c335d3, 0x54919a57, 0x588dead1, 0x5cba514a, 0x611911ea,
	0x65ac8c2f, 0x6a773c39, 0x6f7bbc23, 0x74bcc56c, 0x7a3d3272,
	0x7fffffff,
};

static void aps_update_digital(struct emu10k1_card *card)
{
	int i, l1, r1, l2, r2;
	
	i = card->arrwVol[volidx[SOUND_MIXER_VOLUME]];
	l1 = (i & 0xff);
	r1 = ((i >> 8) & 0xff);

	i = card->arrwVol[volidx[SOUND_MIXER_PCM]];
	l2 = (i & 0xff);
	r2 = ((i >> 8) & 0xff);
	
	for (i = 0; i < 108; i++) {
		if (card->digmix[i] != DM_MUTE) {
			if ((i % 18 >= 0) && (i % 18 < 4))
				card->digmix[i] = ((i & 1) ? ((u64) db_table[r1] * (u64) db_table[r2]) : ((u64) db_table[l1] * (u64) db_table[l2])) >> 31;
			else
				card->digmix[i] = (i & 1) ? db_table[r1] : db_table[l1];
			sblive_writeptr(card, FXGPREGBASE + 0x10 + i, 0, card->digmix[i]);
		}
	}
}

static void update_digital(struct emu10k1_card *card)
{
	int i, k, l1, r1, l2, r2, l3, r3, l4, r4;
	u64 j;

	i = card->arrwVol[volidx[SOUND_MIXER_VOLUME]];
	l1 = (i & 0xff);
	r1 = ((i >> 8) & 0xff);
	i = card->arrwVol[volidx[SOUND_MIXER_LINE3]];
	l2 = i & 0xff;
	r2 = (i >> 8) & 0xff;

	i = card->arrwVol[volidx[SOUND_MIXER_PCM]];
	l3 = i & 0xff;
	r3 = (i >> 8) & 0xff;

	i = card->arrwVol[volidx[SOUND_MIXER_DIGITAL1]];
	l4 = i & 0xff;
	r4 = (i >> 8) & 0xff;

	i = (r1 * r2) / 50;
	if (r2 > 50)
		r2 = 2 * r1 - i;
	else {
		r2 = r1;
		r1 = i;
	}

	i = (l1 * l2) / 50;
	if (l2 > 50)
		l2 = 2 * l1 - i;
	else {
		l2 = l1;
		l1 = i;
	}

	for (i = 0; i < 36; i++) {
		if (card->digmix[i] != DM_MUTE) {
			if (((i >= 0) && (i < 4)) || ((i >= 18) && (i < 22)))
				j = (i & 1) ? ((u64) db_table[r1] * (u64) db_table[r3]) : ((u64) db_table[l1] * (u64) db_table[l3]);
			else if ((i == 6) || (i == 7) || (i == 24) || (i == 25))
				j = (i & 1) ? ((u64) db_table[r1] * (u64) db_table[r4]) : ((u64) db_table[l1] * (u64) db_table[l4]);
			else
				j = ((i & 1) ? db_table[r1] : db_table[l1]) << 31;
			card->digmix[i] = j >> 31;
			sblive_writeptr(card, FXGPREGBASE + 0x10 + i, 0, card->digmix[i]);
		}
	}

	for (i = 72; i < 90; i++) {
		if (card->digmix[i] != DM_MUTE) {
			if ((i >= 72) && (i < 76))
				j = (i & 1) ? ((u64) db_table[r2] * (u64) db_table[r3]) : ((u64) db_table[l2] * (u64) db_table[l3]);
			else if ((i == 78) || (i == 79))
				j = (i & 1) ? ((u64) db_table[r2] * (u64) db_table[r4]) : ((u64) db_table[l2] * (u64) db_table[l4]);
			else
				j = ((i & 1) ? db_table[r2] : db_table[l2]) << 31;
			card->digmix[i] = j >> 31;
			sblive_writeptr(card, FXGPREGBASE + 0x10 + i, 0, card->digmix[i]);
		}
	}

	for (i = 36; i <= 90; i += 18) {
		if (i != 72) {
			for (k = 0; k < 4; k++)
				if (card->digmix[i + k] != DM_MUTE) {
					card->digmix[i + k] = db_table[l3];
					sblive_writeptr(card, FXGPREGBASE + 0x10 + i + k, 0, card->digmix[i + k]);
				}
			if (card->digmix[i + 6] != DM_MUTE) {
				card->digmix[i + 6] = db_table[l4];
				sblive_writeptr(card, FXGPREGBASE + 0x10 + i + 6, 0, card->digmix[i + 6]);
			}
			if (card->digmix[i + 7] != DM_MUTE) {
				card->digmix[i + 7] = db_table[r4];
				sblive_writeptr(card, FXGPREGBASE + 0x10 + i + 7, 0, card->digmix[i + 7]);
			}
		}
	}

}

#ifdef PRIVATE_PCM_VOLUME

/* calc & set attenuation factor for given channel */
static int set_pcm_attn(struct emu10k1_card *card, int ch, int l)
{
#ifndef PCMLEVEL
#define PCMLEVEL 110		/* almost silence */
#endif
	int vol = IFATN_ATTENUATION_MASK;	/* silence */

	if (l > 0)
		vol = (PCMLEVEL - (l * PCMLEVEL + 50) / 100);
	sblive_writeptr(card, IFATN, ch, IFATN_FILTERCUTOFF_MASK | vol);
	DPD(2, "SOUND_MIXER_PCM: channel:%d  level:%d  attn:%d\n", ch, l, vol);

	return vol;
#undef PCMLEVEL
}

/* update value of local PCM volume level (using channel attenuation)
 *
 * return 1: in case its local change
 *        0: if the current process doesn't have entry in table
 *	     (it means this process have not opened audio (mixer usually)
 */
static int update_pcm_attn(struct emu10k1_card *card, unsigned l1, unsigned r1)
{
	int i;
	int mixer = (r1 << 8) | l1;

	for (i = 0; i < MAX_PCM_CHANNELS; i++) {
		if (sblive_pcm_volume[i].files == current->files) {
			sblive_pcm_volume[i].mixer = pcm_last_mixer = mixer;
			if (sblive_pcm_volume[i].opened) {
				if (sblive_pcm_volume[i].channel_r < NUM_G) {
					sblive_pcm_volume[i].attn_r = set_pcm_attn(card, sblive_pcm_volume[i].channel_r, r1);
					if (sblive_pcm_volume[i].channel_l < NUM_G)
						sblive_pcm_volume[i].attn_l = set_pcm_attn(card, sblive_pcm_volume[i].channel_l, l1);
				} else {
					/* mono voice */
					if (sblive_pcm_volume[i].channel_l < NUM_G)
						sblive_pcm_volume[i].attn_l =
						    set_pcm_attn(card, sblive_pcm_volume[i].channel_l, (l1 >= r1) ? l1 : r1);
						/* to correctly handle mono voice here we would need
						   to go into stereo mode and move the voice to the right & left
						   looks a bit overcomplicated... */
				}
			}

			return 1;

		}
	}

	card->arrwVol[volidx[SOUND_MIXER_PCM]] = mixer;
	return 0;
}
#endif

int emu10k1_mixer_wrch(struct emu10k1_card *card, unsigned int ch, int val)
{
	int i;
	unsigned l1, r1;
	u16 wval;

	l1 = val & 0xff;
	r1 = (val >> 8) & 0xff;
	if (l1 > 100)
		l1 = 100;
	if (r1 > 100)
		r1 = 100;

	DPD(4, "emu10k1_mixer_wrch() called: ch=%u, l1=%u, r1=%u\n", ch, l1, r1);

	if (!volidx[ch])
		return -EINVAL;
#ifdef PRIVATE_PCM_VOLUME
	if (ch != SOUND_MIXER_PCM)
#endif
		card->arrwVol[volidx[ch]] = (r1 << 8) | l1;

	switch (ch) {
	case SOUND_MIXER_VOLUME:
		DPF(4, "SOUND_MIXER_VOLUME:\n");
		if (card->isaps)
			aps_update_digital(card);
		else
			update_digital(card);
		return 0;
	case SOUND_MIXER_PCM:
		DPF(4, "SOUND_MIXER_PCM\n");
#ifdef PRIVATE_PCM_VOLUME
		if (update_pcm_attn(card, l1, r1))
			return 0;
#endif
		if (card->isaps)
			aps_update_digital(card);
		else
			update_digital(card);
		return 0;
#ifdef TONE_CONTROL
	case SOUND_MIXER_TREBLE:
                DPF(4, "SOUND_MIXER_TREBLE:\n");
                set_treble(card, l1, r1);
                return 0;

        case SOUND_MIXER_BASS:
                DPF(4, "SOUND_MIXER_BASS:\n");
                set_bass(card, l1, r1);
		return 0;
#endif
	default:
		break;
	}


	if (card->isaps)
		return -EINVAL;

	switch (ch) {
	case SOUND_MIXER_DIGITAL1:
	case SOUND_MIXER_LINE3:
		DPD(4, "SOUND_MIXER_%s:\n", (ch == SOUND_MIXER_DIGITAL1) ? "DIGITAL1" : "LINE3");
		update_digital(card);
		return 0;
	case SOUND_MIXER_DIGITAL2:
	case SOUND_MIXER_LINE2:
	case SOUND_MIXER_LINE1:
	case SOUND_MIXER_LINE:
	case SOUND_MIXER_CD:
		DPD(4, "SOUND_MIXER_%s:\n",
		    (ch == SOUND_MIXER_LINE1) ? "LINE1" :
		    (ch == SOUND_MIXER_LINE2) ? "LINE2" : (ch == SOUND_MIXER_LINE) ? "LINE" : (ch == SOUND_MIXER_DIGITAL2) ? "DIGITAL2" : "CD");
		wval = ((((100 - l1) * 32 + 50) / 100) << 8) | (((100 - r1) * 32 + 50) / 100);
		if (wval == 0x2020)
			wval = 0x8000;
		else
			wval -= ((wval & 0x2020) / 0x20);
		sblive_writeac97(card, volreg[ch], wval);
		return 0;

	case SOUND_MIXER_OGAIN:
	case SOUND_MIXER_PHONEIN:
		DPD(4, "SOUND_MIXER_%s:\n", (ch == SOUND_MIXER_PHONEIN) ? "PHONEIN" : "OGAIN");
		sblive_writeac97(card, volreg[ch], (l1 < 2) ? 0x8000 : ((100 - l1) * 32 + 50) / 100);
		return 0;

	case SOUND_MIXER_SPEAKER:
		DPF(4, "SOUND_MIXER_SPEAKER:\n");
		sblive_writeac97(card, volreg[ch], (l1 < 4) ? 0x8000 : (((100 - l1) * 16 + 50) / 100) << 1);
		return 0;

	case SOUND_MIXER_MIC:
		DPF(4, "SOUND_MIXER_MIC:\n");
		i = 0;
		if (l1 >= 30)
			/* 20dB / (34.5dB + 12dB + 20dB) * 100 = 30 */
		{
			l1 -= 30;
			i = 0x40;
		}
		sblive_writeac97(card, volreg[ch], (l1 < 2) ? 0x8000 : ((((70 - l1) * 0x20 + 35) / 70) | i));
		return 0;

	case SOUND_MIXER_RECLEV:
		DPF(4, "SOUND_MIXER_RECLEV:\n");

		wval = (((l1 * 16 + 50) / 100) << 8) | ((r1 * 16 + 50) / 100);
		if (wval == 0)
			wval = 0x8000;
		else {
			if (wval & 0xff)
				wval--;
			if (wval & 0xff00)
				wval -= 0x0100;
		}
		sblive_writeac97(card, volreg[ch], wval);
		return 0;

	default:
		DPF(2, "Got unknown SOUND_MIXER ioctl\n");
		return -EINVAL;
	}
}

static loff_t emu10k1_mixer_llseek(struct file *file, loff_t offset, int origin)
{
	DPF(2, "sblive_mixer_llseek() called\n");
	return -ESPIPE;
}

/* Mixer file operations */

/* FIXME: Do we need spinlocks in here? */
/* WARNING! not all the ioctl's are supported by the emu-APS
   (anything AC97 related). As a general rule keep the AC97 related ioctls
   separate from the rest. This will make it easier to rewrite the mixer
   using the kernel AC97 interface. */ 
static int emu10k1_mixer_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	static const char id[] = "SBLive";
	static const char name[] = "Creative SBLive";
	int i, val;
	struct emu10k1_card *card = (struct emu10k1_card *) file->private_data;
	u16 reg;

	switch (cmd) {

	case SOUND_MIXER_INFO:{
			mixer_info info;

			DPF(4, "SOUND_MIXER_INFO\n");

			strncpy(info.id, id, sizeof(info.id));
			strncpy(info.name, name, sizeof(info.name));

			info.modify_counter = card->modcnt;
			if (copy_to_user((void *) arg, &info, sizeof(info)))
				return -EFAULT;

			return 0;
		}
		break;
	case SOUND_OLD_MIXER_INFO:{
			_old_mixer_info info;

			DPF(4, "SOUND_OLD_MIXER_INFO\n");

			strncpy(info.id, id, sizeof(info.id));
			strncpy(info.name, name, sizeof(info.name));

			if (copy_to_user((void *) arg, &info, sizeof(info)))
				return -EFAULT;

			return 0;
		}
		break;

	case OSS_GETVERSION:
		DPF(4, "OSS_GETVERSION\n");
		return put_user(SOUND_VERSION, (int *) arg);
		break;

	case SOUND_MIXER_PRIVATE1:
		DPF(4, "SOUND_MIXER_PRIVATE1");

		if (copy_to_user((void *) arg, card->digmix, sizeof(card->digmix)))
			return -EFAULT;

		return 0;

		break;
	case SOUND_MIXER_PRIVATE2:
		DPF(4, "SOUND_MIXER_PRIVATE2");

		if (copy_from_user(card->digmix, (void *) arg, sizeof(card->digmix)))
			return -EFAULT;

		for (i = 0; i < sizeof(card->digmix) / sizeof(card->digmix[0]); i++)
			sblive_writeptr(card, FXGPREGBASE + 0x10 + i, 0, (card->digmix[i] & DM_MUTE) ? 0 : card->digmix[i]);
		return 0;

		break;
	case SOUND_MIXER_PRIVATE3: {
			struct mixer_private_ioctl ctl;

			if (copy_from_user(&ctl, (void *) arg, sizeof(struct mixer_private_ioctl)))
				return -EFAULT;

			switch (ctl.cmd) {
#ifdef EMU10K1_DEBUG
			case CMD_WRITEFN0:
				emu10k1_writefn0(card, ctl.val[0], ctl.val[1]);
				return 0;
				break;

			case CMD_WRITEPTR:
				if(ctl.val[1] >= 0x40)
					return -EINVAL;

				if(ctl.val[0] > 0xff)
					return -EINVAL;

				if((ctl.val[0] & 0x7ff) > 0x3f)
					ctl.val[1] = 0x00;

				sblive_writeptr(card, ctl.val[0], ctl.val[1], ctl.val[2]);

				return 0;
				break;
#endif
			case CMD_READFN0:
				ctl.val[2] = emu10k1_readfn0(card, ctl.val[0]);

				if (copy_to_user((void *) arg, &ctl, sizeof(struct mixer_private_ioctl)))
					return -EFAULT;

				return 0;
				break;

			case CMD_READPTR:
				if(ctl.val[1] >= 0x40)
					return -EINVAL;

				if((ctl.val[0] & 0x7ff) > 0xff)
					return -EINVAL;

				if((ctl.val[0] & 0x7ff) > 0x3f)
					ctl.val[1] = 0x00;

				ctl.val[2] = sblive_readptr(card, ctl.val[0], ctl.val[1]);

				if (copy_to_user((void *) arg, &ctl, sizeof(struct mixer_private_ioctl)))
					return -EFAULT;

				return 0;
				break;

			case CMD_SETRECSRC:
				switch(ctl.val[0]){
				case WAVERECORD_AC97:
					if(card->isaps)
						return -EINVAL;
					card->wavein.recsrc = WAVERECORD_AC97;
					break;
				case WAVERECORD_MIC:	
					card->wavein.recsrc = WAVERECORD_MIC;
					break;
				case WAVERECORD_FX:
					card->wavein.recsrc = WAVERECORD_FX;
					card->wavein.fxwc = ctl.val[1] & 0xffff;
					if(!card->wavein.fxwc)
						return -EINVAL;
					break;
				default:
					return -EINVAL;
				}
				return 0;
				break;

			case CMD_GETRECSRC:
				ctl.val[0] = card->wavein.recsrc;
				ctl.val[1] = card->wavein.fxwc;
				if (copy_to_user((void *) arg, &ctl, sizeof(struct mixer_private_ioctl)))
					return -EFAULT;

				return 0;
				break;

			case CMD_GETVOICEPARAM:

				ctl.val[0] = card->waveout.send_routing[0];
				ctl.val[1] = card->waveout.send_a[0] | card->waveout.send_b[0] << 8 |
				             card->waveout.send_c[0] << 16 | card->waveout.send_d[0] << 24;

				ctl.val[2] = card->waveout.send_routing[1]; 
				ctl.val[3] = card->waveout.send_a[1] | card->waveout.send_b[1] << 8 |
					     card->waveout.send_c[1] << 16 | card->waveout.send_d[1] << 24;

				ctl.val[4] = card->waveout.send_routing[2]; 
				ctl.val[5] = card->waveout.send_a[2] | card->waveout.send_b[2] << 8 |
					     card->waveout.send_c[2] << 16 | card->waveout.send_d[2] << 24;

				if (copy_to_user((void *) arg, &ctl, sizeof(struct mixer_private_ioctl)))
					return -EFAULT;

				return 0;
				break;

			case CMD_SETVOICEPARAM:
				card->waveout.send_routing[0] = ctl.val[0] & 0xffff;
				card->waveout.send_a[0] = ctl.val[1] & 0xff;
				card->waveout.send_b[0] = (ctl.val[1] >> 8) & 0xff;
				card->waveout.send_c[0] = (ctl.val[1] >> 16) & 0xff;
				card->waveout.send_d[0] = (ctl.val[1] >> 24) & 0xff;

				card->waveout.send_routing[1] = ctl.val[2] & 0xffff;
				card->waveout.send_a[1] = ctl.val[3] & 0xff;
				card->waveout.send_b[1] = (ctl.val[3] >> 8) & 0xff;
				card->waveout.send_c[1] = (ctl.val[3] >> 16) & 0xff;
				card->waveout.send_d[1] = (ctl.val[3] >> 24) & 0xff;

				card->waveout.send_routing[2] = ctl.val[4] & 0xffff;
				card->waveout.send_a[2] = ctl.val[5] & 0xff;
				card->waveout.send_b[2] = (ctl.val[5] >> 8) & 0xff;
				card->waveout.send_c[2] = (ctl.val[5] >> 16) & 0xff;
				card->waveout.send_d[2] = (ctl.val[5] >> 24) & 0xff;

				return 0;
				break;

			default:
				return -EINVAL;
				break;
			}
		}
		break;

	case SOUND_MIXER_PRIVATE4:{
			u32 size;
			int size_reg = 0;

			if (copy_from_user(&size, (void *) arg, sizeof(size)))
				return -EFAULT;

			DPD(2,"External tram size 0x%x\n", size);

			if(size > 0x1fffff)
				return -EINVAL;

			if (size != 0) {	
				size = (size - 1) >> 14;

        	                while (size) {
                	                size >>= 1;
                        	        size_reg++;
                        	}

                        	size = 0x4000 << size_reg;
			}

			DPD(2,"External tram size 0x%x 0x%x\n", size, size_reg);

			if (size != card->tankmem.size) {
				if (card->tankmem.size > 0) {
					emu10k1_writefn0(card, HCFG_LOCKTANKCACHE, 1);

					sblive_writeptr_tag(card, 0, TCB, 0,
							    TCBS, 0,
							    TAGLIST_END);

					pci_free_consistent(card->pci_dev, card->tankmem.size,
							    card->tankmem.addr, card->tankmem.dma_handle);

					card->tankmem.size = 0;
				}

				if (size != 0) {
					if ((card->tankmem.addr = pci_alloc_consistent(card->pci_dev, size,
					     &card->tankmem.dma_handle)) == NULL)
						return -ENOMEM;

					card->tankmem.size = size;

					sblive_writeptr_tag(card, 0, TCB, card->tankmem.dma_handle,
                        	                            TCBS, size_reg,
                                	                    TAGLIST_END);

					emu10k1_writefn0(card, HCFG_LOCKTANKCACHE, 0);
				}
			}
			return 0;
		}
		break;

	default:
		break;
	}

	if (_IOC_TYPE(cmd) != 'M' || _IOC_SIZE(cmd) != sizeof(int))
		return -EINVAL;

	if (_IOC_DIR(cmd) == _IOC_READ) {
		switch (_IOC_NR(cmd)) {
			case SOUND_MIXER_DEVMASK:       /* Arg contains a bit for each supported device */
                        DPF(4, "SOUND_MIXER_READ_DEVMASK\n");
			if (card->isaps)
#ifdef TONE_CONTROL
				return put_user(SOUND_MASK_PCM | SOUND_MASK_VOLUME |
						SOUND_MASK_BASS | SOUND_MASK_TREBLE,
						(int *) arg); 
#else
				return put_user(SOUND_MASK_PCM | SOUND_MASK_VOLUME,
						(int *) arg); 
#endif
		
#ifdef TONE_CONTROL
			return put_user(SOUND_MASK_LINE | SOUND_MASK_CD |
                                        SOUND_MASK_OGAIN | SOUND_MASK_LINE1 |
                                        SOUND_MASK_PCM | SOUND_MASK_VOLUME |
                                        SOUND_MASK_PHONEIN | SOUND_MASK_MIC |
                                        SOUND_MASK_BASS | SOUND_MASK_TREBLE |
                                        SOUND_MASK_RECLEV | SOUND_MASK_SPEAKER |
                                        SOUND_MASK_LINE3 | SOUND_MASK_DIGITAL1 | 
                                        SOUND_MASK_DIGITAL2 | SOUND_MASK_LINE2, (int *) arg);
#else
			return put_user(SOUND_MASK_LINE | SOUND_MASK_CD |
                                        SOUND_MASK_OGAIN | SOUND_MASK_LINE1 |
                                        SOUND_MASK_PCM | SOUND_MASK_VOLUME |
                                        SOUND_MASK_PHONEIN | SOUND_MASK_MIC |
                                        SOUND_MASK_RECLEV | SOUND_MASK_SPEAKER |
                                        SOUND_MASK_LINE3 | SOUND_MASK_DIGITAL1 | 
                                        SOUND_MASK_DIGITAL2 | SOUND_MASK_LINE2, (int *) arg);
#endif

			case SOUND_MIXER_RECMASK:       /* Arg contains a bit for each supported recording source */
				DPF(2, "SOUND_MIXER_READ_RECMASK\n");
				if (card->isaps)
					return put_user(0, (int *) arg);

				return put_user(SOUND_MASK_MIC | SOUND_MASK_CD |
					SOUND_MASK_LINE1 | SOUND_MASK_LINE |
					SOUND_MASK_VOLUME | SOUND_MASK_OGAIN |
					SOUND_MASK_PHONEIN, (int *) arg);

			case SOUND_MIXER_STEREODEVS:    /* Mixer channels supporting stereo */
				DPF(2, "SOUND_MIXER_READ_STEREODEVS\n");

				if (card->isaps)
#ifdef TONE_CONTROL
					return put_user(SOUND_MASK_PCM | SOUND_MASK_VOLUME |
                                        		SOUND_MASK_BASS | SOUND_MASK_TREBLE,
                                        		(int *) arg);
#else
					return put_user(SOUND_MASK_PCM | SOUND_MASK_VOLUME,
                                         		(int *) arg);
#endif

#ifdef TONE_CONTROL
				return put_user(SOUND_MASK_LINE | SOUND_MASK_CD |
					SOUND_MASK_OGAIN | SOUND_MASK_LINE1 |
					SOUND_MASK_PCM | SOUND_MASK_VOLUME |
					SOUND_MASK_BASS | SOUND_MASK_TREBLE |
					SOUND_MASK_RECLEV | SOUND_MASK_LINE3 |
					SOUND_MASK_DIGITAL1 | SOUND_MASK_DIGITAL2 |
					SOUND_MASK_LINE2, (int *) arg);
#else
				return put_user(SOUND_MASK_LINE | SOUND_MASK_CD |
					SOUND_MASK_OGAIN | SOUND_MASK_LINE1 |
					SOUND_MASK_PCM | SOUND_MASK_VOLUME |
					SOUND_MASK_RECLEV | SOUND_MASK_LINE3 |
					SOUND_MASK_DIGITAL1 | SOUND_MASK_DIGITAL2 |
					SOUND_MASK_LINE2, (int *) arg);
#endif

			case SOUND_MIXER_CAPS:
				DPF(2, "SOUND_MIXER_READ_CAPS\n");
				return put_user(SOUND_CAP_EXCL_INPUT, (int *) arg);
#ifdef PRIVATE_PCM_VOLUME
                case SOUND_MIXER_PCM:
                        /* needs to be before default: !!*/
                        {
                                int i;

                                for (i = 0; i < MAX_PCM_CHANNELS; i++) {
                                        if (sblive_pcm_volume[i].files == current->files) {
                                                return put_user((int) sblive_pcm_volume[i].mixer, (int *) arg);
                                        }
                                }
                        }
#endif
		default:
			break;
		}

		switch (_IOC_NR(cmd)) {
		case SOUND_MIXER_RECSRC:	/* Arg contains a bit for each recording source */
			DPF(2, "SOUND_MIXER_READ_RECSRC\n");
			if (card->isaps)
				return put_user(0, (int *) arg);

			sblive_readac97(card, AC97_RECORDSELECT, &reg);
			return put_user(recsrc[reg & 7], (int *) arg);

		default:
			i = _IOC_NR(cmd);
			DPD(4, "SOUND_MIXER_READ(%d)\n", i);
			if (i >= SOUND_MIXER_NRDEVICES)
				return -EINVAL;
#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
			return mixer_rdch(card, i, (int *) arg);
#else				/* OSS_DOCUMENTED_MIXER_SEMANTICS */
			if (!volidx[i])
				return -EINVAL;
			return put_user(card->arrwVol[volidx[i]], (int *) arg);

#endif				/* OSS_DOCUMENTED_MIXER_SEMANTICS */
		}
	}

	/* End of _IOC_READ */
	if (_IOC_DIR(cmd) != (_IOC_READ | _IOC_WRITE))
		return -EINVAL;

	/* _IOC_WRITE */
	card->modcnt++;

	switch (_IOC_NR(cmd)) {
	case SOUND_MIXER_RECSRC:	/* Arg contains a bit for each recording source */
		DPF(2, "SOUND_MIXER_WRITE_RECSRC\n");

		if (card->isaps)
			return -EINVAL;

		if (get_user(val, (int *) arg))
			return -EFAULT;

		i = hweight32(val);
		if (i == 0)
			return 0;	/* val = mixer_recmask(s); */
		else if (i > 1) {
			sblive_readac97(card, AC97_RECORDSELECT, &reg);
			val &= ~recsrc[reg & 7];
		}

		for (i = 0; i < 8; i++) {
			if (val & recsrc[i]) {
				DPD(2, "Selecting record source to be 0x%04x\n", 0x0101 * i);
				sblive_writeac97(card, AC97_RECORDSELECT, 0x0101 * i);
				return 0;
			}
		}
		return 0;

	default:
		i = _IOC_NR(cmd);
		DPD(4, "SOUND_MIXER_WRITE(%d)\n", i);

		if (i >= SOUND_MIXER_NRDEVICES)
			return -EINVAL;
		if (get_user(val, (int *) arg))
			return -EFAULT;

		if (emu10k1_mixer_wrch(card, i, val))
			return -EINVAL;

#ifdef OSS_DOCUMENTED_MIXER_SEMANTICS
		return mixer_rdch(card, i, (int *) arg);
#else				/* OSS_DOCUMENTED_MIXER_SEMANTICS */
		return put_user(card->arrwVol[volidx[i]], (int *) arg);
#endif				/* OSS_DOCUMENTED_MIXER_SEMANTICS */

	}
}

static int emu10k1_mixer_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct emu10k1_card *card;
	struct list_head *entry;

	DPF(4, "emu10k1_mixer_open()\n");

	list_for_each(entry, &emu10k1_devs) {
		card = list_entry(entry, struct emu10k1_card, list);

		if (card->mixer_num == minor)
			break;
	}

	if (entry == &emu10k1_devs)
		return -ENODEV;

	file->private_data = card;
	return 0;
}

static int emu10k1_mixer_release(struct inode *inode, struct file *file)
{
	DPF(4, "emu10k1_mixer_release()\n");
	return 0;
}

struct file_operations emu10k1_mixer_fops = {
        owner:		THIS_MODULE,
	llseek:		emu10k1_mixer_llseek,
	ioctl:		emu10k1_mixer_ioctl,
	open:		emu10k1_mixer_open,
	release:	emu10k1_mixer_release,
};
