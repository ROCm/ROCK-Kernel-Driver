#ifdef SD_OLD_INTERFACE

/* Routines for the 2.4 kernel sys_security profile interface */

/**
 * get_name - copy name from user space
 * @name_u: user space name pointer.
 * @maxlen: max length of name.
 */
static inline char * 
get_name (char *name_u, int maxlen)
{
	char *name = NULL;
	int len, retval;

	/* strnlen_user() includes \0 in it's value */
	len = strnlen_user(name_u, maxlen); 
	if ((len <= 0) || (len > maxlen))
		return NULL;

	name = (char *)kmalloc(len, GFP_KERNEL);
	if (!name)
		return NULL;

	retval = strncpy_from_user(name ,name_u ,len);
	if (retval != (len - 1)) {
		kfree(name);
		return NULL;
	}
	return name;
}       

/**
 * get_pattern - copy compiled pcre pattern from user space
 * @name_u: user space name pointer.
 * @maxlen: max length of name.
 */
static inline pcre * 
get_pattern (pcre *pattern_u)
{
pcre tdata, *pattern_k;

	size_t len;

	if (copy_from_user(&tdata, pattern_u, sizeof(pcre))){
		return NULL;
	}

	/* actual size of pcre structure */
	len=tdata.size;
	
	if (!(pattern_k=(pcre*)kmalloc(len, GFP_KERNEL))){
		return NULL;
	}

	(void)memcpy(pattern_k, &tdata, sizeof(pcre));

	if (copy_from_user(((uschar*)pattern_k)+sizeof(pcre), ((uschar*)pattern_u)+sizeof(pcre),
	    		len-sizeof(pcre))){
		kfree(pattern_k);
		return NULL;
	}

	/* stitch in pcre patterns, it was NULLed out by parser */
	// pcre_default_tables defined in pcre_tables.h */

	pattern_k->tables=pcre_default_tables;

	return pattern_k;
}

/**
 * get_iface - copy interface name from user space
 * @iface_u: user space interface name pointer
 */
static inline char * get_iface(char * iface_u) 
{
	char * iface = NULL;
	int u_len, u_ret;

	u_len = strnlen_user ( iface_u, IFNAMSIZ );

	/* cmw: should this be u_len <= 0 ??? */
	if ( ( u_len <= 0)  || ( u_len > IFNAMSIZ) ) 
		return NULL; 

 	iface = (char *)kmalloc(u_len, GFP_KERNEL);
 	if (!iface)
 		return NULL;
 		
	u_ret = strncpy_from_user(iface, iface_u, IFNAMSIZ);
	if (u_ret != (u_len - 1)) {
		kfree (iface);
		return NULL;
	}
	return iface;
}

/**
 * copy_entry_from_user - copy sd entry from user
 * @entry_u: user space entry
 */

static inline
struct sd_entry * copy_entry_from_user(struct cod_entry_user *entry_u)
{
	char *name = NULL,
	     *regex = NULL;
	pcre *compiled = NULL;
	struct sd_entry *entry = NULL;

	if (!(name = get_name(entry_u->name, PATH_MAX))){
		goto error;
	}

	/* is there a regex pattern present? */
	if (entry_u->pattern_type == ePatternRegex)
	{
		if (!(regex = get_name(entry_u->pat.regex, PATH_MAX))){
			goto error;
		}

		if (!(compiled = get_pattern(entry_u->pat.compiled))){
			goto error;
		}
	}	

	if ((entry = alloc_sd_entry())){
		entry->filename = name;

		/* exec alone without qualifier is no longer allowed */
		if (SD_EXEC_MASK(entry_u->mode) == KERN_COD_MAY_EXEC){
			SD_ERROR("%s: Invalid mode while parsing entry %s.  KERN_COD_MAY_EXEC must now be combined with an exec qualifier (inherit, uncontrained or profile). Please verify that kernel and subdomain_parser versions are compatible\n",
				__FUNCTION__,
				entry->filename);
			goto error;
		}else{
			entry->mode = entry_u->mode;
		}

		entry->pattern_type = entry_u->pattern_type;
		if (entry->pattern_type == ePatternRegex){
			entry->regex=regex;
			entry->compiled=compiled;
		}else{
			entry->regex=NULL;
			entry->compiled=NULL;
		}
	} else {
		goto error;
	}

	switch (entry->pattern_type){
		case ePatternBasic:
			SD_DEBUG("%s: %s [no pattern] mode=0x%x\n", 
				__FUNCTION__,
				entry->filename,
				entry->mode);
			break;
		case ePatternTailGlob:
			SD_DEBUG("%s: %s [tailglob] mode=0x%x\n", 
				__FUNCTION__,
				entry->filename,
				entry->mode);
			break;
		case ePatternRegex:
			SD_DEBUG("%s: %s [regex] regex='%s' pattern_length=%d mode=0x%x\n", 
				__FUNCTION__,
				entry->filename,
				entry->regex,
				(unsigned int) entry->compiled->size,
				entry->mode);
			break;
		default:
			SD_WARN("%s: INVALID pattern_type %d\n", 
				__FUNCTION__,
				(int)entry->pattern_type);
			goto error;
	}

	return entry;

error:
	free_sd_entry(entry);
	put_name(name);
	put_name(regex);
	put_pattern(compiled);

	return NULL;
}


/************************************************************************/
/*                                                                      */
/* copy_net_entry_from_user                                             */
/*                                                                      */
/*                                                                      */
/************************************************************************/


static inline struct nd_entry *
copy_net_entry_from_user(struct cod_net_entry_user *net_entry_u) 
{
	char *iface;
	struct nd_entry *entry = NULL;

	struct in_addr *p_saddr, *p_smask, *p_daddr, *p_dmask;
	unsigned short *p_sport, *p_dport;

	SD_DEBUG("%s: BEGIN\n", 
		__FUNCTION__);

	/* interface not required, may be NULL */
	iface = get_iface(net_entry_u->iface);
	
	entry = alloc_nd_entry();

	if (!entry) {
		SD_DEBUG("%s: No entry\n", __FUNCTION__);
		goto error;
	}

	/* values in entry are zeroed by alloc_nd_entry 
	 * N.B for in_addr values, 0 == INADDR_ANY 
	 * */


	entry->mode = net_entry_u->mode;

	ND_DEBUG ("%s: mode: %x\n", 
		__FUNCTION__,
		entry->mode);

	/* N.B
	 * The parser and the kernel have an opposite view of
	 * addresses.
	 * 
	 * As far as we (the kernel) are concerned:
	 * "tcp_accept from"  specifies a remote address (daddr)
	 * "tcp_connect from" specifies a local address (saddr)
	 * And so on (inversely) for "to"
	 *
	 * The parser considers <from> to always be a source
	 * and <to> to always specify a remote regardless of
	 * the mode (accept, connect etc)
	 *
	 * We'll switch them around here
	 */

	if (entry->mode == KERN_COD_TCP_CONNECT ||
	    entry->mode == KERN_COD_UDP_SEND){

		p_saddr = net_entry_u->saddr;
		p_smask = net_entry_u->smask;
		p_sport = net_entry_u->src_port;
		p_daddr = net_entry_u->daddr;
		p_dmask = net_entry_u->dmask;
		p_dport = net_entry_u->dst_port;

	}else if (entry->mode == KERN_COD_TCP_ACCEPT ||
	    entry->mode == KERN_COD_UDP_RECEIVE){

		p_saddr = net_entry_u->daddr;
		p_smask = net_entry_u->dmask;
		p_sport = net_entry_u->dst_port;
		p_daddr = net_entry_u->saddr;
		p_dmask = net_entry_u->smask;
		p_dport = net_entry_u->src_port;
	}else{
		SD_WARN("%s: INVALID mode %x\n", 
			__FUNCTION__,
			entry->mode);
		goto error;
	}

	if (net_entry_u->saddr) {
		entry->saddr = p_saddr->s_addr;
		ND_DEBUG ("%s: saddr: %u.%u.%u.%u\n", 
			__FUNCTION__, NIPQUAD(entry->saddr));
	} 
	if (net_entry_u->smask) {
		entry->smask = p_smask->s_addr;
		ND_DEBUG ("%s: smask: %u.%u.%u.%u\n", 
			__FUNCTION__, NIPQUAD(entry->smask));
	} 
	if (net_entry_u->daddr) { 
		entry->daddr = p_daddr->s_addr;
		ND_DEBUG ("%s: daddr: %u.%u.%u.%u\n", 
			__FUNCTION__, NIPQUAD(entry->daddr));
	} 
	if (net_entry_u->dmask) {
		entry->dmask = p_dmask->s_addr;
		ND_DEBUG ("%s: dmask: %u.%u.%u.%u\n", 
			__FUNCTION__, NIPQUAD(entry->dmask));
	}
	
	entry->src_port[0] = p_sport[0];
	entry->src_port[1] = p_sport[1];
	entry->dst_port[0] = p_dport[0];
	entry->dst_port[1] = p_dport[1];

	ND_DEBUG ("%s: sport: %u - %u  dport: %u - %u\n",
		__FUNCTION__, 
		entry->src_port[0], entry->src_port[1],
		entry->dst_port[0], entry->dst_port[1]);

	entry->iface = iface;

	ND_DEBUG ("%s: iface: %s\n", 
		__FUNCTION__,
		entry->iface ? entry->iface : "NULL");


	ND_DEBUG ("%s: EBD 0x%p\n", 
		__FUNCTION__,
		entry);

	return entry;

error:
	put_iface(iface);
	free_nd_entry(entry);

	return NULL;
}

/**
 * copy_profile_from_user - copy profile from user space
 * @profile_u: user profile
 * @parent: parent profile
 */
static struct sdprofile *
copy_profile_from_user(struct codomain_user *codomain_u,
				struct sdprofile *parent)
{
	char *name = NULL, *sub_name = NULL; 
	struct sdprofile *profile = NULL, *subprofile = NULL;
	struct codomain_user *sub_u = NULL, sub_k;
	struct cod_entry_user *file_u = NULL, entry_k;
	struct cod_net_entry_user *net_u = NULL, net_k;

	name = get_name(codomain_u->name, PATH_MAX);
	if (!name){
		goto error;
	}

	SD_DEBUG("%s: %s\n", __FUNCTION__ , name);

	if (codomain_u->sub_name != NULL) {
		sub_name = get_name(codomain_u->sub_name, KERN_COD_HAT_SIZE);
		if (!sub_name)
			goto nosub;
	}

	profile = alloc_sdprofile();
	if (!profile){
		goto noprof;
	}

	profile->name = name;
	/* XXX Hack to handle subprofiles */
	if (parent) {
		profile->name = sub_name;
		put_name(name);
		name=NULL;
	}

	/* copy per profile debug flags (debug, complain, audit) */
	profile->flags = codomain_u->flags;

	/* copy per profile capabilities (are &ed with tasks effective) */
	profile->capabilities = (kernel_cap_t)codomain_u->capabilities;

	//profile->sub_name = sub_name;
	//profile->parent = parent;

	/* 
	 * Warning, make these as non-recursive as possible. 
	 * Otherwise, the kernel stack will get blown out.
	 */
	file_u = codomain_u->entries;
	while (file_u) {
		struct sd_entry *file_entry = NULL;
		int i;

		if (copy_from_user(&entry_k, file_u, sizeof(struct cod_entry_user))){
			goto error;
		}
		file_entry = copy_entry_from_user(&entry_k);
		if (!file_entry){
			goto error;
		}

		/* profile->file_entry initially set to NULL 
		 * in alloc_sdprofile() */
		file_entry->next = profile->file_entry;
		profile->file_entry = file_entry;

		/* 
		 * Handle partitioned lists
		 * Chain entries onto sublists based on individual 
		 * permission bits. This allows more rapid searching.
		 */
		for (i=0; i<=POS_KERN_COD_FILE_MAX; i++){
			if (file_entry->mode & (1<<i)){
				/* profile->file_entryp[i] initially set to
				 * NULL in alloc_sdprofile() */
				file_entry->nextp[i]=profile->file_entryp[i];
				profile->file_entryp[i]=file_entry;
			}
		}

		file_u = entry_k.next;
	}
	count_entries(profile);

	net_u = codomain_u->net_entries;

	if (parent) {
		if (net_u){
			SD_WARN("%s: Ignoring network entries for subprofile %s^%s. There is currently no kernel Netdomain support for changehat.\n",
				__FUNCTION__,
				parent->name, profile->name);
		}
	}else{
		while (net_u) {
			struct nd_entry *net_entry = NULL;
			int i;
	
			if (copy_from_user(&net_k, net_u, sizeof(struct cod_net_entry_user))){
				goto error;
			}
			net_entry = copy_net_entry_from_user(&net_k);
			if (!net_entry){
				goto error;
			}
			net_entry->next = profile->net_entry;
			profile->net_entry = net_entry;
	
			/* 
			 * Handle partitioned lists
			 * Chain entries onto sublists based on individual 
			 * permission bits. This allows more rapid searching.
			 */
			for (i=POS_KERN_COD_NET_MIN; i<=POS_KERN_COD_NET_MAX; i++){
				int j;
	
				j=NET_POS_TO_INDEX(i);
	
				if (net_entry->mode & (1<<i)){
					/* profile->net_entryp[i] initially set to
					 * NULL in alloc_sdprofile() */
					net_entry->nextp[j]=profile->net_entryp[j];
					profile->net_entryp[j]=net_entry;
				}
			}
	
			net_u = net_k.next;
		} 
		count_net_entries(profile);
	}
	
	sub_u = codomain_u->subdomain; 
	while (sub_u) {
		if (copy_from_user(&sub_k, sub_u, sizeof(struct codomain_user))){
			goto error;
		}
		subprofile = copy_profile_from_user(&sub_k, profile);
		if (!subprofile){
			goto error;
		}
		get_sdprofile(subprofile);
		list_add(&subprofile->list, &profile->sub);
		sub_u = sub_k.next ;
	}

	/* DONE */
	return profile;

error:
	if (profile){
		free_sdprofile(profile);
		profile=NULL;
	}
		
	/* name/sub_name will be released by above free_sdprofile
	 * if necessary
	 */
	return NULL;

noprof:
	put_name(sub_name);
	/* fall thru */
nosub:
	put_name(name);
	return NULL;
}

/**
 * sysctl_add - add new profile
 * @codomain_u: user space profile
 */
static inline int sysctl_add(struct codomain_user *codomain_u)
{
	struct sdprofile *profile = NULL, *old_profile = NULL;

	if (sd_is_confined()){
		struct subdomain *sd = SD_SUBDOMAIN(current->security);

		SD_WARN("REJECTING access to profile addition (%s(%d) profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);

		return -EPERM;
	}

	profile = copy_profile_from_user(codomain_u, NULL);
	if (!profile)
		return -ENOMEM;

	old_profile = sd_profilelist_find(profile->name);
	if (old_profile) {
		SD_WARN("%s: trying to add profile (%s) that "
			"already exists.\n", __FUNCTION__, profile->name);
		put_sdprofile(old_profile);
		free_sdprofile(profile);
		return -EINVAL ;
	}

	sd_profilelist_add(profile);

	/* XXX */
	return 1;
}       

/**
 * sysctl_del - remove profile
 * @codomain_u: user space codomain
 */
static inline int sysctl_del(struct codomain_user *codomain_u)
{
	struct sdprofile *profile = NULL,
			 *old_profile;

	if (sd_is_confined()){
		struct subdomain *sd = SD_SUBDOMAIN(current->security);

		SD_WARN("REJECTING access to profile deletion (%s(%d) profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);

		return -EPERM;
	}

	profile = copy_profile_from_user(codomain_u, NULL);
	if (!profile)
		return -ENOMEM;


	/* Do this step to get a guaranteed reference to profile
	 * as sd_profilelist_remove may drop it to zero which would
	 * made subsequent attempt to iterate using it unsafe
	 */
	old_profile=sd_profilelist_find(profile->name);

	if (old_profile){
		if (sd_profilelist_remove(profile->name) != 0) {
			SD_WARN("%s: race trying to remove profile (%s)\n",
				__FUNCTION__, profile->name) ;
		}

		/* remove profile from any tasks using it */
		sd_subdomainlist_iterateremove(taskremove_iter, (void*)old_profile);

#if defined NETDOMAIN && defined NETDOMAIN_SKUSERS
		/* drop all netdomains using this profile */
		nd_skusers_exch(old_profile, NULL, 1);
#endif

		/* drop reference obtained by sd_profilelist_find */
		put_sdprofile(old_profile);
	}else{
		SD_WARN("%s: trying to remove profile (%s) that "
			"doesn't exist - skipping.\n",
			__FUNCTION__, profile->name ) ;
	}

	/* The entire copy_profile_from_user step above was performed
	 * to simply get profile_name.  Copying in an entire profile to
	 * just get the name is very inefficient, but this is the way it's
	 * done.
	 *
	 * Since we no longer need the profile, we must delete it
	 */
	free_sdprofile(profile);

	/* XXX */
	return 1;
}       

/**
 * sysctl_repl - replace a profile
 * @codomain_u: user supplied profile
 */

static inline int sysctl_repl(struct codomain_user *codomain_u)
{
	struct sd_taskreplace_data data;

	struct sdprofile *old_profile = NULL;

	if (sd_is_confined()){
		struct subdomain *sd = SD_SUBDOMAIN(current->security);

		SD_WARN("REJECTING access to profile replacement (%s(%d) profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);

		return -EPERM;
	}

	data.new_profile = copy_profile_from_user(codomain_u, NULL);
	if (!data.new_profile)
		return -ENOMEM;

	/* Replace the profile on the global profile list.
	 * This list is used by all new exec's to find the correct profile.
	 * If there was a previous profile, it is returned, else NULL.
	 *
	 * N.B The old profile released still has a reference so it must
	 * be put when no longer required.
	 */
	data.old_profile = sd_profilelist_replace(data.new_profile);

	/* If there was an old profile,  find all currently executing tasks
	 * using this profile and replace the old profile with the new.
	 */
	if (data.old_profile) {
		SD_DEBUG("%s: try to replace profile (%p)%s\n", 
			__FUNCTION__,
			data.old_profile, 
			data.old_profile->name);

		sd_subdomainlist_iterate(taskreplace_iter, (void*)&data);

#if defined NETDOMAIN && defined NETDOMAIN_SKUSERS
		/* Change any sockets (soft intr context) using old profile
		 * to use new profile
		 */
		nd_skusers_exch(data.old_profile, data.new_profile, 0);
#endif

		/* mark old profile as stale */
		data.old_profile->isstale=1;

		/* it's off global list, and we are done replacing */
		put_sdprofile(data.old_profile);
	} else {
		SD_WARN("%s: trying to replace profile %s that doesn't exist\n", __FUNCTION__, data.new_profile->name);
		free_sdprofile(data.new_profile);
		return -EINVAL;
	}

	/* XXX */
	return 1;
}       

/**
 * sysctl_dbg - debugging interface
 * @codomain_u: user supplied profile
 *
 * Dumps the supplied profile, then dumps the profile list
 */
static inline int sysctl_dbg(struct codomain_user *codomain_u)
{
	if (codomain_u) {
		struct sdprofile *profile = NULL;

		profile = copy_profile_from_user(codomain_u, NULL);

		if (!profile)
			return -ENOMEM;

		sd_profile_dump(profile);

		free_sdprofile(profile);
	}

	sd_profilelist_dump();
	return 0;
}

static inline int dump_mount_info(void)
{
	struct namespace *namespace;
	struct list_head *lh;
	char *page, *path;

	SD_DEBUG("%s\n", __FUNCTION__);
	task_lock(current);
	namespace = current->namespace;
	task_unlock(current);
	if (!namespace)
		return -ENOENT;
	page = (char *)__get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	list_for_each(lh, &namespace->list) {
		struct vfsmount *mnt=list_entry(lh, struct vfsmount, mnt_list);
		path = d_path(mnt->mnt_root, mnt, page, PAGE_SIZE);
		SD_INFO("(%p->%p)%s:%s\n", mnt, mnt->mnt_root, mnt->mnt_devname, path);
#if 0
		/* should always be "/" */
		path = d_path(mnt->mnt_root, mnt->mnt_parent, page, PAGE_SIZE);
		SD_INFO("(%p)%s:%s\n", mnt, mnt->mnt_devname, path);
		/* junk... "/home/home" */
		path = d_path(mnt->mnt_mountpoint, mnt, page, PAGE_SIZE);
		SD_INFO("(%p)%s:%s\n", mnt, mnt->mnt_devname, path);
#endif
		path = d_path(mnt->mnt_mountpoint, mnt->mnt_parent, page, PAGE_SIZE);
		SD_INFO("(%p->%p)%s:%s\n", mnt->mnt_parent, mnt->mnt_parent->mnt_mountpoint, mnt->mnt_parent->mnt_devname, path);
	}
	free_page((unsigned long)page);
	return 0;
}

int sd_sys_security(unsigned int id, unsigned int call, unsigned long *args)
{
	struct codomain_user cod_k;
	int error = -EINVAL;

	SD_DEBUG("%s: 0x%x %d\n", __FUNCTION__, id, call);

	if (id != SD_ID_MAGIC)
		goto out;

	switch (call) {
	case SD_ADD_PROFILE:
		error = -EFAULT;
		if (!copy_from_user(&cod_k, args,
				sizeof(struct codomain_user)))
			error = sysctl_add(&cod_k);
		break;

	case SD_DELETE_PROFILE:
		error = -EFAULT;
		if (!copy_from_user(&cod_k, args,
				sizeof(struct codomain_user)))
			error = sysctl_del(&cod_k);
		break;

	case SD_REPLACE_PROFILE:
		error = -EFAULT;
		if (!copy_from_user(&cod_k, args,
				sizeof(struct codomain_user)))
			error = sysctl_repl(&cod_k);
		break;

	case SD_DEBUG_PROFILE:
		if (args) {
			if (!copy_from_user(&cod_k, args,
				sizeof(struct codomain_user)))
				error = sysctl_dbg(&cod_k);
		}
		else
			error = sysctl_dbg(NULL);
		break;

	case 5:
		error = dump_mount_info();
		break;

	default:
		error = -EINVAL;
		break;
	}

out:
	return error;
}

#endif //SD_OLD_INTERFACE
