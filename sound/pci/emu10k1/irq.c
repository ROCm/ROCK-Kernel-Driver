/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *                   Creative Labs, Inc.
 *  Routines for IRQ control of EMU10K1 chips
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *    --
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/emu10k1.h>

irqreturn_t snd_emu10k1_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	emu10k1_t *emu = snd_magic_cast(emu10k1_t, dev_id, return IRQ_NONE);
	unsigned int status;
	int handled = 0;

	while ((status = inl(emu->port + IPR)) != 0) {
		// printk("irq - status = 0x%x\n", status);
		handled = 1;
		if (status & IPR_PCIERROR) {
			snd_printk("interrupt: PCI error\n");
			snd_emu10k1_intr_disable(emu, INTE_PCIERRORENABLE);
		}
		if (status & (IPR_VOLINCR|IPR_VOLDECR|IPR_MUTE)) {
			if (emu->hwvol_interrupt)
				emu->hwvol_interrupt(emu, status);
			else
				snd_emu10k1_intr_disable(emu, INTE_VOLINCRENABLE|INTE_VOLDECRENABLE|INTE_MUTEENABLE);
			outl(status & (IPR_VOLINCR|IPR_VOLDECR|IPR_MUTE), emu->port + IPR);
		}
		if (status & IPR_CHANNELLOOP) {
			int voice;
			int voice_max = status & IPR_CHANNELNUMBERMASK;
			int voice_max_l;
			u32 val;
			emu10k1_voice_t *pvoice = emu->voices;

			val = snd_emu10k1_ptr_read(emu, CLIPL, 0);
			voice_max_l = voice_max;
			if (voice_max_l >= 0x20)
				voice_max_l = 0x1f;
			for (voice = 0; voice <= voice_max_l; voice++) {
				if (val & 1) {
					if (pvoice->use && pvoice->interrupt != NULL) {
						pvoice->interrupt(emu, pvoice);
						snd_emu10k1_voice_intr_ack(emu, voice);
					} else {
						snd_emu10k1_voice_intr_disable(emu, voice);
					}
				}
				val >>= 1;
				pvoice++;
			}
			if (voice_max > 0x1f) {
				val = snd_emu10k1_ptr_read(emu, CLIPH, 0);
				for (; voice <= voice_max; voice++) {
					if(val & 1) {
						if (pvoice->use && pvoice->interrupt != NULL) {
							pvoice->interrupt(emu, pvoice);
							snd_emu10k1_voice_intr_ack(emu, voice);
						} else {
							snd_emu10k1_voice_intr_disable(emu, voice);
						}
					}
					val >>= 1;
					pvoice++;
				}
			}
			outl(IPR_CHANNELLOOP | (status & IPR_CHANNELNUMBERMASK), emu->port + IPR);
		}
		if (status & (IPR_ADCBUFFULL|IPR_ADCBUFHALFFULL)) {
			if (emu->capture_interrupt)
				emu->capture_interrupt(emu, status);
			else
				snd_emu10k1_intr_disable(emu, INTE_ADCBUFENABLE);
			outl(status & (IPR_ADCBUFFULL|IPR_ADCBUFHALFFULL), emu->port + IPR);
		}
		if (status & (IPR_MICBUFFULL|IPR_MICBUFHALFFULL)) {
			if (emu->capture_mic_interrupt)
				emu->capture_mic_interrupt(emu, status);
			else
				snd_emu10k1_intr_disable(emu, INTE_MICBUFENABLE);
			outl(status & (IPR_MICBUFFULL|IPR_MICBUFHALFFULL), emu->port + IPR);
		}
		if (status & (IPR_EFXBUFFULL|IPR_EFXBUFHALFFULL)) {
			if (emu->capture_efx_interrupt)
				emu->capture_efx_interrupt(emu, status);
			else
				snd_emu10k1_intr_disable(emu, INTE_EFXBUFENABLE);
			outl(status & (IPR_EFXBUFFULL|IPR_EFXBUFHALFFULL), emu->port + IPR);
		}
		if (status & (IPR_MIDITRANSBUFEMPTY|IPR_MIDIRECVBUFEMPTY)) {
			if (emu->midi.interrupt)
				emu->midi.interrupt(emu, status);
			else
				snd_emu10k1_intr_disable(emu, INTE_MIDITXENABLE|INTE_MIDIRXENABLE);
			outl(status & (IPR_MIDITRANSBUFEMPTY|IPR_MIDIRECVBUFEMPTY), emu->port + IPR);
		}
		if (status & (IPR_A_MIDITRANSBUFEMPTY2|IPR_A_MIDIRECVBUFEMPTY2)) {
			if (emu->midi2.interrupt)
				emu->midi2.interrupt(emu, status);
			else
				snd_emu10k1_intr_disable(emu, INTE_A_MIDITXENABLE2|INTE_A_MIDIRXENABLE2);
			outl(status & (IPR_A_MIDITRANSBUFEMPTY2|IPR_A_MIDIRECVBUFEMPTY2), emu->port + IPR);
		}
		if (status & IPR_INTERVALTIMER) {
			if (emu->timer_interrupt)
				emu->timer_interrupt(emu);
			else
				snd_emu10k1_intr_disable(emu, INTE_INTERVALTIMERENB);
			outl(IPR_INTERVALTIMER, emu->port + IPR);
		}
		if (status & (IPR_GPSPDIFSTATUSCHANGE|IPR_CDROMSTATUSCHANGE)) {
			if (emu->spdif_interrupt)
				emu->spdif_interrupt(emu, status);
			else
				snd_emu10k1_intr_disable(emu, INTE_GPSPDIFENABLE|INTE_CDSPDIFENABLE);
			outl(status & (IPR_GPSPDIFSTATUSCHANGE|IPR_CDROMSTATUSCHANGE), emu->port + IPR);
		}
		if (status & IPR_FXDSP) {
			if (emu->dsp_interrupt)
				emu->dsp_interrupt(emu);
			else
				snd_emu10k1_intr_disable(emu, INTE_FXDSPENABLE);
			outl(IPR_FXDSP, emu->port + IPR);
		}
	}
	return IRQ_RETVAL(handled);
}
