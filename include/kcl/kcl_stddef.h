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

#ifndef DECLARE_FLEX_ARRAY
/**
 * DECLARE_FLEX_ARRAY() - Declare a flexible array usable in a union
 *
 * @TYPE: The type of each flexible array element
 * @NAME: The name of the flexible array member
 *
 * In order to have a flexible array member in a union or alone in a
 * struct, it needs to be wrapped in an anonymous struct with at least 1
 * named member, but that member can be empty.
 */
#define DECLARE_FLEX_ARRAY(TYPE, NAME) \
        struct { \
                struct { } __empty_ ## NAME; \
                TYPE NAME[]; \
        }
#endif

#endif
