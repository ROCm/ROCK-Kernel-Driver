/*
 * Immunix SubDomain sysctl handling
 *
 * Original 2.2 work
 * Copyright 1998, 1999, 2000, 2001 Wirex Communications &
 *			Oregon Graduate Institute
 * 
 * 	Written by Steve Beattie <steve@wirex.net>
 *
 * Updated 2.4/5 work
 * Copyright (C) 2002 WireX Communications, Inc.
 *
 * Ported from 2.2 by Chris Wright <chris@wirex.com>
 *
 * Ported to 2.6 by Tony Jones <tony@immunix.com>
 * Copyright (C) 2003-2004 Immunix, Inc
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/if.h>
#include <linux/namespace.h>

#include <asm/uaccess.h>

#include "subdomain.h"
#include "immunix.h"

#include "inline.h"

#include "pcre_tables.h"

/* inlines must be forward of there use in newer version of gcc,
   just forward declaring with a prototype won't work anymore */

/**
 * put_name - release name obtained by get_name
 * @name: pointer to memory allocated for name by get_name
 *
 * Simple wrapper for kfree.
 */
static __INLINE__ void put_name(char *name)
{
	if (name){
		kfree(name);
	}
}

/**
 * put_pattern - release name obtained by get_pattern
 * @name: pointer to memory allocated for name by get_name
 *
 * Simple wrapper for kfree.
 */
static __INLINE__ void put_pattern(pcre *pattern)
{
	if (pattern){
		kfree(pattern);
	}
}

static __INLINE__ void put_sd_entry(struct sd_entry *entry)
{
	if (entry) {
		put_name (entry->filename);
		if (entry->pattern_type == ePatternRegex){
			put_name(entry->regex);
			put_pattern(entry->compiled);
		}
		kfree(entry);
	}
}

/*
 * free_sd_entry - destroy existing sd_entry 
 */
static __INLINE__ void free_sd_entry(struct sd_entry *entry)
{
	if (entry) {
		kfree(entry);
	}
}

/**
 * alloc_nd_entry - create new network entry
 *
 * Creates, zeroes and returns a new network subdomain structure.
 * Returns NULL on failure.
 */
static __INLINE__ struct nd_entry * alloc_nd_entry(void)
{
	struct nd_entry *entry;

	ND_DEBUG("%s\n", __FUNCTION__);

	entry = kmalloc(sizeof(struct nd_entry), GFP_KERNEL);
	if (entry)
		memset(entry, 0, sizeof(struct nd_entry));
	return entry;
}		

/*
 * free_nd_entry - destroy existing nd_entry 
 */
static __INLINE__ void free_nd_entry(struct nd_entry * entry)
{
	if (entry){
		kfree(entry);
	}
}

/**
 * alloc_sd_entry - create new empty sd_entry
 *
 * This routine allocates, initializes, and returns a new SubDomain
 * file entry structure.  Structure is zeroed.  Returns new structure on
 * success, NULL on failure.
 */
static __INLINE__ struct sd_entry * alloc_sd_entry(void) 
{
	struct sd_entry *entry;

	SD_DEBUG("%s\n", __FUNCTION__);
	entry = kmalloc(sizeof(struct sd_entry) , GFP_KERNEL);
	if (entry)
		memset(entry, 0, sizeof(struct sd_entry));
	return entry;
}


/**
 * count_entries - counts sd (file) entries in a profile
 * @profile: profile to count
 */
static __INLINE__ void count_entries(struct sdprofile *profile)
{
	struct sd_entry *entry;
	int i, count=0;

	SD_DEBUG("%s\n", __FUNCTION__) ;

	for (entry = profile->file_entry; entry; entry = entry->next) {

		count++;
	}

	for (i=0; i<=POS_KERN_COD_FILE_MAX; i++){
		struct sd_entry *pentry;
		int pcount=0;

		pentry=profile->file_entryp[i];
		for (; pentry ; pentry = pentry->nextp[i]){
			pcount++;
		}

		profile->num_file_pentries[i]=pcount;
		SD_DEBUG("%s index %d entries %d\n",  
			__FUNCTION__, i, pcount);
	}

	profile->num_file_entries = count;

	SD_DEBUG("%s %d total entries\n",  __FUNCTION__, count);
}

/**
 * count_net_entries - counts non globbed sd (file) entries in a profile
 * @profile: profile to count
 */
static __INLINE__ void count_net_entries(struct sdprofile *profile)
{
	struct nd_entry *entry;
	int i, count = 0;
	
	SD_DEBUG("%s\n", __FUNCTION__);

	for (entry = profile->net_entry; entry; entry = entry->next) {
		count++;
	}

	for (i=POS_KERN_COD_NET_MIN; i<=POS_KERN_COD_NET_MAX; i++){
		struct nd_entry *pentry;
		int j, pcount=0;

		j=NET_POS_TO_INDEX(i);

		pentry=profile->net_entryp[j];
		for (;pentry ; pentry = pentry->nextp[j]){
			pcount++;
		}

		profile->num_net_pentries[j]=pcount;
		SD_DEBUG("%s index %d entries %d\n",  
			__FUNCTION__, j, pcount);
	}

	profile->num_net_entries = count;

	SD_DEBUG("%s %d total entries\n",  __FUNCTION__, count);
}


/**
 * put_iface - free interface name (simple kfree() wrapper)
 * @iface: pointer to interface name
 */
static __INLINE__ void put_iface(char *iface)
{
	if (iface){
		kfree(iface);
	}
}
	

/* NULL profile
 *
 * Used when an attempt is made to changehat into a non-existant
 * subhat.   In the NULL profile,  no file access is allowed
 * (currently full network access is allowed).  Using a NULL
 * profile ensures that active is always non zero.  
 *
 * Leaving the NULL profile is by either successfully changehatting
 * into a sibling hat, or changehatting back to the parent (NULL hat).
 */
struct sdprofile null_profile = {
	.name	= "null-profile",	/* NULL profile */
	.count	= {1},			/* start count at 1 */
};

/* NULL complain profile
 *
 * Used when in complain mode, to emit Permitting messages for non-existant
 * profiles and hats.  This is necessary because of selective mode, in which
 * case we need a complain null_profile and enforce null_profile
 *
 * The null_complain_profile cannot be statically allocated, because it
 * can be associated to files which keep their reference even if subdomain is
 * unloaded
 */
struct sdprofile *null_complain_profile;

/**
 * free_sdprofile - free sdprofile structure
 */
void free_sdprofile(struct sdprofile *profile)
{
	struct sd_entry *sdent, *next_sdent = NULL;
	struct nd_entry *ndent, *next_ndent = NULL;
	struct list_head *lh, *tmp;

	SD_DEBUG("%s(%p)\n",__FUNCTION__, profile);

	if (!profile)
		return;

	/* these profiles should never be freed */
	if (profile == &null_profile){
		SD_ERROR("%s: internal error, attempt to remove profile '%s'\n",
			__FUNCTION__,
			profile->name);
		BUG();
	}
		
	/* profile is still on global prpfile list -- invalid */
	if (!list_empty(&profile->list)){
		SD_ERROR("%s: internal error, profile '%s' still on global list\n",
			__FUNCTION__,
			profile->name);
		BUG();
	}

	for (sdent = profile->file_entry; sdent; sdent = next_sdent) {
		next_sdent = sdent->next;
		if (sdent->filename) {
			SD_DEBUG("freeing sd_entry: %p %s\n" ,
				  sdent->filename, sdent->filename);
/*
			put_name(sdent->filename);
			if (sdent->pattern_type == ePatternRegex){
				put_name(sdent->regex);
				put_pattern(sdent->compiled);
			}
*/
		}
//		kfree(sdent);
		put_sd_entry(sdent);
	}

	for (ndent = profile->net_entry; ndent; ndent = next_ndent) {
		next_ndent = ndent->next;
		if (ndent == NULL) 
			SD_DEBUG("%s: NULL entry!!!\n", __FUNCTION__);	
		else
			kfree(ndent);
	}

	list_for_each_safe(lh, tmp, &profile->sub) {
		struct sdprofile *p = list_entry(lh, struct sdprofile, list);
		list_del_init(&p->list);
		put_sdprofile(p);
	}

	if (profile->sub_name) {
		SD_DEBUG("%s: %s %s\n", __FUNCTION__, profile->name,
				profile->sub_name);
		kfree(profile->sub_name);
	}

	if (profile->name) {
		SD_DEBUG("%s: %s\n", __FUNCTION__, profile->name);
		kfree(profile->name);
	}

	kfree(profile);
}

/** task_remove
 *
 * remove profile in a task's subdomain leaving the task unconfined
 *
 * @sd: task's subdomain
 */
static __INLINE__ void
task_remove(struct subdomain *sd)
{
	/* SD_WLOCK held here */
	SD_DEBUG("%s: removing profile from task %s(%d) profile %s active %s\n",
		__FUNCTION__,
		sd->task->comm,
		sd->task->pid,
		sd->profile->name,
		sd->active->name);

	put_sdprofile(sd->profile);
	put_sdprofile(sd->active);
	sd->profile = sd->active = NULL;

}

/** taskremove_iter
 *
 * Iterate over all subdomains. 
 *
 * If any matches old_profile,  then call task_remove to remove it.
 * This leaves the task (subdomain) unconfined.
 */
static int taskremove_iter(struct subdomain *sd, void *cookie)
{
struct sdprofile *old_profile = (struct sdprofile *) cookie;
int remove=0;

	SD_WLOCK;

	if (__sd_is_confined(sd) && 
	     sd->profile == old_profile){
//	    !(strcmp(sd->profile->name, old_profile->name)))
		remove=1; /* remove item from list */
		task_remove(sd);
	}

	SD_WUNLOCK;

	return remove;
}

/** task_replace
 *
 * replace profile in a task's subdomain with newly loaded profile
 *
 * @sd: task's subdomain
 * @new: old profile
 */
static __INLINE__ void
task_replace(struct subdomain *sd , struct sdprofile *new)
{
	struct sdprofile *subprofile = NULL;
	struct sdprofile *active = sd->active;
	struct sdprofile *profile = sd->profile;

	SD_DEBUG("%s: replacing profile for task %s(%d) profile=%s (%p) active=%s (%p)\n", 
		__FUNCTION__,
		sd->task->comm, sd->task->pid,
		sd->profile->name, sd->profile,
		sd->active->name, sd->active);

	if (sd->profile == sd->active)
		sd->active = get_sdprofile(new);
	else if (sd->active) {
		/* old in hat, new profile has hats */
		/* XXX need a lock for this list */
		subprofile = __sd_find_profile(sd->active->name, &new->sub);

		/* old subprofile does not exist, set active equal to null_profile */
		if (!subprofile) {
			if (new->flags.complain) {
				subprofile = get_sdprofile(null_complain_profile);
			} else {
				subprofile = get_sdprofile(&null_profile);
			}
		}
		sd->active = subprofile;
	}
	sd->profile = get_sdprofile(new);

	/* release the old profiles */
	put_sdprofile(profile);
	put_sdprofile(active);
}

struct sd_taskreplace_data {
	struct sdprofile *old_profile;
	struct sdprofile *new_profile;
};

/** taskreplace_iter
 *
 * Iterate over all subdomains. 
 *
 * If any matches old_profile,  then call task_replace to replace with
 * new_profile
 */
static int taskreplace_iter(struct subdomain *sd, void *cookie)
{
struct sd_taskreplace_data *data = (struct sd_taskreplace_data *)cookie;

	SD_WLOCK;

	if (__sd_is_confined(sd) && 
	     sd->profile == data->old_profile){
//	    !(strcmp(sd->profile->name, old_profile->name)))
		task_replace(sd, data->new_profile);
	}

	SD_WUNLOCK;

	return 0;
}


#ifdef SUBDOMAIN_PROCATTR
int sd_setprocattr_changehat(char *hatinfo, size_t infosize)
{
	int error = -EINVAL;
	char *token=NULL, 
	     *hat, *smagic, *tmp;
	__u32 magic;
	int rc, len, consumed;

	SD_DEBUG("%s: %p %d\n",  
		__FUNCTION__,
		hatinfo, (int) infosize);

	/* strip leading white space */
	while(infosize && isblank(*hatinfo)) {
		hatinfo++;
		infosize--;
	}

	if (infosize == 0){
		goto out;
	}

	/* 
	 * Copy string to a new buffer so we can play with it
	 * It may be zero terminated but we add a trailing 0 
	 * for 100% safety
	 */
	token=kmalloc(infosize+1, GFP_KERNEL);

	if (!token){
		error=-ENOMEM;
		goto out;
	}

	memcpy(token, hatinfo, infosize);
	token[infosize] = 0;

	/* error is INVAL until we have at least parsed something */
	error = -EINVAL;

 	tmp = token;
	while (*tmp && *tmp != '^'){
		tmp++;
	}

	if (!*tmp || tmp == token){
		SD_WARN("%s: Invalid input '%s'\n", 
			__FUNCTION__,
			token);
		goto out;
	}

	/* split magic and hat into two strings */
	*tmp = 0;
	smagic = token;


	/* 
	 * Initially set consumed=strlen(magic), as if sscanf 
	 * consumes all input via the %x it will not process the %n 
	 * directive. Otherwise, if sscanf does not consume all the 
	 * input it will process the %n and update consumed.
	 */
	consumed=len=strlen(smagic);

	rc=sscanf(smagic, "%x%n", &magic, &consumed);
	
	if (rc != 1 || consumed != len){
		SD_WARN("%s: Invalid hex magic %s\n", 
			__FUNCTION__,
			smagic);
		goto out;
	}

	hat = tmp+1;

	if (!*hat){
		hat = NULL;
	}

	if (!hat && !magic){
		SD_WARN("%s: Invalid input, NULL hat and NULL magic\n", 
			__FUNCTION__);
		goto out;
	}

	SD_DEBUG("%s: Magic 0x%x Hat '%s'\n", 
		__FUNCTION__,
		magic, hat ? hat : NULL);

	SD_WLOCK;
	error = sd_change_hat(hat, magic);
	SD_WUNLOCK;
		
out:
	if (token){
		memset(token, 0, infosize);
		kfree(token);
	}

	return error;
}

int sd_setprocattr_setprofile(struct task_struct *p, char *profilename, 
			      size_t profilesize)
{
	int error = -EINVAL;
	struct sdprofile *profile;
	char *name=NULL;

	SD_DEBUG("%s: current %s(%d)\n",
		__FUNCTION__,
		current->comm, current->pid);	

	/* strip leading white space */
	while (profilesize && isblank(*profilename)) {
		profilename++;
		profilesize--;
	}

	if (profilesize == 0){
		goto out;
	}
		
	/* 
	 * Copy string to a new buffer so we guarantee it is zero
	 * terminated
	 */
	name=kmalloc(profilesize+1, GFP_KERNEL);

	if (!name){
		error=-ENOMEM;
		goto out;
	}

	strncpy(name, profilename, profilesize);
	name[profilesize]=0;

	if (strcmp(name, SD_UNCONSTRAINED) == 0){
		profile=&null_profile;
	}else{	
		profile=sd_profilelist_find(name);
	}

	if (profile){
		struct subdomain *sd;

		SD_WLOCK;

		sd = SD_SUBDOMAIN(p->security);

		/* switch to unconstrained */
		if (profile == &null_profile){
			if (__sd_is_confined(sd)){
				SD_WARN("%s: Unconstraining task %s(%d) profile %s active %s\n",
					__FUNCTION__,
					p->comm, p->pid,
					sd->profile->name,
					sd->active->name);

				put_sdprofile(sd->profile);
				put_sdprofile(sd->active);
				sd->profile = sd->active = NULL;
				sd->sd_hat_magic = 0;
			}else{
				SD_WARN("%s: task %s(%d) is already unconstrained\n",
					__FUNCTION__,
					p->comm, p->pid);
			}
		}else{
			if (!sd){
				/* this task was created before module was 
				 * loaded, allocate a subdomain
			 	*/
				SD_WARN("%s: task %s(%d) has no subdomain\n",
					__FUNCTION__,
					p->comm, p->pid);

				sd = alloc_subdomain(p);
				if (!sd){
					SD_WARN("%s: Unable to allocate subdomain for task %s(%d). Cannot confine task to profile %s\n",
						__FUNCTION__,
						p->comm, p->pid,
						name);

					error = -ENOMEM;
					SD_WUNLOCK;

					goto out;
				}
			}

			/* we do not do a normal task replace since we are not 
		 	 * replacing with the same profile.
		 	 * If existing process is in a hat, it will be moved 
			 * into the new parent profile, even if this new 
			 * profile has a identical named hat.
		 	 */
			
			SD_WARN("%s: Switching task %s(%d) profile %s active %s to new profile %s\n",
				__FUNCTION__,
				p->comm, p->pid,
				sd->profile ? sd->profile->name : SD_UNCONSTRAINED,
				sd->active ? sd->profile->name : SD_UNCONSTRAINED,
				name);

			/* these are no-ops if profile/active is NULL */
			put_sdprofile(sd->profile);
			put_sdprofile(sd->active);

			sd->profile=profile;	/* already refcounted by
						 * sd_profilelist_find
						 */
			sd->active=get_sdprofile(profile);

			/* reset magic in case we were in a subhat before */
			sd->sd_hat_magic = 0;

		}

		SD_WUNLOCK;
	}else{
		SD_WARN("%s: Unable to switch task %s(%d) to profile '%s'. No such profile.\n",
			__FUNCTION__,
			p->comm, p->pid,
			name);

		error = -EINVAL;
	}

out:
	if (name){
		kfree(name);
	}

	return error;
}
#endif // SUBDOMAIN_PROCATTR

#ifdef SD_OLD_INTERFACE
#error "Old interface is deprecated"
#endif

#include "module_interface.c"
