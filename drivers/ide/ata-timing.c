/*
 * $Id: ata-timing.c,v 2.0 2002/03/12 15:48:43 vojtech Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by e-mail -
 * mail your message to <vojtech@ucw.cz>, or by paper mail: Vojtech Pavlik,
 * Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include "timing.h"

/*
 * PIO 0-5, MWDMA 0-2 and UDMA 0-6 timings (in nanoseconds).  These were taken
 * from ATA/ATAPI-6 standard, rev 0a, except for PIO 5, which is a nonstandard
 * extension and UDMA6, which is currently supported only by Maxtor drives.
 */

struct ata_timing ata_timing[] = {

	{ XFER_UDMA_6,     0,   0,   0,   0,   0,   0,   0,  15 },
	{ XFER_UDMA_5,     0,   0,   0,   0,   0,   0,   0,  20 },
	{ XFER_UDMA_4,     0,   0,   0,   0,   0,   0,   0,  30 },
	{ XFER_UDMA_3,     0,   0,   0,   0,   0,   0,   0,  45 },

	{ XFER_UDMA_2,     0,   0,   0,   0,   0,   0,   0,  60 },
	{ XFER_UDMA_1,     0,   0,   0,   0,   0,   0,   0,  80 },
	{ XFER_UDMA_0,     0,   0,   0,   0,   0,   0,   0, 120 },

	{ XFER_UDMA_SLOW,  0,   0,   0,   0,   0,   0,   0, 150 },

	{ XFER_MW_DMA_2,  25,   0,   0,   0,  70,  25, 120,   0 },
	{ XFER_MW_DMA_1,  45,   0,   0,   0,  80,  50, 150,   0 },
	{ XFER_MW_DMA_0,  60,   0,   0,   0, 215, 215, 480,   0 },

	{ XFER_SW_DMA_2,  60,   0,   0,   0, 120, 120, 240,   0 },
	{ XFER_SW_DMA_1,  90,   0,   0,   0, 240, 240, 480,   0 },
	{ XFER_SW_DMA_0, 120,   0,   0,   0, 480, 480, 960,   0 },

	{ XFER_PIO_5,     20,  50,  30, 100,  50,  30, 100,   0 },
	{ XFER_PIO_4,     25,  70,  25, 120,  70,  25, 120,   0 },
	{ XFER_PIO_3,     30,  80,  70, 180,  80,  70, 180,   0 },

	{ XFER_PIO_2,     30, 290,  40, 330, 100,  90, 240,   0 },
	{ XFER_PIO_1,     50, 290,  93, 383, 125, 100, 383,   0 },
	{ XFER_PIO_0,     70, 290, 240, 600, 165, 150, 600,   0 },

	{ XFER_PIO_SLOW, 120, 290, 240, 960, 290, 240, 960,   0 },

	{ -1 }
};

/*
 * Determine the best transfer mode appilcable to a particular drive.  This has
 * then to be matched agains in esp. other drives no the same channel or even
 * the whole particular host chip.
 */
short ata_timing_mode(struct ata_device *drive, int map)
{
	struct hd_driveid *id = drive->id;
	short best = 0;

	if (!id)
		return XFER_PIO_SLOW;

	/* Want UDMA and UDMA bitmap valid */
	if ((map & XFER_UDMA) && (id->field_valid & 4)) {
		if ((map & XFER_UDMA_133) == XFER_UDMA_133)
			if ((best = (id->dma_ultra & 0x0040) ? XFER_UDMA_6 : 0)) return best;

		if ((map & XFER_UDMA_100) == XFER_UDMA_100)
			if ((best = (id->dma_ultra & 0x0020) ? XFER_UDMA_5 : 0)) return best;

		if ((map & XFER_UDMA_66_4) == XFER_UDMA_66_4)
			if ((best = (id->dma_ultra & 0x0010) ? XFER_UDMA_4 : 0)) return best;

		if ((map & XFER_UDMA_66_3) == XFER_UDMA_66_3)
			if ((best = (id->dma_ultra & 0x0008) ? XFER_UDMA_3 : 0)) return best;

                if ((best = (id->dma_ultra & 0x0004) ? XFER_UDMA_2 :
			    (id->dma_ultra & 0x0002) ? XFER_UDMA_1 :
			    (id->dma_ultra & 0x0001) ? XFER_UDMA_0 : 0)) return best;
	}

	/* Want MWDMA and drive has EIDE fields */
	if ((map & XFER_MWDMA) && (id->field_valid & 2)) {
		if ((best = (id->dma_mword & 0x0004) ? XFER_MW_DMA_2 :
			    (id->dma_mword & 0x0002) ? XFER_MW_DMA_1 :
			    (id->dma_mword & 0x0001) ? XFER_MW_DMA_0 : 0))
			return best;
	}

	/* Want SWDMA */
	if (map & XFER_SWDMA) {

		/* EIDE SWDMA */
		if (id->field_valid & 2) {
			if ((best = (id->dma_1word & 0x0004) ? XFER_SW_DMA_2 :
				    (id->dma_1word & 0x0002) ? XFER_SW_DMA_1 :
				    (id->dma_1word & 0x0001) ? XFER_SW_DMA_0 : 0))
				return best;
		}

		/* Pre-EIDE style SWDMA */
		if (id->capability & 1) {
			if ((best = (id->tDMA == 2) ? XFER_SW_DMA_2 :
				    (id->tDMA == 1) ? XFER_SW_DMA_1 :
				    (id->tDMA == 0) ? XFER_SW_DMA_0 : 0))
				return best;
		}
	}

	/* EIDE PIO modes */
	if ((map & XFER_EPIO) && (id->field_valid & 2)) {
		if ((best = (drive->id->eide_pio_modes & 4) ? XFER_PIO_5 :
			    (drive->id->eide_pio_modes & 2) ? XFER_PIO_4 :
			    (drive->id->eide_pio_modes & 1) ? XFER_PIO_3 : 0))
			return best;
	}

	return  (drive->id->tPIO == 2) ? XFER_PIO_2 :
		(drive->id->tPIO == 1) ? XFER_PIO_1 :
		(drive->id->tPIO == 0) ? XFER_PIO_0 : XFER_PIO_SLOW;
}

/*
 * Just get a pointer to the struct describing the timing values used commonly
 * for a particular mode.
 */
struct ata_timing* ata_timing_data(short speed)
{
	struct ata_timing *t;

	for (t = ata_timing; t->mode != speed; t++)
		if (t->mode < 0)
			return NULL;
	return t;
}

/*
 * This is just unit conversion.
 */
void ata_timing_quantize(struct ata_timing *t, struct ata_timing *q,
		int T, int UT)
{
	q->setup   = EZ(t->setup   * 1000,  T);
	q->act8b   = EZ(t->act8b   * 1000,  T);
	q->rec8b   = EZ(t->rec8b   * 1000,  T);
	q->cyc8b   = EZ(t->cyc8b   * 1000,  T);
	q->active  = EZ(t->active  * 1000,  T);
	q->recover = EZ(t->recover * 1000,  T);
	q->cycle   = EZ(t->cycle   * 1000,  T);
	q->udma    = EZ(t->udma    * 1000, UT);
}

/*
 * Match against each other the timing setup for two devices on one channel.
 * Becouse they share the electrical interface we can unsually only use the
 * lowest common denominator between them.
 */
void ata_timing_merge(struct ata_timing *a, struct ata_timing *b,
		struct ata_timing *m, unsigned int what)
{
	if (what & IDE_TIMING_SETUP)
		m->setup   = max(a->setup,   b->setup);
	if (what & IDE_TIMING_ACT8B)
		m->act8b   = max(a->act8b,   b->act8b);
	if (what & IDE_TIMING_REC8B)
		m->rec8b   = max(a->rec8b,   b->rec8b);
	if (what & IDE_TIMING_CYC8B)
		m->cyc8b   = max(a->cyc8b,   b->cyc8b);
	if (what & IDE_TIMING_ACTIVE)
		m->active  = max(a->active,  b->active);
	if (what & IDE_TIMING_RECOVER)
		m->recover = max(a->recover, b->recover);
	if (what & IDE_TIMING_CYCLE)
		m->cycle   = max(a->cycle,   b->cycle);
	if (what & IDE_TIMING_UDMA)
		m->udma    = max(a->udma,    b->udma);
}

/*
 * Not all controllers can do separate timing for 8-bit command transfers
 * and 16-bit data transfers.
 */

void ata_timing_merge_8bit(struct ata_timing *t)
{
	t->active = max(t->active, t->act8b);
	t->recover = max(t->recover, t->rec8b);
	t->cycle = max(t->cycle, t->cyc8b);
}

int ata_timing_compute(struct ata_device *drive, short speed, struct ata_timing *t,
		int T, int UT)
{
	struct hd_driveid *id = drive->id;
	struct ata_timing *s, p;

	/* Find the mode.
	 */

	if (!(s = ata_timing_data(speed)))
		return -EINVAL;

	memcpy(t, s, sizeof(struct ata_timing));

	/* If the drive is an EIDE drive, it can tell us it needs extended
	 * PIO/MWDMA cycle timing.
	 */

	/* EIDE drive */
	if (id && id->field_valid & 2) {
		memset(&p, 0, sizeof(p));

		switch (speed & XFER_MODE) {
			case XFER_PIO:
				if (speed <= XFER_PIO_2) p.cycle = p.cyc8b = id->eide_pio;
						    else p.cycle = p.cyc8b = id->eide_pio_iordy;
				break;

			case XFER_MWDMA:
				p.cycle = id->eide_dma_min;
				break;
		}

		ata_timing_merge(&p, t, t, IDE_TIMING_CYCLE | IDE_TIMING_CYC8B);
	}

	/* Convert the timing to bus clock counts.
	 */

	ata_timing_quantize(t, t, T, UT);

	/* Even in DMA/UDMA modes we still use PIO access for IDENTIFY,
	 * S.M.A.R.T and some other commands. We have to ensure that the DMA
	 * cycle timing is slower/equal than the fastest PIO timing.
	 */

	if ((speed & XFER_MODE) != XFER_PIO) {
		ata_timing_compute(drive, ata_timing_mode(drive, XFER_PIO | XFER_EPIO), &p, T, UT);
		ata_timing_merge(&p, t, t, IDE_TIMING_ALL);
	}

	/* Lenghten active & recovery time so that cycle time is correct.
	 */

	if (t->act8b + t->rec8b < t->cyc8b) {
		t->act8b += (t->cyc8b - (t->act8b + t->rec8b)) / 2;
		t->rec8b = t->cyc8b - t->act8b;
	}

	if (t->active + t->recover < t->cycle) {
		t->active += (t->cycle - (t->active + t->recover)) / 2;
		t->recover = t->cycle - t->active;
	}

	return 0;
}

u8 ata_best_pio_mode(struct ata_device *drive)
{
	static u16 eide_pio_timing[6] = { 600, 383, 240, 180, 120, 90 };
	u16 pio_min;
	u8 pio;

	pio = ata_timing_mode(drive, XFER_PIO | XFER_EPIO) - XFER_PIO_0;

	/* downgrade mode if necessary */
	pio_min = (pio > 2) ? drive->id->eide_pio_iordy : drive->id->eide_pio;

	if (pio_min)
		while (pio && pio_min > eide_pio_timing[pio])
			pio--;

	if (!pio && drive->id->tPIO)
		return XFER_PIO_SLOW;

	/* don't allow XFER_PIO_5 for now */
	return XFER_PIO_0 + min_t(u8, pio, 4);
}
