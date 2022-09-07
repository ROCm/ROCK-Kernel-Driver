/*
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <kcl/kcl_workqueue.h>

#ifndef HAVE_CANCEL_WORK
static bool (*_kcl_cancel_work)(struct work_struct *work, bool is_dwork);

bool _kcl_cancel_work_stub(struct work_struct *work, bool is_dwork)
{
    pr_warn_once("cancel_work function is not supported\n");
    return false;
}

bool kcl_cancel_work(struct work_struct *work)
{
    return _kcl_cancel_work(work, false);
}
EXPORT_SYMBOL(kcl_cancel_work);
#endif

void amdkcl_workqueue_init(void)
{
#ifndef HAVE_CANCEL_WORK
    _kcl_cancel_work = amdkcl_fp_setup("__cancel_work", _kcl_cancel_work_stub);
#endif /* HAVE_CANCEL_WORK */
}

