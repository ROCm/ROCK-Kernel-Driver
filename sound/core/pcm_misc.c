/*
 *  PCM Interface - misc routines
 *  Copyright (c) 1998 by Jaroslav Kysela <perex@suse.cz>
 *
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
  
#include <sound/driver.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/pcm.h>
#define bswap_16 swab16
#define bswap_32 swab32
#define bswap_64 swab64
#define SND_PCM_FORMAT_UNKNOWN (-1)
#define snd_enum_to_int(v) (v)
#define snd_int_to_enum(v) (v)

/**
 * snd_pcm_format_signed - Check the PCM format is signed linear
 * @format: the format to check
 *
 * Returns 1 if the given PCM format is signed linear, 0 if unsigned
 * linear, and a negative error code for non-linear formats.
 */
int snd_pcm_format_signed(snd_pcm_format_t format)
{
	switch (snd_enum_to_int(format)) {
	case SNDRV_PCM_FORMAT_S8:
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S16_BE:
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_BE:
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_S32_BE:
	case SNDRV_PCM_FORMAT_S24_3LE:
	case SNDRV_PCM_FORMAT_S24_3BE:
	case SNDRV_PCM_FORMAT_S20_3LE:
	case SNDRV_PCM_FORMAT_S20_3BE:
	case SNDRV_PCM_FORMAT_S18_3LE:
	case SNDRV_PCM_FORMAT_S18_3BE:
		return 1;
	case SNDRV_PCM_FORMAT_U8:
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_U16_BE:
	case SNDRV_PCM_FORMAT_U24_LE:
	case SNDRV_PCM_FORMAT_U24_BE:
	case SNDRV_PCM_FORMAT_U32_LE:
	case SNDRV_PCM_FORMAT_U32_BE:
	case SNDRV_PCM_FORMAT_U24_3LE:
	case SNDRV_PCM_FORMAT_U24_3BE:
	case SNDRV_PCM_FORMAT_U20_3LE:
	case SNDRV_PCM_FORMAT_U20_3BE:
	case SNDRV_PCM_FORMAT_U18_3LE:
	case SNDRV_PCM_FORMAT_U18_3BE:
		return 0;
	default:
		return -EINVAL;
	}
}

/**
 * snd_pcm_format_unsigned - Check the PCM format is unsigned linear
 * @format: the format to check
 *
 * Returns 1 if the given PCM format is unsigned linear, 0 if signed
 * linear, and a negative error code for non-linear formats.
 */
int snd_pcm_format_unsigned(snd_pcm_format_t format)
{
	int val;

	val = snd_pcm_format_signed(format);
	if (val < 0)
		return val;
	return !val;
}

/**
 * snd_pcm_format_linear - Check the PCM format is linear
 * @format: the format to check
 *
 * Returns 1 if the given PCM format is linear, 0 if not.
 */
int snd_pcm_format_linear(snd_pcm_format_t format)
{
	return snd_pcm_format_signed(format) >= 0;
}

/**
 * snd_pcm_format_little_endian - Check the PCM format is little-endian
 * @format: the format to check
 *
 * Returns 1 if the given PCM format is little-endian, 0 if
 * big-endian, or a negative error code if endian not specified.
 */
int snd_pcm_format_little_endian(snd_pcm_format_t format)
{
	switch (snd_enum_to_int(format)) {
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_U24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_U32_LE:
	case SNDRV_PCM_FORMAT_FLOAT_LE:
	case SNDRV_PCM_FORMAT_FLOAT64_LE:
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
	case SNDRV_PCM_FORMAT_S20_3LE:
	case SNDRV_PCM_FORMAT_S18_3LE:
	case SNDRV_PCM_FORMAT_U24_3LE:
	case SNDRV_PCM_FORMAT_U20_3LE:
	case SNDRV_PCM_FORMAT_U18_3LE:
		return 1;
	case SNDRV_PCM_FORMAT_S16_BE:
	case SNDRV_PCM_FORMAT_U16_BE:
	case SNDRV_PCM_FORMAT_S24_BE:
	case SNDRV_PCM_FORMAT_U24_BE:
	case SNDRV_PCM_FORMAT_S32_BE:
	case SNDRV_PCM_FORMAT_U32_BE:
	case SNDRV_PCM_FORMAT_FLOAT_BE:
	case SNDRV_PCM_FORMAT_FLOAT64_BE:
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_BE:
	case SNDRV_PCM_FORMAT_S24_3BE:
	case SNDRV_PCM_FORMAT_S20_3BE:
	case SNDRV_PCM_FORMAT_S18_3BE:
	case SNDRV_PCM_FORMAT_U24_3BE:
	case SNDRV_PCM_FORMAT_U20_3BE:
	case SNDRV_PCM_FORMAT_U18_3BE:
		return 0;
	default:
		return -EINVAL;
	}
}

/**
 * snd_pcm_format_big_endian - Check the PCM format is big-endian
 * @format: the format to check
 *
 * Returns 1 if the given PCM format is big-endian, 0 if
 * little-endian, or a negative error code if endian not specified.
 */
int snd_pcm_format_big_endian(snd_pcm_format_t format)
{
	int val;

	val = snd_pcm_format_little_endian(format);
	if (val < 0)
		return val;
	return !val;
}

/**
 * snd_pcm_format_cpu_endian - Check the PCM format is CPU-endian
 * @format: the format to check
 *
 * Returns 1 if the given PCM format is CPU-endian, 0 if
 * opposite, or a negative error code if endian not specified.
 */
int snd_pcm_format_cpu_endian(snd_pcm_format_t format)
{
#ifdef SNDRV_LITTLE_ENDIAN
	return snd_pcm_format_little_endian(format);
#else
	return snd_pcm_format_big_endian(format);
#endif
}

/**
 * snd_pcm_format_width - return the bit-width of the format
 * @format: the format to check
 *
 * Returns the bit-width of the format, or a negative error code
 * if unknown format.
 */
int snd_pcm_format_width(snd_pcm_format_t format)
{
	switch (snd_enum_to_int(format)) {
	case SNDRV_PCM_FORMAT_S8:
	case SNDRV_PCM_FORMAT_U8:
		return 8;
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S16_BE:
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_U16_BE:
		return 16;
	case SNDRV_PCM_FORMAT_S18_3LE:
	case SNDRV_PCM_FORMAT_S18_3BE:
	case SNDRV_PCM_FORMAT_U18_3LE:
	case SNDRV_PCM_FORMAT_U18_3BE:
		return 18;
	case SNDRV_PCM_FORMAT_S20_3LE:
	case SNDRV_PCM_FORMAT_S20_3BE:
	case SNDRV_PCM_FORMAT_U20_3LE:
	case SNDRV_PCM_FORMAT_U20_3BE:
		return 20;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_BE:
	case SNDRV_PCM_FORMAT_U24_LE:
	case SNDRV_PCM_FORMAT_U24_BE:
	case SNDRV_PCM_FORMAT_S24_3LE:
	case SNDRV_PCM_FORMAT_S24_3BE:
	case SNDRV_PCM_FORMAT_U24_3LE:
	case SNDRV_PCM_FORMAT_U24_3BE:
		return 24;
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_S32_BE:
	case SNDRV_PCM_FORMAT_U32_LE:
	case SNDRV_PCM_FORMAT_U32_BE:
	case SNDRV_PCM_FORMAT_FLOAT_LE:
	case SNDRV_PCM_FORMAT_FLOAT_BE:
		return 32;
	case SNDRV_PCM_FORMAT_FLOAT64_LE:
	case SNDRV_PCM_FORMAT_FLOAT64_BE:
		return 64;
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE:
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_BE:
		return 24;
	case SNDRV_PCM_FORMAT_MU_LAW:
	case SNDRV_PCM_FORMAT_A_LAW:
		return 8;
	case SNDRV_PCM_FORMAT_IMA_ADPCM:
		return 4;
	default:
		return -EINVAL;
	}
}

/**
 * snd_pcm_format_physical_width - return the physical bit-width of the format
 * @format: the format to check
 *
 * Returns the physical bit-width of the format, or a negative error code
 * if unknown format.
 */
int snd_pcm_format_physical_width(snd_pcm_format_t format)
{
	switch (snd_enum_to_int(format)) {
	case SNDRV_PCM_FORMAT_S8:
	case SNDRV_PCM_FORMAT_U8:
		return 8;
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S16_BE:
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_U16_BE:
		return 16;
	case SNDRV_PCM_FORMAT_S18_3LE:
	case SNDRV_PCM_FORMAT_S18_3BE:
	case SNDRV_PCM_FORMAT_U18_3LE:
	case SNDRV_PCM_FORMAT_U18_3BE:
	case SNDRV_PCM_FORMAT_S20_3LE:
	case SNDRV_PCM_FORMAT_S20_3BE:
	case SNDRV_PCM_FORMAT_U20_3LE:
	case SNDRV_PCM_FORMAT_U20_3BE:
	case SNDRV_PCM_FORMAT_S24_3LE:
	case SNDRV_PCM_FORMAT_S24_3BE:
	case SNDRV_PCM_FORMAT_U24_3LE:
	case SNDRV_PCM_FORMAT_U24_3BE:
		return 24;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_BE:
	case SNDRV_PCM_FORMAT_U24_LE:
	case SNDRV_PCM_FORMAT_U24_BE:
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_S32_BE:
	case SNDRV_PCM_FORMAT_U32_LE:
	case SNDRV_PCM_FORMAT_U32_BE:
	case SNDRV_PCM_FORMAT_FLOAT_LE:
	case SNDRV_PCM_FORMAT_FLOAT_BE:
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE:
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_BE:
		return 32;
	case SNDRV_PCM_FORMAT_FLOAT64_LE:
	case SNDRV_PCM_FORMAT_FLOAT64_BE:
		return 64;
	case SNDRV_PCM_FORMAT_MU_LAW:
	case SNDRV_PCM_FORMAT_A_LAW:
		return 8;
	case SNDRV_PCM_FORMAT_IMA_ADPCM:
		return 4;
	default:
		return -EINVAL;
	}
}

/**
 * snd_pcm_format_size - return the byte size of samples on the given format
 * @format: the format to check
 *
 * Returns the byte size of the given samples for the format, or a
 * negative error code if unknown format.
 */
ssize_t snd_pcm_format_size(snd_pcm_format_t format, size_t samples)
{
	switch (snd_enum_to_int(format)) {
	case SNDRV_PCM_FORMAT_S8:
	case SNDRV_PCM_FORMAT_U8:
		return samples;
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S16_BE:
	case SNDRV_PCM_FORMAT_U16_LE:
	case SNDRV_PCM_FORMAT_U16_BE:
		return samples * 2;
	case SNDRV_PCM_FORMAT_S18_3LE:
	case SNDRV_PCM_FORMAT_S18_3BE:
	case SNDRV_PCM_FORMAT_U18_3LE:
	case SNDRV_PCM_FORMAT_U18_3BE:
	case SNDRV_PCM_FORMAT_S20_3LE:
	case SNDRV_PCM_FORMAT_S20_3BE:
	case SNDRV_PCM_FORMAT_U20_3LE:
	case SNDRV_PCM_FORMAT_U20_3BE:
	case SNDRV_PCM_FORMAT_S24_3LE:
	case SNDRV_PCM_FORMAT_S24_3BE:
	case SNDRV_PCM_FORMAT_U24_3LE:
	case SNDRV_PCM_FORMAT_U24_3BE:
		return samples * 3;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_BE:
	case SNDRV_PCM_FORMAT_U24_LE:
	case SNDRV_PCM_FORMAT_U24_BE:
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_S32_BE:
	case SNDRV_PCM_FORMAT_U32_LE:
	case SNDRV_PCM_FORMAT_U32_BE:
	case SNDRV_PCM_FORMAT_FLOAT_LE:
	case SNDRV_PCM_FORMAT_FLOAT_BE:
		return samples * 4;
	case SNDRV_PCM_FORMAT_FLOAT64_LE:
	case SNDRV_PCM_FORMAT_FLOAT64_BE:
		return samples * 8;
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE:
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_BE:
		return samples * 4;
	case SNDRV_PCM_FORMAT_MU_LAW:
	case SNDRV_PCM_FORMAT_A_LAW:
		return samples;
	case SNDRV_PCM_FORMAT_IMA_ADPCM:
		if (samples & 1)
			return -EINVAL;
		return samples / 2;
	default:
		return -EINVAL;
	}
}

/**
 * snd_pcm_format_silence_64 - return the silent data in 64bit integer
 * @format: the format to check
 *
 * Returns the silent data in 64bit integer for the given format.
 */
u_int64_t snd_pcm_format_silence_64(snd_pcm_format_t format)
{
	switch (snd_enum_to_int(format)) {
	case SNDRV_PCM_FORMAT_S8:
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S16_BE:
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_BE:
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_S32_BE:
	case SNDRV_PCM_FORMAT_S24_3LE:
	case SNDRV_PCM_FORMAT_S24_3BE:
	case SNDRV_PCM_FORMAT_S20_3LE:
	case SNDRV_PCM_FORMAT_S20_3BE:
	case SNDRV_PCM_FORMAT_S18_3LE:
	case SNDRV_PCM_FORMAT_S18_3BE:
		return 0;
	case SNDRV_PCM_FORMAT_U8:
		return 0x8080808080808080ULL;
#ifdef SNDRV_LITTLE_ENDIAN
	case SNDRV_PCM_FORMAT_U16_LE:
		return 0x8000800080008000ULL;
	case SNDRV_PCM_FORMAT_U24_LE:
		return 0x0080000000800000ULL;
	case SNDRV_PCM_FORMAT_U32_LE:
		return 0x8000000080000000ULL;
	case SNDRV_PCM_FORMAT_U16_BE:
		return 0x0080008000800080ULL;
	case SNDRV_PCM_FORMAT_U24_BE:
		return 0x0000800000008000ULL;
	case SNDRV_PCM_FORMAT_U32_BE:
		return 0x0000008000000080ULL;
	case SNDRV_PCM_FORMAT_U24_3LE:
		return 0x0000800000800000ULL;
	case SNDRV_PCM_FORMAT_U24_3BE:
		return 0x0080000080000080ULL;
	case SNDRV_PCM_FORMAT_U20_3LE:
		return 0x0000080000080000ULL;
	case SNDRV_PCM_FORMAT_U20_3BE:
		return 0x0008000008000008ULL;
	case SNDRV_PCM_FORMAT_U18_3LE:
		return 0x0000020000020000ULL;
	case SNDRV_PCM_FORMAT_U18_3BE:
		return 0x0002000002000002ULL;
#else
	case SNDRV_PCM_FORMAT_U16_LE:
		return 0x0080008000800080ULL;
	case SNDRV_PCM_FORMAT_U24_LE:
		return 0x0000800000008000ULL;
	case SNDRV_PCM_FORMAT_U32_LE:
		return 0x0000008000000080ULL;
	case SNDRV_PCM_FORMAT_U16_BE:
		return 0x8000800080008000ULL;
	case SNDRV_PCM_FORMAT_U24_BE:
		return 0x0080000000800000ULL;
	case SNDRV_PCM_FORMAT_U32_BE:
		return 0x8000000080000000ULL;
	case SNDRV_PCM_FORMAT_U24_3LE:
		return 0x0080000080000080ULL;
	case SNDRV_PCM_FORMAT_U24_3BE:
		return 0x0000800000800000ULL;
	case SNDRV_PCM_FORMAT_U20_3LE:
		return 0x0008000008000008ULL;
	case SNDRV_PCM_FORMAT_U20_3BE:
		return 0x0000080000080000ULL;
	case SNDRV_PCM_FORMAT_U18_3LE:
		return 0x0002000002000002ULL;
	case SNDRV_PCM_FORMAT_U18_3BE:
		return 0x0000020000020000ULL;
#endif
	case SNDRV_PCM_FORMAT_FLOAT_LE:
	{
		union {
			float f;
			u_int32_t i;
		} u;
		u.f = 0.0;
#ifdef SNDRV_LITTLE_ENDIAN
		return u.i;
#else
		return bswap_32(u.i);
#endif
	}
	case SNDRV_PCM_FORMAT_FLOAT64_LE:
	{
		union {
			double f;
			u_int64_t i;
		} u;
		u.f = 0.0;
#ifdef SNDRV_LITTLE_ENDIAN
		return u.i;
#else
		return bswap_64(u.i);
#endif
	}
	case SNDRV_PCM_FORMAT_FLOAT_BE:		
	{
		union {
			float f;
			u_int32_t i;
		} u;
		u.f = 0.0;
#ifdef SNDRV_LITTLE_ENDIAN
		return bswap_32(u.i);
#else
		return u.i;
#endif
	}
	case SNDRV_PCM_FORMAT_FLOAT64_BE:
	{
		union {
			double f;
			u_int64_t i;
		} u;
		u.f = 0.0;
#ifdef SNDRV_LITTLE_ENDIAN
		return bswap_64(u.i);
#else
		return u.i;
#endif
	}
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE:
	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_BE:
		return 0;	
	case SNDRV_PCM_FORMAT_MU_LAW:
		return 0x7f7f7f7f7f7f7f7fULL;
	case SNDRV_PCM_FORMAT_A_LAW:
		return 0x5555555555555555ULL;
	case SNDRV_PCM_FORMAT_IMA_ADPCM:	/* special case */
	case SNDRV_PCM_FORMAT_MPEG:
	case SNDRV_PCM_FORMAT_GSM:
	case SNDRV_PCM_FORMAT_SPECIAL:
		return 0;
	default:
		return -EINVAL;
	}
	return 0;
}

u_int32_t snd_pcm_format_silence_32(snd_pcm_format_t format)
{
	return (u_int32_t)snd_pcm_format_silence_64(format);
}

u_int16_t snd_pcm_format_silence_16(snd_pcm_format_t format)
{
	return (u_int16_t)snd_pcm_format_silence_64(format);
}

u_int8_t snd_pcm_format_silence(snd_pcm_format_t format)
{
	return (u_int8_t)snd_pcm_format_silence_64(format);
}

/**
 * snd_pcm_format_set_silence - set the silence data on the buffer
 * @format: the PCM format
 * @data: the buffer pointer
 * @samples: the number of samples to set silence
 *
 * Sets the silence data on the buffer for the given samples.
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_pcm_format_set_silence(snd_pcm_format_t format, void *data, unsigned int samples)
{
	if (samples == 0)
		return 0;
	switch (snd_pcm_format_width(format)) {
	case 4: {
		u_int8_t silence = snd_pcm_format_silence_64(format);
		unsigned int samples1;
		if (samples % 2 != 0)
			return -EINVAL;
		samples1 = samples / 2;
		memset(data, silence, samples1);
		break;
	}
	case 8: {
		u_int8_t silence = snd_pcm_format_silence_64(format);
		memset(data, silence, samples);
		break;
	}
	case 16: {
		u_int16_t silence = snd_pcm_format_silence_64(format);
		if (! silence)
			memset(data, 0, samples * 2);
		else {
			while (samples-- > 0)
				*((u_int16_t *)data)++ = silence;
		}
		break;
	}
	case 24: {
		u_int32_t silence = snd_pcm_format_silence_64(format);
		if (! silence)
			memset(data, 0, samples * 3);
		else {
			while (samples-- > 0) {
#ifdef SNDRV_LITTLE_ENDIAN
				*((u_int8_t *)data)++ = silence >> 0;
				*((u_int8_t *)data)++ = silence >> 8;
				*((u_int8_t *)data)++ = silence >> 16;
#else
				*((u_int8_t *)data)++ = silence >> 16;
				*((u_int8_t *)data)++ = silence >> 8;
				*((u_int8_t *)data)++ = silence >> 0;
#endif
			}
		}
		break;
	}
	case 32: {
		u_int32_t silence = snd_pcm_format_silence_64(format);
		if (! silence)
			memset(data, 0, samples * 4);
		else {
			while (samples-- > 0)
				*((u_int32_t *)data)++ = silence;
		}
		break;
	}
	case 64: {
		u_int64_t silence = snd_pcm_format_silence_64(format);
		if (! silence)
			memset(data, 0, samples * 8);
		else {
			while (samples-- > 0)
				*((u_int64_t *)data)++ = silence;
		}
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

static int linear_formats[4*2*2] = {
	SNDRV_PCM_FORMAT_S8,
	SNDRV_PCM_FORMAT_S8,
	SNDRV_PCM_FORMAT_U8,
	SNDRV_PCM_FORMAT_U8,
	SNDRV_PCM_FORMAT_S16_LE,
	SNDRV_PCM_FORMAT_S16_BE,
	SNDRV_PCM_FORMAT_U16_LE,
	SNDRV_PCM_FORMAT_U16_BE,
	SNDRV_PCM_FORMAT_S24_LE,
	SNDRV_PCM_FORMAT_S24_BE,
	SNDRV_PCM_FORMAT_U24_LE,
	SNDRV_PCM_FORMAT_U24_BE,
	SNDRV_PCM_FORMAT_S32_LE,
	SNDRV_PCM_FORMAT_S32_BE,
	SNDRV_PCM_FORMAT_U32_LE,
	SNDRV_PCM_FORMAT_U32_BE
};

/**
 * snd_pcm_build_linear_format - return the suitable linear format for the given condition
 * @width: the bit-width
 * @unsignd: 1 if unsigned, 0 if signed.
 * @big_endian: 1 if big-endian, 0 if little-endian
 *
 * Returns the suitable linear format for the given condition.
 */
snd_pcm_format_t snd_pcm_build_linear_format(int width, int unsignd, int big_endian)
{
	switch (width) {
	case 8:
		width = 0;
		break;
	case 16:
		width = 1;
		break;
	case 24:
		width = 2;
		break;
	case 32:
		width = 3;
		break;
	default:
		return SND_PCM_FORMAT_UNKNOWN;
	}
	return snd_int_to_enum(((int(*)[2][2])linear_formats)[width][!!unsignd][!!big_endian]);
}
