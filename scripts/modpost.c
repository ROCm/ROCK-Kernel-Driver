/* Postprocess module symbol versions
 *
 * Copyright 2003       Kai Germaschewski
 *           2002-2003  Rusty Russell, IBM Corporation
 *
 * Based in part on module-init-tools/depmod.c,file2alias
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Usage: modpost vmlinux module1.o module2.o ...
 */

#include <ctype.h>
#include "modpost.h"

/* Are we using CONFIG_MODVERSIONS? */
int modversions = 0;
/* Do we have vmlinux? */
int have_vmlinux = 0;

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

void *do_nofail(void *ptr, const char *file, int line, const char *expr)
{
	if (!ptr) {
		fatal("Memory allocation failure %s line %d: %s.\n",
		      file, line, expr);
	}
	return ptr;
}

/* A list of all modules we processed */

static struct module *modules;

struct module *
new_module(char *modname)
{
	struct module *mod;
	char *p, *s;
	
	mod = NOFAIL(malloc(sizeof(*mod)));
	memset(mod, 0, sizeof(*mod));
	p = NOFAIL(strdup(modname));

	/* strip trailing .o */
	if ((s = strrchr(p, '.')) != NULL)
		if (strcmp(s, ".o") == 0)
			*s = '\0';

	/* add to list */
	mod->name = p;
	mod->next = modules;
	modules = mod;

	return mod;
}

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
alloc_symbol(const char *name, struct symbol *next)
{
	struct symbol *s = NOFAIL(malloc(sizeof(*s) + strlen(name) + 1));

	memset(s, 0, sizeof(*s));
	strcpy(s->name, name);
	s->next = next;
	return s;
}

/* For the hash of exported symbols */

void
new_symbol(const char *name, struct module *module, unsigned int *crc)
{
	unsigned int hash;
	struct symbol *new;

	hash = tdb_hash(name) % SYMBOL_HASH_SIZE;
	new = symbolhash[hash] = alloc_symbol(name, symbolhash[hash]);
	new->module = module;
	if (crc) {
		new->crc = *crc;
		new->crc_valid = 1;
	}
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
add_exported_symbol(const char *name, struct module *module, unsigned int *crc)
{
	struct symbol *s = find_symbol(name);

	if (!s) {
		new_symbol(name, module, crc);
		return;
	}
	if (crc) {
		s->crc = *crc;
		s->crc_valid = 1;
	}
}

void *
grab_file(const char *filename, unsigned long *size)
{
	struct stat st;
	void *map;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fstat(fd, &st) != 0)
		return NULL;

	*size = st.st_size;
	map = mmap(NULL, *size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
	close(fd);

	if (map == MAP_FAILED)
		return NULL;
	return map;
}

/*
   Return a copy of the next line in a mmap'ed file.
   spaces in the beginning of the line is trimmed away.
   Return a pointer to a static buffer.
*/
char*
get_next_line(unsigned long *pos, void *file, unsigned long size)
{
	static char line[4096];
	int skip = 1;
	size_t len = 0;
	char *p = (char *)file + *pos;
	char *s = line;

	for (; *pos < size ; (*pos)++)
	{
		if (skip && isspace(*p)) {
			p++;
			continue;
		}
		skip = 0;
		if (*p != '\n' && (*pos < size)) {
			len++;
			*s++ = *p++;
			if (len > 4095)
				break; /* Too long, stop */
		} else {
			/* End of string */
			*s = '\0';
			return line;
		}
	}
	/* End of buffer */
	return NULL;
}

void
release_file(void *file, unsigned long size)
{
	munmap(file, size);
}

void
parse_elf(struct elf_info *info, const char *filename)
{
	unsigned int i;
	Elf_Ehdr *hdr = info->hdr;
	Elf_Shdr *sechdrs;
	Elf_Sym  *sym;

	hdr = grab_file(filename, &info->size);
	if (!hdr) {
		perror(filename);
		abort();
	}
	info->hdr = hdr;
	if (info->size < sizeof(*hdr))
		goto truncated;

	/* Fix endianness in ELF header */
	hdr->e_shoff    = TO_NATIVE(hdr->e_shoff);
	hdr->e_shstrndx = TO_NATIVE(hdr->e_shstrndx);
	hdr->e_shnum    = TO_NATIVE(hdr->e_shnum);
	sechdrs = (void *)hdr + hdr->e_shoff;
	info->sechdrs = sechdrs;

	/* Fix endianness in section headers */
	for (i = 0; i < hdr->e_shnum; i++) {
		sechdrs[i].sh_type   = TO_NATIVE(sechdrs[i].sh_type);
		sechdrs[i].sh_offset = TO_NATIVE(sechdrs[i].sh_offset);
		sechdrs[i].sh_size   = TO_NATIVE(sechdrs[i].sh_size);
		sechdrs[i].sh_link   = TO_NATIVE(sechdrs[i].sh_link);
		sechdrs[i].sh_name   = TO_NATIVE(sechdrs[i].sh_name);
	}
	/* Find symbol table. */
	for (i = 1; i < hdr->e_shnum; i++) {
		const char *secstrings
			= (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;

		if (sechdrs[i].sh_offset > info->size)
			goto truncated;
		if (strcmp(secstrings+sechdrs[i].sh_name, ".modinfo") == 0) {
			info->modinfo = (void *)hdr + sechdrs[i].sh_offset;
			info->modinfo_len = sechdrs[i].sh_size;
		}
		if (sechdrs[i].sh_type != SHT_SYMTAB)
			continue;

		info->symtab_start = (void *)hdr + sechdrs[i].sh_offset;
		info->symtab_stop  = (void *)hdr + sechdrs[i].sh_offset 
			                         + sechdrs[i].sh_size;
		info->strtab       = (void *)hdr + 
			             sechdrs[sechdrs[i].sh_link].sh_offset;
	}
	if (!info->symtab_start) {
		fprintf(stderr, "modpost: %s no symtab?\n", filename);
		abort();
	}
	/* Fix endianness in symbols */
	for (sym = info->symtab_start; sym < info->symtab_stop; sym++) {
		sym->st_shndx = TO_NATIVE(sym->st_shndx);
		sym->st_name  = TO_NATIVE(sym->st_name);
		sym->st_value = TO_NATIVE(sym->st_value);
		sym->st_size  = TO_NATIVE(sym->st_size);
	}
	return;

 truncated:
	fprintf(stderr, "modpost: %s is truncated.\n", filename);
	abort();
}

void
parse_elf_finish(struct elf_info *info)
{
	release_file(info->hdr, info->size);
}

#define CRC_PFX     MODULE_SYMBOL_PREFIX "__crc_"
#define KSYMTAB_PFX MODULE_SYMBOL_PREFIX "__ksymtab_"

void
handle_modversions(struct module *mod, struct elf_info *info,
		   Elf_Sym *sym, const char *symname)
{
	unsigned int crc;

	switch (sym->st_shndx) {
	case SHN_COMMON:
		fprintf(stderr, "*** Warning: \"%s\" [%s] is COMMON symbol\n",
			symname, mod->name);
		break;
	case SHN_ABS:
		/* CRC'd symbol */
		if (memcmp(symname, CRC_PFX, strlen(CRC_PFX)) == 0) {
			crc = (unsigned int) sym->st_value;
			add_exported_symbol(symname + strlen(CRC_PFX),
					    mod, &crc);
			modversions = 1;
		}
		break;
	case SHN_UNDEF:
		/* undefined symbol */
		if (ELF_ST_BIND(sym->st_info) != STB_GLOBAL)
			break;
		/* ignore global offset table */
		if (strcmp(symname, "_GLOBAL_OFFSET_TABLE_") == 0)
			break;
		/* ignore __this_module, it will be resolved shortly */
		if (strcmp(symname, MODULE_SYMBOL_PREFIX "__this_module") == 0)
			break;
#ifdef STT_REGISTER
		if (info->hdr->e_machine == EM_SPARC ||
		    info->hdr->e_machine == EM_SPARCV9) {
			/* Ignore register directives. */
			if (ELF_ST_TYPE(sym->st_info) == STT_REGISTER)
				break;
		}
#endif
		
		if (memcmp(symname, MODULE_SYMBOL_PREFIX,
			   strlen(MODULE_SYMBOL_PREFIX)) == 0)
			mod->unres = alloc_symbol(symname +
						  strlen(MODULE_SYMBOL_PREFIX),
						  mod->unres);
		break;
	default:
		/* All exported symbols */
		if (memcmp(symname, KSYMTAB_PFX, strlen(KSYMTAB_PFX)) == 0) {
			add_exported_symbol(symname + strlen(KSYMTAB_PFX),
					    mod, NULL);
		}
		break;
	}
}

int
is_vmlinux(const char *modname)
{
	const char *myname;

	if ((myname = strrchr(modname, '/')))
		myname++;
	else
		myname = modname;

	return strcmp(myname, "vmlinux") == 0;
}

void
read_symbols(char *modname)
{
	const char *symname;
	struct module *mod;
	struct elf_info info = { };
	Elf_Sym *sym;

	/* When there's no vmlinux, don't print warnings about
	 * unresolved symbols (since there'll be too many ;) */
	have_vmlinux = is_vmlinux(modname);

	parse_elf(&info, modname);

	mod = new_module(modname);

	for (sym = info.symtab_start; sym < info.symtab_stop; sym++) {
		symname = info.strtab + sym->st_name;

		handle_modversions(mod, &info, sym, symname);
		handle_moddevtable(mod, &info, sym, symname);
	}
	maybe_frob_version(modname, info.modinfo, info.modinfo_len,
			   (void *)info.modinfo - (void *)info.hdr);
	parse_elf_finish(&info);

	/* Our trick to get versioning for struct_module - it's
	 * never passed as an argument to an exported function, so
	 * the automatic versioning doesn't pick it up, but it's really
	 * important anyhow */
	if (modversions) {
		mod->unres = alloc_symbol("struct_module", mod->unres);

		/* Always version init_module and cleanup_module, in
		 * case module doesn't have its own. */
		mod->unres = alloc_symbol("init_module", mod->unres);
		mod->unres = alloc_symbol("cleanup_module", mod->unres);
	}
}

#define SZ 500

/* We first write the generated file into memory using the
 * following helper, then compare to the file on disk and
 * only update the later if anything changed */

void __attribute__((format(printf, 2, 3)))
buf_printf(struct buffer *buf, const char *fmt, ...)
{
	char tmp[SZ];
	int len;
	va_list ap;
	
	va_start(ap, fmt);
	len = vsnprintf(tmp, SZ, fmt, ap);
	if (buf->size - buf->pos < len + 1) {
		buf->size += 128;
		buf->p = realloc(buf->p, buf->size);
	}
	strncpy(buf->p + buf->pos, tmp, len + 1);
	buf->pos += len;
	va_end(ap);
}

void
buf_write(struct buffer *buf, const char *s, int len)
{
	if (buf->size - buf->pos < len) {
		buf->size += len;
		buf->p = realloc(buf->p, buf->size);
	}
	strncpy(buf->p + buf->pos, s, len);
	buf->pos += len;
}

/* Header for the generated file */

void
add_header(struct buffer *b)
{
	buf_printf(b, "#include <linux/module.h>\n");
	buf_printf(b, "#include <linux/vermagic.h>\n");
	buf_printf(b, "#include <linux/compiler.h>\n");
	buf_printf(b, "\n");
	buf_printf(b, "MODULE_INFO(vermagic, VERMAGIC_STRING);\n");
	buf_printf(b, "\n");
	buf_printf(b, "struct module __this_module\n");
	buf_printf(b, "__attribute__((section(\".gnu.linkonce.this_module\"))) = {\n");
	buf_printf(b, " .name = __stringify(KBUILD_MODNAME),\n");
	buf_printf(b, " .init = init_module,\n");
	buf_printf(b, "#ifdef CONFIG_MODULE_UNLOAD\n");
	buf_printf(b, " .exit = cleanup_module,\n");
	buf_printf(b, "#endif\n");
	buf_printf(b, "};\n");
}

/* Record CRCs for unresolved symbols */

void
add_versions(struct buffer *b, struct module *mod)
{
	struct symbol *s, *exp;

	for (s = mod->unres; s; s = s->next) {
		exp = find_symbol(s->name);
		if (!exp || exp->module == mod) {
			if (have_vmlinux)
				fprintf(stderr, "*** Warning: \"%s\" [%s.ko] "
				"undefined!\n",	s->name, mod->name);
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
	buf_printf(b, "__attribute_used__\n");
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
		m->seen = is_vmlinux(m->name);
	}

	buf_printf(b, "\n");
	buf_printf(b, "static const char __module_depends[]\n");
	buf_printf(b, "__attribute_used__\n");
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
	struct module *mod;
	struct buffer buf = { };
	char fname[SZ];

	for (; argv[1]; argv++) {
		read_symbols(argv[1]);
	}

	for (mod = modules; mod; mod = mod->next) {
		if (is_vmlinux(mod->name))
			continue;

		buf.pos = 0;

		add_header(&buf);
		add_versions(&buf, mod);
		add_depends(&buf, mod, modules);
		add_moddevtable(&buf, mod);

		sprintf(fname, "%s.mod.c", mod->name);
		write_if_changed(&buf, fname);
	}
	return 0;
}

