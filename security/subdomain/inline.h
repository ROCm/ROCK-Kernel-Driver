#ifndef __INLINE_H
#define __INLINE_H

#include <linux/version.h>
#include <net/sock.h>
#include <linux/list.h>
#include <linux/namespace.h>

static __INLINE__ int __sd_is_confined(struct subdomain *sd)
{
int rc=0;

	if (sd && sd->sd_magic == SD_ID_MAGIC && sd->profile){
		BUG_ON(!sd->active);
		rc=1;
	}

	return rc;
}

static __INLINE__ int __sd_is_ndconfined(struct subdomain *sd)
{
	return __sd_is_confined(sd) && 
		sd->profile->num_net_entries;
}

/**
 *  sd_is_confined - is process confined
 *  @sd: subdomain
 *
 *  Check if @sd contains a valid profile.
 *  Return 1 if confined, 0 otherwise.
 */
static __INLINE__ int sd_is_confined(void)
{
	struct subdomain *sd = SD_SUBDOMAIN(current->security);
	return __sd_is_confined(sd);
}

/**
 * sd_sub_defined - check if there is at least one subprofile defined
 *
 * Return 1 if there is at least one SubDomain subprofile defined,
 * 0 otherwise.
 *Used to obtain 
 */
static __INLINE__ int __sd_sub_defined(struct subdomain *sd)
{
	 if (__sd_is_confined(sd) && !list_empty(&sd->profile->sub))
		 return 1;

	 return 0;
}
static __INLINE__ int sd_sub_defined(void)
{
	struct subdomain *sd = SD_SUBDOMAIN(current->security);
	return __sd_sub_defined(sd);
}

static __INLINE__ struct sdprofile * get_sdprofile(struct sdprofile *p)
{
	if (p)
		atomic_inc(&p->count);
	return p;
}

static __INLINE__ void put_sdprofile(struct sdprofile *p)
{
	if (p) 
		if (atomic_dec_and_test(&p->count))
				free_sdprofile(p);
}

/* simple struct subdomain alloc/free wrappers */
static __INLINE__ struct subdomain * alloc_subdomain(struct task_struct *tsk)
{
	struct subdomain *sd = kmalloc(sizeof(struct subdomain), GFP_KERNEL);
	/* zero it first */
	if (sd) {
		memset(sd, 0, sizeof(struct subdomain));
		sd->sd_magic = SD_ID_MAGIC;
	}

	/* back pointer to task */
	sd->task = tsk;

	/* any readers of the list must make sure that they can handle 
	 * case where sd->profile and sd->active are not yet set (null)
	 */
	sd_subdomainlist_add(sd);

	return sd;
}
static __INLINE__ void free_subdomain(struct subdomain *sd)
{
	sd_subdomainlist_remove(sd);
	kfree(sd);
}

/**
 * alloc_sdprofile - allocate new empty profile
 *
 * This routine allocates, initializes, and returns a new zeored
 * profile structure. Returns NULL on failure.
 */
static __INLINE__ struct sdprofile * alloc_sdprofile(void) 
{
	struct sdprofile *profile;

	profile = (struct sdprofile *) kmalloc(sizeof(struct sdprofile),
						     GFP_KERNEL);
	SD_DEBUG("%s(%p)\n", __FUNCTION__, profile);
	if (profile) {
		memset(profile, 0, sizeof(struct sdprofile));
		INIT_LIST_HEAD(&profile->list);
		INIT_LIST_HEAD(&profile->sub);
	}
	return profile;
}

/**
 * sd_put_name - release name (really just free_page)
 * @name: name to release.
 */
static __INLINE__ void sd_put_name(char *name)
{
	free_page((unsigned long)name);
}

static __INLINE__ struct sdprofile * __sd_find_profile(const char *name, struct list_head *head)
{
	struct list_head *lh;

	if (!name || !head)
		return NULL;

	SD_DEBUG("%s: finding profile %s\n", __FUNCTION__, name);
	list_for_each(lh, head) {
		struct sdprofile *p = list_entry(lh, struct sdprofile, list);
		if (!strcmp(p->name, name)) {
			/* return refcounted object */
			p = get_sdprofile(p);
			return p;
		}else{
			SD_DEBUG("%s: skipping %s\n", __FUNCTION__, p->name);
		}
	}
	return NULL;
}

static __INLINE__ struct subdomain * __get_sdcopy(struct subdomain *new,
				 	      struct task_struct *tsk)
{
	struct subdomain *old, 
			 *temp = NULL;

       	old = SD_SUBDOMAIN(tsk->security);

	if (old){
		new->sd_magic = old->sd_magic;
		new->sd_hat_magic = old->sd_hat_magic;

		new->active = get_sdprofile(old->active);

		if (old->profile == old->active){
			new->profile = new->active;
		}else{
			new->profile = get_sdprofile(old->profile);
		}

		temp=new;
	}

	return temp;
}

static __INLINE__ struct subdomain *get_sdcopy(struct subdomain *new)
{
	struct subdomain *temp;

	SD_RLOCK;

	temp=__get_sdcopy(new, current);

	SD_RUNLOCK;

	return temp;
}

static __INLINE__ void put_sdcopy(struct subdomain *temp)
{
	if (temp){
		put_sdprofile(temp->active);
		if (temp->active != temp->profile){
			(void)put_sdprofile(temp->profile);
		}
	}
}

/* sd_path_begin2
 * Setup data for iterating over paths to dentry (sd_path_getname)
 * @rdentry is used to obtain the filesystem root dentry
 * @dentry is the actual dentry object we want to obtain pathnames to.
 */
static __INLINE__ void sd_path_begin2(struct dentry *rdentry, struct dentry *dentry, struct sd_path_data *data)
{
	data->dentry = dentry;
	data->root = dget(rdentry->d_sb->s_root);
	data->namespace = current->namespace;
	data->head = &data->namespace->list; 
	data->pos = data->head->next; prefetch(data->pos->next);
	data->errno = 0;

	down_read(&data->namespace->sem);
}

/* sd_path_begin
 * Setup data for iterating over paths to dentry (sd_path_getname)
 * @dentry is used both for obtaining the filesystem root 
 *  and also for the actual dentry object we want to obtain pathnames to.
 */
static __INLINE__ void sd_path_begin(struct dentry *dentry, struct sd_path_data *data)
{
	sd_path_begin2(dentry, dentry, data);
}

/* sd_path_getname
 * Return the next pathname that dentry (from sd_path_begin) may be reached
 * through.  If no more paths exists or in the case of error, NULL is returned.
 */
static __INLINE__ char *sd_path_getname(struct sd_path_data *data)
{
char *name = NULL;
struct vfsmount *mnt;

	while (data->pos != data->head){
		mnt = list_entry(data->pos, struct vfsmount, mnt_list);

		/* advance to next -- so that it is done before we break */
		data->pos = data->pos->next; prefetch(data->pos->next);
		
		if (mnt->mnt_root == data->root){
			name=__sd_get_name(data->dentry, mnt);
			if (!name){
				data->errno = -ENOMEM;
			}
			break;
		}
	}

	return name;
}

/* sd_path_getmnt
 * Return the next mountpoint which has the same root dentry as was passed
 * to sd_path_begin2. If no more mount points exist, NULL is returned.
 */
static __INLINE__ struct vfsmount *sd_path_getmnt(struct sd_path_data *data)
{
struct vfsmount *mnt = NULL;

	while (data->pos != data->head){
		mnt = list_entry(data->pos, struct vfsmount, mnt_list);

		/* advance to next -- so that it is done before we break */
		data->pos = data->pos->next; prefetch(data->pos->next);
 
		if (mnt->mnt_root == data->root){
			mntget(mnt);
			break;
		}
	}

	return mnt;
} 

/* sd_path_end
 * end iterating over the namespace
 * release all dentries and semaphores that were allocated by sd_path_begin
 * If an error occured in a previous sd_path_getmnt it is returned.
 * Otherwise 0 is returned
 */
static __INLINE__ int sd_path_end(struct sd_path_data *data)
{
	up_read(&data->namespace->sem);
	dput(data->root);

	return data->errno;
}

static __INLINE__ int isblank(unsigned char c)
{
	return c == ' ' || c == '\t';
}

#endif  /* __INLINE_H__ */
