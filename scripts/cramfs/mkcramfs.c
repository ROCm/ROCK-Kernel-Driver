#include <sys/types.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

/* zlib required.. */
#include <zlib.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#include "cramfs.h"

static const char *progname = "mkcramfs";

/* N.B. If you change the disk format of cramfs, please update fs/cramfs/README. */

static void usage(void)
{
	fprintf(stderr, "Usage: '%s dirname outfile'\n"
		" where <dirname> is the root of the\n"
		" filesystem to be compressed.\n", progname);
	exit(1);
}

/*
 * If DO_HOLES is defined, then mkcramfs can create explicit holes in the
 * data, which saves 26 bytes per hole (which is a lot smaller a saving than
 * most filesystems).
 *
 * Note that kernels up to at least 2.3.39 don't support cramfs holes, which
 * is why this defaults to undefined at the moment.
 */
/* #define DO_HOLES 1 */

#define PAGE_CACHE_SIZE (4096)
/* The kernel assumes PAGE_CACHE_SIZE as block size. */
static unsigned int blksize = PAGE_CACHE_SIZE;

static int warn_dev, warn_gid, warn_namelen, warn_size, warn_uid;

#ifndef MIN
# define MIN(_a,_b) ((_a) < (_b) ? (_a) : (_b))
#endif

/* In-core version of inode / directory entry. */
struct entry {
	/* stats */
	char *name;
	unsigned int mode, size, uid, gid;

	/* FS data */
	void *uncompressed;
        /* points to other identical file */
        struct entry *same;
        unsigned int offset;            /* pointer to compressed data in archive */
	unsigned int dir_offset;	/* Where in the archive is the directory entry? */

	/* organization */
	struct entry *child; /* null for non-directories and empty directories */
	struct entry *next;
};

/*
 * Width of various bitfields in struct cramfs_inode.
 * Used only to generate warnings.
 */
#define SIZE_WIDTH 24
#define UID_WIDTH 16
#define GID_WIDTH 8
#define OFFSET_WIDTH 26

/*
 * The longest file name component to allow for in the input directory tree.
 * Ext2fs (and many others) allow up to 255 bytes.  A couple of filesystems
 * allow longer (e.g. smbfs 1024), but there isn't much use in supporting
 * >255-byte names in the input directory tree given that such names get
 * truncated to 255 bytes when written to cramfs.
 */
#define MAX_INPUT_NAMELEN 255

static int find_identical_file(struct entry *orig,struct entry *newfile)
{
        if(orig==newfile) return 1;
        if(!orig) return 0;
        if(orig->size==newfile->size && orig->uncompressed && !memcmp(orig->uncompressed,newfile->uncompressed,orig->size)) {
                newfile->same=orig;
                return 1;
        }
        return find_identical_file(orig->child,newfile) ||
                   find_identical_file(orig->next,newfile);
}

static void eliminate_doubles(struct entry *root,struct entry *orig) {
        if(orig) {
                if(orig->size && orig->uncompressed) 
			find_identical_file(root,orig);
                eliminate_doubles(root,orig->child);
                eliminate_doubles(root,orig->next);
        }
}

static unsigned int parse_directory(struct entry *root_entry, const char *name, struct entry **prev, loff_t *fslen_ub)
{
	DIR *dir;
	int count = 0, totalsize = 0;
	struct dirent *dirent;
	char *path, *endpath;
	size_t len = strlen(name);

	dir = opendir(name);
	if (!dir) {
		perror(name);
		exit(2);
	}

	/* Set up the path. */
	/* TODO: Reuse the parent's buffer to save memcpy'ing and duplication. */
	path = malloc(len + 1 + MAX_INPUT_NAMELEN + 1);
	if (!path) {
		perror(NULL);
		exit(1);
	}
	memcpy(path, name, len);
	endpath = path + len;
	*endpath = '/';
	endpath++;

	while ((dirent = readdir(dir)) != NULL) {
		struct entry *entry;
		struct stat st;
		int size;
		size_t namelen;

		/* Ignore "." and ".." - we won't be adding them to the archive */
		if (dirent->d_name[0] == '.') {
			if (dirent->d_name[1] == '\0')
				continue;
			if (dirent->d_name[1] == '.') {
				if (dirent->d_name[2] == '\0')
					continue;
			}
		}
		namelen = strlen(dirent->d_name);
		if (namelen > MAX_INPUT_NAMELEN) {
			fprintf(stderr,
				"Very long (%u bytes) filename `%s' found.\n"
				" Please increase MAX_INPUT_NAMELEN in mkcramfs.c and recompile.  Exiting.\n",
				namelen, dirent->d_name);
			exit(1);
		}
		memcpy(endpath, dirent->d_name, namelen + 1);

		if (lstat(path, &st) < 0) {
			perror(endpath);
			continue;
		}
		entry = calloc(1, sizeof(struct entry));
		if (!entry) {
			perror(NULL);
			exit(5);
		}
		entry->name = strdup(dirent->d_name);
		if (!entry->name) {
			perror(NULL);
			exit(1);
		}
		if (namelen > 255) {
			/* Can't happen when reading from ext2fs. */

			/* TODO: we ought to avoid chopping in half
			   multi-byte UTF8 characters. */
			entry->name[namelen = 255] = '\0';
			warn_namelen = 1;
		}
		entry->mode = st.st_mode;
		entry->size = st.st_size;
		entry->uid = st.st_uid;
		if (entry->uid >= 1 << UID_WIDTH)
			warn_uid = 1;
		entry->gid = st.st_gid;
		if (entry->gid >= 1 << GID_WIDTH)
			/* TODO: We ought to replace with a default
                           gid instead of truncating; otherwise there
                           are security problems.  Maybe mode should
                           be &= ~070.  Same goes for uid once Linux
                           supports >16-bit uids. */
			warn_gid = 1;
		size = sizeof(struct cramfs_inode) + ((namelen + 3) & ~3);
		*fslen_ub += size;
		if (S_ISDIR(st.st_mode)) {
			entry->size = parse_directory(root_entry, path, &entry->child, fslen_ub);
		} else if (S_ISREG(st.st_mode)) {
			/* TODO: We ought to open files in do_compress, one
			   at a time, instead of amassing all these memory
			   maps during parse_directory (which don't get used
			   until do_compress anyway).  As it is, we tend to
			   get EMFILE errors (especially if mkcramfs is run
			   by non-root).

			   While we're at it, do analagously for symlinks
			   (which would just save a little memory). */
			int fd = open(path, O_RDONLY);
			if (fd < 0) {
				perror(path);
				continue;
			}
			if (entry->size) {
				if ((entry->size >= 1 << SIZE_WIDTH)) {
					warn_size = 1;
					entry->size = (1 << SIZE_WIDTH) - 1;
				}

				entry->uncompressed = mmap(NULL, entry->size, PROT_READ, MAP_PRIVATE, fd, 0);
				if (-1 == (int) (long) entry->uncompressed) {
					perror("mmap");
					exit(5);
				}
			}
			close(fd);
		} else if (S_ISLNK(st.st_mode)) {
			entry->uncompressed = malloc(entry->size);
			if (!entry->uncompressed) {
				perror(NULL);
				exit(5);
			}
			if (readlink(path, entry->uncompressed, entry->size) < 0) {
				perror(path);
				continue;
			}
		} else {
			entry->size = st.st_rdev;
			if (entry->size & -(1<<SIZE_WIDTH))
				warn_dev = 1;
		}

		if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
			/* block pointers & data expansion allowance + data */
                        if(entry->size) 
                                *fslen_ub += ((4+26)*((entry->size - 1) / blksize + 1)
                                              + MIN(entry->size + 3, st.st_blocks << 9));
                        else 
                                *fslen_ub += MIN(entry->size + 3, st.st_blocks << 9);
                }

		/* Link it into the list */
		*prev = entry;
		prev = &entry->next;
		count++;
		totalsize += size;
	}
	closedir(dir);
	free(path);
	return totalsize;
}

static void set_random(void *area, size_t size)
{
	int fd = open("/dev/random", O_RDONLY);

	if (fd >= 0) {
		if (read(fd, area, size) == size)
			return;
	}
	memset(area, 0x00, size);
}

/* Returns sizeof(struct cramfs_super), which includes the root inode. */
static unsigned int write_superblock(struct entry *root, char *base)
{
	struct cramfs_super *super = (struct cramfs_super *) base;
	unsigned int offset = sizeof(struct cramfs_super);

	super->magic = CRAMFS_MAGIC;
	super->flags = 0;
	/* Note: 0x10000 is meaningless, which is a bug; but
	   super->size is never used anyway. */
	super->size = 0x10000;
	memcpy(super->signature, CRAMFS_SIGNATURE, sizeof(super->signature));
	set_random(super->fsid, sizeof(super->fsid));
	strncpy(super->name, "Compressed", sizeof(super->name));

	super->root.mode = root->mode;
	super->root.uid = root->uid;
	super->root.gid = root->gid;
	super->root.size = root->size;
	super->root.offset = offset >> 2;

	return offset;
}

static void set_data_offset(struct entry *entry, char *base, unsigned long offset)
{
	struct cramfs_inode *inode = (struct cramfs_inode *) (base + entry->dir_offset);
	assert ((offset & 3) == 0);
	if (offset >= (1 << (2 + OFFSET_WIDTH))) {
		fprintf(stderr, "filesystem too big.  Exiting.\n");
		exit(1);
	}
	inode->offset = (offset >> 2);
}


/*
 * We do a width-first printout of the directory
 * entries, using a stack to remember the directories
 * we've seen.
 */
#define MAXENTRIES (100)
static unsigned int write_directory_structure(struct entry *entry, char *base, unsigned int offset)
{
	int stack_entries = 0;
	struct entry *entry_stack[MAXENTRIES];

	for (;;) {
		int dir_start = stack_entries;
		while (entry) {
			struct cramfs_inode *inode = (struct cramfs_inode *) (base + offset);
			size_t len = strlen(entry->name);

			entry->dir_offset = offset;

			inode->mode = entry->mode;
			inode->uid = entry->uid;
			inode->gid = entry->gid;
			inode->size = entry->size;
			inode->offset = 0;
			/* Non-empty directories, regfiles and symlinks will
			   write over inode->offset later. */

			offset += sizeof(struct cramfs_inode);
			memcpy(base + offset, entry->name, len);
			/* Pad up the name to a 4-byte boundary */
			while (len & 3) {
				*(base + offset + len) = '\0';
				len++;
			}
			inode->namelen = len >> 2;
			offset += len;

			/* TODO: this may get it wrong for chars >= 0x80.
			   Most filesystems use UTF8 encoding for filenames,
			   whereas the console is a single-byte character
			   set like iso-latin-1. */
			printf("  %s\n", entry->name);
			if (entry->child) {
				if (stack_entries >= MAXENTRIES) {
					fprintf(stderr, "Exceeded MAXENTRIES.  Raise this value in mkcramfs.c and recompile.  Exiting.\n");
					exit(1);
				}
				entry_stack[stack_entries] = entry;
				stack_entries++;
			}
			entry = entry->next;
		}

		/*
		 * Reverse the order the stack entries pushed during
                 * this directory, for a small optimization of disk
                 * access in the created fs.  This change makes things
                 * `ls -UR' order.
		 */
		{
			struct entry **lo = entry_stack + dir_start;
			struct entry **hi = entry_stack + stack_entries;
			struct entry *tmp;

			while (lo < --hi) {
				tmp = *lo;
				*lo++ = *hi;
				*hi = tmp;
			}
		}

		/* Pop a subdirectory entry from the stack, and recurse. */
		if (!stack_entries)
			break;
		stack_entries--;
		entry = entry_stack[stack_entries];

		set_data_offset(entry, base, offset);
		printf("'%s':\n", entry->name);
		entry = entry->child;
	}
	return offset;
}

#ifdef DO_HOLES
/*
 * Returns non-zero iff the first LEN bytes from BEGIN are all NULs.
 */
static int
is_zero(char const *begin, unsigned len)
{
	return (len-- == 0 ||
		(begin[0] == '\0' &&
		 (len-- == 0 ||
		  (begin[1] == '\0' &&
		   (len-- == 0 ||
		    (begin[2] == '\0' &&
		     (len-- == 0 ||
		      (begin[3] == '\0' &&
		       memcmp(begin, begin + 4, len) == 0))))))));
}
#else /* !DO_HOLES */
# define is_zero(_begin,_len) (0)  /* Never create holes. */
#endif /* !DO_HOLES */

/*
 * One 4-byte pointer per block and then the actual blocked
 * output. The first block does not need an offset pointer,
 * as it will start immediately after the pointer block;
 * so the i'th pointer points to the end of the i'th block
 * (i.e. the start of the (i+1)'th block or past EOF).
 *
 * Note that size > 0, as a zero-sized file wouldn't ever
 * have gotten here in the first place.
 */
static unsigned int do_compress(char *base, unsigned int offset, char const *name, char *uncompressed, unsigned int size)
{
	unsigned long original_size = size;
	unsigned long original_offset = offset;
	unsigned long new_size;
	unsigned long blocks = (size - 1) / blksize + 1;
	unsigned long curr = offset + 4 * blocks;
	int change;

	do {
		unsigned long len = 2 * blksize;
		unsigned int input = size;
		if (input > blksize)
			input = blksize;
		size -= input;
		if (!is_zero (uncompressed, input)) {
			compress(base + curr, &len, uncompressed, input);
			curr += len;
		}
		uncompressed += input;

		if (len > blksize*2) {
			/* (I don't think this can happen with zlib.) */
			printf("AIEEE: block \"compressed\" to > 2*blocklength (%ld)\n", len);
			exit(1);
		}

		*(u32 *) (base + offset) = curr;
		offset += 4;
	} while (size);

	curr = (curr + 3) & ~3;
	new_size = curr - original_offset;
	/* TODO: Arguably, original_size in these 2 lines should be
	   st_blocks * 512.  But if you say that then perhaps
	   administrative data should also be included in both. */
	change = new_size - original_size;
	printf("%6.2f%% (%+d bytes)\t%s\n",
	       (change * 100) / (double) original_size, change, name);

	return curr;
}


/*
 * Traverse the entry tree, writing data for every item that has
 * non-null entry->compressed (i.e. every symlink and non-empty
 * regfile).
 */
static unsigned int write_data(struct entry *entry, char *base, unsigned int offset)
{
	do {
		if (entry->uncompressed) {
                        if(entry->same) {
                                set_data_offset(entry, base, entry->same->offset);
                                entry->offset=entry->same->offset;
                        } else {
                                set_data_offset(entry, base, offset);
                                entry->offset=offset;
                                offset = do_compress(base, offset, entry->name, entry->uncompressed, entry->size);
                        }
		}
		else if (entry->child)
			offset = write_data(entry->child, base, offset);
                entry=entry->next;
	} while (entry);
	return offset;
}


/*
 * Maximum size fs you can create is roughly 256MB.  (The last file's
 * data must begin within 256MB boundary but can extend beyond that.)
 *
 * Note that if you want it to fit in a ROM then you're limited to what the
 * hardware and kernel can support (64MB?).
 */
#define MAXFSLEN ((((1 << OFFSET_WIDTH) - 1) << 2) /* offset */ \
		  + (1 << SIZE_WIDTH) - 1 /* filesize */ \
		  + (1 << SIZE_WIDTH) * 4 / PAGE_CACHE_SIZE /* block pointers */ )


/*
 * Usage:
 *
 *      mkcramfs directory-name outfile
 *
 * where "directory-name" is simply the root of the directory
 * tree that we want to generate a compressed filesystem out
 * of.
 */
int main(int argc, char **argv)
{
	struct stat st;
	struct entry *root_entry;
	char *rom_image;
	unsigned int offset;
	ssize_t written;
	int fd;
	loff_t fslen_ub = 0; /* initial guess (upper-bound) of
				required filesystem size */
	char const *dirname;

	if (argc)
		progname = argv[0];
	if (argc != 3)
		usage();

	if (stat(dirname = argv[1], &st) < 0) {
		perror(argv[1]);
		exit(1);
	}
	fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0666);

	root_entry = calloc(1, sizeof(struct entry));
	if (!root_entry) {
		perror(NULL);
		exit(5);
	}
	root_entry->mode = st.st_mode;
	root_entry->uid = st.st_uid;
	root_entry->gid = st.st_gid;

	root_entry->size = parse_directory(root_entry, argv[1], &root_entry->child, &fslen_ub);
	if (fslen_ub > MAXFSLEN) {
		fprintf(stderr,
			"warning: guestimate of required size (upper bound) is %luMB, but maximum image size is %uMB.  We might die prematurely.\n",
			(unsigned long) (fslen_ub >> 20),
			MAXFSLEN >> 20);
		fslen_ub = MAXFSLEN;
	}

        /* find duplicate files. TODO: uses the most inefficient algorithm
           possible. */
        eliminate_doubles(root_entry,root_entry);


	/* TODO: Why do we use a private/anonymous mapping here
           followed by a write below, instead of just a shared mapping
           and a couple of ftruncate calls?  Is it just to save us
           having to deal with removing the file afterwards?  If we
           really need this huge anonymous mapping, we ought to mmap
           in smaller chunks, so that the user doesn't need nn MB of
           RAM free.  If the reason is to be able to write to
           un-mmappable block devices, then we could try shared mmap
           and revert to anonymous mmap if the shared mmap fails. */
	rom_image = mmap(NULL, fslen_ub, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (-1 == (int) (long) rom_image) {
		perror("ROM image map");
		exit(1);
	}
	offset = write_superblock(root_entry, rom_image);
	printf("Super block: %d bytes\n", offset);

	offset = write_directory_structure(root_entry->child, rom_image, offset);
	printf("Directory data: %d bytes\n", offset);

	offset = write_data(root_entry, rom_image, offset);

	/* We always write a multiple of blksize bytes, so that
           losetup works. */
	offset = ((offset - 1) | (blksize - 1)) + 1;
	printf("Everything: %d kilobytes\n", offset >> 10);

	written = write(fd, rom_image, offset);
	if (written < 0) {
		perror("rom image");
		exit(1);
	}
	if (offset != written) {
		fprintf(stderr, "ROM image write failed (%d %d)\n", written, offset);
		exit(1);
	}

	/* (These warnings used to come at the start, but they scroll off the
           screen too quickly.) */
	if (warn_namelen) /* (can't happen when reading from ext2fs) */
		fprintf(stderr, /* bytes, not chars: think UTF8. */
			"warning: filenames truncated to 255 bytes.\n");
	if (warn_size)
		fprintf(stderr,
			"warning: file sizes truncated to %luMB (minus 1 byte).\n",
			1L << (SIZE_WIDTH - 20));
	if (warn_uid) /* (not possible with current Linux versions) */
		fprintf(stderr,
			"warning: uids truncated to %u bits.  (This may be a security concern.)\n",
			UID_WIDTH);
	if (warn_gid)
		fprintf(stderr,
			"warning: gids truncated to %u bits.  (This may be a security concern.)\n",
			GID_WIDTH);
	if (warn_dev)
		fprintf(stderr,
			"WARNING: device numbers truncated to %u bits.  This almost certainly means\n"
			"that some device files will be wrong.\n",
			OFFSET_WIDTH);
	return 0;
}
