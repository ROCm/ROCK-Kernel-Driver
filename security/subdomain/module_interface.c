/* Codes of the types of basic structures that are understood */
#define SD_CODE_SIZE (sizeof(__u8))
#define SD_STR_LEN (sizeof(__u16))
#define MATCHENTRY(A,B) (strcmp(A->filename,B->filename)==0)
#define INTERFACE_ID "INTERFACE"

#define SUBDOMAIN_INTERFACE_VERSION 2

enum sd_code
{
	SD_U8,
	SD_U16,
	SD_U32,
	SD_U64,
	SD_NAME,		/* same as string except it is items name */
	SD_STRING,
	SD_BLOB,
	SD_STRUCT,
	SD_STRUCTEND,
	SD_LIST,
	SD_LISTEND,
	SD_OFFSET,
	SD_BAD};

const char *sd_code_names[] = { "SD_U8",
				"SD_U16",
				"SD_U32",
				"SD_U64",
				"SD_NAME",
				"SD_STRING",
				"SD_BLOB",
				"SD_STRUCT",
				"SD_STRUCTEND",
				"SD_LIST",
				"SD_LISTEND",
				"SD_OFFSET"};

typedef struct {
	void *start;
	void *end;
	u32  version;
} sd_ext;

static __INLINE__ int
sd_inbounds (void *p, sd_ext *e, size_t size)
{
	return (p+size <= e->end);
}

static __INLINE__ void *
sd_inc(void *p, sd_ext *e, size_t size)
{
	if (sd_inbounds(p, e, size))
		return p+size;
	SD_DEBUG("%s: requested inc of %zd bytes out of bounds (start %p, extent %p, pos %p\n", INTERFACE_ID, size, e->start, e->end, p);
	return 0;
}

static __INLINE__ void *
sd_is_X (void *p, sd_ext *e, enum sd_code code, int required)
{
	__u8 *b = (__u8 *) p;
	__u8 conv;
	if (sd_inbounds(p, e, SD_CODE_SIZE)) {
		conv = *b;
		if ((conv == code))
			return sd_inc(p, e, SD_CODE_SIZE);
		if (required)
			SD_DEBUG("%s: type code incorrect @%d, got %s(%d) expected %s(%d)\n", INTERFACE_ID, (int) (p-e->start), conv<SD_BAD ? sd_code_names[conv] : "unknown", conv, sd_code_names[code], code);

	}
	return 0;
}

static __INLINE__ void *
sd_read8 (void *p, sd_ext *e, __u8 *b)
{
	__u8 *c = (__u8 *) p;
	if (!sd_inbounds(p, e, 1)) return 0;
	*b = *c;
	return sd_inc(p, e, 1);
}

static __INLINE__ void *
sd_read16 (void *p, sd_ext *e, __u16 *b)
{
	__u16 *c = (__u16 *) p;
	if (!sd_inbounds(p, e, 2)) return 0;
	*b = le16_to_cpu(*c);
	return sd_inc(p, e, 2);
}

static __INLINE__ void *
sd_read32 (void *p, sd_ext *e, __u32 *b)
{
	__u32 *c = (__u32 *) p;
	if (!sd_inbounds(p, e, 4)) return 0;
	*b = le32_to_cpu(*c);
	return sd_inc(p, e, 4);
}

void *
sd_match_name (void *p, sd_ext *e, char *name, int required)
{
	void *c;
	__u16 size;
	char *str;
	char *message=NULL;

	if (!(c = sd_is_X(p, e, SD_NAME, (name!=NULL)*required))) {
		if (name) {
			message = "missing required tag name";
			goto fail;
		}
		/* no name trivially matches no name entry */
		return p;
	}
	if (!(c = sd_read16(c, e, &size))) {
		message = "unable to read tag name size";
		goto fail;
	}
	if (!sd_inbounds(c, e, size)) {
		message = "tag name size out of bounds";
		goto fail;
	}
	str = (char *) c;

	/* null name matches any */
	if ((name && strncmp(name, str, size) == 0) ||
            (name == 0))
		return sd_inc(c, e, size);
	
 fail:
	if (required)
		SD_DEBUG("%s: %s, expected %s\n", INTERFACE_ID, message, name);
	return 0;
}

void *
sd_is_X_name (void *p, sd_ext *e, enum sd_code code, char *name, int required)
{
	void *c=p;

	if ((c = sd_match_name(c, e, name, required)) &&
	    (c = sd_is_X(c, e, code, required))) {
		return c;
	}
	if (required && !c && name) 
		SD_DEBUG("%s: bad type for tag %s\n", INTERFACE_ID, name);
	return 0;
}

/* len includes terminating 0 */
void *
sd_is_string (void *p, sd_ext *e, char *name, __u32 *len)
{
	void *c;
	__u16 size;
	if (!(c = sd_is_X_name(p, e, SD_STRING, name, name!=NULL))) goto fail;
	if (!sd_inbounds(c, e, 2) || !(c = sd_read16(c, e, &size))) goto fail;
	if (!sd_inbounds(c, e, size)) goto fail;
	*len = size;
	return c;
 fail:
	return 0;
}

void *
sd_is_blob (void *p, sd_ext *e, char *name, __u32 *len)
{
	void *c;
	__u32 size;
	if (!(c = sd_is_X_name(p, e, SD_BLOB, name, name!=NULL))) goto fail;
	if (!sd_inbounds(c, e, 4) || !(c = sd_read32(c, e, &size))) goto fail;
	if (!sd_inbounds(c, e, size)) goto fail;
	*len = size;
	return c;
 fail:
	*len = 0;
	return 0;
}

static __INLINE__ void *
sd_read8_t (void *p, sd_ext *e, __u8 *b, char *name)
{
	void *c;
	if (!(c = sd_is_X_name(p, e, SD_U8, NULL, 1))) goto fail;
	if (!(c = sd_read8(c, e, b))) goto fail;
	return c;

 fail:
	SD_DEBUG("%s: could not read 8 bit element %s\n", INTERFACE_ID, name);
	return 0;
}

static __INLINE__ void *
sd_read16_t (void *p, sd_ext *e, __u16 *b, char *name)
{
	void *c;
	if (!(c = sd_is_X_name(p, e, SD_U16, NULL, 1))) goto fail;
	if (!(c = sd_read16(c, e, b))) goto fail;
	return c;
 fail:
	SD_DEBUG("%s: could not read 16 bit element %s\n", INTERFACE_ID, name);
	return 0;
}

static __INLINE__ void *
sd_read32_t (void *p, sd_ext *e, __u32 *b, char *name)
{
	void *c;
	if (!(c = sd_is_X_name(p, e, SD_U32, NULL, 1))) goto fail;
	if (!(c = sd_read32(c, e, b))) goto fail;
	return c;
 fail:
	SD_DEBUG("%s: could not read 32 bit element %s\n", INTERFACE_ID, name);
	return 0;
}

void *
sd_read_blob (void *p, sd_ext *e, char *name, void *buf, int size)
{
	__u32 len;
	void *c;
	if ((c = sd_is_blob (p, e, name, &len)) &&
	    len <= size) {
		memcpy(buf, c, len);
		return sd_inc(c, e, len);
	}
	return 0;
}

static __INLINE__ void *
sd_read_structhead(void *p, sd_ext *e, char *name)
{
	return sd_is_X_name(p, e, SD_STRUCT, name, name!=NULL);
}

static __INLINE__ void *
sd_read_structend(void *p, sd_ext *e)
{
	return sd_is_X(p, e, SD_STRUCTEND, 1);
}

static __INLINE__ void *
sd_read_listhead(void *p, sd_ext *e, char *name)
{
	return sd_is_X_name(p, e, SD_LIST, name, name!=NULL);
}

static __INLINE__ void *
sd_read_listend(void *p, sd_ext *e)
{
	return sd_is_X(p, e, SD_LISTEND, 1);
}

/* use in place of get_name, still use put name */
void *
sd_get_string(void *p, sd_ext *e, int maxsize, char *name, char **string)
{
	void *c;
	char *str;
	__u32 len;

	if (!(c = sd_is_string(p, e, name, &len))) goto fail;
	if (len > maxsize) goto fail;

	str = (char *) kmalloc(len, GFP_KERNEL);
	if (!str) goto fail;
	memcpy(str, c, len);
	str[len-1] = 0;		/* force expect 0 termination */

	*string = str;
	return sd_inc(c, e, len);

 fail:
	return 0;
}

static __INLINE__ void *
sd_activate_net_entry(void *p, sd_ext *e, struct nd_entry **r_entry)
{
	char *iface = NULL;
	struct nd_entry *entry = NULL;

	void *c = p;

	__u32 p_saddr, p_smask, p_daddr, p_dmask;
  
	ND_DEBUG("%s: BEGIN\n", 
		 __FUNCTION__);

	if (!(entry = alloc_nd_entry())) goto out;

	if (!(c = sd_read_structhead(c, e, "ne"))) goto out;

	/* values in entry are zeroed by alloc_nd_entry 
	 * N.B for in_addr values, 0 == INADDR_ANY 
	 * */

	if (!(c = sd_read32_t(c, e, &(entry->mode), "net.mode"))) goto out;

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
	    entry->mode == KERN_COD_TCP_CONNECTED ||
	    entry->mode == KERN_COD_UDP_SEND){
		if (!(c = sd_read32_t(c, e, &p_saddr, "net.p_saddr"))) goto out;
		if (!(c = sd_read32_t(c, e, &p_smask, "net.p_smask"))) goto out;
		if (!(c = sd_read16_t(c, e, &entry->src_port[0], "net.src_port[0]"))) goto out;
		if (!(c = sd_read16_t(c, e, &entry->src_port[1], "net.src_port[1]"))) goto out;
		if (!(c = sd_read32_t(c, e, &p_daddr, "net.p_daddr"))) goto out;
		if (!(c = sd_read32_t(c, e, &p_dmask, "net.p_dmask"))) goto out;
		if (!(c = sd_read16_t(c, e, &entry->dst_port[0], "net.dst_port[0]"))) goto out;
		if (!(c = sd_read16_t(c, e, &entry->dst_port[1], "net.dst_port[1]"))) goto out;
	}else if (entry->mode == KERN_COD_TCP_ACCEPT ||
		  entry->mode == KERN_COD_TCP_ACCEPTED ||
		  entry->mode == KERN_COD_UDP_RECEIVE) {
		if (!(c = sd_read32_t(c, e, &p_daddr, "net.p_daddr"))) goto out;
		if (!(c = sd_read32_t(c, e, &p_dmask, "net.p_dmask"))) goto out;
		if (!(c = sd_read16_t(c, e, &entry->dst_port[0], "net.dst_port[0]"))) goto out;
		if (!(c = sd_read16_t(c, e, &entry->dst_port[1], "net.dst_port[1]"))) goto out;
		if (!(c = sd_read32_t(c, e, &p_saddr, "net.p_saddr"))) goto out;
		if (!(c = sd_read32_t(c, e, &p_smask, "net.p_smask"))) goto out;
		if (!(c = sd_read16_t(c, e, &entry->src_port[0], "net.src_port[0]"))) goto out;
		if (!(c = sd_read16_t(c, e, &entry->src_port[1], "net.src_port[1]"))) goto out;
	}else{
		ND_DEBUG("%s: INVALID mode %x\n", 
			__FUNCTION__,
			entry->mode);
		goto out;
	}
	if (p_saddr) {
		entry->saddr = p_saddr;
		ND_DEBUG ("%s: saddr: %u.%u.%u.%u\n", 
			  __FUNCTION__, NIPQUAD(entry->saddr));
	} 
	if (p_smask) {
		entry->smask = p_smask;
		ND_DEBUG ("%s: smask: %u.%u.%u.%u\n", 
			  __FUNCTION__, NIPQUAD(entry->smask));
	} 
	if (p_daddr) { 
		entry->daddr = p_daddr;
		ND_DEBUG ("%s: daddr: %u.%u.%u.%u\n", 
			  __FUNCTION__, NIPQUAD(entry->daddr));
	} 
	if (p_dmask) {
		entry->dmask = p_dmask;
		ND_DEBUG ("%s: dmask: %u.%u.%u.%u\n", 
	      __FUNCTION__, NIPQUAD(entry->dmask));
	}
	
	
	ND_DEBUG ("%s: sport: %u - %u  dport: %u - %u\n",
		  __FUNCTION__, 
		  entry->src_port[0], entry->src_port[1],
		  entry->dst_port[0], entry->dst_port[1]);
	
	if (sd_is_X(c, e, SD_STRING, 0)) {
		if (!(c = sd_get_string(c, e, IFNAMSIZ, NULL, &iface))) goto out;
		entry->iface = iface;
	}
	
	if (!(c = sd_read_structend(c, e))) goto out;
	
	ND_DEBUG ("%s: iface: %s\n", 
		  __FUNCTION__,
		  entry->iface ? entry->iface : "NULL");
	
	ND_DEBUG ("%s: EBD 0x%p\n", 
		  __FUNCTION__,
		  entry);
	
	*r_entry = entry;
	return c;
	
 out:
	put_iface(iface);
	free_nd_entry(entry);
	
	*r_entry = NULL;
	return 0;
}

/* use in place of get_pattern */
static __INLINE__ void *
sd_activate_pattern(void *p, sd_ext *e, pcre **r_pcre)
{
	pcre *pattern_k = NULL;
	__u32 size, magic, opts;
	__u8 t_char;
	void *c = p;
	/* size determines the real size of the pcre struct,
	   it is size_t - sizeof(pcre) on user side.
	   uschar must be the same in user and kernel space */
	/* check that we are processing the correct structure */
	if (!(c = sd_read_structhead(c, e, "pcre"))) goto out;
	if (!(c = sd_read32_t(c, e, &size, "pattern.size"))) goto out;
	if (!(c = sd_read32_t(c, e, &magic, "pattern.magic"))) goto out;
	if (!(pattern_k = (pcre *) kmalloc(size+sizeof(pcre), GFP_KERNEL))) goto out;
	memset(pattern_k, 0, size+sizeof(pcre));

	pattern_k->magic_number = magic;
	pattern_k->size = size+sizeof(pcre);
	if (!(c = sd_read32_t(c, e, &opts, "pattern.options"))) goto out;
	pattern_k->options = opts;
	if (!(c = sd_read16_t(c, e, &pattern_k->top_bracket, "pattern.top_bracket"))) goto out;
	if (!(c = sd_read16_t(c, e, &pattern_k->top_backref, "pattern.top_backref"))) goto out;
	if (!(c = sd_read8_t(c, e, &t_char, "pattern.fist_char"))) goto out;
	pattern_k->first_char = t_char;
	if (!(c = sd_read8_t(c, e, &t_char, "pattern.req_char"))) goto out;
	pattern_k->req_char = t_char;
	if (!(c = sd_read8_t(c, e, &t_char, "pattern.code[0]"))) goto out;
	pattern_k->code[0] = t_char;
	if (!(c = sd_read_blob(c, e, NULL, &pattern_k->code[1], size))) goto out;
	
	if (!(c = sd_read_structend(c, e))) goto out;
	
	/* stitch in pcre patterns, it was NULLed out by parser */
	// pcre_default_tables defined in pcre_tables.h */
	pattern_k->tables=pcre_default_tables;
	
	*r_pcre = pattern_k;
	return c;
	
 out:
	put_pattern(pattern_k);
	*r_pcre = NULL;
	return 0;
}

static __INLINE__ void *
sd_activate_file_entry(void *p, sd_ext *e, struct sd_entry **r_entry)
{
	char *name = NULL,
	     *regex = NULL;
	pcre *compiled = NULL;
	struct sd_entry *entry = NULL;
	void *c = p;

	if (!(entry = alloc_sd_entry())) goto out;
	/* check that we have the right struct being processed */
	if (!(c = sd_read_structhead(c, e, "fe"))) goto out;

	/* get the name */
	if (!(c = sd_get_string(c, e, PATH_MAX, NULL, &name))) goto out;
	entry->filename = name;

	/* get the mode */
	if (!(c = sd_read32_t(c, e, &entry->mode, "file.mode"))) goto out;

	/* get the pattern type */
	if (!(c = sd_read32_t(c, e, &entry->pattern_type, "file.pattern_type"))) goto out;

	entry->regex = NULL;
	entry->compiled = NULL;

	if (entry->pattern_type == ePatternRegex) {
		/* get PCRE pattern if regexp */
		if (!(c = sd_get_string(c, e, PATH_MAX, NULL, &regex))) goto out;
		entry->regex = regex;
		if (!(c = sd_activate_pattern(c, e, &compiled))) goto out;
		entry->compiled = compiled;
	}
	
	if (!(c = sd_read_structend(c, e))) goto out;

	switch (entry->pattern_type) {
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
		SD_DEBUG("%s: %s regex='%s' pattern_length=%d mode=0x%x\n", 
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
		goto out;
	}
	*r_entry = entry;
	return c;
 
 out:
	free_sd_entry(entry);
	put_name(name);
	put_name(regex);
	put_pattern(compiled);
	*r_entry = NULL;
	return 0;
}

static __INLINE__ int
check_rule_and_add(struct sd_entry *file_entry, struct sdprofile *profile,
		   char **message)
{
	/* verify consistency of x, px, ix, ux for entry against
	   possible duplicates for this entry */
	int mode = SD_EXEC_MODIFIER_MASK(file_entry->mode);
	//struct sd_entry *lentry;
	int i;

	if (mode && !(KERN_COD_MAY_EXEC & file_entry->mode)) {
		*message = "inconsistent rule, x modifiers without x";
		goto out;
	}
			
	/* check that only 1 of the modifiers is set */
	if (mode && (mode & (mode-1))) {
		*message = "inconsistent rule, multiple x modifiers";
		goto out;
	}
			
	file_entry->next = profile->file_entry;
	profile->file_entry = file_entry;
	mode = file_entry->mode;

	/* 
	 * Handle partitioned lists
	 * Chain entries onto sublists based on individual 
	 * permission bits. This allows more rapid searching.
	 */
	for (i=0; i<=POS_KERN_COD_FILE_MAX; i++){
		if (mode & (1<<i)){
			/* profile->file_entryp[i] initially set to
			 * NULL in alloc_sdprofile() */
			file_entry->nextp[i]=profile->file_entryp[i];
			profile->file_entryp[i]=file_entry;
		}
	}

	return 1;

 out:
	put_sd_entry(file_entry);
	return 0;
}

static __INLINE__ int
check_netrule_and_add(struct nd_entry *net_entry, struct sdprofile *profile,
		      char **message)
{
	int i;
	
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

	/* currently no failure case */
	return 1;
}

void *
sd_activate_profile(void *p, sd_ext *e, struct sdprofile *parent,
		    struct sdprofile **r_profile, ssize_t *error)
{
	struct sdprofile *profile = NULL, *subprofile = NULL;
	char *name = NULL;
	char *rulename = "";
	int count = 0;
	char *error_string = "Invalid Profile";
	void *c = p;

	*error = -EPROTO;

	profile = alloc_sdprofile();
	if (!profile) {
		error_string = "Could not allocate profile";
		*error = -ENOMEM;
		goto out;
	}

	/* check that we have the right struct being passed */
	if (!(c = sd_read_structhead(c, e, "profile"))) goto out;
	/* get the profile name, if this is a subprofile, the name
	   will be that of the subprofile */
	if (!(c = sd_get_string(c, e, PATH_MAX, NULL, &name))) goto out;
	profile->name = name;

	SD_DEBUG("%s: %s\n", __FUNCTION__ , name);

	error_string = "Invalid flags";
	/* per profile debug flags (debug, complain, audit) */
	if (!(c = sd_read_structhead(c, e, "flags"))) goto out;
	if (!(c = sd_read32_t(c, e, &(profile->flags.debug), "profile.flags.debug"))) goto out;
	if (!(c = sd_read32_t(c, e, &(profile->flags.complain), "profile.flags.complain"))) goto out;
	if (!(c = sd_read32_t(c, e, &(profile->flags.audit), "profile.flags.audit"))) goto out;
	if (!(c = sd_read_structend(c, e))) goto out;

	error_string = "Invalid capabilities";
	/* per profile capabilities (are &ed with tasks effective) */
	if (!(c = sd_read32_t(c, e, &(profile->capabilities), "profile.capabilities"))) goto out;

	/* get the file entries. */  
	if (sd_is_X_name(c, e, SD_LIST, "pgent", 0)) {
		rulename = "";
		error_string = "Invalid pcre file entry";
		if (!(c = sd_read_listhead(c, e, "pgent"))) goto out;
		while (!sd_is_X(c, e, SD_LISTEND, 0)) {
			struct sd_entry *file_entry = NULL;
			c = sd_activate_file_entry(c, e, &file_entry);
			if (!file_entry) goto out;
			if (!check_rule_and_add(file_entry, profile,
						&error_string)) {
				rulename = file_entry->filename;
				goto out;
			}

	        } /* while */
		if (!(c = sd_read_listend(c, e))) goto out;
		count_entries(profile);
	}


	/* get the simple globbing file entries. */  
	if (sd_is_X_name(c, e, SD_LIST, "sgent", 0)) {
		rulename = "";
		error_string = "Invalid tail glob file entry";
		if (!(c = sd_read_listhead(c, e, "sgent"))) goto out;
		while (!sd_is_X(c, e, SD_LISTEND, 0)) {
			struct sd_entry *file_entry = NULL;
			c = sd_activate_file_entry(c, e, &file_entry);
			if (!file_entry) goto out;
			if (!check_rule_and_add(file_entry, profile,
						&error_string)) {
				rulename = file_entry->filename;
				goto out;
			}

	        } /* while */
		if (!(c = sd_read_listend(c, e))) goto out;
		count_entries(profile);
	}


	/* get the basic file entries. */  
	if (sd_is_X_name(c, e, SD_LIST, "fent", 0)) {
		rulename = "";
		error_string = "Invalid file entry";
		if (!(c = sd_read_listhead(c, e, "fent"))) goto out;
		while (!sd_is_X(c, e, SD_LISTEND, 0)) {
			struct sd_entry *file_entry = NULL;
			c = sd_activate_file_entry(c, e, &file_entry);
			if (!file_entry) goto out;
			if (!check_rule_and_add(file_entry, profile,
						&error_string)) {
				rulename = file_entry->filename;
				goto out;
			}

	        } /* while */
		if (!(c = sd_read_listend(c, e))) goto out;
		count_entries(profile);
	}

	/* get the net entries */
	if (sd_is_X_name(c, e, SD_LIST, "net", 0)) {
		rulename = "";
		error_string = "Invalid net entry";
		if (!(c = sd_read_listhead(c, e, "net"))) goto out;
		count = 0;
		while (!sd_is_X(c, e, SD_LISTEND, 0)) {
			struct nd_entry *net_entry = NULL;
			c = sd_activate_net_entry(c, e, &net_entry);
			if (!net_entry) goto out;
			count++;
			if (!parent) {
				/* No parent profile, allow the net entries */
				if (!check_netrule_and_add(net_entry, profile,
							   &error_string)) {
					goto out;
				}
			} else {
				/* ignore net rules in sub_profiles (hats) */
				if (count == 1)
					SD_WARN("%s: Ignoring network entries for subprofile %s^%s. There is currently no kernel Netdomain support for changehat.\n",
						__FUNCTION__,
						parent->name, profile->name);
			}
		} /* while */
		if (!(c = sd_read_listend(c, e))) goto out;
		count_net_entries(profile);
	}
	rulename = "";

	/* get subprofiles */
	if (sd_is_X_name(c, e, SD_LIST, "hats", 0)) {
		error_string = "Invalid profile hat";
		if (!(c = sd_read_listhead(c, e, "hats"))) goto out;
		count = 0;
		
		while (!sd_is_X(c, e, SD_LISTEND, 0)) {
			c = sd_activate_profile(c, e, profile, &subprofile, error);
			if (!subprofile) goto out;
			get_sdprofile(subprofile);
			list_add(&subprofile->list, &profile->sub);
		} /* while */
		if (!(c = sd_read_listend(c, e))) goto out;
	}

	error_string = "Invalid end of profile";
	if (!(c = sd_read_structend(c, e))) goto out;

	*r_profile = profile;
	return c;
	
 out:
	if (profile) {
		free_sdprofile(profile);
		profile = NULL;
	}

	*r_profile = NULL;
	if (name) {
		SD_WARN("%s: %s %s in profile %s\n", INTERFACE_ID, rulename, error_string, name);
	} else {
		SD_WARN("%s: %s\n", INTERFACE_ID, error_string);
	}
	return c;

}

void *
sd_activate_top_profile(void *p, sd_ext *e,
		    struct sdprofile **r_profile, ssize_t *error)
{
	void *c = p;
	
	/* get the interface version */
	if (!(c = sd_read32_t(c, e, &e->version, "version"))) {
		SD_WARN("%s: version missing\n", INTERFACE_ID);
		*error = -EPROTONOSUPPORT;
		*r_profile = NULL;
		goto out;
	}

	/* check that the interface version is currently supported*/
	if (e->version != 2) {
		SD_WARN("%s: unsupported interface version (%d)\n",
			INTERFACE_ID, e->version);
		*error = -EPROTONOSUPPORT;
		*r_profile = NULL;
		return 0;
	}

	c = sd_activate_profile(c, e, NULL, r_profile, error);
 out:
	return c;
}

ssize_t
sd_file_prof_add(void *data, size_t size)
{
	struct sdprofile *profile = NULL, *old_profile = NULL;
	
	sd_ext e = {data, data+size};
	void *c = data;
	ssize_t error;

	c = sd_activate_top_profile (c, &e, &profile, &error);
	if (!profile) {
		SD_DEBUG("could'nt activate profile\n");
		return error;

	}

	old_profile = sd_profilelist_find(profile->name);

	if (old_profile) {
		SD_WARN("%s: trying to add profile (%s) that "
			"already exists.\n", __FUNCTION__, profile->name);
		put_sdprofile(old_profile);
		free_sdprofile(profile);
		return -EEXIST;
	}

	sd_profilelist_add(profile);
	/* XXX */
	return size;
}       


ssize_t
sd_file_prof_repl (void *udata, size_t size)
{
	struct sd_taskreplace_data data;
	sd_ext e = {udata, udata+size};
	void *c = udata;
	ssize_t error;

	sd_activate_top_profile (c, &e, &data.new_profile, &error);	
	if (!data.new_profile) {
		SD_DEBUG("could'nt activate profile\n");
		return error;
	}

	 
	// Grab reference to close race window (see comment below)
	get_sdprofile(data.new_profile);

	/* Replace the profile on the global profile list.
	 * This list is used by all new exec's to find the correct profile.
	 * If there was a previous profile, it is returned, else NULL.
	 *
	 * N.B sd_profilelist_replace does not drop the refcnt on 
	 * old_profile when removing it from the global list, otherwise it 
	 * could reach zero and be automatically free'd. We nust manually 
	 * drop it at the end of this function when we are finished with it.
	 */
	data.old_profile = sd_profilelist_replace(data.new_profile);


	/* RACE window here.
	 * At this point another task could preempt us trying to replace
	 * the SAME profile. If it makes it to this point,  it has removed
	 * the original tasks new_profile from the global list and holds a 
	 * reference of 1 to it in it's old_profile.  If the new task 
	 * reaches the end of the function it will put old_profile causing 
	 * the profile to be deleted.
	 * When the original task is rescheduled it will continue calling
	 * sd_subdomainlist_iterate relabelling tasks with a profile 
	 * which points to free'd memory. 
	 */


	/* If there was an old profile,  find all currently executing tasks
	 * using this profile and replace the old profile with the new.
	 */
	if (data.old_profile) {
		SD_DEBUG("%s: try to replace profile (%p)%s\n", 
			__FUNCTION__,
			data.old_profile, 
			data.old_profile->name);

		sd_subdomainlist_iterate(taskreplace_iter, (void*)&data);

		/* mark old profile as stale */
		data.old_profile->isstale=1;

		/* it's off global list, and we are done replacing */
		put_sdprofile(data.old_profile);
	} 

	/* Free reference obtained above */
	put_sdprofile(data.new_profile);

	return size;
}


ssize_t
sd_file_prof_remove (const char *name, size_t size)
{
	struct sdprofile *old_profile;

	/* Do this step to get a guaranteed reference to profile
	 * as sd_profilelist_remove may drop it to zero which would
	 * made subsequent attempt to iterate using it unsafe
	 */
	old_profile=sd_profilelist_find(name);

	if (old_profile){
		if (sd_profilelist_remove(name) != 0) {
			SD_WARN("%s: race trying to remove profile (%s)\n",
				__FUNCTION__, name) ;
		}

		/* remove profile from any tasks using it */
		sd_subdomainlist_iterateremove(taskremove_iter, (void*)old_profile);

		/* drop reference obtained by sd_profilelist_find */
		put_sdprofile(old_profile);
	}else{
		SD_WARN("%s: trying to remove profile (%s) that "
			"doesn't exist - skipping.\n",
			__FUNCTION__, name ) ;
		return -ENOENT;
	}

	/* XXX */
	return size;
}       


ssize_t
sd_file_prof_debug(void *data, size_t size)
{
	struct sdprofile *profile = NULL;

	sd_ext e = {data, data+size};
	void *c = data;
	ssize_t error;

	c = sd_activate_top_profile (c, &e, &profile, &error);
	if (!profile) {
		return error;

	}

	sd_profile_dump(profile);
	
	free_sdprofile(profile);

	sd_profilelist_dump();
	return size;

}       

