/*
 * Mostly platform independent upcall operations to Venus:
 *  -- upcalls
 *  -- upcall routines
 *
 * Linux 2.0 version
 * Copyright (C) 1996 Peter J. Braam <braam@cs.cmu.edu>,
 * Michael Callahan <callahan@maths.ox.ac.uk>
 *
 * Redone for Linux 2.1
 * Copyright (C) 1997 Carnegie Mellon University
 *
 * Carnegie Mellon University encourages users of this code to contribute
 * improvements to the Coda project. Contact Peter Braam <coda@cs.cmu.edu>.
 *
 * Much cleaned up for InterMezzo
 * Copyright (C) 1998 Peter J. Braam <braam@cs.cmu.edu>,
 * Copyright (C) 1999 Carnegie Mellon University
 *
 */

#include <asm/system.h>
#include <asm/segment.h>
#include <asm/signal.h>
#include <linux/signal.h>

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <asm/segment.h>

#include <linux/intermezzo_fs.h>
#include <linux/intermezzo_upcall.h>
#include <linux/intermezzo_psdev.h>

/*
  At present: four upcalls
  - opendir: fetch a directory (synchronous & asynchronous)
  - open: fetch file (synchronous)
  - journal: send a journal page (asynchronous)
  - permit: get a permit (synchronous)

  Errors returned here are positive.

 */


#define INSIZE(tag) sizeof(struct lento_ ## tag ## _in)
#define OUTSIZE(tag) sizeof(struct lento_ ## tag ## _out)
#define SIZE(tag)  ( (INSIZE(tag)>OUTSIZE(tag)) ? INSIZE(tag) : OUTSIZE(tag) )

#define UPARG(op)\
do {\
        PRESTO_ALLOC(inp, union up_args *, insize);\
        if ( !inp ) { return -ENOMEM; }\
        outp = (union down_args *) (inp);\
        inp->uh.opcode = (op);\
        inp->uh.pid = current->pid;\
        inp->uh.uid = current->fsuid;\
        outsize = insize;\
} while (0)

#define BUFF_ALLOC(buffer)                              \
        PRESTO_ALLOC(buffer, char *, PAGE_SIZE);        \
        if ( !buffer ) {                                \
                printk("PRESTO: out of memory!\n");     \
                return -ENOMEM;                         \
        }

/* the upcalls */
int lento_kml(int minor, unsigned int offset, unsigned int first_recno,
              unsigned int length, unsigned int last_recno, int namelen,
              char *fsetname)
{
        union up_args *inp;
        union down_args *outp;
        int insize, outsize, error;
        ENTRY;

        if (!presto_lento_up(minor)) {
                EXIT;
                return 0;
        }

        insize = SIZE(kml) + namelen + 1;
        UPARG(LENTO_KML);
        inp->lento_kml.namelen = namelen;
        memcpy(inp->lento_kml.fsetname, fsetname, namelen);
        inp->lento_kml.fsetname[namelen] = '\0';
        inp->lento_kml.offset = offset;
        inp->lento_kml.first_recno = first_recno;
        inp->lento_kml.length = length;
        inp->lento_kml.last_recno = last_recno;

        CDEBUG(D_UPCALL, "KML: fileset %s, offset %d, length %d, "
               "first %d, last %d; minor %d\n",
               inp->lento_kml.fsetname,
               inp->lento_kml.offset,
               inp->lento_kml.length,
               inp->lento_kml.first_recno,
               inp->lento_kml.last_recno, minor);

        error = lento_upcall(minor, insize, &outsize, inp,
                             ASYNCHRONOUS, NULL);

        EXIT;
        return error;
}

int lento_release_permit( int minor, int mycookie )
{
        union up_args *inp;
        union down_args *outp;
        int insize, outsize, error;
        ENTRY;

        if (!presto_lento_up(minor)) {
                EXIT;
                return 0;
        }

        insize= SIZE(response_cookie);
        UPARG(LENTO_COOKIE);
        inp->lento_response_cookie.cookie= mycookie;

        CDEBUG(D_UPCALL, "cookie %d\n", mycookie);

        error = lento_upcall(minor, insize, &outsize, inp,
                             ASYNCHRONOUS, NULL);

        EXIT;
        return error;
}

int lento_opendir(int minor, int pathlen, char *path, int async)
{
        union up_args *inp;
        union down_args *outp;
        int insize, outsize, error;
        ENTRY;

        insize = SIZE(opendir) + pathlen + 1;
        UPARG(LENTO_OPENDIR);
        inp->lento_opendir.async = async;
        inp->lento_opendir.pathlen = pathlen;
        memcpy(inp->lento_opendir.path, path, pathlen);
        inp->lento_opendir.path[pathlen] = '\0';

        CDEBUG(D_UPCALL, "path %s\n", inp->lento_opendir.path);

        if (async) {
                error = lento_upcall(minor, insize, &outsize, inp,
                                     ASYNCHRONOUS, NULL);
                return 0;
        }

        error = lento_upcall(minor, insize, &outsize, inp,
                             SYNCHRONOUS, NULL);
        if (error && error != EISFSETROOT) {
                printk("lento_opendir: error %d\n", error);
        }

        EXIT;
        return error;
}

int lento_open(int minor, int pathlen, char *path)
{
        union up_args *inp;
        union down_args *outp;
        int insize, outsize, error;

	ENTRY;
	insize = SIZE(open) + pathlen + 1;
	UPARG(LENTO_OPEN);
	inp->lento_open.pathlen = pathlen;
	memcpy(inp->lento_open.path, path, pathlen);
	inp->lento_open.path[pathlen] = '\0';

	CDEBUG(D_UPCALL, "path %s\n", inp->lento_open.path);

	error = lento_upcall(minor, insize, &outsize, inp,
			     SYNCHRONOUS, NULL);
	if (error) {
	        printk("lento_open: error %d\n", error);
	}

        EXIT;
        return error;
}


int lento_permit(int minor, int pathlen, int fsetnamelen, char *path, char *fsetname)
{
        union up_args *inp;
        union down_args *outp;
        int insize, outsize, error;
        ENTRY;

        insize = SIZE(permit) + pathlen + 1 + fsetnamelen + 1;
        UPARG(LENTO_PERMIT);
        inp->lento_permit.pathlen = pathlen;
        inp->lento_permit.fsetnamelen = fsetnamelen;

        memcpy(inp->lento_permit.path, path, pathlen);
        inp->lento_permit.path[pathlen] = '\0';

	memcpy(&(inp->lento_permit.path[pathlen+1]), fsetname, fsetnamelen); 
        inp->lento_permit.path[fsetnamelen + 1 + pathlen] = '\0';

        CDEBUG(D_UPCALL, "Permit minor %d path %s\n", minor,
               inp->lento_permit.path);

        error = lento_upcall(minor, insize, &outsize, inp,
                             SYNCHRONOUS, NULL);
        if (error) {
                if( error == -EROFS ) {
                        int err;
                        printk("lento_permit: ERROR - requested permit for "
                               "read-only fileset.\n"
                               "   Setting \"%s\" read-only!\n",
                               path);
                        err= presto_mark_cache(path, 0xFFFFFFFF, 
                                               CACHE_CLIENT_RO, NULL);
                        if( err ) {
                                printk("ERROR : mark_cache %d\n", err);
                        }
                }
                else {
                        printk("lento_permit: error %d\n", error);
                }
        }

        EXIT;

        return error;
}

