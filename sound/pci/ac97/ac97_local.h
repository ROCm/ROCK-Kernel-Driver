/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Universal interface for Audio Codec '97
 *
 *  For more details look to AC '97 component specification revision 2.2
 *  by Intel Corporation (http://developer.intel.com).
 *
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

#define AC97_SINGLE(xname, reg, shift, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .info = snd_ac97_info_single, \
  .get = snd_ac97_get_single, .put = snd_ac97_put_single, \
  .private_value = (reg) | ((shift) << 8) | ((mask) << 16) | ((invert) << 24) }

/* ac97_codec.c */
extern const char *snd_ac97_stereo_enhancements[];
extern const snd_kcontrol_new_t snd_ac97_controls_3d[];
extern const snd_kcontrol_new_t snd_ac97_controls_spdif[];
snd_kcontrol_t *snd_ac97_cnew(const snd_kcontrol_new_t *_template, ac97_t * ac97);
void snd_ac97_get_name(ac97_t *ac97, unsigned int id, char *name, int modem);
int snd_ac97_info_single(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo);
int snd_ac97_get_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol);
int snd_ac97_put_single(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol);
int snd_ac97_try_bit(ac97_t * ac97, int reg, int bit);

/* ac97_proc.c */
void snd_ac97_proc_init(snd_card_t * card, ac97_t * ac97, const char *prefix);
