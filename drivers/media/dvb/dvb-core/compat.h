#ifndef __CRAP_H
#define __CRAP_H

/**
 *  compatibility crap for old kernels. No guarantee for a working driver
 *  even when everything compiles.
 */


#include <linux/module.h>
#include <linux/list.h>

#ifndef MODULE_LICENSE
#define MODULE_LICENSE(x)
#endif

#ifndef list_for_each_safe
#define list_for_each_safe(pos, n, head) \
        for (pos = (head)->next, n = pos->next; pos != (head); \
                pos = n, n = pos->next)
#endif

#endif

