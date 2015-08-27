/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __DAL_IRQ_TYPES_H__
#define __DAL_IRQ_TYPES_H__

struct dal_context;

typedef void (*interrupt_handler)(void *);

typedef void *irq_handler_idx;
#define DAL_INVALID_IRQ_HANDLER_IDX NULL


/* The order of the IRQ sources is important and MUST match the one's
of base driver */
enum dal_irq_source {
	/* Use as mask to specify invalid irq source */
	DAL_IRQ_SOURCE_INVALID = 0,

	DAL_IRQ_SOURCE_HPD1,
	DAL_IRQ_SOURCE_HPD2,
	DAL_IRQ_SOURCE_HPD3,
	DAL_IRQ_SOURCE_HPD4,
	DAL_IRQ_SOURCE_HPD5,
	DAL_IRQ_SOURCE_HPD6,

	DAL_IRQ_SOURCE_HPD1RX,
	DAL_IRQ_SOURCE_HPD2RX,
	DAL_IRQ_SOURCE_HPD3RX,
	DAL_IRQ_SOURCE_HPD4RX,
	DAL_IRQ_SOURCE_HPD5RX,
	DAL_IRQ_SOURCE_HPD6RX,

	DAL_IRQ_SOURCE_I2C_DDC1,
	DAL_IRQ_SOURCE_I2C_DDC2,
	DAL_IRQ_SOURCE_I2C_DDC3,
	DAL_IRQ_SOURCE_I2C_DDC4,
	DAL_IRQ_SOURCE_I2C_DDC5,
	DAL_IRQ_SOURCE_I2C_DDC6,

	DAL_IRQ_SOURCE_AZALIA0,
	DAL_IRQ_SOURCE_AZALIA1,
	DAL_IRQ_SOURCE_AZALIA2,
	DAL_IRQ_SOURCE_AZALIA3,
	DAL_IRQ_SOURCE_AZALIA4,
	DAL_IRQ_SOURCE_AZALIA5,

	DAL_IRQ_SOURCE_DPSINK1,
	DAL_IRQ_SOURCE_DPSINK2,
	DAL_IRQ_SOURCE_DPSINK3,
	DAL_IRQ_SOURCE_DPSINK4,
	DAL_IRQ_SOURCE_DPSINK5,
	DAL_IRQ_SOURCE_DPSINK6,

	DAL_IRQ_SOURCE_CRTC1VSYNC,
	DAL_IRQ_SOURCE_CRTC2VSYNC,
	DAL_IRQ_SOURCE_CRTC3VSYNC,
	DAL_IRQ_SOURCE_CRTC4VSYNC,
	DAL_IRQ_SOURCE_CRTC5VSYNC,
	DAL_IRQ_SOURCE_CRTC6VSYNC,
	DAL_IRQ_SOURCE_TIMER,

	DAL_IRQ_SOURCE_PFLIP_FIRST,
	DAL_IRQ_SOURCE_PFLIP1 = DAL_IRQ_SOURCE_PFLIP_FIRST,
	DAL_IRQ_SOURCE_PFLIP2,
	DAL_IRQ_SOURCE_PFLIP3,
	DAL_IRQ_SOURCE_PFLIP4,
	DAL_IRQ_SOURCE_PFLIP5,
	DAL_IRQ_SOURCE_PFLIP6,
	DAL_IRQ_SOURCE_PFLIP_UNDERLAY0,
	DAL_IRQ_SOURCE_PFLIP_LAST = DAL_IRQ_SOURCE_PFLIP_UNDERLAY0,

	DAL_IRQ_SOURCE_GPIOPAD0,
	DAL_IRQ_SOURCE_GPIOPAD1,
	DAL_IRQ_SOURCE_GPIOPAD2,
	DAL_IRQ_SOURCE_GPIOPAD3,
	DAL_IRQ_SOURCE_GPIOPAD4,
	DAL_IRQ_SOURCE_GPIOPAD5,
	DAL_IRQ_SOURCE_GPIOPAD6,
	DAL_IRQ_SOURCE_GPIOPAD7,
	DAL_IRQ_SOURCE_GPIOPAD8,
	DAL_IRQ_SOURCE_GPIOPAD9,
	DAL_IRQ_SOURCE_GPIOPAD10,
	DAL_IRQ_SOURCE_GPIOPAD11,
	DAL_IRQ_SOURCE_GPIOPAD12,
	DAL_IRQ_SOURCE_GPIOPAD13,
	DAL_IRQ_SOURCE_GPIOPAD14,
	DAL_IRQ_SOURCE_GPIOPAD15,
	DAL_IRQ_SOURCE_GPIOPAD16,
	DAL_IRQ_SOURCE_GPIOPAD17,
	DAL_IRQ_SOURCE_GPIOPAD18,
	DAL_IRQ_SOURCE_GPIOPAD19,
	DAL_IRQ_SOURCE_GPIOPAD20,
	DAL_IRQ_SOURCE_GPIOPAD21,
	DAL_IRQ_SOURCE_GPIOPAD22,
	DAL_IRQ_SOURCE_GPIOPAD23,
	DAL_IRQ_SOURCE_GPIOPAD24,
	DAL_IRQ_SOURCE_GPIOPAD25,
	DAL_IRQ_SOURCE_GPIOPAD26,
	DAL_IRQ_SOURCE_GPIOPAD27,
	DAL_IRQ_SOURCE_GPIOPAD28,
	DAL_IRQ_SOURCE_GPIOPAD29,
	DAL_IRQ_SOURCE_GPIOPAD30,

	DAL_IRQ_SOURCE_DC1UNDERFLOW,
	DAL_IRQ_SOURCE_DC2UNDERFLOW,
	DAL_IRQ_SOURCE_DC3UNDERFLOW,
	DAL_IRQ_SOURCE_DC4UNDERFLOW,
	DAL_IRQ_SOURCE_DC5UNDERFLOW,
	DAL_IRQ_SOURCE_DC6UNDERFLOW,

	DAL_IRQ_SOURCE_DMCU_SCP,
	DAL_IRQ_SOURCE_VBIOS_SW,

	DAL_IRQ_SOURCES_NUMBER
};

#define DAL_VALID_IRQ_SRC_NUM(src) \
	((src) <= DAL_IRQ_SOURCES_NUMBER && (src) > DAL_IRQ_SOURCE_INVALID)

/* Number of Page Flip IRQ Sources. */
#define DAL_PFLIP_IRQ_SRC_NUM \
	(DAL_IRQ_SOURCE_PFLIP_LAST - DAL_IRQ_SOURCE_PFLIP_FIRST + 1)

/* the number of contexts may be expanded in the future based on needs */
enum dal_interrupt_context {
	INTERRUPT_LOW_IRQ_CONTEXT = 0,
	INTERRUPT_HIGH_IRQ_CONTEXT,
	INTERRUPT_CONTEXT_NUMBER
};

enum dal_interrupt_porlarity {
	INTERRUPT_POLARITY_DEFAULT = 0,
	INTERRUPT_POLARITY_LOW = INTERRUPT_POLARITY_DEFAULT,
	INTERRUPT_POLARITY_HIGH,
	INTERRUPT_POLARITY_BOTH
};

#define DAL_DECODE_INTERRUPT_POLARITY(int_polarity) \
	(int_polarity == INTERRUPT_POLARITY_LOW) ? "Low" : \
	(int_polarity == INTERRUPT_POLARITY_HIGH) ? "High" : \
	(int_polarity == INTERRUPT_POLARITY_BOTH) ? "Both" : "Invalid"

struct dal_timer_interrupt_params {
	uint64_t micro_sec_interval;
	enum dal_interrupt_context int_context;
	bool no_mutex_wait;
};

struct dal_interrupt_params {
	/* The polarity *change* which will trigger an interrupt.
	 * If 'requested_polarity == INTERRUPT_POLARITY_BOTH', then
	 * 'current_polarity' must be initialised. */
	enum dal_interrupt_porlarity requested_polarity;
	/* If 'requested_polarity == INTERRUPT_POLARITY_BOTH',
	 * 'current_polarity' should contain the current state, which means
	 * the interrupt will be triggered when state changes from what is,
	 * in 'current_polarity'. */
	enum dal_interrupt_porlarity current_polarity;
	enum dal_irq_source irq_source;
	enum dal_interrupt_context int_context;
	bool no_mutex_wait;
	bool one_shot;/* true - trigger once and automatically disable
			false - trigger every time without disabling
			Note that timer interrupt is always 'one shot'. */
};

#endif
