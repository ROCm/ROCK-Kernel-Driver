/*
 * KML REINT
 *
 * Copryright (C) 1996 Arthur Ma <arthur.ma@mountainviewdata.com>
 *
 * Copyright (C) 2000 Mountainview Data, Inc.
 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <linux/intermezzo_fs.h>
#include <linux/intermezzo_kml.h>
#include <linux/intermezzo_psdev.h>
#include <linux/intermezzo_upcall.h>

static void kmlreint_pre_secure (struct kml_rec *rec);
static void kmlreint_post_secure (struct kml_rec *rec);

static void kmlreint_pre_secure (struct kml_rec *rec)
{
        if (current->fsuid != current->uid)
                CDEBUG (D_KML, "reint_kmlreint_pre_secure: cannot setfsuid\n");
        if (current->fsgid != current->gid)
                CDEBUG (D_KML, "reint_kmlreint_pre_secure: cannot setfsgid\n");
        current->fsuid = rec->rec_head.uid;
        current->fsgid = rec->rec_head.fsgid;
}

static void kmlreint_post_secure (struct kml_rec *rec)
{
        current->fsuid = current->uid; 
        current->fsgid = current->gid;
        /* current->egid = current->gid; */ 
        /* ????????????? */
}

static int reint_create (int slot_offset, struct kml_rec *rec)
{
        struct  lento_vfs_context info;
        struct  kml_create *create = &rec->rec_kml.create;
        mm_segment_t old_fs;
        int     error;

        ENTRY;
        kmlreint_pre_secure (rec);

        info.slot_offset = slot_offset;
        info.recno = rec->rec_tail.recno;
        info.kml_offset = rec->rec_kml_offset;
        info.flags = 0; 

        CDEBUG (D_KML, "=====REINT_CREATE::%s\n", create->path);
        old_fs = get_fs();
        set_fs (get_ds());
        error = lento_create(create->path, create->mode, &info);
        set_fs (old_fs);
        kmlreint_post_secure (rec);

        EXIT;
        return error;
}

static int reint_open (int slot_offset, struct kml_rec *rec)
{
        return 0;
}

static int reint_mkdir (int slot_offset, struct kml_rec *rec)
{
        struct  lento_vfs_context info;
        struct  kml_mkdir *mkdir = &rec->rec_kml.mkdir;
        mm_segment_t old_fs;
        int     error;

        ENTRY;
        kmlreint_pre_secure (rec);

        info.slot_offset = slot_offset;
        info.recno = rec->rec_tail.recno;
        info.kml_offset = rec->rec_kml_offset;
        info.flags = 0; 
        old_fs = get_fs();
        set_fs (get_ds());
        error = lento_mkdir (mkdir->path, mkdir->mode, &info);
        set_fs (old_fs);
        kmlreint_post_secure (rec);

        EXIT;
        return error;
}

static int reint_rmdir (int slot_offset, struct kml_rec *rec)
{
        struct  kml_rmdir  *rmdir = &rec->rec_kml.rmdir;
        struct  lento_vfs_context info;
        mm_segment_t old_fs;
        char *name;
        int error;

        ENTRY;
        kmlreint_pre_secure (rec);
        name = bdup_printf ("%s/%s", rmdir->path, rmdir->name);
        if (name == NULL)
        {
                kmlreint_post_secure (rec);
                EXIT;
                return -ENOMEM;
        }
        info.slot_offset = slot_offset;
        info.recno = rec->rec_tail.recno;
        info.kml_offset = rec->rec_kml_offset;
        info.flags = 0;

        old_fs = get_fs();
        set_fs (get_ds());
        error = lento_rmdir (name, &info);
        set_fs (old_fs);

        PRESTO_FREE (name, strlen (name) + 1);
        kmlreint_post_secure (rec);
        EXIT;
        return error;
}

static int reint_link (int slot_offset, struct kml_rec *rec)
{
        struct  kml_link *link = &rec->rec_kml.link;
        struct  lento_vfs_context info;
        mm_segment_t old_fs;
        int     error;

        ENTRY;
        kmlreint_pre_secure (rec);

        info.slot_offset = slot_offset;
        info.recno = rec->rec_tail.recno;
        info.kml_offset = rec->rec_kml_offset;
        info.flags = 0; 

        old_fs = get_fs();
        set_fs (get_ds());
        error = lento_link (link->sourcepath, link->targetpath, &info);
        set_fs (old_fs);
        kmlreint_post_secure (rec);
        EXIT;
        return error;
}

static int reint_unlink (int slot_offset, struct kml_rec *rec)
{
        struct  kml_unlink *unlink = &rec->rec_kml.unlink;
        struct  lento_vfs_context info;
        mm_segment_t old_fs;
        int     error;
        char   *name;

        ENTRY;
        kmlreint_pre_secure (rec);
        name = bdup_printf ("%s/%s", unlink->path, unlink->name);
        if (name == NULL)
        {
                kmlreint_post_secure (rec);
                EXIT;
                return -ENOMEM;
        }
        info.slot_offset = slot_offset;
        info.recno = rec->rec_tail.recno;
        info.kml_offset = rec->rec_kml_offset;
        info.flags = 0;

        old_fs = get_fs();
        set_fs (get_ds());
        error = lento_unlink (name, &info);
        set_fs (old_fs);
        PRESTO_FREE (name, strlen (name));
        kmlreint_post_secure (rec);

        EXIT;
        return error;
}

static int reint_symlink (int slot_offset, struct kml_rec *rec)
{
        struct  kml_symlink *symlink = &rec->rec_kml.symlink;
        struct  lento_vfs_context info;
        mm_segment_t old_fs;
        int     error;

        ENTRY;
        kmlreint_pre_secure (rec);

        info.slot_offset = slot_offset;
        info.recno = rec->rec_tail.recno;
        info.kml_offset = rec->rec_kml_offset;
        info.flags = 0; 

        old_fs = get_fs();
        set_fs (get_ds());
        error = lento_symlink (symlink->targetpath, 
                        symlink->sourcepath, &info);
        set_fs (old_fs);
        kmlreint_post_secure (rec);
        EXIT;
        return error;
}

static int reint_rename (int slot_offset, struct kml_rec *rec)
{
        struct  kml_rename *rename = &rec->rec_kml.rename;
        struct  lento_vfs_context info;
        mm_segment_t old_fs;
        int     error;

        ENTRY;
        kmlreint_pre_secure (rec);

        info.slot_offset = slot_offset;
        info.recno = rec->rec_tail.recno;
        info.kml_offset = rec->rec_kml_offset;
        info.flags = 0;

        old_fs = get_fs();
        set_fs (get_ds());
        error = lento_rename (rename->sourcepath, rename->targetpath, &info);
        set_fs (old_fs);
        kmlreint_post_secure (rec);

        EXIT;
        return error;
}

static int reint_setattr (int slot_offset, struct kml_rec *rec)
{
        struct  kml_setattr *setattr = &rec->rec_kml.setattr;
        struct  lento_vfs_context info;
        mm_segment_t old_fs;
        int     error;

        ENTRY;
        kmlreint_pre_secure (rec);

        info.slot_offset = slot_offset;
        info.recno = rec->rec_tail.recno;
        info.kml_offset = rec->rec_kml_offset;
        info.flags = setattr->iattr.ia_attr_flags;

        old_fs = get_fs();
        set_fs (get_ds());
        error = lento_setattr (setattr->path, &setattr->iattr, &info);
        set_fs (old_fs);
        kmlreint_post_secure (rec);
        EXIT;
        return error;
}

static int reint_mknod (int slot_offset, struct kml_rec *rec)
{
        struct  kml_mknod *mknod = &rec->rec_kml.mknod;
        struct  lento_vfs_context info;
        mm_segment_t old_fs;
        int     error;

        ENTRY;
        kmlreint_pre_secure (rec);

        info.slot_offset = slot_offset;
        info.recno = rec->rec_tail.recno;
        info.kml_offset = rec->rec_kml_offset;
        info.flags = 0;

        old_fs = get_fs();
        set_fs (get_ds());
        error = lento_mknod (mknod->path, mknod->mode, 
                MKDEV(mknod->major, mknod->minor), &info);
        set_fs (old_fs);
        kmlreint_post_secure (rec);
        EXIT;
        return error;
}

int kml_reint (char *mtpt, int slot_offset, struct kml_rec *rec)
{
        int error = 0;
        switch (rec->rec_head.opcode)
        {
                case KML_CREATE:
                        error = reint_create (slot_offset, rec);
                        break;
                case KML_OPEN:
                        error = reint_open (slot_offset, rec);
                        break;
                case KML_CLOSE:
                        /* error = reint_close (slot_offset, rec);
                           force the system to return to lento */
                        error = KML_CLOSE_BACKFETCH;
                        break;
                case KML_MKDIR:
                        error = reint_mkdir (slot_offset, rec);
                        break;
                case KML_RMDIR:
                        error = reint_rmdir (slot_offset, rec);
                        break;
                case KML_UNLINK:
                        error = reint_unlink (slot_offset, rec);
                        break;
                case KML_LINK:
                        error =  reint_link (slot_offset, rec);
                        break;
                case KML_SYMLINK:
                        error = reint_symlink (slot_offset, rec);
                        break;
                case KML_RENAME:
                        error = reint_rename (slot_offset, rec);
                        break;
                case KML_SETATTR:
                        error =  reint_setattr (slot_offset, rec);
                        break;
                case KML_MKNOD:
                        error = reint_mknod (slot_offset, rec);
                        break;
                default:
                        CDEBUG (D_KML, "wrong opcode::%d\n", rec->rec_head.opcode);
                        return -EBADF;
        }
        if (error != 0 && error != KML_CLOSE_BACKFETCH)
                CDEBUG (D_KML, "KML_ERROR::error = %d\n", error);
        return error;
}

/* return the old mtpt */
/*
struct fs_struct {
        atomic_t count;
        int umask;
        struct dentry * root, * pwd;
};
*/
static int do_set_fs_root (struct dentry *newroot, 
                                        struct dentry **old_root)
{
        struct dentry *de = current->fs->root;
        current->fs->root = newroot;
	if (old_root != (struct dentry **) NULL)
        	*old_root = de;
        return 0;
}

static int set_system_mtpt (char *mtpt, struct dentry **old_root)
{
	struct nameidata nd;
        struct dentry *dentry;
	int error;

	if (path_init(pathname, LOOKUP_PARENT, &nd))
		error = path_walk(mtpt, &nd);
        if (error) {
                CDEBUG (D_KML, "Yean!!!!::Can't find mtpt::%s\n", mtpt);
                return error;
	}

        dentry = nd.dentry;
        error = do_set_fs_root (dentry, old_root);
        path_release (&nd);
        return error;
}

int kml_reintbuf (struct  kml_fsdata *kml_fsdata,
                  char *mtpt, struct kml_rec **close_rec)
{
        struct kml_rec *rec = NULL;
        struct list_head *head, *tmp;
        struct dentry *old_root;
        int    error = 0;

        head = &kml_fsdata->kml_reint_cache;
        if (list_empty(head))
                return 0;

        if (kml_fsdata->kml_reint_current == NULL ||
            kml_fsdata->kml_reint_current == head->next)
                return 0;

        error = set_system_mtpt (mtpt, &old_root);
        if (error)
                return error;

        tmp = head->next;
        while (error == 0 &&  tmp != head ) {
                rec = list_entry(tmp, struct kml_rec, kml_optimize.kml_chains);
                error = kml_reint (mtpt, rec->rec_kml_offset, rec);
                tmp = tmp->next;
        }

        do_set_fs_root (old_root, NULL);

        if (error == KML_CLOSE_BACKFETCH)
                *close_rec = rec;
        kml_fsdata->kml_reint_current = tmp;
        return error;
}

