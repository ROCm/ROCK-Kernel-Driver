/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KCL_KCL_STDDEF_H_
#define _KCL_KCL_STDDEF_H_

#include <linux/stddef.h>
#ifndef sizeof_field
/**
 * sizeof_field() - Report the size of a struct field in bytes
 *
 * @TYPE: The structure containing the field of interest
 * @MEMBER: The field to return the size of
 */
#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))
#endif

#endif
