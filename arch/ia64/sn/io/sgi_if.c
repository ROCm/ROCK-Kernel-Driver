/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>
#include <asm/sn/pci/bridge.h>
#include <asm/sn/ioerror_handling.h>
#include <asm/sn/pci/pciio.h>
#include <asm/sn/slotnum.h>

#define spinlock_init(x,name) mutex_init(x, MUTEX_DEFAULT, name);

void *
kmem_zalloc(size_t size, int flag)
{
        void *ptr = kmalloc(size, GFP_KERNEL);
        BZERO(ptr, size);
        return ptr;
}

#define xtod(c)         ((c) <= '9' ? '0' - (c) : 'a' - (c) - 10)
long
atoi(register char *p)
{
        register long n;
        register int c, neg = 0;

        if (p == NULL)
                return 0;

        if (!isdigit(c = *p)) {
                while (isspace(c))
                        c = *++p;
                switch (c) {
                case '-':
                        neg++;
                case '+': /* fall-through */
                        c = *++p;
                }
                if (!isdigit(c))
                        return (0);
        }
        if (c == '0' && *(p + 1) == 'x') {
                p += 2;
                c = *p;
                n = xtod(c);
                while ((c = *++p) && isxdigit(c)) {
                        n *= 16; /* two steps to avoid unnecessary overflow */
                        n += xtod(c); /* accum neg to avoid surprises at MAX */
                }
        } else {
                n = '0' - c;
                while ((c = *++p) && isdigit(c)) {
                        n *= 10; /* two steps to avoid unnecessary overflow */
                        n += '0' - c; /* accum neg to avoid surprises at MAX */
                }
        }
        return (neg ? n : -n);
}
