#define MSNFS	/* HACK HACK */
/*
 * linux/fs/nfsd/export.c
 *
 * NFS exporting and validation.
 *
 * We maintain a list of clients, each of which has a list of
 * exports. To export an fs to a given client, you first have
 * to create the client entry with NFSCTL_ADDCLIENT, which
 * creates a client control block and adds it to the hash
 * table. Then, you call NFSCTL_EXPORT for each fs.
 *
 *
 * Copyright (C) 1995, 1996 Olaf Kirch, <okir@monad.swb.de>
 */

#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/in.h>
#include <linux/seq_file.h>
#include <linux/rwsem.h>
#include <linux/namei.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/nfsfh.h>
#include <linux/nfsd/syscall.h>
#include <linux/lockd/bind.h>

#define NFSDDBG_FACILITY	NFSDDBG_EXPORT
#define NFSD_PARANOIA 1

typedef struct auth_domain	svc_client;
typedef struct svc_export	svc_export;

static void		exp_unexport_all(svc_client *clp);
static void		exp_do_unexport(svc_export *unexp);
static int		exp_verify_string(char *cp, int max);

#define	EXPKEY_HASHBITS		8
#define	EXPKEY_HASHMAX		(1 << EXPKEY_HASHBITS)
#define	EXPKEY_HASHMASK		(EXPKEY_HASHMAX -1)
static struct list_head expkey_table[EXPKEY_HASHMAX];

static inline int expkey_hash(struct auth_domain *clp, int type, u32 *fsidv)
{
	int hash = type;
	char * cp = (char*)fsidv;
	int len = (type==0)?8:4;
	while (len--)
		hash += *cp++;
	cp = (char*)&clp;
	len = sizeof(clp);
	while (len--)
		hash += *cp++;
	return hash & EXPKEY_HASHMASK;
}

/* hash table of exports indexed by dentry+client */
#define	EXPORT_HASHBITS		8
#define	EXPORT_HASHMAX		(1<< EXPORT_HASHBITS)
#define	EXPORT_HASHMASK		(EXPORT_HASHMAX -1)

struct list_head	export_table[EXPORT_HASHMAX];

static int export_hash(svc_client *clp, struct dentry *dentry)
{
	void *k[2];
	unsigned char *cp;
	int rv, i;
	k[0] = clp;
	k[1] = dentry;

	cp = (char*)k;
	rv = 0;
	for (i=0; i<sizeof(k); i++)
		rv ^= cp[i];
	return rv & EXPORT_HASHMASK;
}

struct svc_expkey *
exp_find_key(svc_client *clp, int fsid_type, u32 *fsidv)
{
	struct list_head *head;
	struct svc_expkey *ek;
	
	if (!clp)
		return NULL;

	head = &expkey_table[expkey_hash(clp, fsid_type, fsidv)];
	list_for_each_entry(ek, head, ek_hash)
		if (ek->ek_fsidtype == fsid_type &&
		    fsidv[0] == ek->ek_fsid[0] &&
		    (fsid_type == 1 || fsidv[1] == ek->ek_fsid[1]) &&
		    clp      == ek->ek_client)
			return ek;

	return NULL;
}

/*
 * Find the client's export entry matching xdev/xino.
 */
static inline struct svc_expkey *
exp_get_key(svc_client *clp, dev_t dev, ino_t ino)
{
	u32 fsidv[2];
	
	mk_fsid_v0(fsidv, dev, ino);
	return exp_find_key(clp, 0, fsidv);
}

static inline svc_export *
exp_get(svc_client *clp, dev_t dev, ino_t ino)
{
	struct svc_expkey *ek;

	ek = exp_get_key(clp, dev, ino);
	if (ek)
		return ek->ek_export;
	else
		return NULL;
}

/*
 * Find the client's export entry matching fsid
 */
static inline struct svc_expkey *
exp_get_fsid_key(svc_client *clp, int fsid)
{
	u32 fsidv[2];

	mk_fsid_v1(fsidv, fsid);

	return exp_find_key(clp, 1, fsidv);
}

static inline svc_export *
exp_get_fsid(svc_client *clp, int fsid)
{
	struct svc_expkey *ek;

	ek = exp_get_fsid_key(clp, fsid);
	if (ek)
		return ek->ek_export;
	else
		return NULL;
}

svc_export *
exp_get_by_name(svc_client *clp, struct vfsmount *mnt, struct dentry *dentry)
{
	svc_export *exp;
	struct list_head *head = &export_table[export_hash(clp, dentry)];

	if (!clp)
		return NULL;

	list_for_each_entry(exp, head, ex_hash) {
		if (exp->ex_dentry == dentry && 
		    exp->ex_mnt    == mnt &&
		    exp->ex_client == clp)
			return exp;
	}
	return NULL;
}

/*
 * Find the export entry for a given dentry.
 */
struct svc_export *
exp_parent(svc_client *clp, struct vfsmount *mnt, struct dentry *dentry)
{
	svc_export *exp;

	read_lock(&dparent_lock);
	exp = exp_get_by_name(clp, mnt, dentry);
	while (exp == NULL && dentry != dentry->d_parent) {
		dentry = dentry->d_parent;
		exp = exp_get_by_name(clp, mnt, dentry);
	}
	read_unlock(&dparent_lock);
	return exp;
}

/*
 * Hashtable locking. Write locks are placed only by user processes
 * wanting to modify export information.
 * Write locking only done in this file.  Read locking
 * needed externally.
 */

static DECLARE_RWSEM(hash_sem);

void
exp_readlock(void)
{
	down_read(&hash_sem);
}

static inline void
exp_writelock(void)
{
	down_write(&hash_sem);
}

void
exp_readunlock(void)
{
	up_read(&hash_sem);
}

static inline void
exp_writeunlock(void)
{
	up_write(&hash_sem);
}

static void exp_fsid_unhash(struct svc_export *exp)
{
	struct svc_expkey *ek;

	if ((exp->ex_flags & NFSEXP_FSID) == 0)
		return;

	ek = exp_get_fsid_key(exp->ex_client, exp->ex_fsid);
	if (ek) {
		list_del(&ek->ek_hash);
		kfree(ek);
	}
}

static int exp_fsid_hash(svc_client *clp, struct svc_export *exp)
{
	struct list_head *head;
	struct svc_expkey *ek;
 
	if ((exp->ex_flags & NFSEXP_FSID) == 0)
		return 0;

	ek = kmalloc(sizeof(*ek), GFP_KERNEL);
	if (ek == NULL)
		return -ENOMEM;

	ek->ek_fsidtype = 1;
	ek->ek_export = exp;
	ek->ek_client = clp;

	mk_fsid_v1(ek->ek_fsid, exp->ex_fsid);
	
	head = &expkey_table[expkey_hash(clp, 1, ek->ek_fsid)];
	list_add(&ek->ek_hash, head);
	return 0;
}

static int exp_hash(struct auth_domain *clp, struct svc_export *exp)
{
	struct list_head *head;
	struct svc_expkey *ek;
	struct inode *inode;
 
	ek = kmalloc(sizeof(*ek), GFP_KERNEL);
	if (ek == NULL)
		return -ENOMEM;

	ek->ek_fsidtype = 0;
	ek->ek_export = exp;
	ek->ek_client = clp;

	inode = exp->ex_dentry->d_inode;
	mk_fsid_v0(ek->ek_fsid, inode->i_sb->s_dev, inode->i_ino);
	
	head = &expkey_table[expkey_hash(clp, 0, ek->ek_fsid)];
	list_add(&ek->ek_hash, head);
	return 0;
}

static void exp_unhash(struct svc_export *exp)
{
	struct svc_expkey *ek;
	struct inode *inode = exp->ex_dentry->d_inode;

	ek = exp_get_key(exp->ex_client, inode->i_sb->s_dev, inode->i_ino);
	if (ek) {
		list_del(&ek->ek_hash);
		kfree(ek);
	}
}
	
extern struct dentry *
find_exported_dentry(struct super_block *sb, void *obj, void *parent,
		     int (*acceptable)(void *context, struct dentry *de),
		     void *context);
/*
 * Export a file system.
 */
int
exp_export(struct nfsctl_export *nxp)
{
	svc_client	*clp;
	svc_export	*exp = NULL;
	svc_export	*fsid_exp;
	struct nameidata nd;
	struct inode	*inode = NULL;
	int		err;

	/* Consistency check */
	err = -EINVAL;
	if (!exp_verify_string(nxp->ex_path, NFS_MAXPATHLEN) ||
	    !exp_verify_string(nxp->ex_client, NFSCLNT_IDMAX))
		goto out;

	dprintk("exp_export called for %s:%s (%x/%ld fl %x).\n",
			nxp->ex_client, nxp->ex_path,
			nxp->ex_dev, (long) nxp->ex_ino, nxp->ex_flags);

	/* Try to lock the export table for update */
	exp_writelock();

	/* Look up client info */
	if (!(clp = auth_domain_find(nxp->ex_client)))
		goto out_unlock;


	/* Look up the dentry */
	err = path_lookup(nxp->ex_path, 0, &nd);
	if (err)
		goto out_unlock;
	inode = nd.dentry->d_inode;
	err = -EINVAL;

	exp = exp_get_by_name(clp, nd.mnt, nd.dentry);

	/* must make sure there wont be an ex_fsid clash */
	if ((nxp->ex_flags & NFSEXP_FSID) &&
	    (fsid_exp = exp_get_fsid(clp, nxp->ex_dev)) &&
	    fsid_exp != exp)
		goto finish;

	if (exp != NULL) {
		/* just a flags/id/fsid update */

		exp_fsid_unhash(exp);
		exp->ex_flags    = nxp->ex_flags;
		exp->ex_anon_uid = nxp->ex_anon_uid;
		exp->ex_anon_gid = nxp->ex_anon_gid;
		exp->ex_fsid     = nxp->ex_dev;

		err = exp_fsid_hash(clp, exp);
		goto finish;
	}

	/* We currently export only dirs and regular files.
	 * This is what umountd does.
	 */
	err = -ENOTDIR;
	if (!S_ISDIR(inode->i_mode) && !S_ISREG(inode->i_mode))
		goto finish;

	err = -EINVAL;
	/* There are two requirements on a filesystem to be exportable.
	 * 1:  We must be able to identify the filesystem from a number.
	 *       either a device number (so FS_REQUIRES_DEV needed)
	 *       or an FSID number (so NFSEXP_FSID needed).
	 * 2:  We must be able to find an inode from a filehandle.
	 *       This means that s_export_op must be set.
	 */
	if (!(inode->i_sb->s_type->fs_flags & FS_REQUIRES_DEV)) {
		if (!(nxp->ex_flags & NFSEXP_FSID)) {
			dprintk("exp_export: export of non-dev fs without fsid");
			goto finish;
		}
	}
	if (!inode->i_sb->s_export_op) {
		dprintk("exp_export: export of invalid fs type.\n");
		goto finish;
	}

	/* Ok, we can export it */;
	if (!inode->i_sb->s_export_op->find_exported_dentry)
		inode->i_sb->s_export_op->find_exported_dentry =
			find_exported_dentry;

	err = -ENOMEM;
	if (!(exp = kmalloc(sizeof(*exp), GFP_USER)))
		goto finish;
	dprintk("nfsd: created export entry %p for client %p\n", exp, clp);

	exp->ex_client = clp;
	cache_get(&clp->h);
	exp->ex_mnt = mntget(nd.mnt);
	exp->ex_dentry = dget(nd.dentry);
	exp->ex_flags = nxp->ex_flags;
	exp->ex_anon_uid = nxp->ex_anon_uid;
	exp->ex_anon_gid = nxp->ex_anon_gid;
	exp->ex_fsid = nxp->ex_dev;

	list_add_tail(&exp->ex_hash,
		      &export_table[export_hash(clp, nd.dentry)]);
	err = 0;

	if (exp_hash(clp, exp) ||
	    exp_fsid_hash(clp, exp)) {
		/* failed to create at least one index */
		exp_do_unexport(exp);
		err = -ENOMEM;
	}

finish:
	if (clp)
		auth_domain_put(clp);
	path_release(&nd);
out_unlock:
	exp_writeunlock();
out:
	return err;
}

/*
 * Unexport a file system. The export entry has already
 * been removed from the client's list of exported fs's.
 */
static void
exp_do_unexport(svc_export *unexp)
{
	struct dentry	*dentry;
	struct vfsmount *mnt;

	list_del(&unexp->ex_hash);
	exp_unhash(unexp);
	exp_fsid_unhash(unexp);
	dentry = unexp->ex_dentry;
	mnt = unexp->ex_mnt;
	dput(dentry);
	mntput(mnt);

	kfree(unexp);
}

/*
 * Revoke all exports for a given client.
 */
static void
exp_unexport_all(svc_client *clp)
{
	struct list_head *lp, *tmp;
	int index;

	dprintk("unexporting all fs's for clnt %p\n", clp);

	for (index=0; index<EXPORT_HASHMAX; index++)
		list_for_each_safe(lp, tmp, &export_table[index]) {
			svc_export *exp = list_entry(lp, struct svc_export, ex_hash);
			if (exp->ex_client == clp)
				exp_do_unexport(exp);
		}
}

/*
 * unexport syscall.
 */
int
exp_unexport(struct nfsctl_export *nxp)
{
	struct auth_domain *dom;
	int		err;

	/* Consistency check */
	if (!exp_verify_string(nxp->ex_client, NFSCLNT_IDMAX))
		return -EINVAL;

	exp_writelock();

	err = -EINVAL;
	dom = auth_domain_find(nxp->ex_client);

	if (dom) {
		svc_export *exp = exp_get(dom, nxp->ex_dev, nxp->ex_ino);
		if (exp) {
			exp_do_unexport(exp);
			err = 0;
		} else
			dprintk("nfsd: no export %x/%lx for %s\n",
				nxp->ex_dev, nxp->ex_ino, nxp->ex_client);
		auth_domain_put(dom);
	} else
		dprintk("nfsd: unexport couldn't find %s\n", nxp->ex_client);

	exp_writeunlock();
	return err;
}

/*
 * Obtain the root fh on behalf of a client.
 * This could be done in user space, but I feel that it adds some safety
 * since its harder to fool a kernel module than a user space program.
 */
int
exp_rootfh(svc_client *clp, char *path, struct knfsd_fh *f, int maxsize)
{
	struct svc_export	*exp;
	struct nameidata	nd;
	struct inode		*inode;
	struct svc_fh		fh;
	int			err;

	err = -EPERM;
	/* NB: we probably ought to check that it's NUL-terminated */
	if (path_lookup(path, 0, &nd)) {
		printk("nfsd: exp_rootfh path not found %s", path);
		return err;
	}
	inode = nd.dentry->d_inode;

	dprintk("nfsd: exp_rootfh(%s [%p] %s:%s/%ld)\n",
		 path, nd.dentry, clp->name,
		 inode->i_sb->s_id, inode->i_ino);
	exp = exp_parent(clp, nd.mnt, nd.dentry);
	if (!exp) {
		dprintk("nfsd: exp_rootfh export not found.\n");
		goto out;
	}

	/*
	 * fh must be initialized before calling fh_compose
	 */
	fh_init(&fh, maxsize);
	if (fh_compose(&fh, exp, dget(nd.dentry), NULL))
		err = -EINVAL;
	else
		err = 0;
	memcpy(f, &fh.fh_handle, sizeof(struct knfsd_fh));
	fh_put(&fh);

out:
	path_release(&nd);
	return err;
}

/*
 * Called when we need the filehandle for the root of the pseudofs,
 * for a given NFSv4 client.   The root is defined to be the
 * export point with fsid==0
 */
int
exp_pseudoroot(struct auth_domain *clp, struct svc_fh *fhp)
{
	struct svc_export *exp;

	exp = exp_get_fsid(clp, 0);
	if (!exp)
		return nfserr_perm;

	dget(exp->ex_dentry);
	return fh_compose(fhp, exp, exp->ex_dentry, NULL);
}

/* Iterator */

static void *e_start(struct seq_file *m, loff_t *pos)
{
	loff_t n = *pos;
	unsigned hash, export;
	svc_export  *exp;
	
	exp_readlock();
	if (!n--)
		return (void *)1;
	hash = n >> 32;
	export = n & ((1LL<<32) - 1);

	list_for_each_entry(exp, &export_table[hash], ex_hash)
		if (!export--)
			return exp;
	n &= ~((1LL<<32) - 1);
	do {
		hash++;
		n += 1LL<<32;
	} while(hash < EXPORT_HASHMAX && list_empty(&export_table[hash]));
	if (hash >= EXPORT_HASHMAX)
		return NULL;
	*pos = n+1;
	return list_entry(export_table[hash].next, svc_export, ex_hash);
}

static void *e_next(struct seq_file *m, void *p, loff_t *pos)
{
	svc_export *exp = p;
	int hash = (*pos >> 32);

	if (p == (void *)1)
		hash = 0;
	else if (exp->ex_hash.next == &export_table[hash]) {
		hash++;
		*pos += 1LL<<32;
	} else {
		++*pos;
		return list_entry(exp->ex_hash.next, svc_export, ex_hash);
	}
	*pos &= ~((1LL<<32) - 1);
	while (hash < EXPORT_HASHMAX && list_empty(&export_table[hash])) {
		hash++;
		*pos += 1LL<<32;
	}
	if (hash >= EXPORT_HASHMAX)
		return NULL;
	++*pos;
	return list_entry(export_table[hash].next, svc_export, ex_hash);
}

static void e_stop(struct seq_file *m, void *p)
{
	exp_readunlock();
}

struct flags {
	int flag;
	char *name[2];
} expflags[] = {
	{ NFSEXP_READONLY, {"ro", "rw"}},
	{ NFSEXP_INSECURE_PORT, {"insecure", ""}},
	{ NFSEXP_ROOTSQUASH, {"root_squash", "no_root_squash"}},
	{ NFSEXP_ALLSQUASH, {"all_squash", ""}},
	{ NFSEXP_ASYNC, {"async", "sync"}},
	{ NFSEXP_GATHERED_WRITES, {"wdelay", "no_wdelay"}},
	{ NFSEXP_UIDMAP, {"uidmap", ""}},
	{ NFSEXP_KERBEROS, { "kerberos", ""}},
	{ NFSEXP_SUNSECURE, { "sunsecure", ""}},
	{ NFSEXP_CROSSMNT, {"nohide", ""}},
	{ NFSEXP_NOSUBTREECHECK, {"no_subtree_check", ""}},
	{ NFSEXP_NOAUTHNLM, {"insecure_locks", ""}},
#ifdef MSNFS
	{ NFSEXP_MSNFS, {"msnfs", ""}},
#endif
	{ 0, {"", ""}}
};

static void exp_flags(struct seq_file *m, int flag, int fsid, uid_t anonu, uid_t anong)
{
	int first = 0;
	struct flags *flg;

	for (flg = expflags; flg->flag; flg++) {
		int state = (flg->flag & flag)?0:1;
		if (*flg->name[state])
			seq_printf(m, "%s%s", first++?",":"", flg->name[state]);
	}
	if (flag & NFSEXP_FSID)
		seq_printf(m, "%sfsid=%d", first++?",":"", fsid);
	if (anonu != (uid_t)-2 && anonu != (0x10000-2))
		seq_printf(m, "%sanonuid=%d", first++?",":"", anonu);
	if (anong != (gid_t)-2 && anong != (0x10000-2))
		seq_printf(m, "%sanongid=%d", first++?",":"", anong);
}

static inline void mangle(struct seq_file *m, const char *s)
{
	seq_escape(m, s, " \t\n\\");
}

static int e_show(struct seq_file *m, void *p)
{
	struct svc_export *exp = p;
	svc_client *clp;
	char *pbuf;

	if (p == (void *)1) {
		seq_puts(m, "# Version 1.1\n");
		seq_puts(m, "# Path Client(Flags) # IPs\n");
		return 0;
	}

	clp = exp->ex_client;

	pbuf = m->private;
	mangle(m, d_path(exp->ex_dentry, exp->ex_mnt,
			 pbuf, PAGE_SIZE));

	seq_putc(m, '\t');
	mangle(m, clp->name);
	seq_putc(m, '(');
	exp_flags(m, exp->ex_flags, exp->ex_fsid, 
		  exp->ex_anon_uid, exp->ex_anon_gid);
	seq_puts(m, ")\n");
	return 0;
}

struct seq_operations nfs_exports_op = {
	.start	= e_start,
	.next	= e_next,
	.stop	= e_stop,
	.show	= e_show,
};

/*
 * Add or modify a client.
 * Change requests may involve the list of host addresses. The list of
 * exports and possibly existing uid maps are left untouched.
 */
int
exp_addclient(struct nfsctl_client *ncp)
{
	struct auth_domain	*dom;
	int			i, err;

	/* First, consistency check. */
	err = -EINVAL;
	if (! exp_verify_string(ncp->cl_ident, NFSCLNT_IDMAX))
		goto out;
	if (ncp->cl_naddr > NFSCLNT_ADDRMAX)
		goto out;

	/* Lock the hashtable */
	exp_writelock();

	dom = unix_domain_find(ncp->cl_ident);

	err = -ENOMEM;
	if (!dom)
		goto out_unlock;

	/* Insert client into hashtable. */
	for (i = 0; i < ncp->cl_naddr; i++)
		auth_unix_add_addr(ncp->cl_addrlist[i], dom);

	auth_unix_forget_old(dom);
	auth_domain_put(dom);

	err = 0;

out_unlock:
	exp_writeunlock();
out:
	return err;
}

/*
 * Delete a client given an identifier.
 */
int
exp_delclient(struct nfsctl_client *ncp)
{
	int		err;
	struct auth_domain *dom;

	err = -EINVAL;
	if (!exp_verify_string(ncp->cl_ident, NFSCLNT_IDMAX))
		goto out;

	/* Lock the hashtable */
	exp_writelock();

	dom = auth_domain_find(ncp->cl_ident);
	/* just make sure that no addresses work 
	 * and that it will expire soon 
	 */
	if (dom) {
		err = auth_unix_forget_old(dom);
		dom->h.expiry_time = CURRENT_TIME;
		auth_domain_put(dom);
	}

	exp_writeunlock();
out:
	return err;
}

/*
 * Verify that string is non-empty and does not exceed max length.
 */
static int
exp_verify_string(char *cp, int max)
{
	int	i;

	for (i = 0; i < max; i++)
		if (!cp[i])
			return i;
	cp[i] = 0;
	printk(KERN_NOTICE "nfsd: couldn't validate string %s\n", cp);
	return 0;
}

/*
 * Initialize the exports module.
 */
void
nfsd_export_init(void)
{
	int		i;

	dprintk("nfsd: initializing export module.\n");

	for (i = 0; i < EXPORT_HASHMAX ; i++)
		INIT_LIST_HEAD(&export_table[i]);

	for (i = 0; i < EXPKEY_HASHMAX; i++)
		INIT_LIST_HEAD(&expkey_table[i]);

}

/*
 * Shutdown the exports module.
 */
void
nfsd_export_shutdown(void)
{

	dprintk("nfsd: shutting down export module.\n");

	exp_writelock();

	exp_unexport_all(NULL);
	svcauth_unix_purge();

	exp_writeunlock();
	dprintk("nfsd: export shutdown complete.\n");
}
