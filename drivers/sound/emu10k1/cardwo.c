/*
 **********************************************************************
 *     cardwo.c - PCM output HAL for emu10k1 driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
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

#include <linux/poll.h>
#include "hwaccess.h"
#include "8010.h"
#include "voicemgr.h"
#include "cardwo.h"
#include "audio.h"

static u32 samplerate_to_linearpitch(u32 samplingrate)
{
	samplingrate = (samplingrate << 8) / 375;
	return (samplingrate >> 1) + (samplingrate & 1);
}

static void query_format(struct wave_format *wave_fmt)
{
	if ((wave_fmt->channels != 1) && (wave_fmt->channels != 2))
		wave_fmt->channels = 2;

	if (wave_fmt->samplingrate >= 0x2ee00)
		wave_fmt->samplingrate = 0x2ee00;

	if ((wave_fmt->bitsperchannel != 8) && (wave_fmt->bitsperchannel != 16))
		wave_fmt->bitsperchannel = 16;

	wave_fmt->bytesperchannel = wave_fmt->bitsperchannel >> 3;
	wave_fmt->bytespersample = wave_fmt->channels * wave_fmt->bytesperchannel;
	wave_fmt->bytespersec = wave_fmt->bytespersample * wave_fmt->samplingrate;

	return;
}

static int alloc_buffer(struct emu10k1_card *card, struct waveout_buffer *buffer)
{
	u32 pageindex, pagecount;
	unsigned long busaddx;
	int i;

	DPD(2, "requested pages is: %d\n", buffer->pages);

	if ((buffer->emupageindex = emu10k1_addxmgr_alloc(buffer->pages * PAGE_SIZE, card)) < 0)
		return -1;

	/* Fill in virtual memory table */
	for (pagecount = 0; pagecount < buffer->pages; pagecount++) {
		if ((buffer->addr[pagecount] = pci_alloc_consistent(card->pci_dev, PAGE_SIZE, &buffer->dma_handle[pagecount])) == NULL) {
			buffer->pages = pagecount;
			return -1;
		}

		DPD(2, "Virtual Addx: %p\n", buffer->addr[pagecount]);

		for (i = 0; i < PAGE_SIZE / EMUPAGESIZE; i++) {
			busaddx = buffer->dma_handle[pagecount] + i * EMUPAGESIZE;

			DPD(3, "Bus Addx: %lx\n", busaddx);

			pageindex = buffer->emupageindex + pagecount * PAGE_SIZE / EMUPAGESIZE + i;

			((u32 *) card->virtualpagetable.addr)[pageindex] = (busaddx * 2) | pageindex;
		}
	}

	return 0;
}

static void free_buffer(struct emu10k1_card *card, struct waveout_buffer *buffer)
{
	u32 pagecount, pageindex;
	int i;

	if (buffer->emupageindex < 0)
		return;

	for (pagecount = 0; pagecount < buffer->pages; pagecount++) {
		pci_free_consistent(card->pci_dev, PAGE_SIZE, buffer->addr[pagecount], buffer->dma_handle[pagecount]);

		for (i = 0; i < PAGE_SIZE / EMUPAGESIZE; i++) {
			pageindex = buffer->emupageindex + pagecount * PAGE_SIZE / EMUPAGESIZE + i;
			((u32 *) card->virtualpagetable.addr)[pageindex] = (card->silentpage.dma_handle * 2) | pageindex;
		}
	}

	emu10k1_addxmgr_free(card, buffer->emupageindex);
	buffer->emupageindex = -1;

	return;
}

static int get_voice(struct emu10k1_card *card, struct woinst *woinst)
{
	struct emu_voice *voice = &woinst->voice;
	/* Allocate voices here, if no voices available, return error.
	 * Init voice_allocdesc first.*/

	voice->usage = VOICE_USAGE_PLAYBACK;

	voice->flags = 0;

	if (woinst->format.channels == 2)
		voice->flags |= VOICE_FLAGS_STEREO;

	if (woinst->format.bitsperchannel == 16)
		voice->flags |= VOICE_FLAGS_16BIT;

	if (emu10k1_voice_alloc(card, voice) < 0)
		return -1;

	/* Calculate pitch */
	voice->initial_pitch = (u16) (srToPitch(woinst->format.samplingrate) >> 8);
	voice->pitch_target = samplerate_to_linearpitch(woinst->format.samplingrate);

	DPD(2, "Initial pitch --> 0x%x\n", voice->initial_pitch);

	voice->startloop = (woinst->buffer.emupageindex << 12) / woinst->format.bytespersample;
	voice->endloop = voice->startloop + woinst->buffer.size / woinst->format.bytespersample;
	voice->start = voice->startloop;

	if (voice->flags & VOICE_FLAGS_STEREO) {
		voice->params[0].send_a = card->waveout.send_a[1];
		voice->params[0].send_b = card->waveout.send_b[1];
		voice->params[0].send_c = card->waveout.send_c[1];
		voice->params[0].send_d = card->waveout.send_d[1];

		if (woinst->device)
			voice->params[0].send_routing = 0xd23c;
		else
			voice->params[0].send_routing = card->waveout.send_routing[1];

		voice->params[0].volume_target = 0xffff;
		voice->params[0].initial_fc = 0xff;
		voice->params[0].initial_attn = 0x00;
		voice->params[0].byampl_env_sustain = 0x7f;
		voice->params[0].byampl_env_decay = 0x7f;

		voice->params[1].send_a = card->waveout.send_a[2];
		voice->params[1].send_b = card->waveout.send_b[2];
		voice->params[1].send_c = card->waveout.send_c[2];
		voice->params[1].send_d = card->waveout.send_d[2];

		if (woinst->device)
			voice->params[1].send_routing = 0xd23c;
		else
			voice->params[1].send_routing = card->waveout.send_routing[2];

		voice->params[1].volume_target = 0xffff;
		voice->params[1].initial_fc = 0xff;
		voice->params[1].initial_attn = 0x00;
		voice->params[1].byampl_env_sustain = 0x7f;
		voice->params[1].byampl_env_decay = 0x7f;
	} else {
		voice->params[0].send_a = card->waveout.send_a[0];
		voice->params[0].send_b = card->waveout.send_b[0];
		voice->params[0].send_c = card->waveout.send_c[0];
		voice->params[0].send_d = card->waveout.send_d[0];

		if (woinst->device)
                        voice->params[0].send_routing = 0xd23c;
                else
			voice->params[0].send_routing = card->waveout.send_routing[0];

		voice->params[0].volume_target = 0xffff;
		voice->params[0].initial_fc = 0xff;
		voice->params[0].initial_attn = 0x00;
		voice->params[0].byampl_env_sustain = 0x7f;
		voice->params[0].byampl_env_decay = 0x7f;
	}

	DPD(2, "voice: startloop=0x%x, endloop=0x%x\n", voice->startloop, voice->endloop);

	emu10k1_voice_playback_setup(voice);

	return 0;
}

int emu10k1_waveout_open(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct woinst *woinst = wave_dev->woinst;
	u32 delay;

	DPF(2, "emu10k1_waveout_open()\n");

	if (alloc_buffer(card, &woinst->buffer) < 0) {
		ERROR();
		emu10k1_waveout_close(wave_dev);
		return -1;
	}

	woinst->buffer.fill_silence = 0;
	woinst->buffer.silence_bytes = 0;
	woinst->buffer.silence_pos = 0;
	woinst->buffer.hw_pos = 0;
	woinst->buffer.bytestocopy = woinst->buffer.size;

	if (get_voice(card, woinst) < 0) {
		ERROR();
		emu10k1_waveout_close(wave_dev);
		return -1;
	}

	delay = (48000 * woinst->buffer.fragment_size) / woinst->format.bytespersec;

	emu10k1_timer_install(card, &woinst->timer, delay / 2);

	woinst->state = WAVE_STATE_OPEN;

	return 0;
}

void emu10k1_waveout_close(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct woinst *woinst = wave_dev->woinst;

	DPF(2, "emu10k1_waveout_close()\n");

	emu10k1_waveout_stop(wave_dev);

	emu10k1_timer_uninstall(card, &woinst->timer);

	emu10k1_voice_free(&woinst->voice);

	free_buffer(card, &woinst->buffer);

	woinst->state = WAVE_STATE_CLOSED;

	return;
}

void emu10k1_waveout_start(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct woinst *woinst = wave_dev->woinst;

	DPF(2, "emu10k1_waveout_start()\n");
	/* Actual start */

	emu10k1_voice_start(&woinst->voice, woinst->total_played);

	emu10k1_timer_enable(card, &woinst->timer);

	woinst->state |= WAVE_STATE_STARTED;

	return;
}

int emu10k1_waveout_setformat(struct emu10k1_wavedevice *wave_dev, struct wave_format *format)
{
	struct emu10k1_card *card = wave_dev->card;
	struct woinst *woinst = wave_dev->woinst;
	u32 delay;

	DPF(2, "emu10k1_waveout_setformat()\n");

	if (woinst->state & WAVE_STATE_STARTED)
		return -1;

	query_format(format);

	if (woinst->format.samplingrate != format->samplingrate ||
	    woinst->format.channels != format->channels ||
	    woinst->format.bitsperchannel != format->bitsperchannel) {

		woinst->format = *format;

		if (woinst->state == WAVE_STATE_CLOSED)
			return 0;

		emu10k1_timer_uninstall(card, &woinst->timer);
		emu10k1_voice_free(&woinst->voice);

		if (get_voice(card, woinst) < 0) {
			ERROR();
			emu10k1_waveout_close(wave_dev);
			return -1;
		}

		delay = (48000 * woinst->buffer.fragment_size) / woinst->format.bytespersec;

		emu10k1_timer_install(card, &woinst->timer, delay / 2);
	}

	return 0;
}

void emu10k1_waveout_stop(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct woinst *woinst = wave_dev->woinst;

	DPF(2, "emu10k1_waveout_stop()\n");

	if (!(woinst->state & WAVE_STATE_STARTED))
		return;

	emu10k1_timer_disable(card, &woinst->timer);

	/* Stop actual voice */
	emu10k1_voice_stop(&woinst->voice);

	emu10k1_waveout_update(woinst);

	woinst->state &= ~WAVE_STATE_STARTED;

	return;
}

void emu10k1_waveout_getxfersize(struct woinst *woinst, u32 * size)
{
	struct waveout_buffer *buffer = &woinst->buffer;
	int pending;

	if (woinst->mmapped) {
		*size = buffer->bytestocopy;
		return;
	}

	pending = buffer->size - buffer->bytestocopy;

	buffer->fill_silence = (pending < (signed) buffer->fragment_size) ? 1 : 0;

	if (pending > (signed) buffer->silence_bytes) {
		*size = buffer->bytestocopy + buffer->silence_bytes;
	} else {
		*size = buffer->size;
		buffer->silence_bytes = pending;
		if (pending < 0) {
			buffer->silence_pos = buffer->hw_pos;
			buffer->silence_bytes = 0;
			buffer->bytestocopy = buffer->size;
			DPF(1, "buffer underrun\n");
		}
	}

	return;
}

static void copy_block(void **dst, u32 str, u8 *src, u32 len)
{
	int i, j, k;

	i = str / PAGE_SIZE;
	j = str % PAGE_SIZE;

	if (len > PAGE_SIZE - j) {
		k = PAGE_SIZE - j;
		copy_from_user(dst[i] + j, src, k);
		len -= k;
		while (len > PAGE_SIZE) {
                	copy_from_user(dst[++i], src + k, PAGE_SIZE);
                	k += PAGE_SIZE;
                	len -= PAGE_SIZE;
        	}
        	copy_from_user(dst[++i], src + k, len);

	} else
		copy_from_user(dst[i] + j, src, len);

	return;
}

static void fill_block(void **dst, u32 str, u8 src, u32 len)
{
	int i, j, k;

	i = str / PAGE_SIZE;
	j = str % PAGE_SIZE;

	if (len > PAGE_SIZE - j) {
                k = PAGE_SIZE - j;
                memset(dst[i] + j, src, k);
                len -= k;
                while (len > PAGE_SIZE) {
                        memset(dst[++i], src, PAGE_SIZE);
                        len -= PAGE_SIZE;
                }
                memset(dst[++i], src, len);

        } else
                memset(dst[i] + j, src, len);

	return;
}

void emu10k1_waveout_xferdata(struct woinst *woinst, u8 *data, u32 *size)
{
	struct waveout_buffer *buffer = &woinst->buffer;
	u32 sizetocopy, sizetocopy_now, start;
	unsigned long flags;

	sizetocopy = min(buffer->size, *size);
	*size = sizetocopy;

	if (!sizetocopy)
		return;

	spin_lock_irqsave(&woinst->lock, flags);
	start = (buffer->size + buffer->silence_pos - buffer->silence_bytes) % buffer->size;

	if(sizetocopy > buffer->silence_bytes) {
		buffer->silence_pos += sizetocopy - buffer->silence_bytes;
		buffer->bytestocopy -= sizetocopy - buffer->silence_bytes;
		buffer->silence_bytes = 0;
	} else
		buffer->silence_bytes -= sizetocopy;

	sizetocopy_now = buffer->size - start;

	spin_unlock_irqrestore(&woinst->lock, flags);

	if (sizetocopy > sizetocopy_now) {
		sizetocopy -= sizetocopy_now;
		copy_block(buffer->addr, start, data, sizetocopy_now);
		copy_block(buffer->addr, 0, data + sizetocopy_now, sizetocopy);
	} else {
		copy_block(buffer->addr, start, data, sizetocopy);
	}

	return;
}

void emu10k1_waveout_fillsilence(struct woinst *woinst)
{
	struct waveout_buffer *buffer = &woinst->buffer;
	u16 filldata;
	u32 sizetocopy, sizetocopy_now, start;
	unsigned long flags;

	sizetocopy = woinst->buffer.fragment_size;

	if (woinst->format.bitsperchannel == 16)
		filldata = 0x0000;
	else
		filldata = 0x8080;

	spin_lock_irqsave(&woinst->lock, flags);
	buffer->silence_bytes += sizetocopy;
	buffer->bytestocopy -= sizetocopy;
	buffer->silence_pos %= buffer->size;
	start = buffer->silence_pos;
	buffer->silence_pos += sizetocopy;
	sizetocopy_now = buffer->size - start;

	spin_unlock_irqrestore(&woinst->lock, flags);

	if (sizetocopy > sizetocopy_now) {
		sizetocopy -= sizetocopy_now;
		fill_block(buffer->addr, start, filldata, sizetocopy_now);
		fill_block(buffer->addr, 0, filldata, sizetocopy);
	} else {
		fill_block(buffer->addr, start, filldata, sizetocopy);
	}

	return;
}

void emu10k1_waveout_update(struct woinst *woinst)
{
	u32 hw_pos;
	u32 diff;

	/* There is no actual start yet */
	if (!(woinst->state & WAVE_STATE_STARTED)) {
		hw_pos = woinst->buffer.hw_pos;
	} else {
		/* hw_pos in sample units */
		hw_pos = sblive_readptr(woinst->voice.card, CCCA_CURRADDR, woinst->voice.num);

		if(hw_pos < woinst->voice.start)
			hw_pos += woinst->buffer.size / woinst->format.bytespersample - woinst->voice.start;
		else
			hw_pos -= woinst->voice.start;

		hw_pos *= woinst->format.bytespersample;
	}

	diff = (woinst->buffer.size + hw_pos - woinst->buffer.hw_pos) % woinst->buffer.size;
	woinst->total_played += diff;
	woinst->buffer.bytestocopy += diff;
	woinst->buffer.hw_pos = hw_pos;

	return;
}
