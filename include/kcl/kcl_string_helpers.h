/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_STRING_HELPERS_H
#define AMDKCL_STRING_HELPERS_H

#include <linux/string_helpers.h>
/* Copied from v5.17-rc2-224-gea4692c75e1c linux/string_helpers.h */

#ifndef HAVE_STR_YES_NO
static inline const char *str_yes_no(bool v)
{
        return v ? "yes" : "no";
}

static inline const char *str_on_off(bool v)
{
        return v ? "on" : "off";
}

static inline const char *str_enable_disable(bool v)
{
        return v ? "enable" : "disable";
}

static inline const char *str_enabled_disabled(bool v)
{
        return v ? "enabled" : "disabled";
}

#endif /* HAVE_STR_YES_NO */
#endif
