#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>

static unsigned int offset;
static unsigned int ino = 721;

static void push_string(const char *name)
{
	unsigned int name_len = strlen(name) + 1;

	fputs(name, stdout);
	putchar(0);
	offset += name_len;
}

static void push_pad (void)
{
	while (offset & 3) {
		putchar(0);
		offset++;
	}
}

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
	const char name[] = "TRAILER!!!";

	sprintf(s, "%s%08X%08X%08lX%08lX%08X%08lX"
	       "%08X%08X%08X%08X%08X%08ZX%08X",
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
	       "%08X%08X%08X%08X%08X%08ZX%08X",
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
	       "%08X%08X%08X%08X%08X%08ZX%08X",
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

/* Not marked static to keep the compiler quiet, as no one uses this yet... */
void cpio_mkfile(const char *filename, const char *location,
			unsigned int mode, uid_t uid, gid_t gid)
{
	char s[256];
	char *filebuf;
	struct stat buf;
	int file;
	int retval;
	int i;

	mode |= S_IFREG;

	retval = stat (filename, &buf);
	if (retval) {
		fprintf (stderr, "Filename %s could not be located\n", filename);
		goto error;
	}

	file = open (filename, O_RDONLY);
	if (file < 0) {
		fprintf (stderr, "Filename %s could not be opened for reading\n", filename);
		goto error;
	}

	filebuf = malloc(buf.st_size);
	if (!filebuf) {
		fprintf (stderr, "out of memory\n");
		goto error_close;
	}

	retval = read (file, filebuf, buf.st_size);
	if (retval < 0) {
		fprintf (stderr, "Can not read %s file\n", filename);
		goto error_free;
	}

	sprintf(s,"%s%08X%08X%08lX%08lX%08X%08lX"
	       "%08X%08X%08X%08X%08X%08ZX%08X",
		"070701",		/* magic */
		ino++,			/* ino */
		mode,			/* mode */
		(long) uid,		/* uid */
		(long) gid,		/* gid */
		1,			/* nlink */
		(long) buf.st_mtime,	/* mtime */
		(int) buf.st_size,	/* filesize */
		3,			/* major */
		1,			/* minor */
		0,			/* rmajor */
		0,			/* rminor */
		strlen(location) + 1,	/* namesize */
		0);			/* chksum */
	push_hdr(s);
	push_string(location);
	push_pad();

	for (i = 0; i < buf.st_size; ++i)
		fputc(filebuf[i], stdout);
	close(file);
	free(filebuf);
	push_pad();
	return;
	
error_free:
	free(filebuf);
error_close:
	close(file);
error:
	exit(-1);
}

int main (int argc, char *argv[])
{
	cpio_mkdir("/dev", 0755, 0, 0);
	cpio_mknod("/dev/console", 0600, 0, 0, 'c', 5, 1);
	cpio_mkdir("/root", 0700, 0, 0);
	cpio_trailer();

	exit(0);

	/* silence compiler warnings */
	return 0;
	(void) argc;
	(void) argv;
}

