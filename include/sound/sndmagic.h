#ifndef __SOUND_SNDMAGIC_H
#define __SOUND_SNDMAGIC_H

/*
 *  Magic allocation, deallocation, check
 *  Copyright (c) 2000 by Abramo Bagnara <abramo@alsa-project.org>
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


#ifdef CONFIG_SND_DEBUG_MEMORY

void *_snd_magic_kcalloc(unsigned long magic, size_t size, int flags);
void *_snd_magic_kmalloc(unsigned long magic, size_t size, int flags);

/**
 * snd_magic_kmalloc - allocate a record with a magic-prefix
 * @type: the type to allocate a record (like xxx_t)
 * @extra: the extra size to allocate in bytes
 * @flags: the allocation condition (GFP_XXX)
 *
 * Allocates a record of the given type with the extra space and
 * returns its pointer.  The allocated record has a secret magic-key
 * to be checked via snd_magic_cast() for safe casts.
 *
 * The allocated pointer must be released via snd_magic_kfree().
 *
 * The "struct xxx" style cannot be used as the type argument
 * because the magic-key constant is generated from the type-name
 * string.
 */
#define snd_magic_kmalloc(type, extra, flags) \
	(type *) _snd_magic_kmalloc(type##_magic, sizeof(type) + extra, flags)
/**
 * snd_magic_kcalloc - allocate a record with a magic-prefix and initialize
 * @type: the type to allocate a record (like xxx_t)
 * @extra: the extra size to allocate in bytes
 * @flags: the allocation condition (GFP_XXX)
 *
 * Works like snd_magic_kmalloc() but this clears the area with zero
 * automatically.
 */
#define snd_magic_kcalloc(type, extra, flags) \
	(type *) _snd_magic_kcalloc(type##_magic, sizeof(type) + extra, flags)

/**
 * snd_magic_kfree - release the allocated area
 * @ptr: the pointer allocated via snd_magic_kmalloc() or snd_magic_kcalloc()
 *
 * Releases the memory area allocated via snd_magic_kmalloc() or
 * snd_magic_kcalloc() function.
 */
void snd_magic_kfree(void *ptr);

static inline unsigned long _snd_magic_value(void *obj)
{
	return obj == NULL ? (unsigned long)-1 : *(((unsigned long *)obj) - 1);
}

static inline int _snd_magic_bad(void *obj, unsigned long magic)
{
	return _snd_magic_value(obj) != magic;
}

#define snd_magic_cast1(t, expr, cmd) snd_magic_cast(t, expr, cmd)

/**
 * snd_magic_cast - check and cast the magic-allocated pointer
 * @type: the type of record to cast
 * @ptr: the magic-allocated pointer
 * @action...: the action to do if failed
 *
 * This macro provides a safe cast for the given type, which was
 * allocated via snd_magic_kmalloc() or snd_magic_kcallc().
 * If the pointer is invalid, i.e. the cast-type doesn't match,
 * the action arguments are called with a debug message.
 */
#define snd_magic_cast(type, ptr, action...) \
	(type *) ({\
	void *__ptr = ptr;\
	unsigned long __magic = _snd_magic_value(__ptr);\
	if (__magic != type##_magic) {\
		snd_printk("bad MAGIC (0x%lx)\n", __magic);\
		action;\
	}\
	__ptr;\
})

#define snd_device_t_magic			0xa15a00ff
#define snd_pcm_t_magic				0xa15a0101
#define snd_pcm_file_t_magic			0xa15a0102
#define snd_pcm_substream_t_magic		0xa15a0103
#define snd_pcm_proc_private_t_magic		0xa15a0104
#define snd_pcm_oss_file_t_magic		0xa15a0105
#define snd_mixer_oss_t_magic			0xa15a0106
// #define snd_pcm_sgbuf_t_magic			0xa15a0107

#define snd_info_private_data_t_magic		0xa15a0201
#define snd_info_entry_t_magic			0xa15a0202
#define snd_ctl_file_t_magic			0xa15a0301
#define snd_kcontrol_t_magic			0xa15a0302
#define snd_rawmidi_t_magic			0xa15a0401
#define snd_rawmidi_file_t_magic		0xa15a0402
#define snd_virmidi_t_magic			0xa15a0403
#define snd_virmidi_dev_t_magic			0xa15a0404
#define snd_timer_t_magic			0xa15a0501
#define snd_timer_user_t_magic			0xa15a0502
#define snd_hwdep_t_magic			0xa15a0601
#define snd_seq_device_t_magic			0xa15a0701

#define es18xx_t_magic				0xa15a1101
#define trident_t_magic				0xa15a1201
#define es1938_t_magic				0xa15a1301
#define cs46xx_t_magic				0xa15a1401
#define cs46xx_pcm_t_magic			0xa15a1402
#define ensoniq_t_magic				0xa15a1501
#define sonicvibes_t_magic			0xa15a1601
#define mpu401_t_magic				0xa15a1701
#define fm801_t_magic				0xa15a1801
#define ac97_t_magic				0xa15a1901
#define ak4531_t_magic				0xa15a1a01
#define snd_uart16550_t_magic			0xa15a1b01
#define emu10k1_t_magic				0xa15a1c01
#define emu10k1_pcm_t_magic			0xa15a1c02
#define emu10k1_midi_t_magic			0xa15a1c03
#define snd_gus_card_t_magic			0xa15a1d01
#define gus_pcm_private_t_magic			0xa15a1d02
#define gus_proc_private_t_magic		0xa15a1d03
#define tea6330t_t_magic			0xa15a1e01
#define ad1848_t_magic				0xa15a1f01
#define cs4231_t_magic				0xa15a2001
#define es1688_t_magic				0xa15a2101
#define opti93x_t_magic				0xa15a2201
#define emu8000_t_magic				0xa15a2301
#define emu8000_proc_private_t_magic		0xa15a2302
#define snd_emux_t_magic			0xa15a2303
#define snd_emux_port_t_magic			0xa15a2304
#define sb_t_magic				0xa15a2401
#define snd_sb_csp_t_magic			0xa15a2402
#define snd_card_dummy_t_magic			0xa15a2501
#define snd_card_dummy_pcm_t_magic		0xa15a2502
#define opl3_t_magic				0xa15a2601
#define opl4_t_magic				0xa15a2602
#define snd_seq_dummy_port_t_magic		0xa15a2701
#define ice1712_t_magic				0xa15a2801
#define ad1816a_t_magic				0xa15a2901
#define intel8x0_t_magic			0xa15a2a01
#define es1968_t_magic				0xa15a2b01
#define esschan_t_magic				0xa15a2b02
#define via82xx_t_magic				0xa15a2c01
#define pdplus_t_magic				0xa15a2d01
#define cmipci_t_magic				0xa15a2e01
#define ymfpci_t_magic				0xa15a2f01
#define ymfpci_pcm_t_magic			0xa15a2f02
#define cs4281_t_magic				0xa15a3001
#define snd_i2c_bus_t_magic			0xa15a3101
#define snd_i2c_device_t_magic			0xa15a3102
#define cs8427_t_magic				0xa15a3111
#define m3_t_magic				0xa15a3201
#define m3_dma_t_magic				0xa15a3202
#define nm256_t_magic				0xa15a3301
#define nm256_dma_t_magic			0xa15a3302
#define sam9407_t_magic				0xa15a3401
#define pmac_t_magic				0xa15a3501
#define ali_t_magic				0xa15a3601
#define mtpav_t_magic				0xa15a3701
#define mtpav_port_t_magic			0xa15a3702
#define korg1212_t_magic			0xa15a3800
#define opl3sa2_t_magic				0xa15a3900
#define serialmidi_t_magic			0xa15a3a00
#define sa11xx_uda1341_t_magic			0xa15a3b00
#define uda1341_t_magic                         0xa15a3c00
#define l3_client_t_magic                       0xa15a3d00
#define snd_usb_audio_t_magic			0xa15a3e01
#define usb_mixer_elem_info_t_magic		0xa15a3e02
#define snd_usb_stream_t_magic			0xa15a3e03
#define snd_usb_midi_t_magic			0xa15a3f01
#define snd_usb_midi_out_endpoint_t_magic	0xa15a3f02
#define snd_usb_midi_in_endpoint_t_magic	0xa15a3f03
#define ak4117_t_magic				0xa15a4000
#define psic_t_magic				0xa15a4100
#define vx_core_t_magic				0xa15a4110
#define vx_pipe_t_magic				0xa15a4112
#define azf3328_t_magic				0xa15a4200

#define snd_card_harmony_t_magic		0xa15a4300

#else

#define snd_magic_kcalloc(type, extra, flags) (type *) snd_kcalloc(sizeof(type) + extra, flags)
#define snd_magic_kmalloc(type, extra, flags) (type *) kmalloc(sizeof(type) + extra, flags)
#define snd_magic_cast(type, ptr, retval) (type *) ptr
#define snd_magic_cast1(type, ptr, retval) snd_magic_cast(type, ptr, retval)
#define snd_magic_kfree kfree

#endif

#endif /* __SOUND_SNDMAGIC_H */
