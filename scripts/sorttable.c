/*
 * sorttable.c: Sort vmlinux tables
 *
 * Copyright 2011 - 2012 Cavium, Inc.
 *
 * Based on code taken from recortmcount.c which is:
 *
 * Copyright 2009 John F. Reiser <jreiser@BitWagon.com>.  All rights reserved.
 * Licensed under the GNU General Public License, version 2 (GPLv2).
 *
 * Restructured to fit Linux format, as well as other updates:
 *  Copyright 2010 Steven Rostedt <srostedt@redhat.com>, Red Hat Inc.
 */

/*
 * Strategy: alter the vmlinux file in-place.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <getopt.h>
#include <elf.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <tools/be_byteshift.h>
#include <tools/le_byteshift.h>

#ifndef EM_ARCOMPACT
#define EM_ARCOMPACT	93
#endif

#ifndef EM_XTENSA
#define EM_XTENSA	94
#endif

#ifndef EM_AARCH64
#define EM_AARCH64	183
#endif

#ifndef EM_MICROBLAZE
#define EM_MICROBLAZE	189
#endif

#ifndef EM_ARCV2
#define EM_ARCV2	195
#endif

static int fd_map = -1;	/* File descriptor for file being modified. */
static int mmap_succeeded; /* Boolean flag. */
static void *ehdr_curr; /* current ElfXX_Ehdr *  for resource cleanup */
static struct stat sb;	/* Remember .st_size, etc. */

/* setjmp() return values */
enum {
	SJ_SETJMP = 0,  /* hardwired first return */
	SJ_FAIL,
	SJ_SUCCEED
};

enum sectype {
	SEC_TYPE_EXTABLE,
	SEC_TYPE_UNDWARF,
};

/* Per-file resource cleanup when multiple files. */
static void
cleanup(void)
{
	if (mmap_succeeded)
		munmap(ehdr_curr, sb.st_size);
	if (fd_map >= 0)
		close(fd_map);
}

/*
 * Get the whole file as a programming convenience in order to avoid
 * malloc+lseek+read+free of many pieces.  If successful, then mmap
 * avoids copying unused pieces; else just read the whole file.
 * Open for both read and write.
 */
static void *mmap_file(char const *fname)
{
	void *addr;

	fd_map = open(fname, O_RDWR);
	if (fd_map < 0 || fstat(fd_map, &sb) < 0) {
		perror(fname);
		return NULL;
	}
	if (!S_ISREG(sb.st_mode)) {
		fprintf(stderr, "not a regular file: %s\n", fname);
		return NULL;
	}
	addr = mmap(0, sb.st_size, PROT_READ|PROT_WRITE, MAP_SHARED,
		    fd_map, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "Could not mmap file: %s\n", fname);
		return NULL;
	}
	mmap_succeeded = 1;

	return addr;
}

static uint64_t r8be(const uint64_t *x)
{
	return get_unaligned_be64(x);
}
static uint32_t rbe(const uint32_t *x)
{
	return get_unaligned_be32(x);
}
static uint16_t r2be(const uint16_t *x)
{
	return get_unaligned_be16(x);
}
static uint64_t r8le(const uint64_t *x)
{
	return get_unaligned_le64(x);
}
static uint32_t rle(const uint32_t *x)
{
	return get_unaligned_le32(x);
}
static uint16_t r2le(const uint16_t *x)
{
	return get_unaligned_le16(x);
}

static void w8be(uint64_t val, uint64_t *x)
{
	put_unaligned_be64(val, x);
}
static void wbe(uint32_t val, uint32_t *x)
{
	put_unaligned_be32(val, x);
}
static void w2be(uint16_t val, uint16_t *x)
{
	put_unaligned_be16(val, x);
}
static void w8le(uint64_t val, uint64_t *x)
{
	put_unaligned_le64(val, x);
}
static void wle(uint32_t val, uint32_t *x)
{
	put_unaligned_le32(val, x);
}
static void w2le(uint16_t val, uint16_t *x)
{
	put_unaligned_le16(val, x);
}

static uint64_t (*r8)(const uint64_t *);
static uint32_t (*r)(const uint32_t *);
static uint16_t (*r2)(const uint16_t *);
static void (*w8)(uint64_t, uint64_t *);
static void (*w)(uint32_t, uint32_t *);
static void (*w2)(uint16_t, uint16_t *);

typedef void (*table_sort_t)(char *, size_t, size_t);

/*
 * Move reserved section indices SHN_LORESERVE..SHN_HIRESERVE out of
 * the way to -256..-1, to avoid conflicting with real section
 * indices.
 */
#define SPECIAL(i) ((i) - (SHN_HIRESERVE + 1))

static inline int is_shndx_special(unsigned int i)
{
	return i != SHN_XINDEX && i >= SHN_LORESERVE && i <= SHN_HIRESERVE;
}

/* Accessor for sym->st_shndx, hides ugliness of "64k sections" */
static inline unsigned int get_secindex(unsigned int shndx,
					unsigned int sym_offs,
					const Elf32_Word *symtab_shndx_start)
{
	if (is_shndx_special(shndx))
		return SPECIAL(shndx);
	if (shndx != SHN_XINDEX)
		return shndx;
	return r(&symtab_shndx_start[sym_offs]);
}

/* 32 bit and 64 bit are very similar */
#include "sorttable.h"
#define SORTTABLE_64
#include "sorttable.h"

static int compare_relative_table(const void *a, const void *b)
{
	int32_t av = (int32_t)r(a);
	int32_t bv = (int32_t)r(b);

	if (av < bv)
		return -1;
	if (av > bv)
		return 1;
	return 0;
}

static void sort_relative_extable(char *image, size_t image_size, size_t entsize)
{
	int i;

	/*
	 * Do the same thing the runtime sort does, first normalize to
	 * being relative to the start of the section.
	 */
	i = 0;
	while (i < image_size) {
		uint32_t *loc = (uint32_t *)(image + i);
		w(r(loc) + i, loc);
		i += 4;
	}

	qsort(image, image_size / entsize, entsize, compare_relative_table);

	/* Now denormalize. */
	i = 0;
	while (i < image_size) {
		uint32_t *loc = (uint32_t *)(image + i);
		w(r(loc) - i, loc);
		i += 4;
	}
}

static void sort_undwarf_table(char *image, size_t image_size, size_t entsize)
{
	int i;

	/*
	 * Do the same thing the runtime sort does, first normalize to
	 * being relative to the start of the section.
	 */
	i = 0;
	while (i < image_size) {
		uint32_t *loc = (uint32_t *)(image + i);
		w(r(loc) + i, loc);
		i += entsize;
	}

	qsort(image, image_size / entsize, entsize, compare_relative_table);

	/* Now denormalize. */
	i = 0;
	while (i < image_size) {
		uint32_t *loc = (uint32_t *)(image + i);
		w(r(loc) - i, loc);
		i += entsize;
	}
}

static int do_file(char const *const fname, enum sectype sectype)
{
	table_sort_t custom_sort;
	Elf32_Ehdr *ehdr;
	const char *secname, *sort_needed_var;
	size_t entsize_32, entsize_64;

	ehdr = mmap_file(fname);
	if (!ehdr)
		return -1;

	ehdr_curr = ehdr;
	switch (ehdr->e_ident[EI_DATA]) {
	default:
		fprintf(stderr, "unrecognized ELF data encoding %d: %s\n",
			ehdr->e_ident[EI_DATA], fname);
		return -1;
		break;
	case ELFDATA2LSB:
		r = rle;
		r2 = r2le;
		r8 = r8le;
		w = wle;
		w2 = w2le;
		w8 = w8le;
		break;
	case ELFDATA2MSB:
		r = rbe;
		r2 = r2be;
		r8 = r8be;
		w = wbe;
		w2 = w2be;
		w8 = w8be;
		break;
	}  /* end switch */
	if (memcmp(ELFMAG, ehdr->e_ident, SELFMAG) != 0
	||  (r2(&ehdr->e_type) != ET_EXEC && r2(&ehdr->e_type) != ET_DYN)
	||  ehdr->e_ident[EI_VERSION] != EV_CURRENT) {
		fprintf(stderr, "unrecognized ET_EXEC/ET_DYN file %s\n", fname);
		return -1;
	}

	custom_sort = NULL;
	switch (r2(&ehdr->e_machine)) {
	default:
		fprintf(stderr, "unrecognized e_machine %d %s\n",
			r2(&ehdr->e_machine), fname);
		return -1;
	case EM_386:
	case EM_X86_64:
		if (sectype == SEC_TYPE_EXTABLE) {
			custom_sort = sort_relative_extable;
			entsize_32 = entsize_64 = 12;
		}
		break;

	case EM_S390:
	case EM_AARCH64:
	case EM_PARISC:
	case EM_PPC:
	case EM_PPC64:
		if (sectype == SEC_TYPE_EXTABLE) {
			custom_sort = sort_relative_extable;
			entsize_32 = entsize_64 = 8;
		}
		break;
	case EM_ARCOMPACT:
	case EM_ARCV2:
	case EM_ARM:
	case EM_MICROBLAZE:
	case EM_MIPS:
	case EM_XTENSA:
		entsize_32 = 8;
		entsize_64 = 16;
		break;
	}  /* end switch */

	switch (sectype) {
	case SEC_TYPE_EXTABLE:
		secname = "__ex_table";
		sort_needed_var = "main_extable_sort_needed";
		break;
	case SEC_TYPE_UNDWARF:
		secname = ".undwarf";
		custom_sort = sort_undwarf_table;
		entsize_32 = entsize_64 = 16;
		sort_needed_var = NULL;
		break;
	}

	switch (ehdr->e_ident[EI_CLASS]) {
	default:
		fprintf(stderr, "unrecognized ELF class %d %s\n",
			ehdr->e_ident[EI_CLASS], fname);
		return -1;
	case ELFCLASS32:
		if (r2(&ehdr->e_ehsize) != sizeof(Elf32_Ehdr)
		||  r2(&ehdr->e_shentsize) != sizeof(Elf32_Shdr)) {
			fprintf(stderr,
				"unrecognized ET_EXEC/ET_DYN file: %s\n", fname);
			return -1;
		}
		if (do32(ehdr, fname, secname, entsize_32, custom_sort, sort_needed_var))
			return -1;
		break;
	case ELFCLASS64: {
		Elf64_Ehdr *const ghdr = (Elf64_Ehdr *)ehdr;
		if (r2(&ghdr->e_ehsize) != sizeof(Elf64_Ehdr)
		||  r2(&ghdr->e_shentsize) != sizeof(Elf64_Shdr)) {
			fprintf(stderr,
				"unrecognized ET_EXEC/ET_DYN file: %s\n", fname);
			return -1;
		}
		if (do64(ghdr, fname, secname, entsize_64, custom_sort, sort_needed_var))
			return -1;
		break;
	}
	}  /* end switch */

	cleanup();

	return 0;
}

int
main(int argc, char *argv[])
{
	char *file;
	enum sectype sectype;

	if (argc != 3) {
		fprintf(stderr, "usage: sorttable <object file> <extable|undwarf>\n");
		return -1;
	}

	file = argv[1];

	if (!strcmp(argv[2], "extable"))
		sectype = SEC_TYPE_EXTABLE;
	else if (!strcmp(argv[2], "undwarf"))
		sectype = SEC_TYPE_UNDWARF;
	else  {
		fprintf(stderr, "unsupported section type %s\n", argv[2]);
		return -1;
	}

	return do_file(file, sectype);
}
