/*
 * Copyright (C) 2012-2013 Canonical Ltd
 *
 * Based on bo.c which bears the following copyright notice,
 * but is dual licensed:
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include <linux/ww_mutex.h>
#include "kcl_common.h"
#include <kcl/kcl_reservation.h>

#ifndef DEFINE_WD_CLASS
#define DEFINE_WD_CLASS(classname) \
	struct ww_class classname = __WW_CLASS_INITIALIZER(classname)
#endif

DEFINE_WD_CLASS(_kcl_reservation_ww_class_stub);
struct lock_class_key _kcl_reservation_seqcount_class_stub;
const char _kcl_reservation_seqcount_string_stub[] = "reservation_seqcount";

struct ww_class *_kcl_reservation_ww_class;
EXPORT_SYMBOL(_kcl_reservation_ww_class);
struct lock_class_key *_kcl_reservation_seqcount_class;
EXPORT_SYMBOL(_kcl_reservation_seqcount_class);
const char *_kcl_reservation_seqcount_string;
EXPORT_SYMBOL(_kcl_reservation_seqcount_string);

void amdkcl_reservation_init(void)
{
	_kcl_reservation_ww_class = amdkcl_fp_setup("reservation_ww_class",
						    &_kcl_reservation_ww_class_stub);
	_kcl_reservation_seqcount_class = amdkcl_fp_setup("reservation_seqcount_class",
							  &_kcl_reservation_seqcount_class_stub);
	_kcl_reservation_seqcount_string = amdkcl_fp_setup("reservation_seqcount_string",
							  &_kcl_reservation_seqcount_string_stub);
}
