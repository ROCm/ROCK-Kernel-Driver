/*
 *  linux/fs/vfat/namei.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  Windows95/Windows NT compatible extended MSDOS filesystem
 *    by Gordon Chaffee Copyright (C) 1995.  Send bug reports for the
 *    VFAT filesystem to <chaffee@cs.berkeley.edu>.  Specify
 *    what file operation caused you trouble and if you can duplicate
 *    the problem, send a script that demonstrates it.
 *
 *  Short name translation 1999 by Wolfram Pienkoss <wp@bszh.de>
 */

#define __NO_VERSION__
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/msdos_fs.h>
#include <linux/nls.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/malloc.h>

#include "../fat/msbuffer.h"

#define DEBUG_LEVEL 0
#if (DEBUG_LEVEL >= 1)
#  define PRINTK1(x) printk x
#else
#  define PRINTK1(x)
#endif
#if (DEBUG_LEVEL >= 2)
#  define PRINTK2(x) printk x
#else
#  define PRINTK2(x)
#endif
#if (DEBUG_LEVEL >= 3)
#  define PRINTK3(x) printk x
#else
#  define PRINTK3(x)
#endif

#ifndef DEBUG
# define CHECK_STACK
#else
# define CHECK_STACK check_stack(__FILE__, __LINE__)
#endif

static int vfat_hashi(struct dentry *parent, struct qstr *qstr);
static int vfat_hash(struct dentry *parent, struct qstr *qstr);
static int vfat_cmpi(struct dentry *dentry, struct qstr *a, struct qstr *b);
static int vfat_cmp(struct dentry *dentry, struct qstr *a, struct qstr *b);
static int vfat_revalidate(struct dentry *dentry, int);

static struct dentry_operations vfat_dentry_ops[4] = {
	{
		d_hash:		vfat_hashi,
		d_compare:	vfat_cmpi,
	},
	{
		d_revalidate:	vfat_revalidate,
		d_hash:		vfat_hashi,
		d_compare:	vfat_cmpi,
	},
	{
		d_hash:		vfat_hash,
		d_compare:	vfat_cmp,
	},
	{
		d_revalidate:	vfat_revalidate,
		d_hash:		vfat_hash,
		d_compare:	vfat_cmp,
	}
};

static int vfat_revalidate(struct dentry *dentry, int flags)
{
	PRINTK1(("vfat_revalidate: %s\n", dentry->d_name.name));
	spin_lock(&dcache_lock);
	if (dentry->d_time == dentry->d_parent->d_inode->i_version) {
		spin_unlock(&dcache_lock);
		return 1;
	}
	spin_unlock(&dcache_lock);
	return 0;
}

static int simple_getbool(char *s, int *setval)
{
	if (s) {
		if (!strcmp(s,"1") || !strcmp(s,"yes") || !strcmp(s,"true")) {
			*setval = 1;
		} else if (!strcmp(s,"0") || !strcmp(s,"no") || !strcmp(s,"false")) {
			*setval = 0;
		} else {
			return 0;
		}
	} else {
		*setval = 1;
	}
	return 1;
}

static int parse_options(char *options,	struct fat_mount_options *opts)
{
	char *this_char,*value,save,*savep;
	int ret, val;

	opts->unicode_xlate = opts->posixfs = 0;
	opts->numtail = 1;
	opts->utf8 = 0;

	if (!options) return 1;
	save = 0;
	savep = NULL;
	ret = 1;
	for (this_char = strtok(options,","); this_char; this_char = strtok(NULL,",")) {
		if ((value = strchr(this_char,'=')) != NULL) {
			save = *value;
			savep = value;
			*value++ = 0;
		}
		if (!strcmp(this_char,"utf8")) {
			ret = simple_getbool(value, &val);
			if (ret) opts->utf8 = val;
		} else if (!strcmp(this_char,"uni_xlate")) {
			ret = simple_getbool(value, &val);
			if (ret) opts->unicode_xlate = val;
		} else if (!strcmp(this_char,"posix")) {
			ret = simple_getbool(value, &val);
			if (ret) opts->posixfs = val;
		} else if (!strcmp(this_char,"nonumtail")) {
			ret = simple_getbool(value, &val);
			if (ret) {
				opts->numtail = !val;
			}
		}
		if (this_char != options)
			*(this_char-1) = ',';
		if (value) {
			*savep = save;
		}
		if (ret == 0) {
			return 0;
		}
	}
	if (opts->unicode_xlate) {
		opts->utf8 = 0;
	}
	return 1;
}

static inline unsigned char
vfat_getlower(struct nls_table *t, unsigned char c)
{
	return t->charset2lower[c];
}

static inline unsigned char
vfat_tolower(struct nls_table *t, unsigned char c)
{
	unsigned char nc = t->charset2lower[c];

	return nc ? nc : c;
}

static inline unsigned char
vfat_getupper(struct nls_table *t, unsigned char c)
{
	return t->charset2upper[c];
}

static inline unsigned char
vfat_toupper(struct nls_table *t, unsigned char c)
{
	unsigned char nc = t->charset2upper[c];

	return nc ? nc : c;
}

static int
vfat_strnicmp(struct nls_table *t, const unsigned char *s1,
					const unsigned char *s2, int len)
{
	while(len--)
		if (vfat_tolower(t, *s1++) != vfat_tolower(t, *s2++))
			return 1;

	return 0;
}

static inline int
vfat_uni2short(struct nls_table *t, wchar_t uc, unsigned char *op, int bound)
{
	int charlen;

	if ( (charlen = t->uni2char(uc, op, bound)) < 0)
		charlen = 0;

	return charlen;
}

static inline int
vfat_uni2upper_short(struct nls_table *t, wchar_t uc, char *op, int bound)
{
	int chi, chl;

	if ( (chl = t->uni2char(uc, op, bound)) < 0)
		chl = 0;

	for (chi = 0; chi < chl; chi++)
		op[chi] = vfat_toupper(t, op[chi]);

	return chl;
}

/*
 * Compute the hash for the vfat name corresponding to the dentry.
 * Note: if the name is invalid, we leave the hash code unchanged so
 * that the existing dentry can be used. The vfat fs routines will
 * return ENOENT or EINVAL as appropriate.
 */
static int vfat_hash(struct dentry *dentry, struct qstr *qstr)
{
	const char *name;
	int len;

	len = qstr->len;
	name = qstr->name;
	while (len && name[len-1] == '.')
		len--;

	qstr->hash = full_name_hash(name, len);

	return 0;
}

/*
 * Compute the hash for the vfat name corresponding to the dentry.
 * Note: if the name is invalid, we leave the hash code unchanged so
 * that the existing dentry can be used. The vfat fs routines will
 * return ENOENT or EINVAL as appropriate.
 */
static int vfat_hashi(struct dentry *dentry, struct qstr *qstr)
{
	struct nls_table *t = MSDOS_SB(dentry->d_inode->i_sb)->nls_io;
	const char *name;
	int len;
	unsigned long hash;

	len = qstr->len;
	name = qstr->name;
	while (len && name[len-1] == '.')
		len--;

	hash = init_name_hash();
	while (len--)
		hash = partial_name_hash(vfat_tolower(t, *name++), hash);
	qstr->hash = end_name_hash(hash);

	return 0;
}

/*
 * Case insensitive compare of two vfat names.
 */
static int vfat_cmpi(struct dentry *dentry, struct qstr *a, struct qstr *b)
{
	struct nls_table *t = MSDOS_SB(dentry->d_inode->i_sb)->nls_io;
	int alen, blen;

	/* A filename cannot end in '.' or we treat it like it has none */
	alen = a->len;
	blen = b->len;
	while (alen && a->name[alen-1] == '.')
		alen--;
	while (blen && b->name[blen-1] == '.')
		blen--;
	if (alen == blen) {
		if (vfat_strnicmp(t, a->name, b->name, alen) == 0)
			return 0;
	}
	return 1;
}

/*
 * Case sensitive compare of two vfat names.
 */
static int vfat_cmp(struct dentry *dentry, struct qstr *a, struct qstr *b)
{
	int alen, blen;

	/* A filename cannot end in '.' or we treat it like it has none */
	alen = a->len;
	blen = b->len;
	while (alen && a->name[alen-1] == '.')
		alen--;
	while (blen && b->name[blen-1] == '.')
		blen--;
	if (alen == blen) {
		if (strncmp(a->name, b->name, alen) == 0)
			return 0;
	}
	return 1;
}

#ifdef DEBUG

static void
check_stack(const char *fname, int lineno)
{
	int stack_level;
	char *pg_dir;

	stack_level = (long)(&pg_dir)-current->kernel_stack_page;
	if (stack_level < 0)
	        printk("*-*-*-* vfat kstack overflow in %s line %d: SL=%d\n",
		       fname, lineno, stack_level);
	else if (stack_level < 500)
	        printk("*-*-*-* vfat kstack low in %s line %d: SL=%d\n",
		       fname, lineno, stack_level);
#if 0
	else
		printk("------- vfat kstack ok in %s line %d: SL=%d\n",
		       fname, lineno, stack_level);
#endif
#if 0
	if (*(unsigned long *) current->kernel_stack_page != STACK_MAGIC) {
		printk("******* vfat stack corruption detected in %s at line %d\n",
		       fname, lineno);
	}
#endif
}

static int debug = 0;
static void dump_fat(struct super_block *sb,int start)
{
	printk("[");
	while (start) {
		printk("%d ",start);
		start = fat_access(sb,start,-1);
		if (!start) {
			printk("ERROR");
			break;
		}
		if (start == -1) break;
	}
	printk("]\n");
}

static void dump_de(struct msdos_dir_entry *de)
{
	int i;
	unsigned char *p = (unsigned char *) de;
	printk("[");

	for (i = 0; i < 32; i++, p++) {
		printk("%02x ", *p);
	}
	printk("]\n");
}

#endif

/* MS-DOS "device special files" */

static const char *reserved3_names[] = {
	"con     ", "prn     ", "nul     ", "aux     ", NULL
};

static const char *reserved4_names[] = {
	"com1    ", "com2    ", "com3    ", "com4    ", "com5    ",
	"com6    ", "com7    ", "com8    ", "com9    ",
	"lpt1    ", "lpt2    ", "lpt3    ", "lpt4    ", "lpt5    ",
	"lpt6    ", "lpt7    ", "lpt8    ", "lpt9    ",
	NULL };


/* Characters that are undesirable in an MS-DOS file name */

static char bad_chars[] = "*?<>|\":/\\";
static char replace_chars[] = "[];,+=";

/* Checks the validity of a long MS-DOS filename */
/* Returns negative number on error, 0 for a normal
 * return, and 1 for . or .. */

static int vfat_valid_longname(const char *name, int len, int xlate)
{
	const char **reserved, *walk;
	unsigned char c;
	int i, baselen;

	if (len && name[len-1] == ' ') return -EINVAL;
	if (len >= 256) return -EINVAL;
	for (i = 0; i < len; i++) {
		c = name[i];
		if (xlate && c == ':') continue;
		if (strchr(bad_chars,c)) {
			return -EINVAL;
		}
	}
 	if (len < 3) return 0;

	for (walk = name; *walk != 0 && *walk != '.'; walk++);
	baselen = walk - name;

	if (baselen == 3) {
		for (reserved = reserved3_names; *reserved; reserved++) {
			if (!strnicmp(name,*reserved,baselen))
				return -EINVAL;
		}
	} else if (baselen == 4) {
		for (reserved = reserved4_names; *reserved; reserved++) {
			if (!strnicmp(name,*reserved,baselen))
				return -EINVAL;
		}
	}
	return 0;
}

static int vfat_valid_shortname(struct nls_table *nls, wchar_t *name, int len)
{
	wchar_t *walk;
	unsigned char c, charbuf[NLS_MAX_CHARSET_SIZE];
	int chl, chi;
	int space;

	if (vfat_uni2upper_short(nls, *name, charbuf, NLS_MAX_CHARSET_SIZE) == 0)
		return -EINVAL;

	if (IS_FREE(charbuf))
		return -EINVAL;

	chl = 0;
	c = 0;
	space = 1; /* disallow names starting with a dot */
	for (walk = name; len && walk-name < 8;) {
		len--;
		chl = nls->uni2char(*walk++, charbuf, NLS_MAX_CHARSET_SIZE);
		if (chl < 0)
			return -EINVAL;

		for (chi = 0; chi < chl; chi++) {
			c = vfat_getupper(nls, charbuf[chi]);
			if (!c) return -EINVAL;
			if (charbuf[chi] != vfat_tolower(nls, c)) return -EINVAL;
			if (strchr(replace_chars,c)) return -EINVAL;
			if (c < ' '|| c==':') return -EINVAL;
			if (c == '.') goto dot;
			space = c == ' ';
		}
	}
dot:;
	if (space) return -EINVAL;
	if (len && c != '.') {
		len--;
		if (vfat_uni2upper_short(nls, *walk++, charbuf, NLS_MAX_CHARSET_SIZE) == 1) {
			if (charbuf[0] != '.') return -EINVAL;
		} else
			return -EINVAL;
		c = '.';
	}
	if (c == '.') {
		if (len >= 4) return -EINVAL;
		while (len > 0) {
			len--;
			chl = nls->uni2char(*walk++, charbuf, NLS_MAX_CHARSET_SIZE);
			if (chl < 0)
				return -EINVAL;
			for (chi = 0; chi < chl; chi++) {
				c = vfat_getupper(nls, charbuf[chi]);
				if (!c) return -EINVAL;
				if (charbuf[chi] != vfat_tolower(nls, c)) return -EINVAL;
				if (strchr(replace_chars,c))
					return -EINVAL;
				if (c < ' ' || c == '.'|| c==':')
					return -EINVAL;
				space = c == ' ';
			}
		}
		if (space) return -EINVAL;
	}

	return 0;
}

static int vfat_find_form(struct inode *dir,char *name)
{
	struct msdos_dir_entry *de;
	struct buffer_head *bh = NULL;
	int ino,res;

	res=fat_scan(dir,name,&bh,&de,&ino);
	fat_brelse(dir->i_sb, bh);
	if (res<0)
		return -ENOENT;
	return 0;
}

static int vfat_format_name(struct nls_table *nls, wchar_t *name,
				int len, char *res)
{
	char *walk;
	unsigned char charbuf[NLS_MAX_CHARSET_SIZE];
	int chi, chl;
	int space;

	if (vfat_uni2upper_short(nls, *name, charbuf, NLS_MAX_CHARSET_SIZE) == 0)
		return -EINVAL;

	if (IS_FREE(charbuf))
		return -EINVAL;

	space = 1; /* disallow names starting with a dot */
	for (walk = res; len--; ) {
		chl = vfat_uni2upper_short(nls, *name++, charbuf, NLS_MAX_CHARSET_SIZE);
		if (chl == 0)
			return -EINVAL;
		for (chi = 0; chi < chl; chi++){
			if (charbuf[chi] == '.') goto dot;
			if (!charbuf[chi]) return -EINVAL;
			if (walk-res == 8) return -EINVAL;
			if (strchr(replace_chars,charbuf[chi])) return -EINVAL;
			if (charbuf[chi] < ' '|| charbuf[chi]==':') return -EINVAL;
			space = charbuf[chi] == ' ';
			*walk = charbuf[chi];
			walk++;
		}
	}
dot:;
	if (space) return -EINVAL;
	if (len >= 0) {
		while (walk-res < 8) *walk++ = ' ';
		while (len > 0 && walk-res < MSDOS_NAME) {
			chl = vfat_uni2upper_short(nls, *name++, charbuf, NLS_MAX_CHARSET_SIZE);
			if (len < chl)
				chl = len;
			len -= chl;
			for (chi = 0; chi < chl; chi++){
				if (!charbuf[chi]) return -EINVAL;
				if (strchr(replace_chars,charbuf[chi]))
					return -EINVAL;
				if (charbuf[chi] < ' ' || charbuf[chi] == '.'|| charbuf[chi]==':')
					return -EINVAL;
				space = charbuf[chi] == ' ';
				*walk++ = charbuf[chi];
			}
		}
		if (space) return -EINVAL;
		if (len) return -EINVAL;
	}
	while (walk-res < MSDOS_NAME) *walk++ = ' ';

	return 0;
}

static char skip_chars[] = ".:\"?<>| ";

/* Given a valid longname, create a unique shortname.  Make sure the
 * shortname does not exist
 */
static int vfat_create_shortname(struct inode *dir, struct nls_table *nls,
					wchar_t *name, int len,
					char *name_res)
{
	wchar_t *ip, *op, *ext_start, *end, *name_start;
	wchar_t msdos_name[13];
	char base[9], ext[4], buf[8], *p;
	unsigned char charbuf[NLS_MAX_CHARSET_SIZE];
	int chl, chi;
	int sz, extlen, baselen, i;

	PRINTK2(("Entering vfat_create_shortname\n"));
	chl = 0;
	sz = 0;			/* Make compiler happy */
	if (len <= 12) {
		/* Do a case insensitive search if the name would be a valid
		 * shortname if is were all capitalized.  However, do not
		 * allow spaces in short names because Win95 scandisk does
		 * not like that */
		for (i = 0, op = &msdos_name[0], ip = name; ; i++, ip++, op++) {
			if (i == len) {
				if (vfat_format_name(nls, &msdos_name[0], len,
							name_res) < 0)
					break;
				PRINTK3(("vfat_create_shortname 1\n"));
				if (vfat_find_form(dir, name_res) < 0)
					return 0;
				return -EEXIST;
			}
			chl = vfat_uni2upper_short(nls, *ip, charbuf, NLS_MAX_CHARSET_SIZE);
			for (chi = 0; chi < chl; chi++){
				if (charbuf[chi] == ' ')
					break;
			}
			if (chi < chl)
				break;

			*op = *ip;
		}
	}

	PRINTK3(("vfat_create_shortname 3\n"));
	/* Now, we need to create a shortname from the long name */
	ext_start = end = &name[len];
	while (--ext_start >= name) {
		chl = vfat_uni2upper_short(nls, *ext_start, charbuf, NLS_MAX_CHARSET_SIZE);
		for (chi = 0; chi < chl; chi++) {
			if (charbuf[chi] == '.') {
				if (ext_start == end - 1) {
					sz = len;
					ext_start = NULL;
				}
				goto stop0;
			}
		}
	}
stop0:;	
	if (ext_start == name - 1) {
		sz = len;
		ext_start = NULL;
	} else if (ext_start) {
		/*
		 * Names which start with a dot could be just
		 * an extension eg. "...test".  In this case Win95
		 * uses the extension as the name and sets no extension.
		 */
		name_start = &name[0];
		while (name_start < ext_start)
		{
			chl = vfat_uni2upper_short(nls, *name_start, charbuf, NLS_MAX_CHARSET_SIZE);
			if (chl == 0)
				break;
			for (chi = 0; chi < chl; chi++)
				if (!strchr(skip_chars, charbuf[chi])) {
					goto stop1;
				}
			name_start++;
		}
stop1:;		
		if (name_start != ext_start) {
			sz = ext_start - name;
			ext_start++;
		} else {
			sz = len;
			ext_start=NULL;
		}
	}

	for (baselen = i = 0, p = base, ip = name; i < sz && baselen < 8; i++, ip++)
	{
		chl = vfat_uni2upper_short(nls, *ip, charbuf, NLS_MAX_CHARSET_SIZE);
		if (chl == 0){
			*p++ = '_';
			baselen++;
			continue;
		}

		for (chi = 0; chi < chl; chi++){
			if (!strchr(skip_chars, charbuf[chi])){
				if (strchr(replace_chars, charbuf[chi]))
					*p = '_';
				else
					*p = charbuf[chi];
				p++; baselen++;
			}
		}
	}
	if (baselen == 0) {
		return -EINVAL;
	}

	extlen = 0;
	if (ext_start) {
		for (p = ext, ip = ext_start; extlen < 3 && ip < end; ip++) {
			chl = vfat_uni2upper_short(nls, *ip, charbuf, NLS_MAX_CHARSET_SIZE);
			if (chl == 0) {
				*p++ = '_';
				extlen++;
				continue;
			}

			for (chi = 0; chi < chl; chi++) {
				if (!strchr(skip_chars, charbuf[chi])) {
					if (strchr(replace_chars, charbuf[chi]))
						*p = '_';
					else
						*p = charbuf[chi];
					p++; extlen++;
				}
			}
		}
	}
	ext[extlen] = '\0';
	base[baselen] = '\0';

	/* Yes, it can happen. ".\xe5" would do it. */
	if (IS_FREE(base))
		base[0]='_';

	/* OK, at this point we know that base is not longer than 8 symbols,
	 * ext is not longer than 3, base is nonempty, both don't contain
	 * any bad symbols (lowercase transformed to uppercase).
	 */

	memset(name_res, ' ', MSDOS_NAME);
	memcpy(name_res,base,baselen);
	memcpy(name_res+8,ext,extlen);
	if (MSDOS_SB(dir->i_sb)->options.numtail == 0)
		if (vfat_find_form(dir, name_res) < 0)
			return 0;

	/*
	 * Try to find a unique extension.  This used to
	 * iterate through all possibilities sequentially,
	 * but that gave extremely bad performance.  Windows
	 * only tries a few cases before using random
	 * values for part of the base.
	 */

	if (baselen>6)
		baselen = 6;
	name_res[baselen] = '~';
	for (i = 1; i < 10; i++) {
		name_res[baselen+1] = i + '0';
		if (vfat_find_form(dir, name_res) < 0)
			return 0;
	}

	i = jiffies & 0xffff;
	sz = (jiffies >> 16) & 0x7;
	if (baselen>2)
		baselen = 2;
	name_res[baselen+4] = '~';
	name_res[baselen+5] = '1' + sz;
	while (1) {
		sprintf(buf, "%04X", i);
		memcpy(&name_res[baselen], buf, 4);
		if (vfat_find_form(dir, name_res) < 0)
			break;
		i -= 11;
	}
	return 0;
}

/* Translate a string, including coded sequences into Unicode */
static int
xlate_to_uni(const char *name, int len, char *outname, int *longlen, int *outlen,
	     int escape, int utf8, struct nls_table *nls)
{
	const unsigned char *ip;
	unsigned char nc;
	char *op;
	unsigned int ec;
	int i, k, fill;
	int charlen;

	if (utf8) {
		*outlen = utf8_mbstowcs((__u16 *) outname, name, PAGE_SIZE);
		if (name[len-1] == '.')
			*outlen-=2;
		op = &outname[*outlen * sizeof(__u16)];
	} else {
		if (name[len-1] == '.') 
			len--;
		if (nls) {
			for (i = 0, ip = name, op = outname, *outlen = 0;
			     i < len && *outlen <= 260; *outlen += 1)
			{
				if (escape && (*ip == ':')) {
					if (i > len - 5)
						return -EINVAL;
					ec = 0;
					for (k = 1; k < 5; k++) {
						nc = ip[k];
						ec <<= 4;
						if (nc >= '0' && nc <= '9') {
							ec |= nc - '0';
							continue;
						}
						if (nc >= 'a' && nc <= 'f') {
							ec |= nc - ('a' - 10);
							continue;
						}
						if (nc >= 'A' && nc <= 'F') {
							ec |= nc - ('A' - 10);
							continue;
						}
						return -EINVAL;
					}
					*op++ = ec & 0xFF;
					*op++ = ec >> 8;
					ip += 5;
					i += 5;
				} else {
					if ((charlen = nls->char2uni(ip, len-i, (wchar_t *)op)) < 0)
						return -EINVAL;

					ip += charlen;
					i += charlen;
					op += 2;
				}
			}
		} else {
			for (i = 0, ip = name, op = outname, *outlen = 0;
			     i < len && *outlen <= 260; i++, *outlen += 1)
			{
				*op++ = *ip++;
				*op++ = 0;
			}
		}
	}
	if (*outlen > 260)
		return -ENAMETOOLONG;

	*longlen = *outlen;
	if (*outlen % 13) {
		*op++ = 0;
		*op++ = 0;
		*outlen += 1;
		if (*outlen % 13) {
			fill = 13 - (*outlen % 13);
			for (i = 0; i < fill; i++) {
				*op++ = 0xff;
				*op++ = 0xff;
			}
			*outlen += fill;
		}
	}

	return 0;
}

static int
vfat_fill_slots(struct inode *dir, struct msdos_dir_slot *ds, const char *name,
		int len, int *slots, int uni_xlate)
{
	struct nls_table *nls_io, *nls_disk;
	wchar_t *uname;
	struct msdos_dir_slot *ps;
	struct msdos_dir_entry *de;
	unsigned long page;
	unsigned char cksum;
	const char *ip;
	char *uniname, msdos_name[MSDOS_NAME];
	int res, utf8, slot, ulen, unilen, i;
	loff_t offset;

	de = (struct msdos_dir_entry *) ds;
	utf8 = MSDOS_SB(dir->i_sb)->options.utf8;
	nls_io = MSDOS_SB(dir->i_sb)->nls_io;
	nls_disk = MSDOS_SB(dir->i_sb)->nls_disk;

	if (name[len-1] == '.') len--;
	if(!(page = __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	uniname = (char *) page;

	res = xlate_to_uni(name, len, uniname, &ulen, &unilen, uni_xlate,
								utf8, nls_io);
	if (res < 0)
		goto out_free;

	uname = (wchar_t *) page;
	if (vfat_valid_shortname(nls_disk, uname, ulen) >= 0) {
		res = vfat_format_name(nls_disk, uname, ulen, de->name);
		if (!res)
			goto out_free;
	}

	res = vfat_create_shortname(dir, nls_disk, uname, ulen, msdos_name);
	if (res)
		goto out_free;

	*slots = unilen / 13;
	for (cksum = i = 0; i < 11; i++) {
		cksum = (((cksum&1)<<7)|((cksum&0xfe)>>1)) + msdos_name[i];
	}
	PRINTK3(("vfat_fill_slots 3: slots=%d\n",*slots));

	for (ps = ds, slot = *slots; slot > 0; slot--, ps++) {
		ps->id = slot;
		ps->attr = ATTR_EXT;
		ps->reserved = 0;
		ps->alias_checksum = cksum;
		ps->start = 0;
		offset = (slot - 1) * 26;
		ip = &uniname[offset];
		memcpy(ps->name0_4, ip, 10);
		memcpy(ps->name5_10, ip+10, 12);
		memcpy(ps->name11_12, ip+22, 4);
	}
	ds[0].id |= 0x40;

	de = (struct msdos_dir_entry *) ps;
	PRINTK3(("vfat_fill_slots 9\n"));
	strncpy(de->name, msdos_name, MSDOS_NAME);
	(*slots)++;

out_free:
	free_page(page);
	return res;
}

/* We can't get "." or ".." here - VFS takes care of those cases */

static int vfat_build_slots(struct inode *dir,const char *name,int len,
     struct msdos_dir_slot *ds, int *slots)
{
	int res, xlate;

	xlate = MSDOS_SB(dir->i_sb)->options.unicode_xlate;
	*slots = 1;
	res = vfat_valid_longname(name, len, xlate);
	if (res < 0)
		return res;
	return vfat_fill_slots(dir, ds, name, len, slots, xlate);
}

static int vfat_add_entry(struct inode *dir,struct qstr* qname,
	int is_dir,struct vfat_slot_info *sinfo_out,
	struct buffer_head **bh, struct msdos_dir_entry **de)
{
	struct super_block *sb = dir->i_sb;
	struct msdos_dir_slot *ps;
	loff_t offset;
	struct msdos_dir_slot *ds;
	int slots, slot;
	int res;
	struct msdos_dir_entry *de1;
	struct buffer_head *bh1;
	int ino;
	int len;
	loff_t dummy;

	ds = (struct msdos_dir_slot *)
	    kmalloc(sizeof(struct msdos_dir_slot)*MSDOS_SLOTS, GFP_KERNEL);
	if (ds == NULL) return -ENOMEM;

	len = qname->len;
	while (len && qname->name[len-1] == '.')
		len--;
	res = fat_search_long(dir, qname->name, len,
			(MSDOS_SB(sb)->options.name_check != 's') ||
			!MSDOS_SB(sb)->options.posixfs,
			&dummy, &dummy);
	if (res > 0) /* found */
		res = -EEXIST;
	if (res)
		goto cleanup;

	res = vfat_build_slots(dir, qname->name, len, ds, &slots);
	if (res < 0) goto cleanup;

	offset = fat_add_entries(dir, slots, &bh1, &de1, &ino);
	if (offset < 0) {
		res = offset;
		goto cleanup;
	}
	fat_brelse(sb, bh1);

	/* Now create the new entry */
	*bh = NULL;
	for (slot = 0, ps = ds; slot < slots; slot++, ps++) {
		if (fat_get_entry(dir,&offset,bh,de, &sinfo_out->ino) < 0) {
			res = -EIO;
			goto cleanup;
		}
		memcpy(*de, ps, sizeof(struct msdos_dir_slot));
		fat_mark_buffer_dirty(sb, *bh);
	}

	dir->i_ctime = dir->i_mtime = dir->i_atime = CURRENT_TIME;
	mark_inode_dirty(dir);

	fat_date_unix2dos(dir->i_mtime,&(*de)->time,&(*de)->date);
	(*de)->ctime_ms = 0;
	(*de)->ctime = (*de)->time;
	(*de)->adate = (*de)->cdate = (*de)->date;
	(*de)->start = 0;
	(*de)->starthi = 0;
	(*de)->size = 0;
	(*de)->attr = is_dir ? ATTR_DIR : ATTR_ARCH;
	(*de)->lcase = CASE_LOWER_BASE | CASE_LOWER_EXT;


	fat_mark_buffer_dirty(sb, *bh);

	/* slots can't be less than 1 */
	sinfo_out->long_slots = slots - 1;
	sinfo_out->longname_offset = offset - sizeof(struct msdos_dir_slot) * slots;
	res = 0;

cleanup:
	kfree(ds);
	return res;
}

static int vfat_find(struct inode *dir,struct qstr* qname,
	struct vfat_slot_info *sinfo, struct buffer_head **last_bh,
	struct msdos_dir_entry **last_de)
{
	struct super_block *sb = dir->i_sb;
	loff_t offset;
	int res,len;

	len = qname->len;
	while (len && qname->name[len-1] == '.') 
		len--;
	res = fat_search_long(dir, qname->name, len,
			(MSDOS_SB(sb)->options.name_check != 's'),
			&offset,&sinfo->longname_offset);
	if (res>0) {
		sinfo->long_slots = res-1;
		if (fat_get_entry(dir,&offset,last_bh,last_de,&sinfo->ino)>=0)
			return 0;
		res = -EIO;
	} 
	return res ? res : -ENOENT;
}

struct dentry *vfat_lookup(struct inode *dir,struct dentry *dentry)
{
	int res;
	struct vfat_slot_info sinfo;
	struct inode *inode;
	struct dentry *alias;
	struct buffer_head *bh = NULL;
	struct msdos_dir_entry *de;
	int table;
	
	PRINTK2(("vfat_lookup: name=%s, len=%d\n", 
		 dentry->d_name.name, dentry->d_name.len));

	table = (MSDOS_SB(dir->i_sb)->options.name_check == 's') ? 2 : 0;
	dentry->d_op = &vfat_dentry_ops[table];

	inode = NULL;
	res = vfat_find(dir,&dentry->d_name,&sinfo,&bh,&de);
	if (res < 0) {
		table++;
		goto error;
	}
	inode = fat_build_inode(dir->i_sb, de, sinfo.ino, &res);
	fat_brelse(dir->i_sb, bh);
	if (res)
		return ERR_PTR(res);
	alias = d_find_alias(inode);
	if (alias) {
		if (d_invalidate(alias)==0)
			dput(alias);
		else {
			iput(inode);
			return alias;
		}
		
	}
error:
	dentry->d_op = &vfat_dentry_ops[table];
	dentry->d_time = dentry->d_parent->d_inode->i_version;
	d_add(dentry,inode);
	return NULL;
}

int vfat_create(struct inode *dir,struct dentry* dentry,int mode)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode = NULL;
	struct buffer_head *bh = NULL;
	struct msdos_dir_entry *de;
	struct vfat_slot_info sinfo;
	int res;

	res = vfat_add_entry(dir, &dentry->d_name, 0, &sinfo, &bh, &de);
	if (res < 0)
		return res;
	inode = fat_build_inode(sb, de, sinfo.ino, &res);
	fat_brelse(sb, bh);
	if (!inode)
		return res;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
	inode->i_version = ++event;
	dir->i_version = event;
	dentry->d_time = dentry->d_parent->d_inode->i_version;
	d_instantiate(dentry,inode);
	return 0;
}

static void vfat_remove_entry(struct inode *dir,struct vfat_slot_info *sinfo,
     struct buffer_head *bh, struct msdos_dir_entry *de)
{
	struct super_block *sb = dir->i_sb;
	loff_t offset;
	int i,ino;

	/* remove the shortname */
	dir->i_mtime = CURRENT_TIME;
	dir->i_atime = CURRENT_TIME;
	dir->i_version = ++event;
	mark_inode_dirty(dir);
	de->name[0] = DELETED_FLAG;
	fat_mark_buffer_dirty(sb, bh);
	/* remove the longname */
	offset = sinfo->longname_offset; de = NULL;
	for (i = sinfo->long_slots; i > 0; --i) {
		if (fat_get_entry(dir, &offset, &bh, &de, &ino) < 0)
			continue;
		de->name[0] = DELETED_FLAG;
		de->attr = 0;
		fat_mark_buffer_dirty(sb, bh);
	}
	if (bh) fat_brelse(sb, bh);
}

int vfat_rmdir(struct inode *dir,struct dentry* dentry)
{
	int res;
	struct vfat_slot_info sinfo;
	struct buffer_head *bh = NULL;
	struct msdos_dir_entry *de;

	res = fat_dir_empty(dentry->d_inode);
	if (res)
		return res;

	res = vfat_find(dir,&dentry->d_name,&sinfo, &bh, &de);
	if (res<0)
		return res;
	dentry->d_inode->i_nlink = 0;
	dentry->d_inode->i_mtime = CURRENT_TIME;
	dentry->d_inode->i_atime = CURRENT_TIME;
	fat_detach(dentry->d_inode);
	mark_inode_dirty(dentry->d_inode);
	/* releases bh */
	vfat_remove_entry(dir,&sinfo,bh,de);
	dir->i_nlink--;
	return 0;
}

int vfat_unlink(struct inode *dir, struct dentry* dentry)
{
	int res;
	struct vfat_slot_info sinfo;
	struct buffer_head *bh = NULL;
	struct msdos_dir_entry *de;

	PRINTK1(("vfat_unlink: %s\n", dentry->d_name.name));
	res = vfat_find(dir,&dentry->d_name,&sinfo,&bh,&de);
	if (res < 0)
		return res;
	dentry->d_inode->i_nlink = 0;
	dentry->d_inode->i_mtime = CURRENT_TIME;
	dentry->d_inode->i_atime = CURRENT_TIME;
	fat_detach(dentry->d_inode);
	mark_inode_dirty(dentry->d_inode);
	/* releases bh */
	vfat_remove_entry(dir,&sinfo,bh,de);

	return res;
}


int vfat_mkdir(struct inode *dir,struct dentry* dentry,int mode)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode = NULL;
	struct vfat_slot_info sinfo;
	struct buffer_head *bh = NULL;
	struct msdos_dir_entry *de;
	int res;

	res = vfat_add_entry(dir, &dentry->d_name, 1, &sinfo, &bh, &de);
	if (res < 0)
		return res;
	inode = fat_build_inode(sb, de, sinfo.ino, &res);
	if (!inode)
		goto out;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
	inode->i_version = ++event;
	dir->i_version = event;
	dir->i_nlink++;
	inode->i_nlink = 2; /* no need to mark them dirty */
	res = fat_new_dir(inode, dir, 1);
	if (res < 0)
		goto mkdir_failed;
	dentry->d_time = dentry->d_parent->d_inode->i_version;
	d_instantiate(dentry,inode);
out:
	fat_brelse(sb, bh);
	return res;

mkdir_failed:
	inode->i_nlink = 0;
	inode->i_mtime = CURRENT_TIME;
	inode->i_atime = CURRENT_TIME;
	fat_detach(inode);
	mark_inode_dirty(inode);
	/* releases bh */
	vfat_remove_entry(dir,&sinfo,bh,de);
	iput(inode);
	dir->i_nlink--;
	return res;
}
 
int vfat_rename(struct inode *old_dir,struct dentry *old_dentry,
		struct inode *new_dir,struct dentry *new_dentry)
{
	struct super_block *sb = old_dir->i_sb;
	struct buffer_head *old_bh,*new_bh,*dotdot_bh;
	struct msdos_dir_entry *old_de,*new_de,*dotdot_de;
	int dotdot_ino;
	struct inode *old_inode, *new_inode;
	int res, is_dir;
	struct vfat_slot_info old_sinfo,sinfo;

	old_bh = new_bh = dotdot_bh = NULL;
	old_inode = old_dentry->d_inode;
	new_inode = new_dentry->d_inode;
	res = vfat_find(old_dir,&old_dentry->d_name,&old_sinfo,&old_bh,&old_de);
	PRINTK3(("vfat_rename 2\n"));
	if (res < 0) goto rename_done;

	is_dir = S_ISDIR(old_inode->i_mode);

	if (is_dir && (res = fat_scan(old_inode,MSDOS_DOTDOT,&dotdot_bh,
				&dotdot_de,&dotdot_ino)) < 0)
		goto rename_done;

	if (new_dentry->d_inode) {
		res = vfat_find(new_dir,&new_dentry->d_name,&sinfo,&new_bh,
				&new_de);
		if (res < 0 || MSDOS_I(new_inode)->i_location != sinfo.ino) {
			/* WTF??? Cry and fail. */
			printk(KERN_WARNING "vfat_rename: fs corrupted\n");
			goto rename_done;
		}

		if (is_dir) {
			res = fat_dir_empty(new_inode);
			if (res)
				goto rename_done;
		}
		fat_detach(new_inode);
	} else {
		res = vfat_add_entry(new_dir,&new_dentry->d_name,is_dir,&sinfo,
					&new_bh,&new_de);
		if (res < 0) goto rename_done;
	}

	new_dir->i_version = ++event;

	/* releases old_bh */
	vfat_remove_entry(old_dir,&old_sinfo,old_bh,old_de);
	old_bh=NULL;
	fat_detach(old_inode);
	fat_attach(old_inode, sinfo.ino);
	mark_inode_dirty(old_inode);

	old_dir->i_version = ++event;
	old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(old_dir);
	if (new_inode) {
		new_inode->i_nlink--;
		new_inode->i_ctime=CURRENT_TIME;
	}

	if (is_dir) {
		int start = MSDOS_I(new_dir)->i_logstart;
		dotdot_de->start = CT_LE_W(start);
		dotdot_de->starthi = CT_LE_W(start>>16);
		fat_mark_buffer_dirty(sb, dotdot_bh);
		old_dir->i_nlink--;
		if (new_inode) {
			new_inode->i_nlink--;
		} else {
			new_dir->i_nlink++;
			mark_inode_dirty(new_dir);
		}
	}

rename_done:
	fat_brelse(sb, dotdot_bh);
	fat_brelse(sb, old_bh);
	fat_brelse(sb, new_bh);
	return res;

}


/* Public inode operations for the VFAT fs */
struct inode_operations vfat_dir_inode_operations = {
	create:		vfat_create,
	lookup:		vfat_lookup,
	unlink:		vfat_unlink,
	mkdir:		vfat_mkdir,
	rmdir:		vfat_rmdir,
	rename:		vfat_rename,
	setattr:	fat_notify_change,
};

struct super_block *vfat_read_super(struct super_block *sb,void *data,
				    int silent)
{
	struct super_block *res;
  
	MSDOS_SB(sb)->options.isvfat = 1;

	res = fat_read_super(sb, data, silent, &vfat_dir_inode_operations);
	if (res == NULL)
		return NULL;

	if (parse_options((char *) data, &(MSDOS_SB(sb)->options))) {
		MSDOS_SB(sb)->options.dotsOK = 0;
		if (MSDOS_SB(sb)->options.posixfs) {
			MSDOS_SB(sb)->options.name_check = 's';
		}
		if (MSDOS_SB(sb)->options.name_check != 's') {
			sb->s_root->d_op = &vfat_dentry_ops[0];
		} else {
			sb->s_root->d_op = &vfat_dentry_ops[2];
		}
	}

	return res;
}
