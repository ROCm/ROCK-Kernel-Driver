
/*
 * Below is an implementation of hand crafted "recursive descent" parser. It
 * is not based on the yacc toolkit.
 *
 * This parser was initially supposed to be a prototype implementation created
 * to develop and test back end methods to be used by the yacc parser. For
 * this reason, parser's functions are placed into proto_* name space.
 *
 * Parser is actually almost non-recursive. That is, it all information
 * required during the parsing is kept in a special stack, allocated by
 * kmalloc, rather than on the native C stack. Parser uses C recursion, but
 * only for the simplicity of prototyping---the only information communicated
 * via C stack is integer return codes. It should be trivial to maintain it in
 * the allocated stack and to re-write parser in the iterative manner.
 *
 * Grammar:
 *
 *     expression ::= binary_exp { ; binary_exp }
 *
 *     binary_exp ::= path | path binop binary_exp
 *
 *     path       ::= literal | rel_path | / rel_path
 *
 *     literal    ::= "string" | #number
 *
 *     rel_path   ::= name { / name }
 *
 *     name       ::= name_token | ( expression )
 *
 *     binop      ::= <-
 *
 *
 * Examples:
 *
 *     (1) a/b <- /etc/passwd
 *
 *     (2) "foo"
 *
 *     (3) #3
 *
 *
 * Implementation:
 *
 * parsing is done in the context represented by data type proto_ctx_t.
 *
 * Types of terminal tokens are represented by values of proto_token_type_t
 * enumeration. Particular tokens, met during parsing are represented by
 * instances of proto_token_t.
 *
 * proto_ctx_t contains a stack used to recursively parse
 * sub-expressions. Each subexpression has a value represented by instance of
 * proto_val_t. Values have types, types are represented by proto_val_type_t
 * enumeration.
 *
 * Each non-terminal token is parsed by special function, and all terminals
 * are parsed by next_token().
 *
 * TO BE DONE:
 *
 *     1. more sophisticated fs_point_t with support for lnodes
 *
 *     2. hub-based assignment
 *
 *     3. locking during name resolutions
 *
 *
 *
 *
 */


#include "debug.h"
#include "lnode.h"

#include <linux/ctype.h>
#include <linux/mount.h> /* mnt{get,put}() */

/* maximal recursion depth */
#define PROTO_LEVELS (100)

/* types of terminal tokens */
typedef enum proto_token_type {
	TOKEN_NAME,		/* file name, part of a pathname */
	TOKEN_SLASH,		/* / */
	TOKEN_ASSIGNMENT,	/* <- */
	TOKEN_LPAREN,		/* ( */
	TOKEN_RPAREN,		/* ) */
	TOKEN_STRING,		/* "foo" string literal */
	TOKEN_NUMBER,		/* #100  decimal number */
	TOKEN_LESS_THAN,	/* < */
	TOKEN_GREATER_THAN,	/* > */
	TOKEN_EQUAL_TO,		/* = */
	TOKEN_SEMICOLON,	/* ; */
	TOKEN_COMMA,    	/* , */
	TOKEN_EOF,		/* eof-of-file reached */
	TOKEN_INVALID		/* syntax-error */
} proto_token_type_t;

/* terminal token */
typedef struct proto_token {
	/* type of the token */
	proto_token_type_t  type;
	/* position within command, where this token starts */
	int                 pos;
	/* union of data associated with this token */
	union {
		struct {
			/* for name and string literal: token length */
			int len;
			/* offset from ->pos to position where actual token
			 * content starts */
			int delta;
		} name, string;
		struct {
			/* for number---its value */
			long val;
		} number;
	} u;
} proto_token_t;

/* types of values that expressions can result in */
typedef enum proto_val_type {
	/* file system object---pathname results in this */
	VAL_FSOBJ,
	/* number---number literal and assignment result in this */
	VAL_NUMBER,
	/* string---string literal results in this */
	VAL_STRING,
	/* error---ill-formed expression, and execution error result in
	 * this */
	VAL_ERROR,
	/* no value */
	VAL_VOID
} proto_val_type_t;

/* file system object representation. This is needed to interface with VFS */
typedef struct fs_point {
	struct dentry   *dentry;
	struct vfsmount *mnt;
} fs_point_t;

/* value of expression */
typedef struct proto_val {
	/* value type */
	proto_val_type_t type;
	/* value itself. Union by various value types. */
	union {
		/* VAL_FSOBJ */
		fs_point_t fsobj;
		/* VAL_NUMBER */
		long       number;
		/* VAL_STRING */
		struct {
			char *string;
			int   len;
		} string;
		/* VAL_ERROR */
		struct {
			/* error message */
			char  *error;
			/* position in a command, where error occurred */
			int    error_pos;
		} error;
	} u;
} proto_val_t;

/* data maintained for each recursion level. */
typedef struct proto_level {
	/* error message, if error occurred at this level */
	const char    *error;
	/* error position within command, if error occurred at this level */
	int            error_pos;
	/* value of expression, calculated at this level */
	proto_val_t    val;
	/* point in a file system from which relative names are resolved at
	 * this level */
	fs_point_t     cur;
} proto_level_t;

/* global parsing flags */
typedef enum proto_flags {
	/* set whenever syntax error is detected */
	CTX_PARSE_ERROR = (1 << 0)
} proto_flags_t;

/* parsing context. */
typedef struct proto_ctx {
	/* global flags */
	__u32          flags;
	/* command being parsed and executed */
	const char    *command;
	/* length of ->command */
	int            len;
	/* current parsing position within ->command */
	int            pos;
	/* recursion depth */
	int            depth;
	/* array of levels */
	proto_level_t *level;
	/* where to resolve relative pathnames from */
	fs_point_t     cwd;
	/* where to resolve absolute pathnames from */
	fs_point_t     root;
} proto_ctx_t;

static int parse_exp(proto_ctx_t *ctx);

#define PTRACE(ctx, format, ... )						\
({										\
	ON_TRACE(TRACE_PARSE, "parse: %02i at %i[%c]: %s: " format "\n",	\
		 ctx->depth,							\
		 ctx->pos, char_at(ctx, ctx->pos) ? : '.',			\
		 __FUNCTION__ , __VA_ARGS__);					\
})

/* methods to manipulate fs_point_t objects */

/* acquire a reference to @fsobj */
static fs_point_t *fsget(fs_point_t *fsobj)
{
	dget(fsobj->dentry);
	mntget(fsobj->mnt);
	return fsobj;
}

/* release a reference to @fsobj */
static void fsput(fs_point_t *fsobj)
{
	if (fsobj->dentry != NULL) {
		dput(fsobj->dentry);
		fsobj->dentry = NULL;
	}
	if (fsobj->mnt != NULL) {
		mntput(fsobj->mnt);
		fsobj->mnt = NULL;
	}
}

/* duplicate a reference to @src in @dst */
static fs_point_t *fscpy(fs_point_t *dst, fs_point_t *src)
{
	*dst = *src;
	return fsget(dst);
}

/* current character in a command */
static char char_at(proto_ctx_t *ctx, int pos)
{
	if (pos < ctx->len)
		return ctx->command[pos];
	else
		return 0;
}

/* current level */
static proto_level_t *get_level(proto_ctx_t *ctx)
{
	assert("nikita-3233", ctx->depth < PROTO_LEVELS);
	return &ctx->level[ctx->depth];
}

/* current value---value stored in the current level */
static proto_val_t *get_val(proto_ctx_t *ctx)
{
	return &get_level(ctx)->val;
}

/* from where relative names should be resolved */
static fs_point_t *get_cur(proto_ctx_t *ctx)
{
	int i;

	for (i = ctx->depth; i >= 0; -- i) {
		if (ctx->level[i].cur.dentry != NULL)
			return &ctx->level[i].cur;
	}
	return &ctx->cwd;
}

/* move typed value from one location to another */
static void proto_val_move(proto_val_t *dst, proto_val_t *src)
{
	xmemmove(dst, src, sizeof *dst);
	src->type = VAL_VOID;
}

/* finish with value */
static void proto_val_put(proto_val_t *val)
{
	switch(val->type) {
	case VAL_FSOBJ:
		fsput(&val->u.fsobj);
		break;
	case VAL_STRING:
		if (val->u.string.string != NULL) {
			kfree(val->u.string.string);
			val->u.string.string = NULL;
		}
		break;
	case VAL_NUMBER:
	case VAL_ERROR:
	case VAL_VOID:
		break;
	}
	val->type = VAL_VOID;
}

/* move value one level up. Useful when value produced by an expression is the
 * value of its sub-expression. */
static void proto_val_up(proto_ctx_t *ctx)
{
	assert("nikita-3236", ctx->depth > 0);
	proto_val_move(&ctx->level[ctx->depth - 1].val, get_val(ctx));
}

/* signal an error */
static void post_error(proto_ctx_t *ctx, char *error)
{
	proto_val_t *val;

	PTRACE(ctx, "%s", error);

	get_level(ctx)->error = error;
	get_level(ctx)->error_pos = ctx->pos;
	ctx->flags |= CTX_PARSE_ERROR;
	val = get_val(ctx);
	proto_val_put(val);
	val->type = VAL_ERROR;
	val->u.error.error = error;
	val->u.error.error_pos = ctx->pos;
}

/* parse string literal */
static proto_token_type_t extract_string(proto_ctx_t *ctx, int *outpos,
					 proto_token_t *token)
{
	int len;
	int pos;

	/* simplistic string literal---no escape handling. Feel free to
	 * improve. */
	pos = *outpos;
	for (len = 0; ; ++ len, ++ pos) {
		char ch;

		ch = char_at(ctx, pos);
		if (ch == '"') {
			token->type = TOKEN_STRING;
			token->u.string.len = len;
			/* string literal start with a quote that should be
			 * skipped */
			token->u.string.delta = 1;
			*outpos = pos + 1;
			PTRACE(ctx, "%i", len);
			break;
		} else if (ch == 0) {
			token->type = TOKEN_INVALID;
			post_error(ctx, "eof in string");
			break;
		}
	}
	return token->type;
}

static int unhex(char ch)
{
	ch = tolower(ch);

	if (ch >= '0' && ch <= '9')
		return ch - '0';
	else if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 0xa;
	return 0xff;
}

/* construct zero number */
static proto_token_type_t number_zero(proto_token_t *token)
{
	token->type = TOKEN_NUMBER;
	token->u.number.val = 0;
	return TOKEN_NUMBER;
}

/* parse number literal */
static proto_token_type_t extract_number(proto_ctx_t *ctx, int *pos,
					 proto_token_t *token)
{
	char ch;
	int  sign;
	int  base;
	long val;

	ch = char_at(ctx, *pos);

	sign = +1;

	if (ch == '+')
		++ *pos;
	else if (ch == '-') {
		++ *pos;
		sign = -1;
	} else if (!isdigit(ch)) {
		token->type = TOKEN_INVALID;
		*pos = token->pos;
		return TOKEN_INVALID;
	}

	val = (ch - '0');
	base = 10;
	++ *pos;
	if (val == 0) {
		base = 010;
		if (!isxdigit(char_at(ctx, *pos)) &&
		    isxdigit(char_at(ctx, *pos + 1))) {
			/* 0[xXoOdDtT]<digits> */
			switch (char_at(ctx, *pos)) {
			case 'x':
			case 'X':
				base = 0x10;
				break;
			case 'o':
			case 'O':
				base = 010;
				break;
			case 'd':
			case 'D':
				base = 10;
				break;
			case 't':
			case 'T':
				base = 2;
				break;
			default:
				return number_zero(token);
			}
			if (unhex(char_at(ctx, *pos + 1)) >= base)
				return number_zero(token);
			++ *pos;
		}
	}
	for (;; ++ *pos) {
		int  digit;
		long newval;

		ch = char_at(ctx, *pos);
		if (!isxdigit(ch))
			break;
		digit = unhex(ch);
		if (digit < 0 || digit >= base)
			break;
		newval = val * base + digit;
		if (newval > val || (val == newval && digit == 0))
			val = newval;
		else {
			token->type = TOKEN_INVALID;
			post_error(ctx, "integer overflow");
			*pos = token->pos;
			return TOKEN_INVALID;
		}
	}
	token->type = TOKEN_NUMBER;
	PTRACE(ctx, "%li", val);
	token->u.number.val = sign * val;
	return token->type;
}

/* parse name token */
static proto_token_type_t extract_name(proto_ctx_t *ctx, int *pos,
				       proto_token_t *token)
{
	int len;

	/* name is sequence of any characters save for /, <, and + */
	for (len = 0;  ; ++ *pos, ++ len) {
		char ch;

		ch = char_at(ctx, *pos);
		if (isspace(ch))
			break;
		if (ch == 0)
			break;
		if (strchr("/+-=()[]<>;,", ch) != NULL)
			break;
	}
	if (len == 0) {
		token->type = TOKEN_INVALID;
	} else {
		token->type = TOKEN_NAME;
		token->u.name.len = len;
		token->u.name.delta = 0;
		PTRACE(ctx, "%i", len);
	}
	return token->type;
}

static proto_token_type_t extract_extended_string(proto_ctx_t *ctx, int *pos,
						  proto_token_t *token,
						  proto_token_type_t ttype)
{
	proto_token_t width;

	/* s<width>:bytes */
	token->type = TOKEN_INVALID;
	++ *pos;
	/* <width>:bytes */
	if (extract_number(ctx, pos, &width) == TOKEN_NUMBER) {
		/* :bytes */
		if (char_at(ctx, *pos) == ':') {
			++ *pos;
			/* bytes */
			token->type = ttype;
			token->u.string.len = width.u.number.val;
			token->u.string.delta = *pos - token->pos;
			*pos += token->u.string.len;
		}
	}
	if (token->type == TOKEN_INVALID)
		*pos = token->pos;
	return token->type;
}

/* parse #-literal */
static proto_token_type_t extract_extended_literal(proto_ctx_t *ctx, int *pos,
						   proto_token_t *token)
{
	char ch;

	ch = char_at(ctx, *pos);
	if (isdigit(ch))
		return extract_number(ctx, pos, token);

	/* "#s<width>:bytes" */
	if (ch == 's')
		return extract_extended_string(ctx, pos, token, TOKEN_STRING);
	if (ch == 'n')
		return extract_extended_string(ctx, pos, token, TOKEN_NAME);
	/* put "#" back */
	-- *pos;
	token->type = TOKEN_INVALID;
	return TOKEN_INVALID;
}

/* return next token */
static proto_token_type_t next_token(proto_ctx_t *ctx,
				     proto_token_t *token)
{
	proto_token_type_t ttype;
	int pos;

	/* skip white spaces */
	for (; isspace(char_at(ctx, ctx->pos)) ; ++ ctx->pos)
	{;}

	pos = token->pos = ctx->pos;
	switch (char_at(ctx, pos ++)) {
	case '/':
		ttype = TOKEN_SLASH;
		break;
	case '(':
		ttype = TOKEN_LPAREN;
		break;
	case ')':
		ttype = TOKEN_RPAREN;
		break;
	case ';':
		ttype = TOKEN_SEMICOLON;
		break;
	case ',':
		ttype = TOKEN_COMMA;
		break;
	case '"':
		ttype = extract_string(ctx, &pos, token);
		break;
	case '<':
		if (char_at(ctx, pos) == '-') {
			ttype = TOKEN_ASSIGNMENT;
			++ pos;
		} else
			ttype = TOKEN_LESS_THAN;
		break;
	case 0:
		ttype = TOKEN_EOF;
		-- pos;
		break;
	case '#':
		ttype = extract_extended_literal(ctx, &pos, token);
		break;
	default:
		-- pos;
		ttype = extract_name(ctx, &pos, token);
		break;
	}
	token->type = ttype;
	ctx->pos = pos;
	PTRACE(ctx, "%i", ttype);
	return ttype;
}

/* push token back into command, so that next_token() will return @token
 * again */
static void back_token(proto_ctx_t *ctx, proto_token_t *token)
{
	assert("nikita-3237", ctx->pos >= token->pos);
	/* it is -that- simple */
	ctx->pos = token->pos;
}

/* finish with context, release all resources */
static void ctx_done(proto_ctx_t *ctx)
{
	if (ctx->level != NULL) {
		kfree(ctx->level);
		ctx->level = NULL;
	}
	fsput(&ctx->cwd);
	fsput(&ctx->root);
}

/* initialize context for parsing and executing @command */
static int ctx_init(proto_ctx_t *ctx, const char *command)
{
	int result;

	xmemset(ctx, 0, sizeof *ctx);
	ctx->command = command;
	ctx->len = strlen(command);
	ctx->level = kmalloc(sizeof (ctx->level[0]) * PROTO_LEVELS,
			     GFP_KERNEL);
	xmemset(ctx->level, 0, sizeof (ctx->level[0]) * PROTO_LEVELS);
	if (ctx->level != NULL) {

		read_lock(&current->fs->lock);
		ctx->cwd.dentry  = dget(current->fs->pwd);
		ctx->cwd.mnt     = mntget(current->fs->pwdmnt);
		ctx->root.dentry = dget(current->fs->root);
		ctx->root.mnt    = mntget(current->fs->rootmnt);
		read_unlock(&current->fs->lock);

		result = 0;
	} else
		result = -ENOMEM;
	if (result != 0)
		ctx_done(ctx);
	return result;
}

/* go one level deeper to parse and execute sub-expression */
static int inlevel(proto_ctx_t *ctx)
{
	if (ctx->depth >= PROTO_LEVELS - 1) {
		/* handle stack overflow */
		post_error(ctx, "stack overflow");
		return -EOVERFLOW;
	}
	++ ctx->depth;
	xmemset(get_level(ctx), 0, sizeof *get_level(ctx));
	get_val(ctx)->type = VAL_VOID;
	return 0;
}

/* go one level up */
static void exlevel(proto_ctx_t *ctx)
{
	assert("nikita-3235", ctx->depth > 0);
	proto_val_put(get_val(ctx));
	fsput(&get_level(ctx)->cur);
	-- ctx->depth;
}

/* given @token which should be token for string literal, produce string
 * value */
static void build_string_val(proto_ctx_t *ctx,
			     proto_token_t *token, proto_val_t *val)
{
	int len;

	assert("nikita-3238",
	       token->type == TOKEN_STRING || token->type == TOKEN_NAME);

	len = token->u.string.len;
	val->type = VAL_STRING;
	val->u.string.string = kmalloc(len + 1, GFP_KERNEL);
	if (val->u.string.string != NULL) {
		strncpy(val->u.string.string,
			ctx->command + token->pos + token->u.string.delta, len);
		val->u.string.string[len] = 0;
		val->u.string.len = len;
	}
}

/* given @token which should be token for a number literal, produce number
 * value */
static void build_number_val(proto_ctx_t *ctx,
			     proto_token_t *token, proto_val_t *val)
{
	assert("nikita-3245", token->type == TOKEN_NUMBER);

	val->type = VAL_NUMBER;
	val->u.number = token->u.number.val;
}

/* follow mount points. COPIED from fs/namei.c */
static void follow_mount(fs_point_t * fsobj)
{
	while (d_mountpoint(fsobj->dentry)) {
		struct vfsmount *mounted;

		spin_lock(&dcache_lock);
		mounted = lookup_mnt(fsobj->mnt, fsobj->dentry);
		if (!mounted) {
			spin_unlock(&dcache_lock);
			break;
		}
		fsobj->mnt = mntget(mounted);
		spin_unlock(&dcache_lock);
		dput(fsobj->dentry);
		mntput(mounted->mnt_parent);
		fsobj->dentry = dget(mounted->mnt_root);
	}
}

/* resolve @name within @parent and return resulting object in @fsobj.
 * COPIED from fs/namei.c, fs/dcache.c */
static int lookup(fs_point_t * parent, const char * name, fs_point_t * fsobj)
{
	unsigned long hash;
	struct qstr qname;
	int result;
	unsigned int c;

	qname.name = name;
	c = *(const unsigned char *)name;

	hash = init_name_hash();
	do {
		name++;
		hash = partial_name_hash(c, hash);
		c = *(const unsigned char *)name;
	} while (c != 0);
	qname.len = name - (const char *) qname.name;
	qname.hash = end_name_hash(hash);

	result = 0;
	fsobj->dentry = __d_lookup(parent->dentry, &qname);
	if (fsobj->dentry == NULL) {
		struct inode *dir;

		dir = parent->dentry->d_inode;
		down(&dir->i_sem);
		fsobj->dentry = d_lookup(parent->dentry, &qname);
		if (fsobj->dentry == NULL) {
			struct dentry * new;

			new = d_alloc(parent->dentry, &qname);
			if (new != NULL) {
				fsobj->dentry = dir->i_op->lookup(dir, new);
				if (fsobj->dentry != NULL) {
					dput(new);
					result = PTR_ERR(fsobj->dentry);
				} else if (new->d_inode != NULL)
					fsobj->dentry = new;
				else {
					dput(new);
					result = RETERR(-ENOENT);
				}
			} else
				result = RETERR(-ENOMEM);
		}
		up(&dir->i_sem);
	}
	if (result == 0) {
		fsobj->mnt = parent->mnt;
		follow_mount(fsobj);
	}
	return result;
}

#define START_KERNEL_IO				\
        {					\
		mm_segment_t __ski_old_fs;	\
						\
		__ski_old_fs = get_fs();	\
		set_fs(KERNEL_DS)

#define END_KERNEL_IO				\
		set_fs(__ski_old_fs);		\
	}

#define PUMP_BUF_SIZE (PAGE_CACHE_SIZE)

/* perform actual assignment (copying) from @righthand to @lefthand */
static int pump(fs_point_t *lefthand, fs_point_t *righthand)
{
	int result;
	char *buf;
	loff_t readoff;
	loff_t writeoff;
	struct file *dst;
	struct file *src;

	buf = kmalloc(PUMP_BUF_SIZE, GFP_KERNEL);
	if (buf == NULL)
		return RETERR(-ENOMEM);

	src = dentry_open(righthand->dentry, righthand->mnt, O_RDONLY);
	if (!IS_ERR(src)) {
		mntget(righthand->mnt); /* simulate open_namei() */
		dget(righthand->dentry);
		dst = dentry_open(lefthand->dentry, lefthand->mnt, O_WRONLY);
		if (!IS_ERR(dst)) {
			mntget(lefthand->mnt); /* simulate open_namei() */
			dget(lefthand->dentry);
			readoff = writeoff = 0;
			result = 0;
			START_KERNEL_IO;
			while (result >= 0) {
				result = vfs_read(src,
						  buf, PUMP_BUF_SIZE, &readoff);
				if (result <= 0)
					break;
				/* give other threads chance to run */
				preempt_point();
				result = vfs_write(dst, buf, result, &writeoff);
			}
			END_KERNEL_IO;
			if (result == 0)
				result = writeoff;
			filp_close(dst, current->files);
		} else
			result = PTR_ERR(dst);
		filp_close(src, current->files);
	} else
		result = PTR_ERR(src);

	kfree(buf);
	return result;
}

/* perform actual assignment (copying) from buffer to @lefthand */
static int pump_buf(fs_point_t *lefthand, char *buf, int len)
{
	int result;
	loff_t writeoff;
	struct file *dst;

	dst = dentry_open(lefthand->dentry, lefthand->mnt, O_WRONLY);
	if (!IS_ERR(dst)) {
		writeoff = 0;
		result = 0;
		START_KERNEL_IO;
		while (len > writeoff && result >= 0)
			result = vfs_write(dst, buf + writeoff,
					   len - writeoff, &writeoff);
		END_KERNEL_IO;
		if (result == 0)
			result = writeoff;
		filp_close(dst, current->files);
	} else
		result = PTR_ERR(dst);

	return result;
}

/* prepare and perform assignment, store result at the level */
static int proto_assign(proto_ctx_t *ctx, proto_val_t *lhs, proto_val_t *rhs)
{
	int result;
	fs_point_t dst;

	if (lhs->type != VAL_FSOBJ) {
		post_error(ctx, "cannot assign");
		return -EINVAL;
	}

	fscpy(&dst, &lhs->u.fsobj);
	switch (rhs->type) {
	case VAL_FSOBJ: {
		result = pump(&dst, &rhs->u.fsobj);
		break;
	}
	case VAL_NUMBER: {
		char buf[20];

		snprintf(buf, sizeof buf, "%li", rhs->u.number);
		result = pump_buf(&dst, buf, strlen(buf));
		break;
	}
	case VAL_STRING: {
		result = pump_buf(&dst, rhs->u.string.string, rhs->u.string.len);
		break;
	}
	default:
		post_error(ctx, "lnode expected");
		result = -EINVAL;
	}
	fsput(&dst);
	if (result >= 0) {
		proto_val_t *ret;

		ret = get_val(ctx);
		proto_val_put(ret);
		ret->type = VAL_NUMBER;
		ret->u.number = result;
		result = 0;
	}
	return result;
}

/* parse "name" token */
static int parse_name(proto_ctx_t *ctx)
{
	int result;
	proto_token_t token;

	/* name ::= name_token | ( expression ) */

	next_token(ctx, &token);
	PTRACE(ctx, "%i", token.type);

	result = 0;
	switch (token.type) {
	case TOKEN_NAME: {
		proto_val_t name;
		fs_point_t  child;

		build_string_val(ctx, &token, &name);
		result = lookup(get_cur(ctx), name.u.string.string, &child);
		if (result == -ENOENT || child.dentry->d_inode == NULL) {
			post_error(ctx, "not found");
			result = -ENOENT;
		} else if (result == 0) {
			proto_val_put(get_val(ctx));
			get_val(ctx)->type = VAL_FSOBJ;
			fscpy(&get_val(ctx)->u.fsobj, &child);
		} else
			post_error(ctx, "lookup failure");
		proto_val_put(&name);
		break;
	}
	case TOKEN_LPAREN: {
		proto_token_t rparen;

		result = inlevel(ctx);
		if (result == 0) {
			result = parse_exp(ctx);
			proto_val_up(ctx);
			exlevel(ctx);
			if (next_token(ctx, &rparen) != TOKEN_RPAREN) {
				post_error(ctx, "expecting `)'");
				result = -EINVAL;
			}
		}
		break;
	}
	case TOKEN_INVALID:
		post_error(ctx, "huh");
		result = -EINVAL;
	default:
		back_token(ctx, &token);
		break;
	}
	return result;
}

/* parse "path" token */
static int parse_rel_path(proto_ctx_t *ctx, fs_point_t *start)
{
	int result;

	/* rel_path ::= name { / name } */

	result = inlevel(ctx);
	if (result != 0)
		return result;

	fscpy(&get_level(ctx)->cur, start);

	while (1) {
		proto_token_t token;
		proto_val_t  *val;

		result = parse_name(ctx);
		if (result != 0)
			break;

		val = get_val(ctx);
		if (val->type != VAL_FSOBJ) {
			post_error(ctx, "name is not an file system object");
			break;
		}

		fsput(&get_level(ctx)->cur);
		fscpy(&get_level(ctx)->cur, &val->u.fsobj);

		next_token(ctx, &token);
		PTRACE(ctx, "%i", token.type);

		if (token.type != TOKEN_SLASH) {
			back_token(ctx, &token);
			break;
		}
	}
	proto_val_up(ctx);
	exlevel(ctx);
	return result;
}

/* parse "path" token */
static int parse_path(proto_ctx_t *ctx)
{
	int result;
	proto_token_t token;

	/* path ::= literal | rel_path | / rel_path */

	next_token(ctx, &token);
	PTRACE(ctx, "%i", token.type);

	result = 0;
	switch (token.type) {
	case TOKEN_STRING:
		build_string_val(ctx, &token, get_val(ctx));
		break;
	case TOKEN_NUMBER:
		build_number_val(ctx, &token, get_val(ctx));
		break;
	case TOKEN_SLASH:
		result = parse_rel_path(ctx, &ctx->root);
		break;
	default:
		back_token(ctx, &token);
		result = parse_rel_path(ctx, get_cur(ctx));
		break;
	case TOKEN_INVALID:
		post_error(ctx, "cannot parse path");
		result = -EINVAL;
		back_token(ctx, &token);
		break;
	}
	return result;
}

/* parse "binary_exp" token */
static int parse_binary_exp(proto_ctx_t *ctx)
{
	int result;
	proto_val_t *lhs;

	/* binary_exp ::= path | path binop binary_exp */

	result = inlevel(ctx);
	if (result != 0)
		return result;

	result = parse_path(ctx);
	if (result == 0) {
		proto_token_t  token;

		lhs = get_val(ctx);

		next_token(ctx, &token);
		PTRACE(ctx, "%i", token.type);

		if (token.type == TOKEN_ASSIGNMENT) {
			result = inlevel(ctx);
			if (result == 0) {
				result = parse_binary_exp(ctx);
				if (result == 0) {
					proto_val_t *rhs;

					rhs = get_val(ctx);
					result = proto_assign(ctx, lhs, rhs);
				}
				proto_val_up(ctx);
				exlevel(ctx);
			}
		} else
			back_token(ctx, &token);
	}
	proto_val_up(ctx);
	exlevel(ctx);
	return result;
}

/* parse "expression" token */
static int parse_exp(proto_ctx_t *ctx)
{
	int result;

	/* expression ::= binary_exp { ; binary_exp } */

	result = inlevel(ctx);
	if (result != 0)
		return result;

	while (1) {
		proto_token_t  token;

		result = parse_binary_exp(ctx);
		proto_val_up(ctx);
		if (result != 0)
			break;

		next_token(ctx, &token);
		PTRACE(ctx, "%i", token.type);

		if (token.type != TOKEN_SEMICOLON) {
			back_token(ctx, &token);
			break;
		}
		/* discard value */
		proto_val_put(get_val(ctx));
	}
	exlevel(ctx);
	return result;
}

/* execute @command */
static int execute(proto_ctx_t *ctx)
{
	int result;

	inlevel(ctx);
	fscpy(&get_level(ctx)->cur, &ctx->cwd);
	result = parse_exp(ctx);
	if (get_val(ctx)->type == VAL_NUMBER)
		result = get_val(ctx)->u.number;
	exlevel(ctx);
	assert("nikita-3234", ctx->depth == 0);
	if (char_at(ctx, ctx->pos) != 0) {
		post_error(ctx, "garbage after expression");
		if (result == 0)
			result = -EINVAL;
	}

	if (ctx->flags & CTX_PARSE_ERROR) {
		int i;

		printk("Syntax error in ``%s''\n", ctx->command);
		for (i = PROTO_LEVELS - 1; i >= 0; --i) {
			proto_level_t *level;

			level = &ctx->level[i];
			if (level->error != NULL) {
				printk("    %02i: %s at %i\n",
				       i, level->error, level->error_pos);
			}
		}
		result = -EINVAL;
	}
	return result;
}

/* entry point */
asmlinkage long sys_reiser4(const char __user * command)
{
	int    result;
	char * inkernel;

	inkernel = getname(command);
	if (!IS_ERR(inkernel)) {
		proto_ctx_t ctx;

		result = ctx_init(&ctx, inkernel);
		if (result == 0) {
			result = execute(&ctx);
			ctx_done(&ctx);
		}
		putname(inkernel);
	} else
		result = PTR_ERR(inkernel);
	return result;
}
