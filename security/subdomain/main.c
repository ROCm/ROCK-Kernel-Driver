/*
 * Immunix SubDomain Core
 * 
 * Copyright (C) 2002 WireX Communications, Inc
 *
 * Ported from 2.2 by Chris Wright <chris@wirex.com>
 *
 * Copyright (C) 2003-2004 Immunix, Inc
 *
 * Ported to 2.6 by Tony Jones <tony@immunix.com>
 */

#include <linux/version.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/sched.h>
#include <linux/slab.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <linux/namei.h>
#endif
#include <linux/namespace.h>
#include <linux/string.h>

#include "subdomain.h"
#include "immunix.h"

#include "inline.h"

#include "pcre_exec.h"

/* temp define for syslog workaround */
#define SYSLOG_TEMPFIX

/***************************
 * PRIVATE UTILITY FUNCTIONS
 **************************/

#ifdef SYSLOG_TEMPFIX
/* Horrible hack
 * Syslog (not syslog-ng) has a bug where it escapes uneven numbers of %
 * symbols.  So we force an enen number here by escaping.
 * This code will GO AWAY once syslog bug is fixed
 */
static __INLINE__ 
const char* _escape_percent(const char *name)
{
int count = 0, len = 0;
const char *sptr;
char *dptr, *newname = (char*)name;

	sptr = name;
	while (*sptr){
		if (*sptr == '%')
			count++;

		len++;
		sptr++;
	}

	if (count){
		newname=kmalloc(len+count+1, GFP_KERNEL);
		if (newname){
			sptr = name;
			dptr = newname;

			while (*sptr){
				if (*sptr =='%'){
					*dptr++ = '%';
				}

				*dptr++ = *sptr++;
			}
			*dptr = 0;
		}
	}
					
	return (const char*)newname;
}		
#endif // SYSLOG_TEMPFIX

/* Linux kernel 2.6.10+ doesn't require the PRINTK_FIX */
#if defined (PRINTK_TEMPFIX) && LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10) &&\
            (defined(CONFIG_SMP) || defined(CONFIG_PREEMPT))
/* A local buffer to get around printk in sys_setscheduler problems */
#define __LOG_BUF_LEN 4096

static char __log_buf[__LOG_BUF_LEN];
static volatile int log_start = 0;
static volatile int log_end = 0;
static volatile int log_dumping = 0;
static volatile int log_overflow = 0;
/* this is used for fast testing in LSM.c */
volatile int sd_log_buf_has_data = 0;

#define LOG_MASK(idx) ((idx) & (__LOG_BUF_LEN - 1))
#define LOG_BUF(idx) (__log_buf[(idx)])

static spinlock_t logbuf_lock = SPIN_LOCK_UNLOCKED;

void dump_sdprintk()
{
	/* check to see if there is any data and  */
	unsigned long flags;
	int start;
	int end;
	int overflow;

	spin_lock_irqsave(&logbuf_lock, flags);
	start = log_start;
	end = log_end;
	if (start == end || log_dumping) goto done;
	/* buffer still has data but other CPUs don't need to try dumping it */
	sd_log_buf_has_data = 0;
	log_dumping = 1;
	overflow = log_overflow;
	spin_unlock_irqrestore(&logbuf_lock, flags);

	/* printk can not be called with the logbuf_lock held, on systems with
	 * the printk scheduler locking bug (sd_printk isn't needed on systems
	 * without this bug).
         * IF dump_sdprintk held the logbuf_lock around the printk, then there
	 * would be a race condition that could deadlock on SMP systems.
         * 1. CPU1 dump_sdprintk takes the logbuf_lock
         * 2. CPU2 another cpu calls set_priority or set_scheduler taking the
	 *    scheduler lock the task is confined and subdomain goes to
	 *    generate a reject message thus attempting to take the logbuf_lock
	 *    (spins)
	 * 3. CPU1 printk finish and invokes the scheduler which tries to take
	 *    the scheduler lock (spins)
         * 4. Deadlock
	 */

	if (start < end) {
		printk("%.*s", end-start, &__log_buf[start]);
	} else {
		/* wrapped around */
		printk("%.*s%.*s", __LOG_BUF_LEN-start, &__log_buf[start],
		       end, __log_buf);
	}
	if (overflow) {
		SD_WARN("Overflow in sd_printk buffer\n");
	}

	spin_lock_irqsave(&logbuf_lock, flags);
	if (overflow) {
		log_overflow = 0;
	}
	log_start = end;
	log_dumping = 0;
	/* its possible data was inserted in the buffer while dumping
	 * we don't loop or even check for this condition to guarentee
	 * we exit.  Any data that got added will be dumped later 
	 */
 done:
	spin_unlock_irqrestore(&logbuf_lock, flags);
}

static asmlinkage int sd_printk(const char *fmt, ...)
{
	va_list args;
	unsigned long flags;
	int printed_len;
	static char printk_buf[512];
	char *p = printk_buf;
	int start;
	int end;
	int i;

	/* emit to temporary buffer */
	va_start(args, fmt);
	printed_len = vsnprintf(printk_buf, sizeof(printk_buf), fmt, args);
	if (printed_len > sizeof(printk_buf)) printed_len = sizeof(printk_buf);
	va_end(args);

	spin_lock_irqsave(&logbuf_lock, flags);

	start = log_start;
	end = log_end;

	/* copy output into sd's log_buf */
	for ( i=0; i<printed_len ; i++) {
		LOG_BUF(end) = *p;
		end = LOG_MASK(end+1);
		p++;
		if (end == start) {
			log_overflow = 1;
			end = LOG_MASK(end-1);
			i--;
			break;
		}
	}
	log_end = end;
	sd_log_buf_has_data = 1;

	spin_unlock_irqrestore(&logbuf_lock, flags);

	return i;
}
#endif /* PRINTK_TEMPFIX */

static __INLINE__ 
const char* sd_getpattern_type(pattern_t ptype)
{
const char *ptype_names[] = {
	"ePatternBasic",
	"ePatternTailGlob",
	"ePatternRegex",
	"ePatternInvalid"
	};

	if (ptype >= ePatternInvalid){
		ptype = ePatternInvalid;
	}

	return ptype_names[ptype];
}

/*
 * sd_taskattr_access:
 * @name: name of file to check permission
 * @mask: permission mask requested for file
 *
 * Determine if request is for write access to /proc/self/attr/current
 */
static __INLINE__ 
int sd_taskattr_access(const char *procrelname)
{
/* 
 * assumes a 32bit pid, which requires max 10 decimal digits to represent
 * sizeof includes trailing \0
 */
char buf[sizeof("/attr/current") + 10];
const int maxbuflen=sizeof(buf);

	snprintf(buf, maxbuflen, "%d/attr/current", current->pid);
	buf[maxbuflen-1] = 0;

	return strcmp(buf, procrelname) == 0;
}

/**
 * sd_pattern_match - check if pathnames matches regex
 * @name: name from sd_entry
 * @pathname: path requested to serach for
 *
 * This compares two pathnames and accounts for globbing name from the
 * sd_entry.  Returns 1 on match, 0 otherwise.
 */
static __INLINE__ 
int sd_pattern_match(const char *regex, pcre *compiled, const char *name)
{
int pcreret;
int retval;

	pcreret = pcre_exec(compiled, NULL, name, strlen(name), 0, 0, NULL, 0);

	retval = (pcreret >= 0);

	SD_DEBUG("%s(%d): %s %s %d\n", __FUNCTION__, retval, name, regex, pcreret);

	return retval;
}


/**
 * sd_path_match - check if pathnames match
 * @name: name from sd_entry
 * @pathname: path requested to serach for
 *
 * This compares two pathnames and accounts for globbing name from the
 * sd_entry.  Returns 1 on match, 0 otherwise.
 */
static __INLINE__ 
int sd_path_match(const char *name, const char *pathname, pattern_t ptype)
{
	int retval;

	/* basic pattern,  no regular expression characters */
	if (ptype == ePatternBasic){
		retval = (strcmp(name, pathname) == 0);

	/* trailing glob pattern */
	}else if (ptype == ePatternTailGlob){
		retval = (strncmp(name, pathname, strlen(name) - 2) == 0);
	}else{
		SD_WARN("%s: Invalid pattern_t %d\n", 
			__FUNCTION__, ptype);
		retval=0;
	}

	SD_DEBUG("%s(%d): %s %s [%s]\n", 
		__FUNCTION__, retval, name, pathname,
		sd_getpattern_type(ptype));

	return retval;
}

/**
 * sd_file_mode - get full mode for file entry from profile
 * @name: filename
 * @profile: profile
 */
static __INLINE__ 
int sd_file_mode(const char *name, struct sdprofile *profile)
{
	struct sd_entry *entry;
	int mode = 0;

	SD_DEBUG("%s: %s\n", __FUNCTION__, name);
	if (!name) {
		SD_DEBUG("%s: no name\n", __FUNCTION__);
		goto out;
	}

	if (!profile) {
		SD_DEBUG("%s: no profile\n", __FUNCTION__);
		goto out;
	}
	for (entry = profile->file_entry; entry; entry = entry->next) {
		if ((entry->pattern_type == ePatternRegex && 
		     sd_pattern_match(entry->regex, entry->compiled, name)) || 
		    (entry->pattern_type != ePatternRegex && 
		     sd_path_match(entry->filename, name, entry->pattern_type))) {
			mode |= entry->mode;
		}
	}
out:
	return mode;
}

/**
 * find_mnt - find first possible mnt that dentry could be mounted on
 * @d: dentry to use for lookup
 *
 * This returns a refcounted vfsmount that matches via looking at
 * all mountpoints and returning the first match.  NULL on no match.
 */
static __INLINE__
struct vfsmount *find_mnt(struct dentry *d)
{
	struct sd_path_data data;
	struct vfsmount *mnt = NULL;

	sd_path_begin(d, &data);
	mnt=sd_path_getmnt(&data);
	(void)sd_path_end(&data);

	return mnt;
}

/*
 * sd_get_execmode - calculate what qualifier to apply to an exec
 * @name: name of file to exec
 * @profile: profile to search
 * @xbits: pointer to a execution mode bit for the rule that was matched
 *         if the rule has no execuition qualifier {pui} then
 *         KERN_COD_MAY_EXEC is returned indicating a naked x
 *         if the has an exec qualifier then only the qualifier bit {pui}
 *         is returned (KERN_COD_MAY_EXEC) is not set.
 *
 * Returns FALSE:
 *    if unable to find profile or there are conflicting regular expressions.  
 *       *xmod - is not modified
 *
 * Returns TRUE:
 *    if not confined
 *       *xmod = KERN_COD_MAY_EXEC
 *    if exec rule matched
 *       if the rule has an execution mode qualifier {pui} then
 *          *xmod = the execution qualifier of the rule {pui}
 *       else
 *          *xmod = KERN_COD_MAY_EXEC
 */
static __INLINE__
int sd_get_execmode(const char *name, struct subdomain *sd, int *xmod)
{
	struct sdprofile *profile;
	struct sd_entry *entry, *match = NULL;

	int regexp_match_invalid = FALSE,
	    rc = FALSE;

	/* not confined */
	if (!__sd_is_confined(sd)){
		SD_DEBUG("%s: not confined\n", __FUNCTION__);
		goto not_confined;
	}

	profile = sd->active;
	
	/* search list of profiles with 'x' permission
	 * this will also include entries with 'p', 'u' and 'i' 
	 * qualifiers.
	 *
	 * If we find a regexp match we will keep looking for an exact match
	 * If we find conflicting regexp matches we will flag (while still
	 * looking for an exact match).  If all we have is a conflict, FALSE
	 * is returned.
	 */

	entry = profile->file_entryp[POS_KERN_COD_MAY_EXEC];

	while (entry){
		if (!regexp_match_invalid &&
		    entry->pattern_type == ePatternRegex && 
		    sd_pattern_match(entry->regex, entry->compiled, name)){
			
			if (match && 
			    SD_EXEC_MASK(entry->mode) != SD_EXEC_MASK(match->mode)){
				regexp_match_invalid = TRUE;
			}else{
				/* got a regexp match, keep searching for an
				 * exact match
				 */
				match = entry;
			}
		}else if ((entry->pattern_type == ePatternBasic ||
			  (!regexp_match_invalid &&
			   entry->pattern_type == ePatternTailGlob)) &&
		     	  sd_path_match(entry->filename, name, entry->pattern_type)) {
			if (entry->pattern_type == ePatternBasic){
				/* got an exact match -- there can be only
				 * one, asserted a profile load time 
				 */
				match = entry;
				regexp_match_invalid = FALSE;
				break;
			}else{ /* entry->pattern_type == ePatternTailGlob */
				if (match &&
				    SD_EXEC_MASK(entry->mode) != SD_EXEC_MASK(match->mode)){
					regexp_match_invalid = TRUE;
				}else{
					/* got a tailglob match, keep searching
					 * for an exact match
					 */
					match = entry;
				}
			}
		}

		entry = entry->nextp[POS_KERN_COD_MAY_EXEC];
	}

	rc = match && !regexp_match_invalid;

	if (rc){
		int mode = SD_EXEC_MASK(match->mode);

		/* check for qualifiers, if present 
		 * we just return the qualifier
		 */
		if (mode & ~KERN_COD_MAY_EXEC){
			mode = mode & ~KERN_COD_MAY_EXEC;
		}

		*xmod = mode;
	}else if (!match){
		SD_DEBUG("%s: Unable to find execute entry in profile for image '%s'\n",
			__FUNCTION__,
			name);
	}else if (regexp_match_invalid){
		SD_WARN("%s: Inconsistency in profile %s. Two (or more) regular expressions specify conflicting exec qualifiers ('u', 'i' or 'p') for image %s\n",
			__FUNCTION__,
			sd->active->name,
			name);
	}

	return rc;

not_confined:
	*xmod = KERN_COD_MAY_EXEC;
	return TRUE;
}


/****************************
 * INTERNAL TRACING FUNCTIONS
 ***************************/

/**
 * sd_attr_trace - trace attempt to change file attributes
 * @name: file requested
 * @sd: current subdomain
 * @iattr: requested new modes
 * @error: error flag
 *
 * Prints out the status of the attribute change request.  Only prints
 * accepted when in audit mode.
 */
static __INLINE__
void sd_attr_trace(const char *name, struct subdomain *sd, struct iattr *iattr, int error)
{
	char *status = "AUDITING";
#ifdef SYSLOG_TEMPFIX
	const char *newname = _escape_percent(name);
#endif

	if (error){
		status = SUBDOMAIN_COMPLAIN(sd) ? "PERMITTING" : "REJECTING";
	}else if (!SUBDOMAIN_AUDIT(sd)){
		return;
	}

#ifdef SYSLOG_TEMPFIX
	SD_WARN("%s%s attribute (%s%s%s%s%s%s%s) change to %s (%s(%d) profile %s active %s)\n",
		status,
		newname != name ? "-SYSLOGFIX" : "",
		iattr->ia_valid & ATTR_MODE ? "mode," : "",
		iattr->ia_valid & ATTR_UID ? "uid," : "",
		iattr->ia_valid & ATTR_GID ? "gid," : "",
		iattr->ia_valid & ATTR_SIZE ? "size," : "",
		((iattr->ia_valid & ATTR_ATIME_SET) ||
		 (iattr->ia_valid & ATTR_ATIME)) ? "atime," : "",
		((iattr->ia_valid & ATTR_MTIME_SET) ||
		 (iattr->ia_valid & ATTR_MTIME)) ? "mtime," : "",
		iattr->ia_valid & ATTR_CTIME ? "ctime," : "",
		newname ? newname : "KMALLOC-ERROR",
		current->comm, current->pid,
		sd->profile->name, sd->active->name);

	if (newname != name)
		kfree(newname);
#else
	SD_WARN("%s attribute (%s%s%s%s%s%s%s) change to %s (%s(%d) profile %s active %s)\n",
		status,
		iattr->ia_valid & ATTR_MODE ? "mode," : "",
		iattr->ia_valid & ATTR_UID ? "uid," : "",
		iattr->ia_valid & ATTR_GID ? "gid," : "",
		iattr->ia_valid & ATTR_SIZE ? "size," : "",
		((iattr->ia_valid & ATTR_ATIME_SET) ||
		 (iattr->ia_valid & ATTR_ATIME)) ? "atime," : "",
		((iattr->ia_valid & ATTR_MTIME_SET) ||
		 (iattr->ia_valid & ATTR_MTIME)) ? "mtime," : "",
		iattr->ia_valid & ATTR_CTIME ? "ctime," : "",
		name, current->comm, current->pid,
		sd->profile->name, sd->active->name);
#endif
}

/**
 * sd_perm_trace - trace permission
 * @name: file requested
 * @sd: current subdomain
 * @mask: requested permission
 * @error: error flag
 *
 * Prints out the status of the permission request.  Only prints
 * accepted when in audit mode.
 */
static __INLINE__
void sd_perm_trace(const char *name, struct subdomain *sd, int mask, int error)
{
	char *status = "AUDITING";
#ifdef SYSLOG_TEMPFIX
	const char *newname = _escape_percent(name);
#endif

	if (error){
		status = SUBDOMAIN_COMPLAIN(sd) ? "PERMITTING" : "REJECTING";
	}else if (!SUBDOMAIN_AUDIT(sd)){
		return;
	}

#ifdef SYSLOG_TEMPFIX
	SD_WARN("%s%s %s%s%s%s access to %s (%s(%d) profile %s active %s)\n",
		status,
		newname != name ? "-SYSLOGFIX" : "",
		mask & KERN_COD_MAY_READ  ? "r": "" ,
		mask & KERN_COD_MAY_WRITE ? "w": "" ,
		mask & KERN_COD_MAY_EXEC  ? "x": "" ,
		mask & KERN_COD_MAY_LINK  ? "l": "" ,
		newname ? newname : "KMALLOC-ERROR",
		current->comm, current->pid,
		sd->profile->name, sd->active->name);

	if (newname != name)
		kfree(newname);
#else
	SD_WARN("%s %s%s%s%s access to %s (%s(%d) profile %s active %s)\n",
		status,
		mask & KERN_COD_MAY_READ  ? "r": "" ,
		mask & KERN_COD_MAY_WRITE ? "w": "" ,
		mask & KERN_COD_MAY_EXEC  ? "x": "" ,
		mask & KERN_COD_MAY_LINK  ? "l": "" ,
		name, current->comm, current->pid,
		sd->profile->name, sd->active->name);
#endif
}

/**
 * sd_link_perm_trace - trace link permission
 * @lname: name requested as new link
 * @tname: name requested as new link's target
 * @sd: current SubDomain
 * @error: error status
 *
 * Prints out the status of the permission request.  Only prints
 * accepted when in audit mode.
 */
static __INLINE__
void sd_link_perm_trace(const char *lname, const char *tname, 
					struct subdomain *sd,  int error)
{
	char *status = "AUDITING";
#ifdef SYSLOG_TEMPFIX
	const char *newlname = _escape_percent(lname),
	     	   *newtname = _escape_percent(tname);
#endif

	if (error){
		status = SUBDOMAIN_COMPLAIN(sd) ? "PERMITTING" : "REJECTING";
	}else if (!SUBDOMAIN_AUDIT(sd)){
		return;
	}

#ifdef SYSLOG_TEMPFIX
	SD_WARN("%s%s link access from %s to %s (%s(%d) profile %s active %s)\n",
		status, 
		newlname != lname || newtname != tname ? "-SYSLOGFIX" : "",
		newlname ? newlname : "KMALLOC-ERROR",
		newtname ? newtname : "KMALLOC-ERROR",
		current->comm, current->pid,
		sd->profile->name, sd->active->name);

	if (newlname != lname)
		kfree(newlname);

	if (newtname != tname)
		kfree(newtname);
#else
	SD_WARN("%s link access from %s to %s (%s(%d) profile %s active %s)\n",
		status, lname, tname, current->comm, current->pid,
		sd->profile->name, sd->active->name);
#endif
}


/*************************
 * MAIN INTERNAL FUNCTIONS
 ************************/

/**
 * sd_link_perm - test permission to link to a file
 * @link: name of link being created
 * @target: name of target to be linked to
 * @sd: current SubDomain
 * 
 * Look up permission mode on both @link and @target.  @link must have same
 * permission mode as @target.  At least @link must have the link bit enabled.
 * Return 0 on success, error otherwise.
 */
static int 
sd_link_perm(const char *link, const char *target, struct subdomain *sd)
{
	int l_mode, t_mode, error = -EPERM;
	struct sdprofile *profile = sd->active;

	error = -EPERM;
	l_mode = sd_file_mode(link, profile);
	if (!(l_mode & KERN_COD_MAY_LINK))
		goto out;
	/* ok, mask off link bit */
	l_mode &= ~KERN_COD_MAY_LINK;

	t_mode = sd_file_mode(target, profile);
	t_mode &= ~KERN_COD_MAY_LINK;

	if (l_mode == t_mode)
		error = 0;

out:
	sd_link_perm_trace(link, target, sd, error);
	if (SUBDOMAIN_COMPLAIN(sd)){
		error = 0;
	}
	return error;
}

/**************************
 * GLOBAL UTILITY FUNCTIONS
 *************************/

/**
 * __sd_get_name - retrieve fully qualified path name
 * @dentry: relative path element
 * @mnt: where in tree
 *
 * Returns fully qualified path name on sucess, NULL on failure.
 * sd_put_name must be used to free allocated buffer.
 */
char * __sd_get_name(struct dentry *dentry, struct vfsmount *mnt)
{
	char *page, *name = NULL;

	page = (char *)__get_free_page(GFP_KERNEL);
	if (!page)
		goto out;

	name = d_path(dentry, mnt, page, PAGE_SIZE);
	SD_DEBUG("%s: full_path=%s\n", __FUNCTION__, name);
out:
	return name;
}


/***********************************
 * GLOBAL PERMISSION CHECK FUNCTIONS
 ***********************************/

/*
 * sd_file_perm - calculate access mode for file
 * @name: name of file to calculate mode for
 * @profile: profile to search
 * @mask: permission mask requested for file
 *
 * Search the sd_entry list in @profile.  
 * Search looking to verify all permissions passed in mask.
 * Perform the search by looking at the partitioned list of entries, one 
 * partition per permission bit.
 * Return 0 on access allowed, < 0 on error.
 */
int sd_file_perm(const char *name, struct subdomain *sd, int mask, int log)
{
	struct sd_entry *entry;
	struct sdprofile *profile;
	int i, error, mode;

#define PROCPFX "/proc/"
#define PROCLEN sizeof(PROCPFX) - 1

	SD_DEBUG("%s: %s 0x%x\n", __FUNCTION__, name, mask);

	error = 0;

	// should not enter with other than R/W/X/L
	BUG_ON(mask & ~(KERN_COD_MAY_READ|KERN_COD_MAY_WRITE|KERN_COD_MAY_EXEC|KERN_COD_MAY_LINK));

	/* not confined */
	if (!__sd_is_confined(sd)){
		/* exit with access allowed */
		SD_DEBUG("%s: not confined\n", __FUNCTION__);
		goto done_notrace;
	}

	/* 
	 * Ugh :-(
	 * Special case access to /proc/self/attr/current
	 *
	 * Alternative would be to add /proc/self/attr/current to each
	 * profile whenever a profile is loaded but issue is complicated by
	 * the statically shared null-profile.
	 */
	if (strncmp(PROCPFX, name, PROCLEN) == 0 && (!list_empty(&sd->profile->sub) ||  SUBDOMAIN_COMPLAIN(sd))){
		if (mask == MAY_WRITE && sd_taskattr_access(name+PROCLEN)){
			log=0;
			goto done;
		}
	}

	error = -EACCES;

	profile = sd->active;

	mode = 0;

	/* iterate over partition, one permission bit at a time */
	for (i=0; i<=POS_KERN_COD_FILE_MAX; i++){

		/* do we have to accumulate this bit? 
		 * or have we already accumulated it (shortcut below)? */
		if (!(mask & (1<<i)) || mode & (1<<i)){
			continue;
		}


		for (entry = profile->file_entryp[i]; entry; entry = entry->nextp[i]) {
			if ((entry->pattern_type == ePatternRegex && 
		     		sd_pattern_match(entry->regex, entry->compiled, name)) || 
		    	    (entry->pattern_type != ePatternRegex && 
		     		sd_path_match(entry->filename, name, entry->pattern_type))) {
				/* 
				 * even though we are searching each bit 
				 * partition at a time, accumulate all bits
				 * present at this entry as a shortcut 
				 */
				mode |= entry->mode;

				/* 
			 	* mask bits are overloaded.
				* values 1, 2, 4 and 8 (MAY_EXEC, MAY_WRITE, 
				* MAY_READ and MAY_APPEND) are used by entire 
				* kernel, other values are local.
				*
				* lsm.c (external interface into subdomain --
				* subdomain_file_permission) masks kernel
				* provided masks with 
				* MAY_EXEC|MAY_WRITE|MAY_READ 
			 	* before calling this function to prevent 
				* external visibility of these subdomain 
				* extentions.
				*
				* Subdomain internally calls into this function
				* with bits other than the above.
			 	*/
				if ((mode & mask) == mask) {
					SD_DEBUG("MATCH! %s=0x%x [total mode=0x%x]\n", 
						name, mask, mode);
						
					error=0;
					goto done;
				}
			}
		}
	}

done:
	if (log){
		sd_perm_trace(name, sd, mask, error);

		if (SUBDOMAIN_COMPLAIN(sd)){
			error = 0;
		}
	}

done_notrace:
	return error;
}

/**
 * sd_attr - check whether attribute change allowed
 * @dentry: file to check
 * @sd: current subdomain to check against
 * @mask: access mode requested
 *
 * This function is a replica of sd_perm. In fact, calling sd_perm(MAY_WRITE)
 * will achieve the same access control, but logging would appear to indicate
 * success/failure of a "w" rather than an attribute change.  Also, this way we
 * can log a single message indicating success/failure and also what attribite 
 * changes were attempted.  
 */
int sd_attr(struct dentry *dentry, struct subdomain *sd, struct iattr *iattr)
{
	int error = 0, sdpath_error;
	struct sd_path_data data;
	char *name;

	/* if not confined or empty mask permission granted */
	if (!__sd_is_confined(sd) || !iattr)
		goto out;


	/* search all paths to dentry */

	sd_path_begin(dentry, &data);
	do{
		name = sd_path_getname(&data);
		if (name){
			error = sd_file_perm(name, sd, MAY_WRITE, FALSE);
			
			/* access via any path is enough */
			if (error){
				sd_attr_trace(name, sd, iattr, error);
			}

			sd_put_name(name);

			if (!error){
				break;
			}	
		}

	}while (name);
	
	if ((sdpath_error = sd_path_end(&data)) != 0){
		SD_ERROR("%s: An error occured while translating dentry %p inode# %lu to a pathname. Error %d\n",
			 __FUNCTION__,
			dentry,
			dentry->d_inode->i_ino,
			sdpath_error);

		error = sdpath_error;
	}
	
	if (SUBDOMAIN_COMPLAIN(sd)){
		error = 0;
	}

out:
	return error;
}

/**
 * sd_perm - basic SubDomain permissions check
 * @inode: file to check
 * @sd: current subdomain to check against
 * @mask: access mode requested
 *
 * This checks that the @inode is in the current subdomain, @sd, and
 * that it can be accessed in the mode requested by @mask.  Returns 0 on
 * success.
 */
int sd_perm(struct inode *inode, struct subdomain *sd, struct nameidata *nd,
		int mask)
{
	char *name = NULL;
	int error = 0;

	if (!nd){
		goto out;
	}

	/* if not confined or empty mask permission granted */
	if (!__sd_is_confined(sd) || !mask)
		goto out;
	/* we don't care about MAY_APPEND */
	mask &= ~MAY_APPEND;

	/* we only require MAY_WRITE for write to a dir */
	if (nd->dentry->d_inode && S_ISDIR(nd->dentry->d_inode->i_mode))
		if ((mask & (MAY_EXEC|MAY_WRITE)) == mask)
			//mask = MAY_WRITE;
			goto out;

	name = __sd_get_name(nd->dentry, nd->mnt);
	if (!name) {
		error = -ENOMEM;
	}else{
		error = sd_file_perm(name, sd, mask, TRUE);
		sd_put_name(name);
	}
out:
	return error;
}

int sd_perm_dentry(struct dentry *dentry, struct subdomain *sd, int mask)
{
        char *name;
	struct sd_path_data data;
        int error = 0, sdpath_error;

        /* if not confined or empty mask permission granted */
        if (!__sd_is_confined(sd) || !mask)
                goto out;

        /* we don't care about MAY_APPEND */
        mask &= ~MAY_APPEND;

        /* we only require MAY_WRITE for write to a dir */
        if (dentry->d_inode && S_ISDIR(dentry->d_inode->i_mode))
                if ((mask & (MAY_EXEC|MAY_WRITE)) == mask)
                        //mask = MAY_WRITE;
                        goto out;

        /* search all paths to dentry */

	sd_path_begin(dentry, &data);
	do{
		name = sd_path_getname(&data);
		if (name){
			error = sd_file_perm(name, sd, mask, TRUE);
			sd_put_name(name);

			/* access via any path is enough */
			if (!error)
				break;
		}
	}while (name);

	if ((sdpath_error = sd_path_end(&data)) != 0){
		SD_ERROR("%s: An error occured while translating dentry %p inode# %lu to a pathname. Error %d\n",
			__FUNCTION__,
			dentry,
			dentry->d_inode->i_ino,
			sdpath_error);

		error = sdpath_error;
	}

out:
        return error;
}

/**
 * sd_capability - test permission to use capability
 * @cap: capability to be tested
 * 
 * Look up capability in active profile capability set.
 * Return 0 if valid, -EPERM if invalid
 */

int sd_capability(int cap, struct subdomain *sd)
{
int error = 0;

	if (__sd_is_confined(sd)){
		char *status;

		status = SUBDOMAIN_COMPLAIN(sd) ? "PERMITTING" : "REJECTING";

		error = cap_raised(sd->active->capabilities, cap) ? 0 : -EPERM;

		if (error || SUBDOMAIN_AUDIT(sd)){
			if (error == 0){ /* AUDIT */
				status = "AUDITING";
			}

#if defined (PRINTK_TEMPFIX) && LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10) &&\
            (defined(CONFIG_SMP) || defined(CONFIG_PREEMPT))
			/* ugly hack to work around a SMP scheduler bug,
			 * related to calling printk from sys_setscheduler */
			if (cap == CAP_SYS_NICE) 
				sd_printk(KERN_WARNING "SubDomain: %s access to capability '%s' (%s(%d) profile %s active %s)\n",
					  status,
					  capability_to_name(cap),
					  current->comm, current->pid,
					  sd->profile->name, sd->active->name);
			else 
#endif
				SD_WARN("%s access to capability '%s' (%s(%d) profile %s active %s)\n",
					status,
					capability_to_name(cap),
					current->comm, current->pid,
					sd->profile->name, sd->active->name);
			if (SUBDOMAIN_COMPLAIN(sd)){
				error = 0;
			}
		}
	}

	return error;
}

/**
 * sd_link - hard link check
 * @link: dentry for link being created
 * @target: dentry for link target
 * @sd: SubDomain to check against
 *
 * Checks link permissions for all possible name combinations.  This is
 * particularly ugly.  Returns 0 on sucess, error otherwise.
 */
int sd_link(struct dentry *link, struct dentry *target, struct subdomain *sd)
{
	char *iname, *oname;
	struct sd_path_data idata, odata;
	int error = 0, sdpath_error, done;

	if (!__sd_is_confined(sd))
	       goto out;

	/* Perform nested lookup for names.
	 * This is necessary in the case where /dev/block is mounted 
	 * multiple times,  i.e /dev/block->/a and /dev/block->/b
	 * This allows us to detect links where src/dest are on different 
	 * mounts.   N.B no support yet for links across bind mounts of
	 * the form mount -bind /mnt/subpath /mnt2
	 */

	done=0;
	sd_path_begin2(target, link, &odata);
	do{
		oname = sd_path_getname(&odata);

		if (oname){
			sd_path_begin(target, &idata);
			do{
				iname = sd_path_getname(&idata);
				if (iname){
					error = sd_link_perm(oname, iname, sd); 
					sd_put_name(iname);
				
					/* access via any path is enough */
					if (!error){
						done=1;
					}
				}
			}while(!done && iname);

			if ((sdpath_error = sd_path_end(&idata)) != 0){
				SD_ERROR("%s: An error occured while translating inner dentry %p inode %lu to a pathname. Error %d\n",
					__FUNCTION__,
					target,
					target->d_inode->i_ino,
					sdpath_error);

				(void)sd_path_end(&odata);
				error = sdpath_error;
				goto out;
			}
			sd_put_name(oname);

		} // name

	}while(!done && oname);

	if ((sdpath_error = sd_path_end(&odata)) != 0){
		SD_ERROR("%s: An error occured while translating outer dentry %p inode %lu to a pathname. Error %d\n",
			__FUNCTION__,
			link,
			link->d_inode->i_ino,
			sdpath_error);

		error = sdpath_error;
	}

out:
	return error;
}

/**
 * sd_symlink - symlink check
 * @link: dentry for link being created
 * @name: name of target
 * @sd: SubDomain to check against
 *
 * This resolves the pathname for the target (and will fail if at least
 * the parent dir (`dirname name`) of the target doesn't exist.  Then it
 * cycles through all possible names for @link and checks link
 * permissions.  Returns 0 on success, error otherwise.
 */
int sd_symlink(struct dentry *link, const char *name, struct subdomain *sd)
{
	struct nameidata nd;
	struct dentry *target = NULL;
	char *lname, *tname;
	int error=0, sdpath_error;
	struct sd_path_data data;


	if (!__sd_is_confined(sd))
	       goto out;

	if (*name == '/')
		error = path_lookup(name, LOOKUP_PARENT, &nd);
	else {
		/* XXX need to resolve pathname relative to link
		 * this is ugly because we have the link dentry
		 * but not the link vfsmount.  for now we assume flat
		 * namespace.  code stolen from path_init.
		 */
		nd.last_type = LAST_ROOT; /* if there are only slashes... */
		nd.flags = LOOKUP_PARENT;
		nd.dentry = dget(link->d_parent); /* relative to link parent */
		nd.mnt = find_mnt(link); /* only finds the first mnt match */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,8)
		nd.depth = 0;
#endif
		error = path_walk(name, &nd);
	}
	if (error)
		goto out;

	target = lookup_hash(&nd.last, nd.dentry);
	if (IS_ERR(target)) {
		error = PTR_ERR(target);
		goto out_release;
	}

	error = -ENOMEM;

	tname = __sd_get_name(target, nd.mnt);
	if (!tname)
		goto out_dput;


	/* search all paths to dentry */

	sd_path_begin(link, &data);
	do{
		lname = sd_path_getname(&data);
		if (lname){
			error = sd_link_perm(lname, tname, sd);
			sd_put_name(lname);
			
			/* access via any path is enough */
			if (!error)
				break;
		}

	}while (lname);
	
	if ((sdpath_error = sd_path_end(&data)) != 0){
		SD_ERROR("%s: An error occured while translating dentry %p inode# %lu to a pathname. Error %d\n",
			 __FUNCTION__,
			link,
			link->d_inode->i_ino,
			sdpath_error);

		error = sdpath_error;
	}

	sd_put_name(tname);

out_dput:
	dput(target);

out_release:
	path_release(&nd);

out:
	return error;
	
}


/**********************************
 * GLOBAL PROCESS RELATED FUNCTIONS
 *********************************/

/**
 * sd_fork - create a new subdomain
 * @p: new process
 *
 * Create a new subdomain struct for the newly created process @p.
 * Copy parent info to child.  If parent has no subdomain, child
 * will get one with NULL values.  Return 0 on sucess.
 */

int sd_fork(struct task_struct *p)
{
	struct subdomain *sd = SD_SUBDOMAIN(current->security);
	struct subdomain *newsd = alloc_subdomain(p);

	SD_DEBUG("%s\n", __FUNCTION__);

	if (!newsd)
		return -ENOMEM;

	if (sd) {
		/* Can get away with a read rather than write lock here
		 * as we just allocated newsd above, so we can guarantee 
		 * that it's active/profile are null and therefore a replace
		 * cannot happen.
		 */
		SD_RLOCK;
		newsd->profile = get_sdprofile(sd->profile);
		newsd->active = get_sdprofile(sd->active);
		newsd->sd_hat_magic = sd->sd_hat_magic;

		if (SUBDOMAIN_COMPLAIN(sd) && newsd->profile && newsd->active) {
			SD_WARN("LOGPROF-HINT fork pid=%d child=%d profile=%s active=%s\n",
				current->pid, p->pid, newsd->profile->name, newsd->active->name);
		}

		SD_RUNLOCK;
	}
	p->security = newsd;
	return 0;
}

/**
 * sd_register - register a new program
 * @filp: file of program being registered
 *
 * Try to register a new program during execve().  This should give the
 * new program a valid SubDomain.
 *
 * This _used_ to be a really simple piece of code :-(
 *
 */
int sd_register(struct file *filp)
{
	char *filename;
	struct subdomain *sd, sdcopy;
	struct sdprofile *newprofile = NULL,
			 unconstrained_flag;
	int 	error = -ENOMEM,
		findprofile=0,
		findprofile_mandatory=0,
		issdcopy=1,
		complain=0;

	SD_DEBUG("%s\n", __FUNCTION__);

	sd=get_sdcopy(&sdcopy);

	/* Must have a SubDomain:
	 * XXX  tony 10/2003
	 * XXX  How is it possible to get here without a Subdomain?
	 */
	if (sd) {
		complain=SUBDOMAIN_COMPLAIN(sd);
	}else{
		issdcopy=0;

		sd = alloc_subdomain(current);
		if (!sd){
			SD_WARN("%s: Failed to allocate SubDomain\n", 
				__FUNCTION__);
			goto out;
		}

		current->security = sd;
	}

	filename = __sd_get_name(filp->f_dentry, filp->f_vfsmnt);
	if (!filename){
		SD_WARN("%s: Failed to get filename\n", 
			__FUNCTION__);
		goto out;
	}

	error = 0;

	/* determine what mode inherit, unconstrained or mandatory
	 * an image is to be loaded in
	 */
	if (__sd_is_confined(sd)){
		int exec_mode=0;

		if (sd_get_execmode(filename, sd, &exec_mode)){
			switch (exec_mode){
				case KERN_COD_EXEC_INHERIT:
					/* do nothing - setting of profile
					 * already handed in sd_fork
					 */
					SD_DEBUG("%s: INHERIT %s\n", 
						__FUNCTION__,
						filename);
					break;

				case KERN_COD_EXEC_UNCONSTRAINED:
					SD_DEBUG("%s: UNCONSTRAINED %s\n", 
						 __FUNCTION__,
						filename);

					/* unload profile */
					newprofile=&unconstrained_flag;
					break;

				case KERN_COD_EXEC_PROFILE:
					SD_DEBUG("%s: PROFILE %s\n", 
						 __FUNCTION__,
						filename);

					findprofile=1;
					findprofile_mandatory=1;
					break;
					
				case KERN_COD_MAY_EXEC:
					/* this should not happen, entries
					 * with just EXEC only should be 
					 * rejected at profile load time
					 */
					SD_ERROR("%s: Rejecting exec(2) of image '%s'. Mode KERN_COD_MAY_EXEC without exec qualifier is invalid (internal error) (%s(%d) profile %s active %s\n", 
						__FUNCTION__,
						filename,
						current->comm, current->pid,
						sd->profile->name, sd->active->name);
					error=-EPERM;
					break;

				default:
					SD_ERROR("%s: Rejecting exec(2) of image '%s'. Unknown exec qualifier %x (internal error) (%s (pid %d) profile %s active %s)\n", 
						__FUNCTION__,
						filename,
						exec_mode,
						current->comm, current->pid,
						sd->profile->name, sd->active->name);
					error=-EPERM;
					break;
			}


		}else{ /* !sd_get_execmode(filename, sd, &exec_mode) */

			if (complain) {
				/* There was no entry in calling profile 
				 * describing mode to execute image in.
				 * Drop into null-profile
				 */
				newprofile=get_sdprofile(null_complain_profile);
			} else {
				SD_WARN("%s: Rejecting exec(2) of image '%s', Unable to determine exec qualifier (%s (pid %d) profile %s active %s)\n", 
					__FUNCTION__,
					filename,
					current->comm, current->pid,
					sd->profile->name, sd->active->name);
				error = -EPERM;
			}
		}

	}else{ /* __sd_is_confined(sd) */

		/* unconfined task, load profile if it exists */
		findprofile=1;
	}



	/* mode has been determined,  try to locate profile if necessary */

find_profile:

	if (findprofile){
		newprofile = sd_profilelist_find(filename);
		if (newprofile) {
			SD_DEBUG("%s: setting profile %s\n", 
				 __FUNCTION__, newprofile->name);
		}else if (findprofile_mandatory){
			/* Profile (mandatory) could not be found */

			if (complain) {
				SD_WARN("LOGPROF-HINT missing_mandatory_profile image=%s pid=%d profile=%s active=%s\n",
				    filename,
				    current->pid,
				    sd->profile->name, 
				    sd->active->name);

				newprofile=get_sdprofile(null_complain_profile);
			}else{
				SD_WARN("REJECTING exec(2) of image '%s', Profile mandatory (exec qualifier 'p' specified) and not found (%s(%d) profile %s active %s)\n",
					filename,
					current->comm, current->pid,
					sd->profile->name, sd->active->name);
				error = -EPERM;
			}
		}else{	
			/* Profile (non-mandatory) could not be found */

			/* Only way we can get into this code is if task
			 * is unconstrained.
			 */

			BUG_ON(__sd_is_confined(sd));

			SD_DEBUG("%s: No profile found for exec image %s\n", 
				__FUNCTION__,
				filename);
		} /* profile */
	} /* findprofile */


	/* Apply profile if necessary */

	if (newprofile){
		struct subdomain *latest_sd;

		if (newprofile == &unconstrained_flag){
			newprofile=NULL;
		}

		/* grab a write lock
		 *
		 * Several things may have changed since the code above
		 *
		 * - If we are a confined process, sd is a refcounted copy of 
		 *   the SubDomain (get_sdcopy) and not the actual SubDomain.
		 *   This allows us to not have to hold a read lock around
		 *   all this code.  However, we need to change the actual
		 *   SubDomain, not the copy.  Also, if profile replacement
		 *   has taken place, our sd->profile may be inaccurate
		 *   so we need to undo the copy and reverse the refcounting.
		 *
		 * - If newprofile points to an actual profile (result of
		 *   sd_profilelist_find above), this profile may have been
		 *   replaced.  We need to fix it up.  Doing this to avoid
		 *   having to hold a write lock around all this code.
		 */
		 
		SD_WLOCK;

		/* task is guaranteed to have a SubDomain (->security)
		 * by this point 
		 */
		latest_sd = SD_SUBDOMAIN(current->security);

		/* Determine if profile we found earlier is stale.
		 * If so, reobtain it.  N.B stale flag should never be 
		 * set on null_complain profile.
		 */
		if (newprofile && unlikely(newprofile->isstale)){
			BUG_ON(newprofile == null_complain_profile);

			/* drop refcnt obtained from earlier get_sdprofile */
			put_sdprofile(newprofile);

			newprofile = sd_profilelist_find(filename);
	
			if (!newprofile){
				/* Race, profile was removed, not replaced.
				 * Redo with error checking
				 */
				SD_WUNLOCK;
				goto find_profile;
			}
		}

		put_sdprofile(latest_sd->profile);
		put_sdprofile(latest_sd->active);

		/* need to drop reference counts we obtained in get_sdcopy
		 * above.  Need to do it before overwriting latest_sd, in 
		 * case latest_sd == sd (no async replacement has taken place).
		 */
		if (issdcopy){
			put_sdcopy(sd);
			issdcopy=0;
		}
			
		latest_sd->profile = newprofile; /* already refcounted */
		latest_sd->active = get_sdprofile(newprofile);

		if (complain) {
			SD_WARN("LOGPROF-HINT changing_profile pid=%d newprofile=%s\n",
				current->pid,
				newprofile ? newprofile->name : SD_UNCONSTRAINED);
		}

		SD_WUNLOCK;
	}

	sd_put_name(filename);

	if (issdcopy){
		put_sdcopy(sd);
	}

out:
	return error;
}

/**
 * sd_release - release the task's SubDomain
 * @p: task being released
 *
 * This is called after a task has exited and the parent has reaped it.
 * @p->security blob is freed.
 */
void sd_release(struct task_struct *p)
{
	struct subdomain *sd = SD_SUBDOMAIN(p->security);
	if (sd) {
		p->security = NULL;

		sd_subdomainlist_remove(sd);

		/* release profiles */
		put_sdprofile(sd->profile);
		put_sdprofile(sd->active);

		kfree(sd);
	}
}

/*****************************
 * GLOBAL SUBPROFILE FUNCTIONS
 ****************************/

/**
 * do_change_hat - actually switch hats
 * @name: name of hat to swtich to
 * @sd: current SubDomain
 *
 * Switch to a new hat.  Return 0 on success, error otherwise.
 */
static __INLINE__ 
int do_change_hat(const char *hat_name, struct subdomain *sd)
{
	struct sdprofile *sub;
	struct sdprofile *p = sd->active;
	int error = 0;

	sub = __sd_find_profile(hat_name, &sd->profile->sub);

	if (sub) {
		/* change hat */
		sd->active = sub;
	} else {
		/* There is no such subprofile change to a NULL profile.
		 * The NULL profile grants no file access.
		 *
		 * 'null_profile' declared in sysctl.c
		 *
		 * This feature is used by changehat_apache.
		 * 
		 * N.B from the null-profile the task can still changehat back 
		 * out to the parent profile (assuming magic != NULL)
		 */
		if (SUBDOMAIN_COMPLAIN(sd)) {
			sd->active = get_sdprofile(null_complain_profile);
			SD_WARN("LOGPROF-HINT unknown_hat %s pid=%d profile=%s active=%s\n",
				 hat_name,
				 current->pid,
				 sd->profile->name, 
				 sd->active->name);
			
		} else {
			SD_DEBUG("%s: Unknown hatname '%s'. Changing to NULL profile (%s(%d) profile %s active %s)\n", 
				 __FUNCTION__,
				 hat_name,
				 current->comm, current->pid,
				 sd->profile->name, sd->active->name);

			sd->active = get_sdprofile(&null_profile);
			error = -EACCES;
		}
	}
	put_sdprofile(p);

	return error;
}


/**
 * sd_change_hat - change hat to/from subprofile
 * @hat_name: specifies hat to change to
 * @hat_magic: token to validate hat change
 *
 * Change to new @hat_name when current hat is top level profile, and store
 * the @hat_magic in the current SubDomain.  If the new @hat_name is
 * NULL, and the @hat_magic matches that stored in the current SubDomain
 * return to original top level profile.  Returns 0 on success, error
 * otherwise.
 */
#define IN_SUBPROFILE(sd)	((sd)->profile != (sd)->active)
int sd_change_hat(const char *hat_name, __u32 hat_magic)
{
	struct subdomain *sd = SD_SUBDOMAIN(current->security);
	int error = 0;

	SD_DEBUG("%s: %p, 0x%x (pid %d)\n", 
		__FUNCTION__, 
		hat_name, hat_magic,
		current->pid);

	/* Dump out above debugging in WARN mode if we are in AUDIT mode */
	if (SUBDOMAIN_AUDIT(sd)){
		SD_WARN("%s: %s, 0x%x (pid %d)\n", 
			__FUNCTION__, hat_name ? hat_name : "NULL", 
			hat_magic, current->pid);
	}

	/* no SubDomains: changehat into the null_profile, since the process
	   has no SubDomains do_change_hat won't find a match which will cause
	   a changehat to null_profile.  We could short circuit this but since
	   the subdprofile (hat) list is empty we would save very little. */

#if 0 	
	/* This is the old behaviour. Bailing does not result in task being
	 * put into the null-profile.
	 */
	if (!__sd_sub_defined(sd))
		goto out;
#endif

	/* check to see if an unconfined process is doing a changehat. */
	if (!__sd_is_confined(sd)) {
		error = -EACCES;
		goto out;
	}

	/* Check whether current domain is parent or one of the sibling children */
	if (sd->profile == sd->active) {
		/* 
		 * parent 
		 */
		if (hat_name) {
			SD_DEBUG("%s: switching to %s, 0x%x\n", 
				__FUNCTION__, 
				hat_name,
				hat_magic);

			/*
			 * N.B hat_magic == 0 has a special meaning
			 * this indicates that the task may never changehat
			 * back to it's parent, it will stay in this subhat
			 * (or null-profile, if the hat doesn't exist) until
			 * the task terminates
			 */
			sd->sd_hat_magic = hat_magic;
			error = do_change_hat(hat_name, sd);
		} else {
			/* Got here via changehat(NULL, magic)
			 *
			 * We used to simply update the magic cookie.
			 * That's an odd behaviour, so just do nothing.
			 */
		}
	} else {
		/* 
		 * child -- check to make sure magic is same as what was 
		 * passed when we switched into this profile,
		 * Handle special casing of NULL magic which confines task
		 * to subprofile and prohibits further changehats
		 */
		if (hat_magic == sd->sd_hat_magic && sd->sd_hat_magic) {
			if (!hat_name) {
				/* 
				 * Got here via changehat(NULL, magic)
			 	 * Return from subprofile, back to parent  
				 */
				put_sdprofile(sd->active);
				sd->active = get_sdprofile(sd->profile);

				/* Reset hat_magic to zero.
				 * New value will be passed on next changehat
				 */
				sd->sd_hat_magic = 0;
			} else {
				/* change to another (sibling) profile */
				error = do_change_hat(hat_name, sd);
			}
		} else if (sd->sd_hat_magic){ /* hat_magic != sd->sd_hat_magic */
		    SD_ERROR("KILLING process %s(%d) Invalid change_hat() magic# 0x%x (hatname %s profile %s active %s)\n", 
			     current->comm, current->pid,
			     hat_magic,
			     hat_name ? hat_name : "NULL", 
			     sd->profile->name, sd->active->name);

		    /* terminate current process */
		    (void)send_sig_info(SIGKILL, NULL, current);
		}else{ /* sd->sd_hat_magic == NULL */
		    SD_ERROR("KILLING process %s(%d) Task was confined to current subprofile (profile %s active %s)\n", 
			     current->comm, current->pid,
			     sd->profile->name, sd->active->name);
		    
		    /* terminate current process */
		    (void)send_sig_info(SIGKILL, NULL, current);
		}

	}

 out:
	return error;
}
