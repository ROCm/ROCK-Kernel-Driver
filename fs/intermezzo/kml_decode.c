/*
 * KML Decoding
 *
 * Copryright (C) 1996 Arthur Ma <arthur.ma@mountainviewdata.com> 
 *
 * Copyright (C) 2001 Mountainview Data, Inc.
 */
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/intermezzo_fs.h>
#include <linux/intermezzo_kml.h>

static int size_round (int val);
static int unpack_create (struct kml_create *rec, char *buf,
                                int pos, int *rec_offs);
static int unpack_open (struct kml_open *rec, char *buf,
                                int pos, int *rec_offs);
static int unpack_symlink (struct kml_symlink *rec, char *buf,
                                int pos, int *rec_offs);
static int unpack_mknod (struct kml_mknod *rec, char *buf,
                                int pos, int *rec_offs);
static int unpack_link (struct kml_link *rec, char *buf,
                                int pos, int *rec_offs);
static int unpack_rename (struct kml_rename *rec, char *buf,
                                int pos, int *rec_offs);
static int unpack_unlink (struct kml_unlink *rec, char *buf,
                                int pos, int *rec_offs);
static int unpack_rmdir (struct kml_rmdir *rec, char *buf,
                                int pos, int *rec_offs);
static int unpack_setattr (struct kml_setattr *rec, char *buf,
                                int pos, int *rec_offs);
static int unpack_close (struct kml_close *rec, char *buf,
                                int pos, int *rec_offs);
static int unpack_mkdir (struct kml_mkdir *rec, char *buf,
                                int pos, int *rec_offs);

#if 0
static int unpack_endmark (struct kml_endmark *rec, char *buf,
                                int pos, int *rec_offs);
static void print_kml_endmark (struct kml_endmark *rec);
#endif

static int kml_unpack (char *kml_buf, int rec_size, int kml_offset,
                        struct kml_rec **newrec);
static char *kml_version (struct presto_version *ver);
static void print_kml_prefix (struct big_journal_prefix *head);
static void print_kml_create (struct kml_create *rec);
static void print_kml_mkdir (struct kml_mkdir *rec);
static void print_kml_unlink (struct kml_unlink *rec);
static void print_kml_rmdir (struct kml_rmdir *rec);
static void print_kml_close (struct kml_close *rec);
static void print_kml_symlink (struct kml_symlink *rec);
static void print_kml_rename (struct kml_rename *rec);
static void print_kml_setattr (struct kml_setattr *rec);
static void print_kml_link (struct kml_link *rec);
static void print_kml_mknod (struct kml_mknod *rec);
static void print_kml_open (struct kml_open *rec);
static void print_kml_suffix (struct journal_suffix *tail);
static char *readrec (char *recbuf, int reclen, int pos, int *size);

#define  KML_PREFIX_WORDS           8
static int kml_unpack (char *kml_buf, int rec_size, int kml_offset, 
                        struct kml_rec **newrec)
{
        struct kml_rec  *rec;
        char            *p;
        int             pos, rec_offs;
        int             error;

        ENTRY;
        if (rec_size < sizeof (struct journal_prefix) +
                       sizeof (struct journal_suffix))
                return -EBADF;

        PRESTO_ALLOC(rec, struct kml_rec *, sizeof (struct kml_rec));
        if (rec == NULL) {
                EXIT;
                return -ENOMEM;
        }
        rec->rec_kml_offset = kml_offset;
        rec->rec_size = rec_size;
        p = kml_buf;
        p = dlogit (&rec->rec_head, p, KML_PREFIX_WORDS * sizeof (int));
        p = dlogit (&rec->rec_head.groups, p, 
                        sizeof (int) * rec->rec_head.ngroups);

        pos = sizeof (struct journal_prefix) + 
                        sizeof (int) * rec->rec_head.ngroups;
        switch (rec->rec_head.opcode)
        {
                case KML_CREATE:
                        error = unpack_create (&rec->rec_kml.create, 
                                        kml_buf, pos, &rec_offs);
                        break;
                case KML_MKDIR:
                        error = unpack_mkdir (&rec->rec_kml.mkdir, 
                                        kml_buf, pos, &rec_offs);
                        break;
                case KML_UNLINK:
                        error = unpack_unlink (&rec->rec_kml.unlink, 
                                        kml_buf, pos, &rec_offs);
                        break;
                case KML_RMDIR:
                        error = unpack_rmdir (&rec->rec_kml.rmdir, 
                                        kml_buf, pos, &rec_offs);
                        break;
                case KML_CLOSE:
                        error = unpack_close (&rec->rec_kml.close, 
                                        kml_buf, pos, &rec_offs);
                        break;
                case KML_SYMLINK:
                        error = unpack_symlink (&rec->rec_kml.symlink, 
                                        kml_buf, pos, &rec_offs);
                        break;
                case KML_RENAME:
                        error = unpack_rename (&rec->rec_kml.rename, 
                                        kml_buf, pos, &rec_offs);
                        break;
                case KML_SETATTR:
                        error = unpack_setattr (&rec->rec_kml.setattr, 
                                        kml_buf, pos, &rec_offs);
                        break;
                case KML_LINK:
                        error = unpack_link (&rec->rec_kml.link, 
                                        kml_buf, pos, &rec_offs);
                        break;
                case KML_OPEN:
                        error = unpack_open (&rec->rec_kml.open, 
                                        kml_buf, pos, &rec_offs);
                        break;
                case KML_MKNOD:
                        error = unpack_mknod (&rec->rec_kml.mknod, 
                                        kml_buf, pos, &rec_offs);
                        break;
#if 0
                case KML_ENDMARK:
                        error = unpack_endmark (&rec->rec_kml.endmark, 
                                        kml_buf, pos, &rec_offs);
                        break;
#endif
                default:
                        CDEBUG (D_KML, "wrong opcode::%u\n", 
                                        rec->rec_head.opcode);
                        EXIT;
                        return -EINVAL;
        } 
        if (error) {
                PRESTO_FREE (rec, sizeof (struct kml_rec));
                return -EINVAL;
        }
        p = kml_buf + rec_offs;
        p = dlogit (&rec->rec_tail, p, sizeof (struct journal_suffix));
        memset (&rec->kml_optimize, 0, sizeof (struct kml_optimize));
        *newrec = rec;
        EXIT;
        return 0;
}

static int size_round (int val)
{
        return (val + 3) & (~0x3);
}

static int unpack_create (struct kml_create *rec, char *buf, 
                                int pos, int *rec_offs)
{
        char *p, *q;
        int unpack_size = 88;
        int pathlen;

        ENTRY;
        p = buf + pos;
        p = dlogit (&rec->old_parentv, p, sizeof (struct presto_version));
        p = dlogit (&rec->new_parentv, p, sizeof (struct presto_version));
        p = dlogit (&rec->new_objectv, p, sizeof (struct presto_version));
        p = dlogit (&rec->mode, p, sizeof (int));
        p = dlogit (&rec->uid, p, sizeof (int));
        p = dlogit (&rec->gid, p, sizeof (int));
        p = dlogit (&pathlen, p, sizeof (int));

        PRESTO_ALLOC(q, char *, pathlen + 1);
        if (q == NULL) {
                EXIT;
                return -ENOMEM;
        }

        memcpy (q, p, pathlen);
        q[pathlen] = '\0';
        rec->path = q;

        *rec_offs = pos + unpack_size + size_round(pathlen);
        EXIT;
        return 0;
}

static int unpack_open (struct kml_open *rec, char *buf, 
                                int pos, int *rec_offs)
{
        *rec_offs = pos;
        return 0;
}

static int unpack_symlink (struct kml_symlink *rec, char *buf, 
                                int pos, int *rec_offs)
{
        char *p, *q;
        int unpack_size = 88;
        int pathlen, targetlen;

        ENTRY;
        p = buf + pos;
        p = dlogit (&rec->old_parentv, p, sizeof (struct presto_version));
        p = dlogit (&rec->new_parentv, p, sizeof (struct presto_version));
        p = dlogit (&rec->new_objectv, p, sizeof (struct presto_version));
        p = dlogit (&rec->uid, p, sizeof (int));
        p = dlogit (&rec->gid, p, sizeof (int));
        p = dlogit (&pathlen, p, sizeof (int));
        p = dlogit (&targetlen, p, sizeof (int));

        PRESTO_ALLOC(q, char *, pathlen + 1);
        if (q == NULL) {
                EXIT;
                return -ENOMEM;
        }

        memcpy (q, p, pathlen);
        q[pathlen] = '\0';
        rec->sourcepath = q;

        PRESTO_ALLOC(q, char *, targetlen + 1);
        if (q == NULL) {
                PRESTO_FREE (rec->sourcepath, pathlen + 1);
                EXIT;
                return -ENOMEM;
        }

        memcpy (q, p, targetlen);
        q[targetlen] = '\0';
        rec->targetpath = q;

        *rec_offs = pos + unpack_size + size_round(pathlen) +
                        size_round(targetlen);
        EXIT;
        return 0;
}

static int unpack_mknod (struct kml_mknod *rec, char *buf, 
                                int pos, int *rec_offs)
{
        char *p, *q;
        int unpack_size = 96;
        int pathlen;

        ENTRY;
        p = buf + pos;
        p = dlogit (&rec->old_parentv, p, sizeof (struct presto_version));
        p = dlogit (&rec->new_parentv, p, sizeof (struct presto_version));
        p = dlogit (&rec->new_objectv, p, sizeof (struct presto_version));
        p = dlogit (&rec->mode, p, sizeof (int));
        p = dlogit (&rec->uid, p, sizeof (int));
        p = dlogit (&rec->gid, p, sizeof (int));
        p = dlogit (&rec->major, p, sizeof (int));
        p = dlogit (&rec->minor, p, sizeof (int));
        p = dlogit (&pathlen, p, sizeof (int));

        PRESTO_ALLOC(q, char *, pathlen + 1);
        if (q == NULL) {
                EXIT;
                return -ENOMEM;
        }

        memcpy (q, p, pathlen);
        q[pathlen] = '\0';
        rec->path = q;

        *rec_offs = pos + unpack_size + size_round(pathlen);
        EXIT;
        return 0;
}

static int unpack_link (struct kml_link *rec, char *buf, 
                                int pos, int *rec_offs)
{
        char *p, *q;
        int unpack_size = 80;
        int pathlen, targetlen;

        ENTRY;
        p = buf + pos;
        p = dlogit (&rec->old_parentv, p, sizeof (struct presto_version));
        p = dlogit (&rec->new_parentv, p, sizeof (struct presto_version));
        p = dlogit (&rec->new_objectv, p, sizeof (struct presto_version));
        p = dlogit (&pathlen, p, sizeof (int));
        p = dlogit (&targetlen, p, sizeof (int));

        PRESTO_ALLOC(q, char *, pathlen + 1);
        if (q == NULL) {
                EXIT;
                return -ENOMEM;
        }

        memcpy (q, p, pathlen);
        q[pathlen] = '\0';
        rec->sourcepath = q;
        p += size_round (pathlen);

        PRESTO_ALLOC(q, char *, targetlen + 1);
        if (q == NULL) {
                PRESTO_FREE (rec->sourcepath, pathlen + 1);
                EXIT;
                return -ENOMEM;
        }
        memcpy (q, p, targetlen);
        q[targetlen] = '\0';
        rec->targetpath = q;

        *rec_offs = pos + unpack_size + size_round(pathlen) +
                        size_round(targetlen);
        EXIT;
        return 0;
}

static int unpack_rename (struct kml_rename *rec, char *buf, 
                                int pos, int *rec_offs)
{
        char *p, *q;
        int unpack_size = 104;
        int pathlen, targetlen;

        ENTRY;
        p = buf + pos;
        p = dlogit (&rec->old_objectv, p, sizeof (struct presto_version));
        p = dlogit (&rec->new_objectv, p, sizeof (struct presto_version));
        p = dlogit (&rec->new_tgtv, p, sizeof (struct presto_version));
        p = dlogit (&rec->old_tgtv, p, sizeof (struct presto_version));
        p = dlogit (&pathlen, p, sizeof (int));
        p = dlogit (&targetlen, p, sizeof (int));

        PRESTO_ALLOC(q, char *, pathlen + 1);
        if (q == NULL) {
                EXIT;
                return -ENOMEM;
        }

        memcpy (q, p, pathlen);
        q[pathlen] = '\0';
        rec->sourcepath = q;
        p += size_round (pathlen);

        PRESTO_ALLOC(q, char *, targetlen + 1);
        if (q == NULL) {
                PRESTO_FREE (rec->sourcepath, pathlen + 1);
                EXIT;
                return -ENOMEM;
        }

        memcpy (q, p, targetlen);
        q[targetlen] = '\0';
        rec->targetpath = q;

        *rec_offs = pos + unpack_size + size_round(pathlen) +
                        size_round(targetlen);
        EXIT;
        return 0;
}

static int unpack_unlink (struct kml_unlink *rec, char *buf, 
                                int pos, int *rec_offs)
{
        char *p, *q;
        int unpack_size = 80;
        int pathlen, targetlen;

        ENTRY;
        p = buf + pos;
        p = dlogit (&rec->old_parentv, p, sizeof (struct presto_version));
        p = dlogit (&rec->new_parentv, p, sizeof (struct presto_version));
        p = dlogit (&rec->old_tgtv, p, sizeof (struct presto_version));
        p = dlogit (&pathlen, p, sizeof (int));
        p = dlogit (&targetlen, p, sizeof (int));

        PRESTO_ALLOC(q, char *, pathlen + 1);
        if (q == NULL) {
                EXIT;
                return -ENOMEM;
        }

        memcpy (q, p, pathlen);
        q[pathlen] = '\0';
        rec->path = q;
        p += size_round (pathlen);

        PRESTO_ALLOC(q, char *, targetlen + 1);
        if (q == NULL) {
                PRESTO_FREE (rec->path, pathlen + 1);
                EXIT;
                return -ENOMEM;
        }

        memcpy (q, p, targetlen);
        q[targetlen] = '\0';
        rec->name = q;

        /* fix the presto_journal_unlink problem */
        *rec_offs = pos + unpack_size + size_round(pathlen) +
                        size_round(targetlen);
        EXIT;
        return 0;
}

static int unpack_rmdir (struct kml_rmdir *rec, char *buf, 
                                int pos, int *rec_offs)
{
        char *p, *q;
        int unpack_size = 80;
        int pathlen, targetlen;

        ENTRY;
        p = buf + pos;
        p = dlogit (&rec->old_parentv, p, sizeof (struct presto_version));
        p = dlogit (&rec->new_parentv, p, sizeof (struct presto_version));
        p = dlogit (&rec->old_tgtv, p, sizeof (struct presto_version));
        p = dlogit (&pathlen, p, sizeof (int));
        p = dlogit (&targetlen, p, sizeof (int));

        PRESTO_ALLOC(q, char *, pathlen + 1);
        if (q == NULL) {
                EXIT;
                return -ENOMEM;
        }

        memcpy (q, p, pathlen);
        q[pathlen] = '\0';
        rec->path = q;
        p += size_round (pathlen);

        PRESTO_ALLOC(q, char *, targetlen + 1);
        if (q == NULL) {
                PRESTO_FREE (rec->path, pathlen + 1);
                EXIT;
                return -ENOMEM;
        }
        memcpy (q, p, targetlen);
        q[targetlen] = '\0';
        rec->name = q;

        *rec_offs = pos + unpack_size + size_round(pathlen) +
                        size_round(targetlen);
        EXIT;
        return 0;
}

static int unpack_setattr (struct kml_setattr *rec, char *buf, 
                                int pos, int *rec_offs)
{
        char *p, *q;
        int unpack_size = 72;
        struct kml_attr {
                __u64   size, mtime, ctime;
        } objattr;
        int     valid, mode, uid, gid, flags;
        int pathlen;

        ENTRY;
        p = buf + pos;
        p = dlogit (&rec->old_objectv, p, sizeof (struct presto_version));
        p = dlogit (&valid, p, sizeof (int));
        p = dlogit (&mode, p, sizeof (int));
        p = dlogit (&uid, p, sizeof (int));
        p = dlogit (&gid, p, sizeof (int));
        p = dlogit (&objattr, p, sizeof (struct kml_attr));
        p = dlogit (&flags, p, sizeof (int));
        p = dlogit (&pathlen, p, sizeof (int));

        rec->iattr.ia_valid = valid;
        rec->iattr.ia_mode = mode;
        rec->iattr.ia_uid = uid;
        rec->iattr.ia_gid = gid;
        rec->iattr.ia_size = objattr.size;
        rec->iattr.ia_mtime = objattr.mtime;
        rec->iattr.ia_ctime = objattr.ctime;
        rec->iattr.ia_atime = 0;
        rec->iattr.ia_attr_flags = flags;

        PRESTO_ALLOC(q, char *, pathlen + 1);
        if (q == NULL) {
                EXIT;
                return -ENOMEM;
        }
        memcpy (q, p, pathlen);
        q[pathlen] = '\0';
        rec->path = q;
        p += pathlen;

        *rec_offs = pos + unpack_size + size_round(pathlen);
        EXIT;
        return 0;
}

static int unpack_close (struct kml_close *rec, char *buf, 
                                int pos, int *rec_offs)
{
        char *p, *q;
        int unpack_size = 52;
        int pathlen;

        ENTRY;
        p = buf + pos;
        p = dlogit (&rec->open_mode, p, sizeof (int));
        p = dlogit (&rec->open_uid, p, sizeof (int));
        p = dlogit (&rec->open_gid, p, sizeof (int));
        p = dlogit (&rec->new_objectv, p, sizeof (struct presto_version));
        p = dlogit (&rec->ino, p, sizeof (__u64));
        p = dlogit (&rec->generation, p, sizeof (int));
        p = dlogit (&pathlen, p, sizeof (int));

        PRESTO_ALLOC(q, char *, pathlen + 1);
        if (q == NULL) {
                EXIT;
                return -ENOMEM;
        }

        memcpy (q, p, pathlen);
        q[pathlen] = '\0';
        rec->path = q;
        p += pathlen;

        *rec_offs = pos + unpack_size + size_round(pathlen);
        EXIT;
        return 0;
}

static int unpack_mkdir (struct kml_mkdir *rec, char *buf, 
                                int pos, int *rec_offs)
{
        char *p, *q;
        int unpack_size = 88;
        int pathlen;

        ENTRY;
        p = buf + pos;
        p = dlogit (&rec->old_parentv, p, sizeof (struct presto_version));
        p = dlogit (&rec->new_parentv, p, sizeof (struct presto_version));
        p = dlogit (&rec->new_objectv, p, sizeof (struct presto_version));
        p = dlogit (&rec->mode, p, sizeof (int));
        p = dlogit (&rec->uid, p, sizeof (int));
        p = dlogit (&rec->gid, p, sizeof (int));
        p = dlogit (&pathlen, p, sizeof (int));

        PRESTO_ALLOC(q, char *, pathlen + 1);
        if (q == NULL) {
                EXIT;
                return -ENOMEM;
        }

        memcpy (q, p, pathlen);
        q[pathlen] = '\0';
        rec->path = q;
        p += pathlen;

        *rec_offs = pos + unpack_size + size_round(pathlen);
        EXIT;
        return 0;
}

#if 0
static int unpack_endmark (struct kml_endmark *rec, char *buf, 
                                int pos, int *rec_offs)
{
        char *p;
        p = buf + pos;
        p = dlogit (&rec->total, p, sizeof (int));

        PRESTO_ALLOC (rec->kop, struct kml_kop_node *, 
                        sizeof (struct kml_kop_node) * rec->total);
        if (rec->kop == NULL) {
                EXIT;
                return -ENOMEM;
        }

        p = dlogit (rec->kop, p, sizeof (struct kml_kop_node) * rec->total);

        *rec_offs = pos + sizeof (int) + sizeof (struct kml_kop_node) * rec->total;
        return 0;
}
#endif

static char *kml_version (struct presto_version *ver)
{
        static char buf[256];
        sprintf (buf, "mt::%lld, ct::%lld, size::%lld",
                ver->pv_mtime, ver->pv_ctime, ver->pv_size); 
        return buf;
}

static void print_kml_prefix (struct big_journal_prefix *head)
{
        int i;

        CDEBUG (D_KML, " === KML PREFIX\n");
        CDEBUG (D_KML, "     len        = %u\n", head->len);
        CDEBUG (D_KML, "     version    = %u\n", head->version);
        CDEBUG (D_KML, "     pid        = %u\n", head->pid);
        CDEBUG (D_KML, "     uid        = %u\n", head->uid);
        CDEBUG (D_KML, "     fsuid      = %u\n", head->fsuid);
        CDEBUG (D_KML, "     fsgid      = %u\n", head->fsgid);
        CDEBUG (D_KML, "     opcode     = %u\n", head->opcode);
        CDEBUG (D_KML, "     ngroup     = %u",  head->ngroups);
        for (i = 0; i < head->ngroups; i++)
                CDEBUG (D_KML, "%u  ",  head->groups[i]);
        CDEBUG (D_KML, "\n");
}

static void print_kml_create (struct kml_create *rec)
{
        CDEBUG (D_KML, " === CREATE\n");
        CDEBUG (D_KML, "     path::%s\n", rec->path);
        CDEBUG (D_KML, "     new_objv::%s\n", kml_version (&rec->new_objectv));
        CDEBUG (D_KML, "     old_parv::%s\n", kml_version (&rec->old_parentv));
        CDEBUG (D_KML, "     new_parv::%s\n", kml_version (&rec->new_parentv));
        CDEBUG (D_KML, "     mode::%o\n", rec->mode);
        CDEBUG (D_KML, "     uid::%d\n", rec->uid);
        CDEBUG (D_KML, "     gid::%d\n", rec->gid);
}

static void print_kml_mkdir (struct kml_mkdir *rec)
{
        CDEBUG (D_KML, " === MKDIR\n");
        CDEBUG (D_KML, "     path::%s\n", rec->path);
        CDEBUG (D_KML, "     new_objv::%s\n", kml_version (&rec->new_objectv));
        CDEBUG (D_KML, "     old_parv::%s\n", kml_version (&rec->old_parentv));
        CDEBUG (D_KML, "     new_parv::%s\n", kml_version (&rec->new_parentv));
        CDEBUG (D_KML, "     mode::%o\n", rec->mode);
        CDEBUG (D_KML, "     uid::%d\n", rec->uid);
        CDEBUG (D_KML, "     gid::%d\n", rec->gid);
}

static void print_kml_unlink (struct kml_unlink *rec)
{
        CDEBUG (D_KML, " === UNLINK\n");
        CDEBUG (D_KML, "     path::%s/%s\n", rec->path, rec->name);
        CDEBUG (D_KML, "     old_tgtv::%s\n", kml_version (&rec->old_tgtv));
        CDEBUG (D_KML, "     old_parv::%s\n", kml_version (&rec->old_parentv));
        CDEBUG (D_KML, "     new_parv::%s\n", kml_version (&rec->new_parentv));
}

static void print_kml_rmdir (struct kml_rmdir *rec)
{
        CDEBUG (D_KML, " === RMDIR\n");
        CDEBUG (D_KML, "     path::%s/%s\n", rec->path, rec->name);
        CDEBUG (D_KML, "     old_tgtv::%s\n", kml_version (&rec->old_tgtv));
        CDEBUG (D_KML, "     old_parv::%s\n", kml_version (&rec->old_parentv));
        CDEBUG (D_KML, "     new_parv::%s\n", kml_version (&rec->new_parentv));
}

static void print_kml_close (struct kml_close *rec)
{
        CDEBUG (D_KML, " === CLOSE\n");
        CDEBUG (D_KML, "     mode::%o\n", rec->open_mode);
        CDEBUG (D_KML, "     uid::%d\n", rec->open_uid);
        CDEBUG (D_KML, "     gid::%d\n", rec->open_gid);
        CDEBUG (D_KML, "     path::%s\n", rec->path);
        CDEBUG (D_KML, "     new_objv::%s\n", kml_version (&rec->new_objectv));
        CDEBUG (D_KML, "     ino::%lld\n", rec->ino);
        CDEBUG (D_KML, "     gen::%u\n", rec->generation);
}

static void print_kml_symlink (struct kml_symlink *rec)
{
        CDEBUG (D_KML, " === SYMLINK\n");
        CDEBUG (D_KML, "     s-path::%s\n", rec->sourcepath);
        CDEBUG (D_KML, "     t-path::%s\n", rec->targetpath);
        CDEBUG (D_KML, "     old_parv::%s\n", kml_version (&rec->old_parentv));
        CDEBUG (D_KML, "     new_parv::%s\n", kml_version (&rec->new_parentv));
        CDEBUG (D_KML, "     new_objv::%s\n", kml_version (&rec->new_objectv));
        CDEBUG (D_KML, "     uid::%d\n", rec->uid);
        CDEBUG (D_KML, "     gid::%d\n", rec->gid);
}

static void print_kml_rename (struct kml_rename *rec)
{
        CDEBUG (D_KML, " === RENAME\n");
        CDEBUG (D_KML, "     s-path::%s\n", rec->sourcepath);
        CDEBUG (D_KML, "     t-path::%s\n", rec->targetpath);
        CDEBUG (D_KML, "     old_tgtv::%s\n", kml_version (&rec->old_tgtv));
        CDEBUG (D_KML, "     new_tgtv::%s\n", kml_version (&rec->new_tgtv));
        CDEBUG (D_KML, "     new_objv::%s\n", kml_version (&rec->new_objectv));
        CDEBUG (D_KML, "     old_objv::%s\n", kml_version (&rec->old_objectv));
}

static void print_kml_setattr (struct kml_setattr *rec)
{
        CDEBUG (D_KML, " === SETATTR\n");
        CDEBUG (D_KML, "     path::%s\n", rec->path);
        CDEBUG (D_KML, "     old_objv::%s\n", kml_version (&rec->old_objectv));
        CDEBUG (D_KML, "     valid::0x%x\n", rec->iattr.ia_valid);
        CDEBUG (D_KML, "     mode::%o\n", rec->iattr.ia_mode);
        CDEBUG (D_KML, "     uid::%d\n", rec->iattr.ia_uid);
        CDEBUG (D_KML, "     gid::%d\n", rec->iattr.ia_gid);
        CDEBUG (D_KML, "     size::%u\n", (u32) rec->iattr.ia_size);
        CDEBUG (D_KML, "     mtime::%u\n", (u32) rec->iattr.ia_mtime);
        CDEBUG (D_KML, "     ctime::%u\n", (u32) rec->iattr.ia_ctime);
        CDEBUG (D_KML, "     flags::%u\n", (u32) rec->iattr.ia_attr_flags);
}

static void print_kml_link (struct kml_link *rec)
{
        CDEBUG (D_KML, " === LINK\n");
        CDEBUG (D_KML, "     path::%s ==> %s\n", rec->sourcepath, rec->targetpath);
        CDEBUG (D_KML, "     old_parv::%s\n", kml_version (&rec->old_parentv));
        CDEBUG (D_KML, "     new_obj::%s\n", kml_version (&rec->new_objectv));
        CDEBUG (D_KML, "     new_parv::%s\n", kml_version (&rec->new_parentv));
}

static void print_kml_mknod (struct kml_mknod *rec)
{
        CDEBUG (D_KML, " === MKNOD\n");
        CDEBUG (D_KML, "     path::%s\n", rec->path);
        CDEBUG (D_KML, "     new_obj::%s\n", kml_version (&rec->new_objectv));
        CDEBUG (D_KML, "     old_parv::%s\n", kml_version (&rec->old_parentv));
        CDEBUG (D_KML, "     new_parv::%s\n", kml_version (&rec->new_parentv));
        CDEBUG (D_KML, "     mode::%o\n", rec->mode);
        CDEBUG (D_KML, "     uid::%d\n", rec->uid);
        CDEBUG (D_KML, "     gid::%d\n", rec->gid);
        CDEBUG (D_KML, "     major::%d\n", rec->major);
        CDEBUG (D_KML, "     minor::%d\n", rec->minor);
}

static void print_kml_open (struct kml_open *rec)
{
        CDEBUG (D_KML, " === OPEN\n");
}

#if 0
static void print_kml_endmark (struct kml_endmark *rec)
{
        int i;
        CDEBUG (D_KML, " === ENDMARK\n");
        CDEBUG (D_KML, "     total::%u\n", rec->total);
        for (i = 0; i < rec->total; i++)
        {       
                CDEBUG (D_KML, "         recno=%ld::flag=%ld,op=%ld, i_ino=%ld, \
                        i_nlink=%ld\n", (long) rec->kop[i].kml_recno, 
                        (long) rec->kop[i].kml_flag, (long) rec->kop[i].kml_op, 
                        (long) rec->kop[i].i_ino, (long) rec->kop[i].i_nlink);
        }
}
#endif

static void print_kml_optimize (struct kml_optimize  *rec)
{
        CDEBUG (D_KML, " === OPTIMIZE\n");
        if (rec->kml_flag == KML_REC_DELETE)
                CDEBUG (D_KML, "     kml_flag::deleted\n");
        else
                CDEBUG (D_KML, "     kml_flag::exist\n");
        CDEBUG (D_KML, "     kml_op::%u\n", rec->kml_op);
        CDEBUG (D_KML, "     i_nlink::%d\n", rec->i_nlink);
        CDEBUG (D_KML, "     i_ino::%u\n", rec->i_ino);
}

static void print_kml_suffix (struct journal_suffix *tail)
{
        CDEBUG (D_KML, " === KML SUFFIX\n");
        CDEBUG (D_KML, "     prevrec::%ld\n", tail->prevrec);
        CDEBUG (D_KML, "     recno::%ld\n", (long) tail->recno);
        CDEBUG (D_KML, "     time::%d\n", tail->time);
        CDEBUG (D_KML, "     len::%d\n", tail->len);
}

void kml_printrec (struct kml_rec *rec, int kml_printop)
{
        if (kml_printop & PRINT_KML_PREFIX)
                print_kml_prefix (&rec->rec_head);
        if (kml_printop & PRINT_KML_REC) 
        { 
                switch (rec->rec_head.opcode)
                {
                        case KML_CREATE:
                                print_kml_create (&rec->rec_kml.create);
                                break;
                        case KML_MKDIR:
                                print_kml_mkdir (&rec->rec_kml.mkdir);
                                break;
                        case KML_UNLINK:
                                print_kml_unlink (&rec->rec_kml.unlink);
                                break;
                        case KML_RMDIR:
                                print_kml_rmdir (&rec->rec_kml.rmdir);
                                break;
                        case KML_CLOSE:
                                print_kml_close (&rec->rec_kml.close);
                                break;
                        case KML_SYMLINK:
                                print_kml_symlink (&rec->rec_kml.symlink);
                                break;
                        case KML_RENAME:
                                print_kml_rename (&rec->rec_kml.rename);
                                break;
                        case KML_SETATTR:
                                print_kml_setattr (&rec->rec_kml.setattr);
                                break;
                        case KML_LINK:
                                print_kml_link (&rec->rec_kml.link);
                                break;
                        case KML_OPEN:
                                print_kml_open (&rec->rec_kml.open);
                                break;
                        case KML_MKNOD:
                                print_kml_mknod (&rec->rec_kml.mknod);
                                break;
#if 0
                        case KML_ENDMARK:
                                print_kml_endmark (&rec->rec_kml.endmark);
#endif
                                break;
                        default:
                                CDEBUG (D_KML, " === BAD RECORD, opcode=%u\n",
                                        rec->rec_head.opcode);
                                break;
                }
        }
        if (kml_printop & PRINT_KML_SUFFIX)
                print_kml_suffix (&rec->rec_tail);
        if (kml_printop & PRINT_KML_OPTIMIZE)
                print_kml_optimize (&rec->kml_optimize);
}

void kml_freerec (struct kml_rec *rec)
{
        char *sourcepath = NULL,
             *targetpath = NULL;
        switch (rec->rec_head.opcode)
        {
                case KML_CREATE:
                        sourcepath = rec->rec_kml.create.path;
                        break;
                case KML_MKDIR:
                        sourcepath = rec->rec_kml.create.path;
                        break;
                case KML_UNLINK:
                        sourcepath = rec->rec_kml.unlink.path;
                        targetpath = rec->rec_kml.unlink.name;
                        break;
                case KML_RMDIR:
                        sourcepath = rec->rec_kml.rmdir.path;
                        targetpath = rec->rec_kml.rmdir.name;
                        break;
                case KML_CLOSE:
                        sourcepath = rec->rec_kml.close.path;
                        break;
                case KML_SYMLINK:
                        sourcepath = rec->rec_kml.symlink.sourcepath;
                        targetpath = rec->rec_kml.symlink.targetpath;
                        break;
                case KML_RENAME:
                        sourcepath = rec->rec_kml.rename.sourcepath;
                        targetpath = rec->rec_kml.rename.targetpath;
                        break;
                case KML_SETATTR:
                        sourcepath = rec->rec_kml.setattr.path;
                        break;
                case KML_LINK:
                        sourcepath = rec->rec_kml.link.sourcepath;
                        targetpath = rec->rec_kml.link.targetpath;
                        break;
                case KML_OPEN:
                        break;
                case KML_MKNOD:
                        sourcepath = rec->rec_kml.mknod.path;
                        break;
#if 0
                case KML_ENDMARK:
                        PRESTO_FREE (rec->rec_kml.endmark.kop, sizeof (int) + 
                                sizeof (struct kml_kop_node) * 
                                rec->rec_kml.endmark.total);
#endif
                        break;
                default:
                        break;
        }
        if (sourcepath != NULL)
                PRESTO_FREE (sourcepath, strlen (sourcepath) + 1);
        if (targetpath != NULL)
                PRESTO_FREE (targetpath, strlen (targetpath) + 1);
}

char *readrec (char *recbuf, int reclen, int pos, int *size)
{
        char *p = recbuf + pos;
        *size = *((int *) p);
        if (*size > (reclen - pos))
            return NULL;
        return p; 
}

int kml_decoderec (char *buf, int pos, int buflen, int *size, 
                        struct kml_rec **newrec)
{
        char *tmp;
        int  error;
        tmp = readrec (buf, buflen, pos, size);
        if (tmp == NULL)
                return -EBADF;
        error = kml_unpack (tmp, *size, pos, newrec); 
        return error;
}

#if 0
static void fill_kmlrec_optimize (struct list_head *head, 
                struct kml_rec *optrec)
{
        struct kml_rec *kmlrec;
        struct list_head *tmp;
        struct kml_endmark *km;
        struct kml_optimize *ko;
        int    n;

        if (optrec->rec_kml.endmark.total == 0)
                return;
        n = optrec->rec_kml.endmark.total - 1;
        tmp = head->prev;
        km = &optrec->rec_kml.endmark;
        while ( n >= 0 && tmp != head ) 
        {
                kmlrec = list_entry(tmp, struct kml_rec,
                        kml_optimize.kml_chains);
                tmp = tmp->prev;
                if (kmlrec->rec_tail.recno == km->kop[n].kml_recno) 
                {
                        ko = &kmlrec->kml_optimize;
                        ko->kml_flag = km->kop[n].kml_flag;
                        ko->kml_op   = km->kop[n].kml_op;
                        ko->i_nlink  = km->kop[n].i_nlink;
                        ko->i_ino    = km->kop[n].i_ino;
                        n --;
                }
        }
        if (n != -1)
                CDEBUG (D_KML, "Yeah!!!, KML optimize error, recno=%d, n=%d\n",
                        optrec->rec_tail.recno, n);     
}
#endif

int decode_kmlrec (struct list_head *head, char *kml_buf, int buflen)
{
        struct kml_rec *rec;
        int    pos = 0, size;
        int    err;
        while (pos < buflen) {
                err = kml_decoderec (kml_buf, pos, buflen, &size, &rec);
                if (err != 0)
                        break;
#if 0
                if (rec->rec_head.opcode == KML_ENDMARK) {
                        fill_kmlrec_optimize (head, rec);
                        mark_rec_deleted (rec);
                }
#endif
                list_add_tail (&rec->kml_optimize.kml_chains, head);
                pos += size;
        }
        return err;
}

int delete_kmlrec (struct list_head *head)
{
        struct kml_rec *rec;
        struct list_head *tmp;

        if (list_empty(head))
                return 0;
        tmp = head->next;
        while ( tmp != head ) {
                rec = list_entry(tmp, struct kml_rec, 
                        kml_optimize.kml_chains);
                tmp = tmp->next;
                kml_freerec (rec);
        }
        INIT_LIST_HEAD(head);
        return 0;
}

int print_allkmlrec (struct list_head *head, int printop)
{
        struct kml_rec *rec;
        struct list_head *tmp;

        if (list_empty(head))
                return 0;
        tmp = head->next;
        while ( tmp != head ) {
                rec = list_entry(tmp, struct kml_rec,
                        kml_optimize.kml_chains);
                tmp = tmp->next;
#if 0
                if (printop & PRINT_KML_EXIST) {
                        if (is_deleted_node (rec))
                                continue;
                }
                else if (printop & PRINT_KML_DELETE) {
                        if (! is_deleted_node (rec))
                                continue;
                }
#endif
                kml_printrec (rec, printop);
        }
        INIT_LIST_HEAD(head);
        return 0;
}

