#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <asm/uaccess.h>

#include <linux/intermezzo_fs.h>
#include <linux/intermezzo_upcall.h>
#include <linux/intermezzo_psdev.h>
#include <linux/intermezzo_kml.h>

static struct presto_file_set * kml_getfset (char *path)
{
        return presto_path2fileset(path);
}

/* Send the KML buffer and related volume info into kernel */
int begin_kml_reint (struct file *file, unsigned long arg)
{
        struct {
                char *volname;
                int   namelen;  
                char *recbuf;
                int   reclen;     /* int   newpos; */
        } input;
        struct kml_fsdata *kml_fsdata = NULL;
        struct presto_file_set *fset = NULL;
        char   *path;
        int    error;

        ENTRY;
        /* allocate buffer & copy it to kernel space */
        error = copy_from_user(&input, (char *)arg, sizeof(input));
        if ( error ) {
                EXIT;
                return error;
        }

        if (input.reclen > kml_fsdata->kml_maxsize)
                return -ENOMEM; /* we'll find solution to this in the future */

        PRESTO_ALLOC(path, char *, input.namelen + 1);
        if ( !path ) {
                EXIT;
                return -ENOMEM;
        }
        error = copy_from_user(path, input.volname, input.namelen);
        if ( error ) {
                PRESTO_FREE(path, input.namelen + 1);
                EXIT;
                return error;
        }
        path[input.namelen] = '\0';
        fset = kml_getfset (path);
        PRESTO_FREE(path, input.namelen + 1);

        kml_fsdata = FSET_GET_KMLDATA(fset);
        /* read the buf from user memory here */
        error = copy_from_user(kml_fsdata->kml_buf, input.recbuf, input.reclen);
        if ( error ) {
                EXIT;
                return error;
        }
        kml_fsdata->kml_len = input.reclen;

        decode_kmlrec (&kml_fsdata->kml_reint_cache,
                        kml_fsdata->kml_buf, kml_fsdata->kml_len);

        kml_fsdata->kml_reint_current = kml_fsdata->kml_reint_cache.next;
        kml_fsdata->kml_reintpos = 0;
        kml_fsdata->kml_count = 0;
        return 0;
}

/* DO_KML_REINT  */
int do_kml_reint (struct file *file, unsigned long arg)
{
        struct {
                char *volname;
                int   namelen;  
                char *path;
                int pathlen;
                int recno;
                int offset;
                int len;
                int generation;
                __u64 ino;
        } input;
        int error;
        char   *path;
        struct kml_rec *close_rec;
        struct kml_fsdata *kml_fsdata;
        struct presto_file_set *fset;

        ENTRY;
        error = copy_from_user(&input, (char *)arg, sizeof(input));
        if ( error ) {
                EXIT;
                return error;
        }
        PRESTO_ALLOC(path, char *, input.namelen + 1);
        if ( !path ) {
                EXIT;
                return -ENOMEM;
        }
        error = copy_from_user(path, input.volname, input.namelen);
        if ( error ) {
                PRESTO_FREE(path, input.namelen + 1);
                EXIT;
                return error;
        }
        path[input.namelen] = '\0';
        fset = kml_getfset (path);
        PRESTO_FREE(path, input.namelen + 1);

        kml_fsdata = FSET_GET_KMLDATA(fset);

        error = kml_reintbuf(kml_fsdata, 
                fset->fset_mtpt->d_name.name, 
                &close_rec);

        if (error == KML_CLOSE_BACKFETCH && close_rec != NULL) {
                struct kml_close *close = &close_rec->rec_kml.close;
                input.ino = close->ino;
                input.generation = close->generation;
                if (strlen (close->path) + 1 < input.pathlen) {
                        strcpy (input.path, close->path);
                        input.pathlen = strlen (close->path) + 1;
                        input.recno = close_rec->rec_tail.recno;
                        input.offset = close_rec->rec_kml_offset;
                        input.len = close_rec->rec_size;
                        input.generation = close->generation;
                        input.ino = close->ino;
                }
                else {
                        CDEBUG(D_KML, "KML_DO_REINT::no space to save:%d < %d",
                                strlen (close->path) + 1, input.pathlen);
                        error = -ENOMEM;
                }
                copy_to_user((char *)arg, &input, sizeof (input));
        }
        return error;
}

/* END_KML_REINT */
int end_kml_reint (struct file *file, unsigned long arg)
{
        /* Free KML buffer and related volume info */
        struct {
                char *volname;
                int   namelen;  
#if 0
                int   count; 
                int   newpos; 
#endif
        } input;
        struct presto_file_set *fset = NULL;
        struct kml_fsdata *kml_fsdata = NULL;
        int error;
        char *path;

        ENTRY;
        error = copy_from_user(&input, (char *)arg, sizeof(input));
        if ( error ) {
               EXIT;
               return error;
        }

        PRESTO_ALLOC(path, char *, input.namelen + 1);
        if ( !path ) {
                EXIT;
                return -ENOMEM;
        }
        error = copy_from_user(path, input.volname, input.namelen);
        if ( error ) {
                PRESTO_FREE(path, input.namelen + 1);
                EXIT;
                return error;
        }
        path[input.namelen] = '\0';
        fset = kml_getfset (path);
        PRESTO_FREE(path, input.namelen + 1);

        kml_fsdata = FSET_GET_KMLDATA(fset);
        delete_kmlrec (&kml_fsdata->kml_reint_cache);

        /* kml reint support */
        kml_fsdata->kml_reint_current = NULL;
        kml_fsdata->kml_len = 0;
        kml_fsdata->kml_reintpos = 0;
        kml_fsdata->kml_count = 0;
#if 0
        input.newpos = kml_upc->newpos;
        input.count = kml_upc->count;
        copy_to_user((char *)arg, &input, sizeof (input));
#endif
        return error;
}
