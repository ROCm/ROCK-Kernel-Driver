#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static unsigned int offset;
static unsigned int ino = 721;

static void push_rest(const char *name)
{
	unsigned int name_len = strlen(name) + 1;
	unsigned int tmp_ofs;

	fputs(name, stdout);
	putchar(0);
	offset += name_len;

	tmp_ofs = name_len + 110;
	while (tmp_ofs & 3) {
		putchar(0);
		offset++;
		tmp_ofs++;
	}
}

static void push_hdr(const char *s)
{
	fputs(s, stdout);
	offset += 110;
}

static void cpio_trailer(void)
{
	char s[256];
	const char *name = "TRAILER!!!";

	sprintf(s, "%s%08X%08X%08lX%08lX%08X%08lX"
	       "%08X%08X%08X%08X%08X%08X%08X",
		"070701",		/* magic */
		0,			/* ino */
		0,			/* mode */
		(long) 0,		/* uid */
		(long) 0,		/* gid */
		1,			/* nlink */
		(long) 0,		/* mtime */
		0,			/* filesize */
		0,			/* major */
		0,			/* minor */
		0,			/* rmajor */
		0,			/* rminor */
		strlen(name) + 1,	/* namesize */
		0);			/* chksum */
	push_hdr(s);
	push_rest(name);

	while (offset % 512) {
		putchar(0);
		offset++;
	}
}

static void cpio_mkdir(const char *name, unsigned int mode,
		       uid_t uid, gid_t gid)
{
	char s[256];
	time_t mtime = time(NULL);

	sprintf(s,"%s%08X%08X%08lX%08lX%08X%08lX"
	       "%08X%08X%08X%08X%08X%08X%08X",
		"070701",		/* magic */
		ino++,			/* ino */
		S_IFDIR | mode,		/* mode */
		(long) uid,		/* uid */
		(long) gid,		/* gid */
		2,			/* nlink */
		(long) mtime,		/* mtime */
		0,			/* filesize */
		3,			/* major */
		1,			/* minor */
		0,			/* rmajor */
		0,			/* rminor */
		strlen(name) + 1,	/* namesize */
		0);			/* chksum */
	push_hdr(s);
	push_rest(name);
}

static void cpio_mknod(const char *name, unsigned int mode,
		       uid_t uid, gid_t gid, int dev_type,
		       unsigned int maj, unsigned int min)
{
	char s[256];
	time_t mtime = time(NULL);

	if (dev_type == 'b')
		mode |= S_IFBLK;
	else
		mode |= S_IFCHR;

	sprintf(s,"%s%08X%08X%08lX%08lX%08X%08lX"
	       "%08X%08X%08X%08X%08X%08X%08X",
		"070701",		/* magic */
		ino++,			/* ino */
		mode,			/* mode */
		(long) uid,		/* uid */
		(long) gid,		/* gid */
		1,			/* nlink */
		(long) mtime,		/* mtime */
		0,			/* filesize */
		3,			/* major */
		1,			/* minor */
		maj,			/* rmajor */
		min,			/* rminor */
		strlen(name) + 1,	/* namesize */
		0);			/* chksum */
	push_hdr(s);
	push_rest(name);
}

int main (int argc, char *argv[])
{
	cpio_mkdir("/dev", 0700, 0, 0);
	cpio_mknod("/dev/console", 0600, 0, 0, 'c', 5, 1);
	cpio_mkdir("/root", 0700, 0, 0);
	cpio_trailer();

	exit(0);

	/* silence compiler warnings */
	return 0;
	(void) argc;
	(void) argv;
}

