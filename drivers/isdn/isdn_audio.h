/* $Id: isdn_audio.h,v 1.9 2000/05/11 22:29:20 kai Exp $

 * Linux ISDN subsystem, audio conversion and compression (linklevel).
 *
 * Copyright 1994-1999 by Fritz Elfert (fritz@isdn4linux.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define DTMF_NPOINTS 205        /* Number of samples for DTMF recognition */
typedef struct adpcm_state {
	int a;
	int d;
	int word;
	int nleft;
	int nbits;
} adpcm_state;

typedef struct dtmf_state {
	char last;
	int idx;
	int buf[DTMF_NPOINTS];
} dtmf_state;

typedef struct silence_state {
	int state;
	unsigned int idx;
} silence_state;

extern void isdn_audio_ulaw2alaw(unsigned char *, unsigned long);
extern void isdn_audio_alaw2ulaw(unsigned char *, unsigned long);
extern adpcm_state *isdn_audio_adpcm_init(adpcm_state *, int);
extern int isdn_audio_adpcm2xlaw(adpcm_state *, int, unsigned char *, unsigned char *, int);
extern int isdn_audio_xlaw2adpcm(adpcm_state *, int, unsigned char *, unsigned char *, int);
extern int isdn_audio_2adpcm_flush(adpcm_state * s, unsigned char *out);
extern void isdn_audio_calc_dtmf(modem_info *, unsigned char *, int, int);
extern void isdn_audio_eval_dtmf(modem_info *);
dtmf_state *isdn_audio_dtmf_init(dtmf_state *);
extern void isdn_audio_calc_silence(modem_info *, unsigned char *, int, int);
extern void isdn_audio_eval_silence(modem_info *);
silence_state *isdn_audio_silence_init(silence_state *);
extern void isdn_audio_put_dle_code(modem_info *, u_char);
