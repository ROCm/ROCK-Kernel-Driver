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
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/hash.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/nfsfh.h>
#include <linux/nfsd/syscall.h>
#include <linux/lockd/bind.h>

#define NFSDDBG_FACILITY	NFSDDBG_EXPORT
#define NFSD_PARANOIA 1

typedef struct auth_domain	svc_client;
typedef struct svc_export	svc_export;

static void		exp_do_unexport(svc_export *unexp);
static int		exp_verify_string(char *cp, int max);

/*
 * We have two caches.
 * One maps client+vfsmnt+dentry to export options - the export map
 * The other maps client+filehandle-fragment to export options. - the expkey map
 *
 * The export options are actually stored in the first map, and the
 * second map contains a reference to the entry in the first map.
 */

#define	EXPKEY_HASHBITS		8
#define	EXPKEY_HASHMAX		(1 << EXPKEY_HASHBITS)
#define	EXPKEY_HASHMASK		(EXPKEY_HASHMAX -1)
static struct cache_head *expkey_table[EXPKEY_HASHMAX];

static inline int svc_expkey_hash(struct svc_expkey *item)
{
	int hash = item->ek_fsidtype;
	char * cp = (char*)item->ek_fsid;
	int len = (item->ek_fsidtype==0)?8:4;

	hash ^= hash_mem(cp, len, EXPKEY_HASHBITS);
	hash ^= hash_ptr(item->ek_client, EXPKEY_HASHBITS);
	return hash & EXPKEY_HASHMASK;
}

void expkey_put(struct cache_head *item, struct cache_detail *cd)
{
	if (cache_put(item, cd)) {
		struct svc_expkey *key = container_of(item, struct svc_expkey, h);
		if (test_bit(CACHE_VALID, &item->flags) &&
		    !test_bit(CACHE_NEGATIVE, &item->flags))
			exp_put(key->ek_export);
		auth_domain_put(key->ek_client);
		kfree(key);
	}
}

void expkey_request(struct cache_detail *cd,
		    struct cache_head *h,
		    char **bpp, int *blen)
{
	/* client fsidtype \xfsid */
	struct svc_expkey *ek = container_of(h, struct svc_expkey, h);
	char type[5];

	qword_add(bpp, blen, ek->ek_client->name);
	snprintf(type, 5, "%d", ek->ek_fsidtype);
	qword_add(bpp, blen, type);
	qword_addhex(bpp, blen, (char*)ek->ek_fsid, ek->ek_fsidtype==0?8:4);
	(*bpp)[-1] = '\n';
}

static struct svc_expkey *svc_expkey_lookup(struct svc_expkey *, int);
int expkey_parse(struct cache_detail *cd, char *mesg, int mlen)
{
	/* client fsidtype fsid [path] */
	char *buf;
	int len;
	struct auth_domain *dom = NULL;
	int err;
	int fsidtype;
	char *ep;
	struct svc_expkey key;

	if (mesg[mlen-1] != '\n')
		return -EINVAL;
	mesg[mlen-1] = 0;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	err = -ENOMEM;
	if (!buf) goto out;

	err = -EINVAL;
	if ((len=qword_get(&mesg, buf, PAGE_SIZE)) <= 0)
		goto out;

	err = -ENOENT;
	dom = auth_domain_find(buf);
	if (!dom)
		goto out;
	dprintk("found domain %s\n", buf);

	err = -EINVAL;
	if ((len=qword_get(&mesg, buf, PAGE_SIZE)) <= 0)
		goto out;
	fsidtype = simple_strtoul(buf, &ep, 10);
	if (*ep)
		goto out;
	dprintk("found fsidtype %d\n", fsidtype);
	if (fsidtype > 1)
		goto out;
	if ((len=qword_get(&mesg, buf, PAGE_SIZE)) <= 0)
		goto out;
	dprintk("found fsid length %d\n", len);
	if (len != ((fsidtype==0)?8:4))
		goto out;

	/* OK, we seem to have a valid key */
	key.h.flags = 0;
	key.h.expiry_time = get_expiry(&mesg);
	if (key.h.expiry_time == 0)
		goto out;

	key.ek_client = dom;	
	key.ek_fsidtype = fsidtype;
	memcpy(key.ek_fsid, buf, len);

	/* now we want a pathname, or empty meaning NEGATIVE  */
	if ((len=qword_get(&mesg, buf, PAGE_SIZE)) < 0)
		goto out;
	dprintk("Path seems to be <%s>\n", buf);
	err = 0;
	if (len == 0)
		set_bit(CACHE_NEGATIVE, &key.h.flags);
	else {
		struct nameidata nd;
		struct svc_expkey *ek;
		struct svc_export *exp;
		err = path_lookup(buf, 0, &nd);
		if (err)
			goto out;

		dprintk("Found the path %s\n", buf);
		exp = exp_get_by_name(dom, nd.mnt, nd.dentry, NULL);

		err = -ENOENT;
		if (!exp)
			goto out_nd;
		key.ek_export = exp;
		dprintk("And found export\n");
		
		ek = svc_expkey_lookup(&key, 2);
		if (ek)
			expkey_put(&ek->h, &svc_expkey_cache);
		svc_export_put(&exp->h, &svc_export_cache);
		err = 0;
	out_nd:
		path_release(&nd);
	}

 out:
	if (dom)
		auth_domain_put(dom);
	if (buf)
		kfree(buf);
	return err;
}

struct cache_detail svc_expkey_cache = {
	.hash_size	= EXPKEY_HASHMAX,
	.hash_table	= expkey_table,
	.name		= "nfsd.fh",
	.cache_put	= expkey_put,
	.cache_request	= expkey_request,
	.cache_parse	= expkey_parse,
};

static inline int svc_expkey_match (struct svc_expkey *a, struct svc_expkey *b)
{
	if (a->ek_fsidtype != b->ek_fsidtype ||
	    a->ek_client != b->ek_client)
		return 0;
	if (a->ek_fsid[0] != b->ek_fsid[0])
		return 0;
	if (a->ek_fsidtype == 1)
		return 1;
	if (a->ek_fsid[1] != b->ek_fsid[1])
		return 0;
	return 1;
}

static inline void svc_expkey_init(struct svc_expkey *new, struct svc_expkey *item)
{
	cache_get(&item->ek_client->h);
	new->ek_client = item->ek_client;
	new->ek_fsidtype = item->ek_fsidtype;
	new->ek_fsid[0] = item->ek_fsid[0];
	new->ek_fsid[1] = item->ek_fsid[1];
}

static inline void svc_expkey_update(struct svc_expkey *new, struct svc_expkey *item)
{
	cache_get(&item->ek_export->h);
	new->ek_export = item->ek_export;
}

static DefineSimpleCacheLookup(svc_expkey)

#define	EXPORT_HASHBITS		8
#define	EXPORT_HASHMAX		(1<< EXPORT_HASHBITS)
#define	EXPORT_HASHMASK		(EXPORT_HASHMAX -1)

static struct cache_head *export_table[EXPORT_HASHMAX];

static inline int svc_export_hash(struct svc_export *item)
{
	int rv;

	rv = hash_ptr(item->ex_client, EXPORT_HASHBITS);
	rv ^= hash_ptr(item->ex_dentry, EXPORT_HASHBITS);
	rv ^= hash_ptr(item->ex_mnt, EXPORT_HASHBITS);
	return rv;
}

void svc_export_put(struct cache_head *item, struct cache_detail *cd)
{
	if (cache_put(item, cd)) {
		struct svc_export *exp = container_of(item, struct svc_export, h);
		dput(exp->ex_dentry);
		mntput(exp->ex_mnt);
		auth_domain_put(exp->ex_client);
		kfree(exp);
	}
}

void svc_export_request(struct cache_detail *cd,
			struct cache_head *h,
			char **bpp, int *blen)
{
	/*  client path */
	struct svc_export *exp = container_of(h, struct svc_export, h);
	char *pth;

	qword_add(bpp, blen, exp->ex_client->name);
	pth = d_path(exp->ex_dentry, exp->ex_mnt, *bpp, *blen);
	qword_add(bpp, blen, pth);
	(*bpp)[-1] = '\n';
}

static struct svc_export *svc_export_lookup(struct svc_export *, int);
int svc_export_parse(struct cache_detail *cd, char *mesg, int mlen)
{
	/* client path expiry [flags anonuid anongid fsid] */
	char *buf;
	int len;
	int err;
	struct auth_domain *dom = NULL;
	struct nameidata nd;
	struct svc_export exp, *expp;
	int an_int;

	nd.dentry = NULL;

	if (mesg[mlen-1] != '\n')
		return -EINVAL;
	mesg[mlen-1] = 0;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	err = -ENOMEM;
	if (!buf) goto out;

	/* client */
	len = qword_get(&mesg, buf, PAGE_SIZE);
	err = -EINVAL;
	if (len <= 0) goto out;

	err = -ENOENT;
	dom = auth_domain_find(buf);
	if (!dom)
		goto out;

	/* path */
	err = -EINVAL;
	if ((len=qword_get(&mesg, buf, PAGE_SIZE)) <= 0)
		goto out;
	err = path_lookup(buf, 0, &nd);
	if (err) goto out;

	exp.h.flags = 0;
	exp.ex_client = dom;
	exp.ex_mnt = nd.mnt;
	exp.ex_dentry = nd.dentry;

	/* expiry */
	err = -EINVAL;
	exp.h.expiry_time = get_expiry(&mesg);
	if (exp.h.expiry_time == 0)
		goto out;

	/* flags */
	err = get_int(&mesg, &an_int);
	if (err == -ENOENT)
		set_bit(CACHE_NEGATIVE, &exp.h.flags);
	else {
		if (err || an_int < 0) goto out;	
		exp.ex_flags= an_int;
	
		/* anon uid */
		err = get_int(&mesg, &an_int);
		if (err) goto out;
		exp.ex_anon_uid= an_int;

		/* anon gid */
		err = get_int(&mesg, &an_int);
		if (err) goto out;
		exp.ex_anon_gid= an_int;

		/* fsid */
		err = get_int(&mesg, &an_int);
		if (err) goto out;
		exp.ex_fsid = an_int;
	}

	expp = svc_export_lookup(&exp, 1);
	if (expp)
		exp_put(expp);
	err = 0;

 out:
	if (nd.dentry)
		path_release(&nd);
	if (dom)
		auth_domain_put(dom);
	if (buf)
		kfree(buf);
	return err;
}

struct cache_detail svc_export_cache = {
	.hash_size	= EXPORT_HASHMAX,
	.hash_table	= export_table,
	.name		= "nfsd.export",
	.cache_put	= svc_export_put,
	.cache_request	= svc_export_request,
	.cache_parse	= svc_export_parse,
};

static inline int svc_export_match(struct svc_export *a, struct svc_export *b)
{
	return a->ex_client == b->ex_client &&
		a->ex_dentry == b->ex_dentry &&
		a->ex_mnt == b->ex_mnt;
}
static inline void svc_export_init(struct svc_export *new, struct svc_export *item)
{
	cache_get(&item->ex_client->h);
	new->ex_client = item->ex_client;
	new->ex_dentry = dget(item->ex_dentry);
	new->ex_mnt = mntget(item->ex_mnt);
}

static inline void svc_export_update(struct svc_export *new, struct svc_export *item)
{
	new->ex_flags = item->ex_flags;
	new->ex_anon_uid = item->ex_anon_uid;
	new->ex_anon_gid = item->ex_anon_gid;
	new->ex_fsid = item->ex_fsid;
}

static DefineSimpleCacheLookup(svc_export)


struct svc_expkey *
exp_find_key(svc_client *clp, int fsid_type, u32 *fsidv, struct cache_req *reqp)
{
	struct svc_expkey key, *ek;
	int err;
	
	if (!clp)
		return NULL;

	key.ek_client = clp;
	key.ek_fsidtype = fsid_type;
	key.ek_fsid[0] = fsidv[0];
	key.ek_fsid[1] = fsidv[1];

	ek = svc_expkey_lookup(&key, 0);
	if (ek != NULL)
		if ((err = cache_check(&svc_expkey_cache, &ek->h, reqp)))
			ek = ERR_PTR(err);
	return ek;
}

int exp_set_key(svc_client *clp, int fsid_type, u32 *fsidv, 
		struct svc_export *exp)
{
	struct svc_expkey key, *ek;

	key.ek_client = clp;
	key.ek_fsidtype = fsid_type;
	key.ek_fsid[0] = fsidv[0];
	key.ek_fsid[1] = fsidv[1];
	key.ek_export = exp;
	key.h.expiry_time = NEVER;
	key.h.flags = 0;

	ek = svc_expkey_lookup(&key, 1);
	if (ek) {
		expkey_put(&ek->h, &svc_expkey_cache);
		return 0;
	}
	return -ENOMEM;
}

/*
 * Find the client's export entry matching xdev/xino.
 */
static inline struct svc_expkey *
exp_get_key(svc_client *clp, dev_t dev, ino_t ino)
{
	u32 fsidv[2];
	
	mk_fsid_v0(fsidv, dev, ino);
	return exp_find_key(clp, 0, fsidv, NULL);
}

/*
 * Find the client's export entry matching fsid
 */
static inline struct svc_expkey *
exp_get_fsid_key(svc_client *clp, int fsid)
{
	u32 fsidv[2];

	mk_fsid_v1(fsidv, fsid);

	return exp_find_key(clp, 1, fsidv, NULL);
}

svc_export *
exp_get_by_name(svc_client *clp, struct vfsmount *mnt, struct dentry *dentry,
		struct cache_req *reqp)
{
	struct svc_export *exp, key;
	
	if (!clp)
		return NULL;

	key.ex_client = clp;
	key.ex_mnt = mnt;
	key.ex_dentry = dentry;

	exp = svc_export_lookup(&key, 0);
	if (exp != NULL) 
		switch (cache_check(&svc_export_cache, &exp->h, reqp)) {
		case 0: break;
		case -EAGAIN:
			exp = ERR_PTR(-EAGAIN);
			break;
		default:
			exp = NULL;
		}

	return exp;
}

/*
 * Find the export entry for a given dentry.
 */
struct svc_export *
exp_parent(svc_client *clp, struct vfsmount *mnt, struct dentry *dentry,
	   struct cache_req *reqp)
{
	svc_export *exp;

	dget(dentry);
	exp = exp_get_by_name(clp, mnt, dentry, reqp);

	while (exp == NULL && dentry != dentry->d_parent) {
		struct dentry *parent;
		read_lock(&dparent_lock);
		parent = dget(dentry->d_parent);
		dput(dentry);
		dentry = parent;
		read_unlock(&dparent_lock);
		exp = exp_get_by_name(clp, mnt, dentry, reqp);
	}
	dput(dentry);
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
	if (ek && !IS_ERR(ek)) {
		ek->h.expiry_time = get_seconds()-1;
		expkey_put(&ek->h, &svc_expkey_cache);
	}
	svc_expkey_cache.nextcheck = get_seconds();
}

static int exp_fsid_hash(svc_client *clp, struct svc_export *exp)
{
	u32 fsid[2];
 
	if ((exp->ex_flags & NFSEXP_FSID) == 0)
		return 0;

	mk_fsid_v1(fsid, exp->ex_fsid);
	return exp_set_key(clp, 1, fsid, exp);
}

static int exp_hash(struct auth_domain *clp, struct svc_export *exp)
{
	u32 fsid[2];
	struct inode *inode;

	inode = exp->ex_dentry->d_inode;
	mk_fsid_v0(fsid,  inode->i_sb->s_dev, inode->i_ino);
	return exp_set_key(clp, 0, fsid, exp);
}

static void exp_unhash(struct svc_export *exp)
{
	struct svc_expkey *ek;
	struct inode *inode = exp->ex_dentry->d_inode;

	ek = exp_get_key(exp->ex_client, inode->i_sb->s_dev, inode->i_ino);
	if (ek && !IS_ERR(ek)) {
		ek->h.expiry_time = get_seconds()-1;
		expkey_put(&ek->h, &svc_expkey_cache);
	}
	svc_expkey_cache.nextcheck = get_seconds();
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
	struct svc_export	*exp = NULL;
	struct svc_export	new;
	struct svc_expkey	*fsid_key = NULL;
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
			(unsigned)nxp->ex_dev, (long)nxp->ex_ino,
			nxp->ex_flags);

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

	exp = exp_get_by_name(clp, nd.mnt, nd.dentry, NULL);

	/* must make sure there won't be an ex_fsid clash */
	if ((nxp->ex_flags & NFSEXP_FSID) &&
	    (fsid_key = exp_get_fsid_key(clp, nxp->ex_dev)) &&
	    !IS_ERR(fsid_key) &&
	    fsid_key->ek_export &&
	    fsid_key->ek_export != exp)
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

	dprintk("nfsd: creating export entry %p for client %p\n", exp, clp);

	new.h.expiry_time = NEVER;
	new.h.flags = 0;
	new.ex_client = clp;
	new.ex_mnt = nd.mnt;
	new.ex_dentry = nd.dentry;
	new.ex_flags = nxp->ex_flags;
	new.ex_anon_uid = nxp->ex_anon_uid;
	new.ex_anon_gid = nxp->ex_anon_gid;
	new.ex_fsid = nxp->ex_dev;

	exp = svc_export_lookup(&new, 1);

	if (exp == NULL)
		goto finish;

	err = 0;

	if (exp_hash(clp, exp) ||
	    exp_fsid_hash(clp, exp)) {
		/* failed to create at least one index */
		exp_do_unexport(exp);
		cache_flush();
		err = -ENOMEM;
	}

finish:
	if (exp)
		exp_put(exp);
	if (fsid_key && !IS_ERR(fsid_key))
		expkey_put(&fsid_key->h, &svc_expkey_cache);
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
	unexp->h.expiry_time = get_seconds()-1;
	svc_export_cache.nextcheck = get_seconds();
	exp_unhash(unexp);
	exp_fsid_unhash(unexp);
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
		struct svc_expkey *key = exp_get_key(dom, nxp->ex_dev, nxp->ex_ino);
		if (key && !IS_ERR(key) && key->ek_export) {
			exp_do_unexport(key->ek_export);
			err = 0;
			expkey_put(&key->h, &svc_expkey_cache);
		} else
			dprintk("nfsd: no export %x/%lx for %s\n",
				(unsigned)nxp->ex_dev,
				(unsigned long) nxp->ex_ino, nxp->ex_client);
		auth_domain_put(dom);
		cache_flush();
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
	exp = exp_parent(clp, nd.mnt, nd.dentry, NULL);
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
	struct svc_expkey *fsid_key;
	int rv;

	fsid_key = exp_get_fsid_key(clp, 0);
	if (!fsid_key || IS_ERR(fsid_key))
		return nfserr_perm;

	dget(fsid_key->ek_export->ex_dentry);
	rv = fh_compose(fhp, fsid_key->ek_export, 
			  fsid_key->ek_export->ex_dentry, NULL);
	expkey_put(&fsid_key->h, &svc_expkey_cache);
	return rv;
}

/* Iterator */

static void *e_start(struct seq_file *m, loff_t *pos)
{
	loff_t n = *pos;
	unsigned hash, export;
	struct cache_head *ch;
	
	exp_readlock();
	read_lock(&svc_export_cache.hash_lock);
	if (!n--)
		return (void *)1;
	hash = n >> 32;
	export = n & ((1LL<<32) - 1);

	
	for (ch=export_table[hash]; ch; ch=ch->next)
		if (!export--)
			return ch;
	n &= ~((1LL<<32) - 1);
	do {
		hash++;
		n += 1LL<<32;
	} while(hash < EXPORT_HASHMAX && export_table[hash]==NULL);
	if (hash >= EXPORT_HASHMAX)
		return NULL;
	*pos = n+1;
	return export_table[hash];
}

static void *e_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct cache_head *ch = p;
	int hash = (*pos >> 32);

	if (p == (void *)1)
		hash = 0;
	else if (ch->next == NULL) {
		hash++;
		*pos += 1LL<<32;
	} else {
		++*pos;
		return ch->next;
	}
	*pos &= ~((1LL<<32) - 1);
	while (hash < EXPORT_HASHMAX && export_table[hash] == NULL) {
		hash++;
		*pos += 1LL<<32;
	}
	if (hash >= EXPORT_HASHMAX)
		return NULL;
	++*pos;
	return export_table[hash];
}

static void e_stop(struct seq_file *m, void *p)
{
	read_unlock(&svc_export_cache.hash_lock);
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
	{ NFSEXP_NOHIDE, {"nohide", ""}},
	{ NFSEXP_CROSSMNT, {"crossmnt", ""}},
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
	struct cache_head *cp = p;
	struct svc_export *exp = container_of(cp, struct svc_export, h);
	svc_client *clp;
	char *pbuf;

	if (p == (void *)1) {
		seq_puts(m, "# Version 1.1\n");
		seq_puts(m, "# Path Client(Flags) # IPs\n");
		return 0;
	}

	clp = exp->ex_client;
	cache_get(&exp->h);
	if (cache_check(&svc_export_cache, &exp->h, NULL))
		return 0;
	if (cache_put(&exp->h, &svc_export_cache)) BUG();
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
		dom->h.expiry_time = get_seconds();
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
	dprintk("nfsd: initializing export module.\n");

	cache_register(&svc_export_cache);
	cache_register(&svc_expkey_cache);

}

/*
 * Flush exports table - called when last nfsd thread is killed
 */
void
nfsd_export_flush(void)
{
	exp_writelock();
	cache_purge(&svc_expkey_cache);
	cache_purge(&svc_export_cache);
	exp_writeunlock();
}

/*
 * Shutdown the exports module.
 */
void
nfsd_export_shutdown(void)
{

	dprintk("nfsd: shutting down export module.\n");

	exp_writelock();

	if (cache_unregister(&svc_expkey_cache))
		printk(KERN_ERR "nfsd: failed to unregister expkey cache\n");
	if (cache_unregister(&svc_export_cache))
		printk(KERN_ERR "nfsd: failed to unregister export cache\n");
	svcauth_unix_purge();

	exp_writeunlock();
	dprintk("nfsd: export shutdown complete.\n");
}
