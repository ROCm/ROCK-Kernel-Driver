#include <linux/list.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "intermezzo_fs.h"
#include "intermezzo_kml.h"


// dlogit -- oppsite to logit ()
//         return the sbuf + size;
char *dlogit (void *tbuf, const void *sbuf, int size)
{
        char *ptr = (char *)sbuf;
        memcpy(tbuf, ptr, size);
        ptr += size;
        return ptr;
}

static spinlock_t kml_lock = SPIN_LOCK_UNLOCKED;
static char  buf[1024];
char * bdup_printf (char *format, ...)
{
        va_list args;
        int  i;
        char *path;
        unsigned long flags;

        spin_lock_irqsave(&kml_lock, flags);
        va_start(args, format);
        i = vsprintf(buf, format, args); /* hopefully i < sizeof(buf) */
        va_end(args);

        PRESTO_ALLOC (path, char *, i + 1);
        if (path == NULL)
                return NULL;
        strcpy (path, buf);

        spin_unlock_irqrestore(&kml_lock, flags);
        return path;
}


