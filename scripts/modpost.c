/* Postprocess module symbol versions
 *
 * Copyright 2003       Kai Germaschewski
 *           2002       Rusty Russell IBM Corporation
 *
 * Based in part on module-init-tools/depmod.c
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Usage: modpost $(NM) vmlinux module1.o module2.o ...
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/* nm command */
const char *nm;

/* Are we using CONFIG_MODVERSIONS? */
int modversions = 0;

void
usage(void)
{
	fprintf(stderr, "Usage: modpost $(NM) vmlinux module1.o module2.o\n");
	exit(1);
}

void
fatal(const char *fmt, ...)
{
	va_list arglist;

	fprintf(stderr, "FATAL: ");

	va_start(arglist, fmt);
	vfprintf(stderr, fmt, arglist);
	va_end(arglist);

	exit(1);
}

void
warn(const char *fmt, ...)
{
	va_list arglist;

	fprintf(stderr, "WARNING: ");

	va_start(arglist, fmt);
	vfprintf(stderr, fmt, arglist);
	va_end(arglist);
}

#define NOFAIL(ptr)	do_nofail((ptr), __FILE__, __LINE__, #ptr)

void *do_nofail(void *ptr, const char *file, int line, const char *expr)
{
	if (!ptr) {
		fatal("Memory allocation failure %s line %d: %s.\n",
		      file, line, expr);
	}
	return ptr;
}

/* A list of all modules we processed */

struct module {
	struct module *next;
	const char *name;
	struct symbol *unres;
	int seen;
};

static struct module *modules;

/* A hash of all exported symbols,
 * struct symbol is also used for lists of unresolved symbols */

#define SYMBOL_HASH_SIZE 1024

struct symbol {
	struct symbol *next;
	struct module *module;
	unsigned int crc;
	int crc_valid;
	char name[0];
};

static struct symbol *symbolhash[SYMBOL_HASH_SIZE];

/* This is based on the hash agorithm from gdbm, via tdb */
static inline unsigned int tdb_hash(const char *name)
{
	unsigned value;	/* Used to compute the hash value.  */
	unsigned   i;	/* Used to cycle through random values. */

	/* Set the initial value from the key size. */
	for (value = 0x238F13AF * strlen(name), i=0; name[i]; i++)
		value = (value + (((unsigned char *)name)[i] << (i*5 % 24)));

	return (1103515243 * value + 12345);
}

/* Allocate a new symbols for use in the hash of exported symbols or
 * the list of unresolved symbols per module */

struct symbol *
alloc_symbol(const char *name)
{
	struct symbol *s = NOFAIL(malloc(sizeof(*s) + strlen(name) + 1));

	memset(s, 0, sizeof(*s));
	strcpy(s->name, name);
	return s;
}

/* For the hash of exported symbols */

void
new_symbol(const char *name, struct module *module, unsigned int *crc)
{
	unsigned int hash;
	struct symbol *new = alloc_symbol(name);

	new->module = module;
	if (crc) {
		new->crc = *crc;
		new->crc_valid = 1;
	}

	hash = tdb_hash(name) % SYMBOL_HASH_SIZE;
	new->next = symbolhash[hash];
	symbolhash[hash] = new;
}

struct symbol *
find_symbol(const char *name)
{
	struct symbol *s;

	/* For our purposes, .foo matches foo.  PPC64 needs this. */
	if (name[0] == '.')
		name++;

	for (s = symbolhash[tdb_hash(name) % SYMBOL_HASH_SIZE]; s; s=s->next) {
		if (strcmp(s->name, name) == 0)
			return s;
	}
	return NULL;
}

/* Add an exported symbol - it may have already been added without a
 * CRC, in this case just update the CRC */
void
add_symbol(const char *name, struct module *module, unsigned int *crc)
{
	struct symbol *s = find_symbol(name);

	if (!s) {
		new_symbol(name, modules, crc);
		return;
	}
	if (crc) {
		s->crc = *crc;
		s->crc_valid = 1;
	}
}

#define SZ 500

void
read_symbols(char *modname)
{
	struct module *mod;
	struct symbol *s;
	char buf[SZ], sym[SZ], *p;
	FILE *pipe;
	unsigned int crc;
	int rc;

	/* read nm output */
	snprintf(buf, SZ, "%s --no-sort %s", nm, modname);
	pipe = NOFAIL(popen(buf, "r"));

	/* strip trailing .o */
	p = strstr(modname, ".o");
	if (p)
		*p = 0;

	mod = NOFAIL(malloc(sizeof(*mod)));
	mod->name = modname;
	/* add to list */
	mod->next = modules;
	modules = mod;
	
	while (fgets(buf, SZ, pipe)) {
		/* actual CRCs */
		rc = sscanf(buf, "%x A __crc_%s\n", &crc, sym);
		if (rc == 2) {
			add_symbol(sym, mod, &crc);
			modversions = 1;
			continue;
		}

		/* all exported symbols */
		rc = sscanf(buf, "%x r __ksymtab_%s", &crc, sym);
		if (rc == 2) {
			add_symbol(sym, mod, NULL);
			continue;
		}

		/* all unresolved symbols */
		rc = sscanf(buf, " U %s\n", sym);
		if (rc == 1) {
			s = alloc_symbol(sym);
			/* add to list */
			s->next = mod->unres;
			mod->unres = s;
			continue;
		}
	};
	pclose(pipe);
}

/* We first write the generated file into memory using the
 * following helper, then compare to the file on disk and
 * only update the later if anything changed */

struct buffer {
	char *p;
	int pos;
	int size;
};

void
__attribute__((format(printf, 2, 3)))
buf_printf(struct buffer *buf, const char *fmt, ...)
{
	char tmp[SZ];
	int len;
	va_list ap;
	
	va_start(ap, fmt);
	len = vsnprintf(tmp, SZ, fmt, ap);
	if (buf->size - buf->pos < len + 1) {
		if (buf->size == 0)
			buf->size = 1024;
		else
			buf->size *= 2;

		buf->p = realloc(buf->p, buf->size);
	}
	strncpy(buf->p + buf->pos, tmp, len + 1);
	buf->pos += len;
	va_end(ap);
}

/* Header for the generated file */

void
add_header(struct buffer *b)
{
	buf_printf(b, "#include <linux/module.h>\n");
	buf_printf(b, "#include <linux/vermagic.h>\n");
	buf_printf(b, "\n");
	buf_printf(b, "const char vermagic[]\n");
	buf_printf(b, "__attribute__((section(\"__vermagic\"))) =\n");
	buf_printf(b, "VERMAGIC_STRING;\n");
}

/* Record CRCs for unresolved symbols */

void
add_versions(struct buffer *b, struct module *mod)
{
	struct symbol *s, *exp;

	for (s = mod->unres; s; s = s->next) {
		exp = find_symbol(s->name);
		if (!exp) {
			fprintf(stderr, "*** Warning: \"%s\" [%s.ko] "
				"undefined!\n",
				s->name, mod->name);
			continue;
		}
		s->module = exp->module;
		s->crc_valid = exp->crc_valid;
		s->crc = exp->crc;
	}

	if (!modversions)
		return;

	buf_printf(b, "\n");
	buf_printf(b, "static const struct modversion_info ____versions[]\n");
	buf_printf(b, "__attribute__((section(\"__versions\"))) = {\n");

	for (s = mod->unres; s; s = s->next) {
		if (!s->module) {
			continue;
		}
		if (!s->crc_valid) {
			fprintf(stderr, "*** Warning: \"%s\" [%s.ko] "
				"has no CRC!\n",
				s->name, mod->name);
			continue;
		}
		buf_printf(b, "\t{ %#8x, \"%s\" },\n", s->crc, s->name);
	}

	buf_printf(b, "};\n");
}

void
add_depends(struct buffer *b, struct module *mod, struct module *modules)
{
	struct symbol *s;
	struct module *m;
	int first = 1;

	for (m = modules; m; m = m->next) {
		if (strcmp(m->name, "vmlinux") == 0)
			m->seen = 1;
		else 
			m->seen = 0;
	}

	buf_printf(b, "\n");
	buf_printf(b, "static const char __module_depends[]\n");
	buf_printf(b, "__attribute__((section(\".modinfo\"))) =\n");
	buf_printf(b, "\"depends=");
	for (s = mod->unres; s; s = s->next) {
		if (!s->module)
			continue;

		if (s->module->seen)
			continue;

		s->module->seen = 1;
		buf_printf(b, "%s%s", first ? "" : ",",
			   strrchr(s->module->name, '/') + 1);
		first = 0;
	}
	buf_printf(b, "\";\n");
}

void
write_if_changed(struct buffer *b, const char *fname)
{
	char *tmp;
	FILE *file;
	struct stat st;

	file = fopen(fname, "r");
	if (!file)
		goto write;

	if (fstat(fileno(file), &st) < 0)
		goto close_write;

	if (st.st_size != b->pos)
		goto close_write;

	tmp = NOFAIL(malloc(b->pos));
	if (fread(tmp, 1, b->pos, file) != b->pos)
		goto free_write;

	if (memcmp(tmp, b->p, b->pos) != 0)
		goto free_write;

	free(tmp);
	fclose(file);
	return;

 free_write:
	free(tmp);
 close_write:
	fclose(file);
 write:
	file = fopen(fname, "w");
	if (!file) {
		perror(fname);
		exit(1);
	}
	if (fwrite(b->p, 1, b->pos, file) != b->pos) {
		perror(fname);
		exit(1);
	}
	fclose(file);
}

int
main(int argc, char **argv)
{
	int i;
	struct module *mod;
	struct buffer buf = { };
	char fname[SZ];

	if (argc < 3)
		usage();

	nm = argv[1];

	for (i = 2; i < argc; i++) {
		read_symbols(argv[i]);
	}

	for (mod = modules; mod; mod = mod->next) {
		if (strcmp(mod->name, "vmlinux") == 0)
			continue;

		buf.pos = 0;

		add_header(&buf);
		add_versions(&buf, mod);
		add_depends(&buf, mod, modules);

		sprintf(fname, "%s.ver.c", mod->name);
		write_if_changed(&buf, fname);
	}
	return 0;
}

