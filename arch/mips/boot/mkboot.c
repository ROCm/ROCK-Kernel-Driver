/*
 * Make a bootable image from a Linux/MIPS kernel.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 by Ralf Baechle
 *
 * This file is written in plain Kernighan & Ritchie C as it has to run
 * on all crosscompile hosts no matter how braindead.  This code might
 * also become part of Milo.  It's therefore important that we don't use
 * seek because the Seek() call of the Magnum 4000 ARC BIOS is broken.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

/*
 * Define this for verbose debugging output.
 */
#undef VERBOSE

/*
 * Don't use the host's elf.h - it might be using incompatible defines
 */

#define EI_NIDENT 16

/*
 * Basic ELF types.
 */
typedef unsigned short Elf32_Half;
typedef unsigned short Elf32_Section;
typedef unsigned int Elf32_Word;
typedef unsigned int Elf32_Addr;
typedef unsigned int Elf32_Off;

typedef struct
{
  unsigned char e_ident[EI_NIDENT];     /* Magic number and other info */
  Elf32_Half    e_type;                 /* Object file type */
  Elf32_Half    e_machine;              /* Architecture */
  Elf32_Word    e_version;              /* Object file version */
  Elf32_Addr    e_entry;                /* Entry point virtual address */
  Elf32_Off     e_phoff;                /* Program header table file offset */
  Elf32_Off     e_shoff;                /* Section header table file offset */
  Elf32_Word    e_flags;                /* Processor-specific flags */
  Elf32_Half    e_ehsize;               /* ELF header size in bytes */
  Elf32_Half    e_phentsize;            /* Program header table entry size */
  Elf32_Half    e_phnum;                /* Program header table entry count */
  Elf32_Half    e_shentsize;            /* Section header table entry size */
  Elf32_Half    e_shnum;                /* Section header table entry count */
  Elf32_Half    e_shstrndx;             /* Section header string table index */
} Elf32_Ehdr;

/*
 * ELF magic number
 */
#define ELFMAG          "\177ELF"
#define SELFMAG         4

#define EI_CLASS        4               /* File class byte index */
#define ELFCLASSNONE    0               /* Invalid class */
#define ELFCLASS32      1               /* 32-bit objects */
#define ELFCLASS64      2               /* 64-bit objects */

#define EI_DATA         5               /* Data encoding byte index */
#define ELFDATA2LSB     1               /* 2's complement, little endian */
#define ELFDATA2MSB     2               /* 2's complement, big endian */

#define EI_VERSION      6               /* File version byte index */
#define EV_CURRENT      1               /* Current version */

/*
 * Acceptable machine type in e_machine.
 */
#define EM_MIPS         8               /* MIPS R3000 big-endian */
#define EM_MIPS_RS4_BE 10               /* MIPS R4000 big-endian */

/*
 * The type of ELF file we accept.
 */
#define ET_EXEC         2               /* Executable file */

/*
 * Definition of a single program header structure
 */
typedef struct
{
  Elf32_Word    p_type;                 /* Segment type */
  Elf32_Off     p_offset;               /* Segment file offset */
  Elf32_Addr    p_vaddr;                /* Segment virtual address */
  Elf32_Addr    p_paddr;                /* Segment physical address */
  Elf32_Word    p_filesz;               /* Segment size in file */
  Elf32_Word    p_memsz;                /* Segment size in memory */
  Elf32_Word    p_flags;                /* Segment flags */
  Elf32_Word    p_align;                /* Segment alignment */
} Elf32_Phdr;

/*
 * Legal values for p_type
 */
#define PT_NULL         0               /* Program header table entry unused */
#define PT_LOAD         1               /* Loadable program segment */
#define PT_DYNAMIC      2               /* Dynamic linking information */
#define PT_INTERP       3               /* Program interpreter */
#define PT_NOTE         4               /* Auxiliary information */
#define PT_SHLIB        5               /* Reserved */
#define PT_PHDR         6               /* Entry for header table itself */
#define PT_NUM          7               /* Number of defined types.  */
#define PT_LOPROC       0x70000000      /* Start of processor-specific */
#define PT_HIPROC       0x7fffffff      /* End of processor-specific */

typedef struct
{
  Elf32_Word    sh_name;                /* Section name (string tbl index) */
  Elf32_Word    sh_type;                /* Section type */
  Elf32_Word    sh_flags;               /* Section flags */
  Elf32_Addr    sh_addr;                /* Section virtual addr at execution */
  Elf32_Off     sh_offset;              /* Section file offset */
  Elf32_Word    sh_size;                /* Section size in bytes */
  Elf32_Word    sh_link;                /* Link to another section */
  Elf32_Word    sh_info;                /* Additional section information */
  Elf32_Word    sh_addralign;           /* Section alignment */
  Elf32_Word    sh_entsize;             /* Entry size if section holds table */
} Elf32_Shdr;

typedef struct
{
  Elf32_Word    st_name;                /* Symbol name (string tbl index) */
  Elf32_Addr    st_value;               /* Symbol value */
  Elf32_Word    st_size;                /* Symbol size */
  unsigned char st_info;                /* Symbol type and binding */
  unsigned char st_other;               /* No defined meaning, 0 */
  Elf32_Section st_shndx;               /* Section index */
} Elf32_Sym;

/* How to extract and insert information held in the st_info field.  */
#define ELF32_ST_BIND(val)              (((unsigned char) (val)) >> 4)
#define ELF32_ST_TYPE(val)              ((val) & 0xf)

/* Legal values for ST_BIND subfield of st_info (symbol binding).  */
#define STB_GLOBAL      1               /* Global symbol */

/* Legal values for ST_TYPE subfield of st_info (symbol type).  */
#define STT_NOTYPE      0               /* Symbol type is unspecified */
#define STT_OBJECT      1               /* Symbol is a data object */
#define STT_FUNC        2               /* Symbol is a code object */

static unsigned int
get_Elf32_Half(unsigned char *p)
{
	return p[0] | (p[1] << 8);
}
#define get_Elf32_Section(p) get_Elf32_Half(p)

static unsigned int
get_Elf32_Word(unsigned char *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}
#define get_Elf32_Addr(p) get_Elf32_Word(p)
#define get_Elf32_Off(p) get_Elf32_Word(p)

static void
put_byte(p, x)
	unsigned char *p;
	unsigned char x;
{
	p[0] = x;
}

static void
put_half(p, x)
	unsigned char *p;
	unsigned short x;
{
	p[0] = x & 0xff;
	p[1] = (x >> 8) & 0xff;
}

static void
put_word(p, x)
	unsigned char *p;
	unsigned long x;
{
	p[0] = x & 0xff;
	p[1] = (x >> 8) & 0xff;
	p[2] = (x >> 16) & 0xff;
	p[3] = (x >> 24) & 0xff;
}

/*
 * Swap a program header in.
 */
static void
get_elfph(p, ph)
	unsigned char *p;
	Elf32_Phdr *ph;
{
	ph->p_type   = get_Elf32_Word(p);
	ph->p_offset = get_Elf32_Off(p + 4);
	ph->p_vaddr  = get_Elf32_Addr(p + 8);
	ph->p_paddr  = get_Elf32_Addr(p + 12);
	ph->p_filesz = get_Elf32_Word(p + 16);
	ph->p_memsz  = get_Elf32_Word(p + 20);
	ph->p_flags  = get_Elf32_Word(p + 24);
	ph->p_align  = get_Elf32_Word(p + 28);
}

/*
 * Swap a section header in.
 */
static void
get_elfsh(p, sh)
	unsigned char *p;
	Elf32_Shdr *sh;
{
	sh->sh_name      = get_Elf32_Word(p);
	sh->sh_type      = get_Elf32_Word(p + 4);
	sh->sh_flags     = get_Elf32_Word(p + 8);
	sh->sh_addr      = get_Elf32_Addr(p + 12);
	sh->sh_offset    = get_Elf32_Off(p + 16);
	sh->sh_size      = get_Elf32_Word(p + 20);
	sh->sh_link      = get_Elf32_Word(p + 24);
	sh->sh_info      = get_Elf32_Word(p + 28);
	sh->sh_addralign = get_Elf32_Word(p + 32);
	sh->sh_entsize   = get_Elf32_Word(p + 36);
}

/*
 * Swap a section header in.
 */
static void
get_elfsym(p, sym)
	unsigned char *p;
	Elf32_Sym *sym;
{
	sym->st_name      = get_Elf32_Word(p);
	sym->st_value     = get_Elf32_Addr(p + 4);
	sym->st_size      = get_Elf32_Word(p + 8);
	sym->st_info      = *(p + 12);
	sym->st_other     = *(p + 13);
	sym->st_shndx     = get_Elf32_Section(p + 14);
}

/*
 * The a.out magic number
 */
#define OMAGIC 0407	/* Code indicating object file or impure executable. */
#define M_MIPS1 151	/* MIPS R3000/R3000 binary */
#define M_MIPS2 152	/* MIPS R6000/R4000 binary */

/*
 * Compute and return an a.out magic number.
 */
#define AOUT_INFO(magic, type, flags) \
        (((magic) & 0xffff) | \
         (((int)(type) & 0xff) << 16) | \
         (((flags) & 0xff) << 24))

/*
 * a.out symbols
 */
#define N_UNDF 0
#define N_ABS 2
#define N_TEXT 4
#define N_DATA 6
#define N_BSS 8
#define N_FN 15
#define N_EXT 1

#define min(x,y) (((x)<(y))?(x):(y))

static void
do_read(fd, buf, size)
	int fd;
	char *buf;
	ssize_t size;
{
	ssize_t rd;

	while(size != 0) {
		rd = read(fd, buf, size);
		if (rd == -1) {
			perror("Can't read from file.");
			exit(1);
		}
		size -= rd;
	}
}

static void
writepad(fd, size)
	int fd;
	size_t size;
{
	static void *zeropage = NULL;
	ssize_t written;

	if (zeropage == NULL) {
		zeropage = malloc(4096);
		if (zeropage == NULL) {
			fprintf(stderr, "Couldn't allocate zero buffer.\n");
			exit(1);
		}
		memset(zeropage, '\0', 4096);
	}
	while(size != 0) {
		written = write(fd, zeropage, min(4096, size));
		if (written == -1) {
			perror("Can't write to boot image");
			exit(1);
		}
		size -= written;
	}
}

static void
do_write(fd, buf, size)
	int fd;
	char *buf;
	ssize_t size;
{
	ssize_t written;

	while(size != 0) {
		written = write(fd, buf, size);
		if (written == -1) {
			perror("Can't write to boot image");
			exit(1);
		}
		size -= written;
	}
}

static int
usage(program_name)
	char *program_name;
{
	fprintf(stderr, "Usage: %s infile outfile\n", program_name);
	exit(0);
}

int
main(argc, argv)
	int argc;
	char *argv[];
{
	char *infile, *outfile;
	struct stat ifstat;
	off_t ifsize;
	char *image;
	int ifd, ofd, i, symtabix, strtabix;
	Elf32_Ehdr eh;
	Elf32_Phdr *ph;
	Elf32_Shdr *sh;
	unsigned long vaddr, entry, bss, kernel_entry, kernel_end;
	unsigned char ahdr[32];
	Elf32_Sym sym;
	int	symnum;
	char *symname;

	/*
	 * Verify some basic assuptions about type sizes made in this code
	 */
	if (sizeof(Elf32_Half) != 2) {
		fprintf(stderr, "Fix mkboot: sizeof(Elf32_Half) != 2\n");
		exit(1);
	}
	if (sizeof(Elf32_Word) != 4) {
		fprintf(stderr, "Fix mkboot: sizeof(Elf32_Word) != 4\n");
		exit(1);
	}
	if (sizeof(Elf32_Addr) != 4) {
		fprintf(stderr, "Fix mkboot: sizeof(Elf32_Addr) != 4\n");
		exit(1);
	}

	if (argc != 3)
		usage(argv[0]);

	infile = argv[1];
	outfile = argv[2];

	if (stat(infile, &ifstat) < 0) {
		perror("Can't stat kernel image.");
		exit(1);
	}

	if (!S_ISREG(ifstat.st_mode)) {
		fprintf(stderr, "Input file isn't a regular file.\n");
		exit(1);
	}
	ifsize = ifstat.st_size;

	image = malloc((size_t)ifsize);
	if (image == NULL) {
		fprintf(stderr, "Can't allocate memory to read file\n");
		exit(1);
	}

	/*
	 * Read the entire input file in.
	 */
	ifd = open(infile, O_RDONLY);
	if(ifd == 0) {
		fprintf(stderr, "Can't open input file\n");
		exit(1);
	}
	do_read(ifd, image, ifsize);
	close(ifd);

	/*
	 * Now swap the ELF header in.  This is ugly but we the file
	 * we're reading might have different type sizes, byteorder
	 * or alignment than the host.
	 */
	memcpy(eh.e_ident, (void *)image, sizeof(eh.e_ident));
	if(memcmp(eh.e_ident, ELFMAG, SELFMAG)) {
		fprintf(stderr, "Input file isn't a ELF file\n");
		exit(1);
	}
	if(eh.e_ident[EI_CLASS] != ELFCLASS32) {
		fprintf(stderr, "Input file isn't a 32 bit ELF file\n");
		exit(1);
	}
	if(eh.e_ident[EI_DATA] != ELFDATA2LSB) {
		fprintf(stderr, "Input file isn't a little endian ELF file\n");
		exit(1);
	}
	if(eh.e_ident[EI_VERSION] != EV_CURRENT) {
		fprintf(stderr, "Input file isn't a version %d ELF file\n",
		        EV_CURRENT);
		exit(1);
	}

	/*
	 * Ok, so far the file looks ok.  Now swap the rest of the header in
	 * and do some more paranoia checks.
	 */
	eh.e_type      = get_Elf32_Half(image + 16);
	eh.e_machine   = get_Elf32_Half(image + 18);
	eh.e_version   = get_Elf32_Word(image + 20);
	eh.e_entry     = get_Elf32_Addr(image + 24);
	eh.e_phoff     = get_Elf32_Off(image + 28);
	eh.e_shoff     = get_Elf32_Off(image + 32);
	eh.e_flags     = get_Elf32_Word(image + 36);
	eh.e_ehsize    = get_Elf32_Half(image + 40);
	eh.e_phentsize = get_Elf32_Half(image + 42);
	eh.e_phnum     = get_Elf32_Half(image + 44);
	eh.e_shentsize = get_Elf32_Half(image + 46);
	eh.e_shnum     = get_Elf32_Half(image + 48);
	eh.e_shstrndx  = get_Elf32_Half(image + 50);

	if(eh.e_type != ET_EXEC) {
		fprintf(stderr, "Input file isn't a executable.\n");
		exit(1);
	}
	if(eh.e_machine != EM_MIPS && eh.e_machine != EM_MIPS_RS4_BE) {
		fprintf(stderr, "Input file isn't a MIPS executable.\n");
		exit(1);
	}

	/*
	 * Now read the program headers ...
	 */
	ph = malloc(sizeof(Elf32_Phdr) * eh.e_phnum);
	if (ph == NULL) {
		fprintf(stderr, "No memory for program header table.\n");
		exit(1);
	}
	for(i = 0;i < eh.e_phnum; i++)
		get_elfph((void *)(image + eh.e_phoff + i * 32), ph + i);

	/*
	 * ... and then the section headers.
	 */
	sh = malloc(sizeof(Elf32_Shdr) * eh.e_shnum);
	if (sh == NULL) {
		fprintf(stderr, "No memory for section header table.\n");
		exit(1);
	}
	for(i = 0;i < eh.e_shnum; i++)
		get_elfsh((void *)(image + eh.e_shoff + (i * 40)), sh + i);

	/*
	 * Find the symboltable and the stringtable in the file.
	 */
	for(i = 0;i < eh.e_shnum; i++) {
		if (!strcmp (image + sh [eh.e_shstrndx].sh_offset + sh[i].sh_name,
		             ".symtab")) {
			symtabix = i;
			continue;
		}
		if (!strcmp (image + sh [eh.e_shstrndx].sh_offset + sh[i].sh_name,
		             ".strtab")) {
			strtabix = i;
			continue;
		}
	}

	if (symtabix == -1) {
		fprintf(stderr, "The executable doesn't have a symbol table\n");
		exit(1);
	}
	if (strtabix == -1) {
		fprintf(stderr, "The executable doesn't have a string table\n");
		exit(1);
	}

	/*
	 * Dig for the two required symbols in the symbol table.
	 */
	symnum = sh[symtabix].sh_size / 16;
	for(i = 0;i < symnum;i++) {
		get_elfsym(image + sh[symtabix].sh_offset + (i * 16), &sym);
		symname = image + sh[strtabix].sh_offset + sym.st_name;
		if (ELF32_ST_BIND(sym.st_info) != STB_GLOBAL)
			continue;
		if (ELF32_ST_TYPE(sym.st_info) != STT_NOTYPE &&
		    ELF32_ST_TYPE(sym.st_info) != STT_OBJECT &&
		    ELF32_ST_TYPE(sym.st_info) != STT_FUNC)
			continue;
		if (strcmp("kernel_entry", symname) == 0) {
			kernel_entry = sym.st_value;
			continue;
		}
		if (strcmp("_end", symname) == 0) {
			kernel_end = sym.st_value;
			continue;
		}
	}

#ifdef VERBOSE
	/*
	 * And print what we will be loaded into memory.
	 */
	for(i = 0;i < eh.e_phnum; i++) {
		if (ph[i].p_type != PT_LOAD) {
			continue;
		}
		printf("  Offset: %08lx\n", ph[i].p_offset);
		printf("  file size: %08lx\n", ph[i].p_filesz);
		printf("  mem size: %08lx\n", ph[i].p_memsz);
		printf("    Loading: %08lx - %08lx\n",
		       ph[i].p_vaddr, ph[i].p_vaddr + ph[i].p_filesz);
		printf("    Zero mapping: %08lx - %08lx\n",
		       ph[i].p_vaddr + ph[i].p_filesz,
		       ph[i].p_vaddr + ph[i].p_memsz);
	}
#endif

	/*
	 * Time to open the outputfile.
	 */
	ofd = open(outfile, O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if (ofd == -1) {
		perror("Can't open boot image for output.");
		exit(1);
	}

	/*
	 * First compute the layout of the file.  We need to do this
	 * first because we can't seek back to the beginning due to the
	 * broken Seek() call in the Magnum firmware.
	 */
	entry = vaddr = 0xffffffff;
	bss = 0;
	for(i = 0;i < eh.e_phnum; i++) {
		if (ph[i].p_type != PT_LOAD)
			continue;
		if (vaddr == 0xffffffff)
			entry = vaddr = ph[i].p_vaddr;
		vaddr = ph[i].p_vaddr + ph[i].p_filesz;
		bss = ph[i].p_memsz - ph[i].p_filesz;
	}

	/*
	 * In the next step we construct the boot image.  The boot file
	 * looks essentially like a dump of the loaded kernel with a
	 * minimal header.  Because Milo supports already a.out image
	 * we simply dump the image in an a.out image ...  First let's
	 * write the header.
	 */

	/*
	 * Create and write the a.out header.
	 */
	put_word(ahdr, AOUT_INFO(OMAGIC, M_MIPS1, 0));
	put_word(ahdr + 4, vaddr - entry);	/* text size */
	put_word(ahdr + 8, 0);			/* data size */
	put_word(ahdr + 12, bss);		/* bss size */
	put_word(ahdr + 16, 2 * 12);		/* size of symbol table */
	put_word(ahdr + 20, entry);		/* base address */
	put_word(ahdr + 24, 0);			/* size of text relocations */
	put_word(ahdr + 28, 0);			/* size of data relocations */
	do_write(ofd, ahdr, 32);

	/*
	 * Write text and data segment combined into the a.out text segment
	 * and a zero length data segment into the file.
	 */
	vaddr = 0xffffffff;
	bss = 0;
	for(i = 0;i < eh.e_phnum; i++) {
		if (ph[i].p_type != PT_LOAD)
			continue;
		if (vaddr == 0xffffffff)
			vaddr = ph[i].p_vaddr;
		writepad(ofd, ph[i].p_vaddr - vaddr);	/* Write zero pad */
		do_write(ofd, image + ph[i].p_offset, ph[i].p_filesz);
		vaddr = ph[i].p_vaddr + ph[i].p_filesz;
		bss = ph[i].p_memsz - ph[i].p_filesz;
	}

	/*
	 * Now write the symbol table.  It has only two symbols,
	 * kernel_entry and _end which we need for booting.
	 */
	put_word(ahdr    , 4);			/* n_un.n_strx */
	put_byte(ahdr + 4, N_TEXT | N_EXT);	/* n_type */
	put_byte(ahdr + 5, 0);			/* n_other */
	put_half(ahdr + 6, 0);			/* n_desc */
	put_word(ahdr + 8, kernel_entry);	/* n_value */
	do_write(ofd, ahdr, 12);

	put_word(ahdr    , 4 + 13);		/* n_un.n_strx */
	put_byte(ahdr + 4, N_ABS | N_EXT);	/* n_type */
	put_byte(ahdr + 5, 0);			/* n_other */
	put_half(ahdr + 6, 0);			/* n_desc */
	put_word(ahdr + 8, kernel_end);		/* n_value */
	do_write(ofd, ahdr, 12);

	/*
	 * Now write stringtable size and the strings.
	 */
	put_word(ahdr, 4 + 20);
	do_write(ofd, ahdr, 4);
	do_write(ofd, "kernel_entry\0_end\0\0", 20);

	/*
	 * That's is all ...
	 */
	close(ofd);

#ifdef VERBOSE
	printf("Entry: %08lx\n", entry);
	printf("Dumped image %08lx - %08lx\n", 0x80000000, vaddr);
	printf("Extra bss at end: %08lx\n", bss);
#endif

	return 0;
}
