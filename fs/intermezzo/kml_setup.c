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

int kml_init (struct presto_file_set *fset)
{
        struct kml_fsdata *data;

        ENTRY;
        PRESTO_ALLOC (data, struct kml_fsdata *, sizeof (struct kml_fsdata));
        if (data == NULL) {
                EXIT;
                return -ENOMEM;
        }
        INIT_LIST_HEAD (&data->kml_reint_cache);
        INIT_LIST_HEAD (&data->kml_kop_cache);

        PRESTO_ALLOC (data->kml_buf, char *, KML_REINT_MAXBUF);
        if (data->kml_buf == NULL) {
                PRESTO_FREE (data, sizeof (struct kml_fsdata));
                EXIT;
                return -ENOMEM;
        }

        data->kml_maxsize = KML_REINT_MAXBUF;
        data->kml_len = 0;
        data->kml_reintpos = 0;
        data->kml_count = 0;
        fset->fset_kmldata = data;
        EXIT;
        return 0;
}

int kml_cleanup (struct presto_file_set *fset)
{
        struct kml_fsdata *data = fset->fset_kmldata;

        if (data == NULL)
                return 0;

        fset->fset_kmldata = NULL;
#if 0
        kml_sop_cleanup (&data->kml_reint_cache);
        kml_kop_cleanup (&data->kml_kop_cache);
#endif
        PRESTO_FREE (data->kml_buf, KML_REINT_MAXBUF);
        PRESTO_FREE (data, sizeof (struct kml_fsdata));
        return 0;
}


