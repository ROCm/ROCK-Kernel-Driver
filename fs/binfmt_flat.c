/*
 *  linux/fs/binfmt_flat.c
 *
 *	Copyright (C) 2000, 2001 Lineo, by David McCullough <davidm@uclinux.org>
 *	Copyright (C) 2002 Greg Ungerer <gerg@snapgear.com>
 *
 *  based heavily on:
 *
 *  linux/fs/binfmt_aout.c:
 *      Copyright (C) 1991, 1992, 1996  Linus Torvalds
 *  linux/fs/binfmt_flat.c for 2.0 kernel
 *	Copyright (C) 1998  Kenneth Albanowski <kjahds@kjahds.com>
 *	JAN/99 -- coded full program relocation (gerg@snapgear.com)
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/a.out.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/slab.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/init.h>
#include <linux/flat.h>

#include <asm/byteorder.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/unaligned.h>
#include <asm/cacheflush.h>

#undef DEBUG
#ifdef DEBUG
#define	DBG_FLT(a...)	printk(##a)
#else
#define	DBG_FLT(a...)
#endif

static int load_flat_binary(struct linux_binprm *, struct pt_regs * regs);
static int load_flat_library(struct file*);
extern void dump_thread(struct pt_regs *, struct user *);

static struct linux_binfmt flat_format = {
	NULL, THIS_MODULE, load_flat_binary, load_flat_library, NULL, PAGE_SIZE
};


/*
 * create_flat_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */
static unsigned long create_flat_tables(
	unsigned long pp,
	struct linux_binprm * bprm)
{
	unsigned long *argv,*envp;
	unsigned long * sp;
	char * p = (char*)pp;
	int argc = bprm->argc;
	int envc = bprm->envc;
	char dummy;

	sp = (unsigned long *) ((-(unsigned long)sizeof(char *))&(unsigned long) p);

	sp -= envc+1;
	envp = sp;
	sp -= argc+1;
	argv = sp;
	put_user((unsigned long) envp, --sp);
	put_user((unsigned long) argv, --sp);
	put_user(argc,--sp);
	current->mm->arg_start = (unsigned long) p;
	while (argc-->0) {
		put_user((unsigned long) p, argv++);
		do {
			get_user(dummy, p); p++;
		} while (dummy);
	}
	put_user((unsigned long) NULL, argv);
	current->mm->arg_end = current->mm->env_start = (unsigned long) p;
	while (envc-->0) {
		put_user((unsigned long)p, envp); envp++;
		do {
			get_user(dummy, p); p++;
		} while (dummy);
	}
	put_user((unsigned long) NULL, envp);
	current->mm->env_end = (unsigned long) p;
	return (unsigned long)sp;
}


#ifdef CONFIG_BINFMT_ZFLAT

#include <linux/zlib.h>

#define LBUFSIZE	4000

/* gzip flag byte */
#define ASCII_FLAG   0x01 /* bit 0 set: file probably ASCII text */
#define CONTINUATION 0x02 /* bit 1 set: continuation of multi-part gzip file */
#define EXTRA_FIELD  0x04 /* bit 2 set: extra field present */
#define ORIG_NAME    0x08 /* bit 3 set: original file name present */
#define COMMENT      0x10 /* bit 4 set: file comment present */
#define ENCRYPTED    0x20 /* bit 5 set: file is encrypted */
#define RESERVED     0xC0 /* bit 6,7:   reserved */

static int decompress_exec(
	struct linux_binprm *bprm,
	unsigned long offset,
	char *dst,
	long len,
	int fd)
{
	unsigned char *buf;
	z_stream strm;
	loff_t fpos;
	int ret;

	DBG_FLT("decompress_exec(offset=%x,buf=%x,len=%x)\n",(int)offset, (int)dst, (int)len);

	memset(&strm, 0, sizeof(strm));
	strm.workspace = kmalloc(zlib_inflate_workspacesize(), GFP_KERNEL);
	if (strm.workspace == NULL) {
		DBG_FLT("binfmt_flat: no memory for decompress workspace\n");
		return -ENOMEM;
	}
	buf = kmalloc(LBUFSIZE, GFP_KERNEL);
	if (buf == NULL) {
		DBG_FLT("binfmt_flat: no memory for read buffer\n");
		return -ENOMEM;
	}

	/* Read in first chunk of data and parse gzip header. */
	fpos = offset;
	ret = bprm->file->f_op->read(bprm->file, buf, LBUFSIZE, &fpos);

	strm.next_in = buf;
	strm.avail_in = ret;
	strm.total_in = 0;

	/* Check minimum size -- gzip header */
	if (ret < 10) {
		DBG_FLT("binfmt_flat: file too small?\n");
		return -ENOEXEC;
	}

	/* Check gzip magic number */
	if ((buf[0] != 037) || ((buf[1] != 0213) && (buf[1] != 0236))) {
		DBG_FLT("binfmt_flat: unknown compression magic?\n");
		return -ENOEXEC;
	}

	/* Check gzip method */
	if (buf[2] != 8) {
		DBG_FLT("binfmt_flat: unknown compression method?\n");
		return -ENOEXEC;
	}
	/* Check gzip flags */
	if ((buf[3] & ENCRYPTED) || (buf[3] & CONTINUATION) ||
	    (buf[3] & RESERVED)) {
		DBG_FLT("binfmt_flat: unknown flags?\n");
		return -ENOEXEC;
	}

	ret = 10;
	if (buf[3] & EXTRA_FIELD)
		ret += 2 + buf[10] + (buf[11] << 8);
	if (buf[3] & ORIG_NAME) {
		for (; (buf[ret] != 0); ret++)
			;
	}
	if (buf[3] & COMMENT) {
		for (; (buf[ret] != 0); ret++)
			;
	}

	strm.next_in += ret;
	strm.avail_in -= ret;

	strm.next_out = dst;
	strm.avail_out = len;
	strm.total_out = 0;

	if (zlib_inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
		DBG_FLT("binfmt_flat: zlib init failed?\n");
		return -ENOEXEC;
	}

	while ((ret = zlib_inflate(&strm, Z_NO_FLUSH)) == Z_OK) {
		ret = bprm->file->f_op->read(bprm->file, buf, LBUFSIZE, &fpos);
		if (ret == 0)
			break;
		if (ret >= (unsigned long) -4096)
			break;
		len -= ret;

		strm.next_in = buf;
		strm.avail_in = ret;
		strm.total_in = 0;
	}

	if (ret < 0) {
		DBG_FLT("binfmt_flat: decompression failed (%d), %s\n",
			ret, strm.msg);
		return -ENOEXEC;
	}

	zlib_inflateEnd(&strm);
	kfree(buf);
	kfree(strm.workspace);
	return 0;
}

#endif /* CONFIG_BINFMT_ZFLAT */


static unsigned long
calc_reloc(unsigned long r, unsigned long text_len)
{
	unsigned long addr;

	if (r > current->mm->start_brk - current->mm->start_data + text_len) {
		printk("BINFMT_FLAT: reloc outside program 0x%x (0 - 0x%x), killing!\n",
				(int) r,(int)(current->mm->start_brk-current->mm->start_code));
		send_sig(SIGSEGV, current, 0);
		return(current->mm->start_brk); /* return something safe to write to */
	}

	if (r < text_len) {
		/* In text segment */
		return r + current->mm->start_code;
	}

	/*
	 * we allow inclusive ranges here so that programs may do things
	 * like reference the end of data (_end) without failing these tests
	 */
	addr =  r - text_len + current->mm->start_data;
	if (addr >= current->mm->start_code &&
			addr <= current->mm->start_code + text_len)
		return(addr);

	if (addr >= current->mm->start_data &&
			addr <= current->mm->start_brk)
		return(addr);

	printk("BINFMT_FLAT: reloc addr outside text/data 0x%x "
			"code(0x%x - 0x%x) data(0x%x - 0x%x) killing\n", (int) addr,
			(int) current->mm->start_code,
			(int) (current->mm->start_code + text_len),
			(int) current->mm->start_data,
			(int) current->mm->start_brk);
	send_sig(SIGSEGV, current, 0);

	return(current->mm->start_brk); /* return something safe to write to */
}


void old_reloc(unsigned long rl)
{
#ifdef DEBUG
	char *segment[] = { "TEXT", "DATA", "BSS", "*UNKNOWN*" };
#endif
	flat_v2_reloc_t	r;
	unsigned long *ptr;
	
	r.value = rl;
#if defined(CONFIG_COLDFIRE)
	ptr = (unsigned long *) (current->mm->start_code + r.reloc.offset);
#else
	ptr = (unsigned long *) (current->mm->start_data + r.reloc.offset);
#endif

#ifdef DEBUG
	printk("Relocation of variable at DATASEG+%x "
		"(address %p, currently %x) into segment %s\n",
		r.reloc.offset, ptr, (int)*ptr, segment[r.reloc.type]);
#endif
	
	switch (r.reloc.type) {
	case OLD_FLAT_RELOC_TYPE_TEXT:
		*ptr += current->mm->start_code;
		break;
	case OLD_FLAT_RELOC_TYPE_DATA:
		*ptr += current->mm->start_data;
		break;
	case OLD_FLAT_RELOC_TYPE_BSS:
		*ptr += current->mm->end_data;
		break;
	default:
		printk("BINFMT_FLAT: Unknown relocation type=%x\n", r.reloc.type);
		break;
	}

#ifdef DEBUG
	printk("Relocation became %x\n", (int)*ptr);
#endif
}		


/*
 * These are the functions used to load flat style executables and shared
 * libraries.  There is no binary dependent code anywhere else.
 */

static int load_flat_binary(struct linux_binprm * bprm, struct pt_regs * regs)
{
	struct flat_hdr * hdr;
	unsigned long textpos = 0, datapos = 0, result;
	unsigned long text_len, data_len, bss_len, stack_len, flags;
	unsigned long memp = 0, memkasked = 0; /* for finding the brk area */
	unsigned long extra, rlim;
	unsigned long p = bprm->p;
	unsigned long *reloc = 0, *rp;
	struct inode *inode;
	int i, rev, relocs = 0;
	loff_t fpos;

	DBG_FLT("BINFMT_FLAT: Loading file: %x\n", bprm->file);

	hdr = ((struct flat_hdr *) bprm->buf);		/* exec-header */
	inode = bprm->file->f_dentry->d_inode;

	text_len  = ntohl(hdr->data_start);
	data_len  = ntohl(hdr->data_end) - ntohl(hdr->data_start);
	bss_len   = ntohl(hdr->bss_end) - ntohl(hdr->data_end);
	stack_len = ntohl(hdr->stack_size);
	relocs    = ntohl(hdr->reloc_count);
	flags     = ntohl(hdr->flags);
	rev       = ntohl(hdr->rev);

	/*
	 * We have to add the size of our arguments to our stack size
	 * otherwise it's too easy for users to create stack overflows
	 * by passing in a huge argument list.  And yes,  we have to be
	 * pedantic and include space for the argv/envp array as it may have
	 * a lot of entries.
	 */
	#define TOP_OF_ARGS (PAGE_SIZE*MAX_ARG_PAGES-sizeof(void *))
	stack_len += TOP_OF_ARGS - bprm->p;             /* the strings */
	stack_len += (bprm->argc + 1) * sizeof(char *); /* the argv array */
	stack_len += (bprm->envc + 1) * sizeof(char *); /* the envp array */

	if (strncmp(hdr->magic, "bFLT", 4) ||
			(rev != FLAT_VERSION && rev != OLD_FLAT_VERSION)) {
		/*
		 * because a lot of people do not manage to produce good
		 * flat binaries,  we leave this printk to help them realise
		 * the problem.  We only print the error if its * not a script file
		 */
		if (strncmp(hdr->magic, "#!", 2))
			printk("BINFMT_FLAT: bad magic/rev (0x%x, need 0x%x)\n",
					rev, (int) FLAT_VERSION);
		return -ENOEXEC;
	}

	/*
	 * fix up the flags for the older format,  there were all kinds
	 * of endian hacks,  this only works for the simple cases
	 */
	if (rev == OLD_FLAT_VERSION && flags)
		flags = FLAT_FLAG_RAM;

#ifndef CONFIG_BINFMT_ZFLAT
	if (flags & (FLAT_FLAG_GZIP|FLAT_FLAG_GZDATA)) {
		printk("Support for ZFLAT executables is not enabled.\n");
		return -ENOEXEC;
	}
#endif

	/*
	 * Check initial limits. This avoids letting people circumvent
	 * size limits imposed on them by creating programs with large
	 * arrays in the data or bss.
	 */
	rlim = current->rlim[RLIMIT_DATA].rlim_cur;
	if (rlim >= RLIM_INFINITY)
		rlim = ~0;
	if (data_len + bss_len > rlim)
		return -ENOMEM;

	/* Flush all traces of the currently running executable */
	result = flush_old_exec(bprm);
	if (result)
		return result;

	/* OK, This is the point of no return */
	set_personality(PER_LINUX);

	/*
	 * there are a couple of cases here,  the separate code/data
	 * case,  and then the fully copied to RAM case which lumps
	 * it all together.
	 */
	if ((flags & (FLAT_FLAG_RAM|FLAT_FLAG_GZIP)) == 0) {
		/*
		 * this should give us a ROM ptr,  but if it doesn't we don't
		 * really care
		 */
		DBG_FLT("BINFMT_FLAT: ROM mapping of file (we hope)\n");

		down_write(&current->mm->mmap_sem);
		textpos = do_mmap(bprm->file, 0, text_len, PROT_READ|PROT_EXEC, 0, 0);
		up_write(&current->mm->mmap_sem);
		if (!textpos  || textpos >= (unsigned long) -4096) {
			if (!textpos)
				textpos = (unsigned long) -ENOMEM;
			printk("Unable to mmap process text, errno %d\n", (int)-textpos);
		}

		extra = max(bss_len + stack_len, relocs * sizeof(unsigned long)),

		down_write(&current->mm->mmap_sem);
		datapos = do_mmap(0, 0, data_len + extra,
				PROT_READ|PROT_WRITE|PROT_EXEC, 0, 0);
		up_write(&current->mm->mmap_sem);

		if (datapos == 0 || datapos >= (unsigned long)-4096) {
			if (!datapos)
				datapos = (unsigned long) -ENOMEM;
			printk("Unable to allocate RAM for process data, errno %d\n",
					(int)-datapos);
			do_munmap(current->mm, textpos, text_len);
			return datapos;
		}

		DBG_FLT("BINFMT_FLAT: Allocated data+bss+stack (%d bytes): %x\n",
				data_len + bss_len + stack_len, datapos);

		fpos = ntohl(hdr->data_start);
#ifdef CONFIG_BINFMT_ZFLAT
		if (flags & FLAT_FLAG_GZDATA) {
			result = decompress_exec(bprm, fpos, (char *) datapos, 
						 data_len + (relocs * sizeof(unsigned long)), 0);
		} else
#endif
		{
			result = bprm->file->f_op->read(bprm->file,
					(char *) datapos, data_len + extra, &fpos);
		}
		if (result >= (unsigned long)-4096) {
			printk("Unable to read data+bss, errno %d\n", (int)-result);
			do_munmap(current->mm, textpos, text_len);
			do_munmap(current->mm, datapos, data_len + extra);
			return result;
		}

		reloc = (unsigned long *) (datapos+(ntohl(hdr->reloc_start)-text_len));
		memp = datapos;
		memkasked = data_len + extra;

	} else {

		/*
		 * calculate the extra space we need to map in
		 */

		extra = max(bss_len + stack_len, relocs * sizeof(unsigned long)),

		down_write(&current->mm->mmap_sem);
		textpos = do_mmap(0, 0, text_len + data_len + extra,
				PROT_READ | PROT_EXEC | PROT_WRITE, 0, 0);
		up_write(&current->mm->mmap_sem);
		if (!textpos  || textpos >= (unsigned long) -4096) {
			if (!textpos)
				textpos = (unsigned long) -ENOMEM;
			printk("Unable to allocate RAM for process text/data, errno %d\n",
					(int)-textpos);
		}

		datapos = textpos + ntohl (hdr->data_start);
		reloc = (unsigned long *) (textpos + ntohl(hdr->reloc_start));
		memp = textpos;
		memkasked = text_len + data_len + extra;

#ifdef CONFIG_BINFMT_ZFLAT
		/*
		 * load it all in and treat it like a RAM load from now on
		 */
		if (flags & FLAT_FLAG_GZIP) {
			result = decompress_exec(bprm, sizeof (struct flat_hdr),
					 (((char *) textpos) + sizeof (struct flat_hdr)),
					 (text_len + data_len + (relocs * sizeof(unsigned long))
						  - sizeof (struct flat_hdr)),
					 0);
		} else if (flags & FLAT_FLAG_GZDATA) {
			fpos = 0;
			result = bprm->file->f_op->read(bprm->file,
					(char *) textpos, text_len, &fpos);
			if (result < (unsigned long) -4096)
				result = decompress_exec(bprm, text_len, (char *) datapos,
						 data_len + (relocs * sizeof(unsigned long)), 0);
		}
		else
#endif
		{
			fpos = 0;
			result = bprm->file->f_op->read(bprm->file,
					(char *) textpos, text_len + data_len + extra, &fpos);
		}
		if (result >= (unsigned long)-4096) {
			printk("Unable to read code+data+bss, errno %d\n",(int)-result);
			do_munmap(current->mm, textpos, text_len + data_len + extra);
			return result;
		}
	}

	DBG_FLT("Mapping is %x, Entry point is %x, data_start is %x\n",
			textpos, ntohl(hdr->entry), ntohl(hdr->data_start));

	current->mm->start_code = textpos + sizeof (struct flat_hdr);
	current->mm->end_code = textpos + text_len;
	current->mm->start_data = datapos;
	current->mm->end_data = datapos + data_len;
	/*
	 *	set up the brk stuff (uses any slack left in data/bss/stack allocation
	 *	We put the brk after the bss (between the bss and stack) like other
	 *	platforms.
	 */
	current->mm->start_brk = datapos + data_len + bss_len;
	current->mm->brk = (current->mm->start_brk + 3) & ~3;
	current->mm->context.end_brk = memp + ksize((void *) memp) - stack_len;
	current->mm->rss = 0;

	DBG_FLT("Load %s: TEXT=%x-%x DATA=%x-%x BSS=%x-%x\n",
		bprm->filename,
		(int) current->mm->start_code, (int) current->mm->end_code,
		(int) current->mm->start_data, (int) current->mm->end_data,
		(int) current->mm->end_data, (int) current->mm->brk);

	text_len -= sizeof(struct flat_hdr); /* the real code len */

	/*
	 * We just load the allocations into some temporary memory to
	 * help simplify all this mumbo jumbo
	 *
	 * We've got two different sections of relocation entries.
	 * The first is the GOT which resides at the begining of the data segment
	 * and is terminated with a -1.  This one can be relocated in place.
	 * The second is the extra relocation entries tacked after the image's
	 * data segment. These require a little more processing as the entry is
	 * really an offset into the image which contains an offset into the
	 * image.
	 */
	
	if (flags & FLAT_FLAG_GOTPIC) {
		for (rp = (unsigned long *)datapos; *rp != 0xffffffff; rp++)
			*rp = calc_reloc(*rp, text_len);
	}

	/*
	 * Now run through the relocation entries.
	 * We've got to be careful here as C++ produces relocatable zero
	 * entries in the constructor and destructor tables which are then
	 * tested for being not zero (which will always occur unless we're
	 * based from address zero).  This causes an endless loop as __start
	 * is at zero.  The solution used is to not relocate zero addresses.
	 * This has the negative side effect of not allowing a global data
	 * reference to be statically initialised to _stext (I've moved
	 * __start to address 4 so that is okay).
	 */

	if (rev > OLD_FLAT_VERSION) {
		for (i=0; i < relocs; i++) {
			unsigned long addr;

			/* Get the address of the pointer to be
			   relocated (of course, the address has to be
			   relocated first).  */
			rp = (unsigned long *) calc_reloc(ntohl(reloc[i]), text_len);

			/* Get the pointer's value.  */
			addr = get_unaligned (rp);

			if (addr != 0) {
				/*
				 * Do the relocation.  PIC relocs in the data section are
				 * already in target order
				 */
				addr = calc_reloc(
						(flags & FLAT_FLAG_GOTPIC) ? addr : ntohl(addr),
						text_len);
				/* Write back the relocated pointer.  */
				put_unaligned (addr, rp);
			}
		}
	} else {
		for (i=0; i < relocs; i++)
			old_reloc(ntohl(reloc[i]));
	}

	/* zero the BSS,  BRK and stack areas */
	memset((void*)(datapos + data_len), 0, bss_len + 
			(current->mm->context.end_brk - current->mm->start_brk) +
			stack_len);

	compute_creds(bprm);
 	current->flags &= ~PF_FORKNOEXEC;

	flush_icache_range(current->mm->start_code, current->mm->end_code);

	set_binfmt(&flat_format);

	p = ((current->mm->context.end_brk + stack_len + 3) & ~3) - 4;
	DBG_FLT("p=%x\n", p);

	/* copy the arg pages onto the stack, this could be more efficient :-) */
	for (i = TOP_OF_ARGS - 1; i >= bprm->p; i--)
		* (char *) --p =
			((char *) page_address(bprm->page[i/PAGE_SIZE]))[i % PAGE_SIZE];

	current->mm->start_stack = (unsigned long) create_flat_tables(p, bprm);

	DBG_FLT("start_thread(regs=0x%x, entry=0x%x, start_stack=0x%x)\n",
		regs, textpos + ntohl(hdr->entry), current->mm->start_stack);
	start_thread(regs,
		     textpos + ntohl(hdr->entry),
		     current->mm->start_stack);

	if (current->ptrace & PT_PTRACED)
		send_sig(SIGTRAP, current, 0);

	return 0;
}

static int load_flat_library(struct file *file)
{
	return(-ENOEXEC);
}

static int __init init_flat_binfmt(void)
{
	return register_binfmt(&flat_format);
}

static void __exit exit_flat_binfmt(void)
{
	unregister_binfmt(&flat_format);
}

EXPORT_NO_SYMBOLS;

module_init(init_flat_binfmt);
module_exit(exit_flat_binfmt);
