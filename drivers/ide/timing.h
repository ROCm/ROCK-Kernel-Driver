/*
 *  Copyright (C) 1996  Linus Torvalds, Igor Abramov, and Mark Lord
 *  Copyright (C) 1999-2001 Vojtech Pavlik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#define XFER_PIO_5		0x0d
#define XFER_UDMA_SLOW		0x4f

struct ata_timing {
	short mode;
	short setup;	/* t1 */
	short act8b;	/* t2 for 8-bit io */
	short rec8b;	/* t2i for 8-bit io */
	short cyc8b;	/* t0 for 8-bit io */
	short active;	/* t2 or tD */
	short recover;	/* t2i or tK */
	short cycle;	/* t0 */
	short udma;	/* t2CYCTYP/2 */
};

extern struct ata_timing ata_timing[];

#define IDE_TIMING_SETUP	0x01
#define IDE_TIMING_ACT8B	0x02
#define IDE_TIMING_REC8B	0x04
#define IDE_TIMING_CYC8B	0x08
#define IDE_TIMING_8BIT		0x0e
#define IDE_TIMING_ACTIVE	0x10
#define IDE_TIMING_RECOVER	0x20
#define IDE_TIMING_CYCLE	0x40
#define IDE_TIMING_UDMA		0x80
#define IDE_TIMING_ALL		0xff

#define FIT(v,x,y)	max_t(int,min_t(int,v,y),x)
#define ENOUGH(v,unit)	(((v)-1)/(unit)+1)
#define EZ(v,unit)	((v)?ENOUGH(v,unit):0)

/* see hpt366.c for details */
#define XFER_UDMA_66_3	0x100
#define XFER_UDMA_66_4	0x200

#define XFER_MODE	0xff0
#define XFER_UDMA_133	0x800
#define XFER_UDMA_100	0x400
#define XFER_UDMA_66	0x300
#define XFER_UDMA	0x040
#define XFER_MWDMA	0x020
#define XFER_SWDMA	0x010
#define XFER_EPIO	0x001
#define XFER_PIO	0x000

#define XFER_UDMA_ALL	0xf40
#define XFER_UDMA_80W	0xf00

/* External interface to host chips channel timing setup.
 *
 * It's a bit elaborate due to the legacy we have to bear.
 */

extern short ata_timing_mode(struct ata_device *drive, int map);
extern void ata_timing_quantize(struct ata_timing *t, struct ata_timing *q,
		int T, int UT);
extern void ata_timing_merge(struct ata_timing *a, struct ata_timing *b,
		struct ata_timing *m, unsigned int what);
void ata_timing_merge_8bit(struct ata_timing *t);
extern struct ata_timing* ata_timing_data(short speed);
extern int ata_timing_compute(struct ata_device *drive,
		short speed, struct ata_timing *t, int T, int UT);
extern u8 ata_best_pio_mode(struct ata_device *drive);
