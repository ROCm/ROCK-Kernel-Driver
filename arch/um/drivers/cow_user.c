#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <byteswap.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/param.h>
#include <netinet/in.h>

#include "cow.h"
#include "cow_sys.h"

#define PATH_LEN_V1 256

struct cow_header_v1 {
	int magic;
	int version;
	char backing_file[PATH_LEN_V1];
	time_t mtime;
	__u64 size;
	int sectorsize;
};

#define PATH_LEN_V2 MAXPATHLEN

struct cow_header_v2 {
	unsigned long magic;
	unsigned long version;
	char backing_file[PATH_LEN_V2];
	time_t mtime;
	__u64 size;
	int sectorsize;
};

union cow_header {
	struct cow_header_v1 v1;
	struct cow_header_v2 v2;
};

#define COW_MAGIC 0x4f4f4f4d  /* MOOO */
#define COW_VERSION 2

void cow_sizes(__u64 size, int sectorsize, int bitmap_offset, 
	       unsigned long *bitmap_len_out, int *data_offset_out)
{
	*bitmap_len_out = (size + sectorsize - 1) / (8 * sectorsize);

	*data_offset_out = bitmap_offset + *bitmap_len_out;
	*data_offset_out = (*data_offset_out + sectorsize - 1) / sectorsize;
	*data_offset_out *= sectorsize;
}

static int absolutize(char *to, int size, char *from)
{
	char save_cwd[256], *slash;
	int remaining;

	if(getcwd(save_cwd, sizeof(save_cwd)) == NULL) {
		cow_printf("absolutize : unable to get cwd - errno = %d\n", 
			   errno);
		return(-1);
	}
	slash = strrchr(from, '/');
	if(slash != NULL){
		*slash = '\0';
		if(chdir(from)){
			*slash = '/';
			cow_printf("absolutize : Can't cd to '%s' - " 
				   "errno = %d\n", from, errno);
			return(-1);
		}
		*slash = '/';
		if(getcwd(to, size) == NULL){
			cow_printf("absolutize : unable to get cwd of '%s' - "
			       "errno = %d\n", from, errno);
			return(-1);
		}
		remaining = size - strlen(to);
		if(strlen(slash) + 1 > remaining){
			cow_printf("absolutize : unable to fit '%s' into %d "
			       "chars\n", from, size);
			return(-1);
		}
		strcat(to, slash);
	}
	else {
		if(strlen(save_cwd) + 1 + strlen(from) + 1 > size){
			cow_printf("absolutize : unable to fit '%s' into %d "
			       "chars\n", from, size);
			return(-1);
		}
		strcpy(to, save_cwd);
		strcat(to, "/");
		strcat(to, from);
	}
	chdir(save_cwd);
	return(0);
}

int write_cow_header(char *cow_file, int fd, char *backing_file, 
		     int sectorsize, long long *size)
{
	struct cow_header_v2 *header;
	struct stat64 buf;
	int err;

	err = cow_seek_file(fd, 0);
	if(err != 0){
		cow_printf("write_cow_header - lseek failed, errno = %d\n", 
			   errno);
		return(-errno);
	}

	err = -ENOMEM;
	header = cow_malloc(sizeof(*header));
	if(header == NULL){
		cow_printf("Failed to allocate COW V2 header\n");
		goto out;
	}
	header->magic = htonl(COW_MAGIC);
	header->version = htonl(COW_VERSION);

	err = -EINVAL;
	if(strlen(backing_file) > sizeof(header->backing_file) - 1){
		cow_printf("Backing file name \"%s\" is too long - names are "
			   "limited to %d characters\n", backing_file, 
			   sizeof(header->backing_file) - 1);
		goto out_free;
	}

	if(absolutize(header->backing_file, sizeof(header->backing_file), 
		      backing_file))
		goto out_free;

	err = stat64(header->backing_file, &buf);
	if(err < 0){
		cow_printf("Stat of backing file '%s' failed, errno = %d\n",
			   header->backing_file, errno);
		err = -errno;
		goto out_free;
	}

	err = cow_file_size(header->backing_file, size);
	if(err){
		cow_printf("Couldn't get size of backing file '%s', "
			   "errno = %d\n", header->backing_file, -*size);
		goto out_free;
	}

	header->mtime = htonl(buf.st_mtime);
	header->size = htonll(*size);
	header->sectorsize = htonl(sectorsize);

	err = write(fd, header, sizeof(*header));
	if(err != sizeof(*header)){
		cow_printf("Write of header to new COW file '%s' failed, "
			   "errno = %d\n", cow_file, errno);
		goto out_free;
	}
	err = 0;
 out_free:
	cow_free(header);
 out:
	return(err);
}

int file_reader(__u64 offset, char *buf, int len, void *arg)
{
	int fd = *((int *) arg);

	return(pread(fd, buf, len, offset));
}

int read_cow_header(int (*reader)(__u64, char *, int, void *), void *arg, 
		    __u32 *magic_out, char **backing_file_out, 
		    time_t *mtime_out, __u64 *size_out, 
		    int *sectorsize_out, int *bitmap_offset_out)
{
	union cow_header *header;
	char *file;
	int err, n;
	unsigned long version, magic;

	header = cow_malloc(sizeof(*header));
	if(header == NULL){
	        cow_printf("read_cow_header - Failed to allocate header\n");
		return(-ENOMEM);
	}
	err = -EINVAL;
	n = (*reader)(0, (char *) header, sizeof(*header), arg);
	if(n < offsetof(typeof(header->v1), backing_file)){
		cow_printf("read_cow_header - short header\n");
		goto out;
	}

	magic = header->v1.magic;
	if(magic == COW_MAGIC) {
		version = header->v1.version;
	}
	else if(magic == ntohl(COW_MAGIC)){
		version = ntohl(header->v1.version);
	}
	/* No error printed because the non-COW case comes through here */
	else goto out;

	*magic_out = COW_MAGIC;

	if(version == 1){
		if(n < sizeof(header->v1)){
			cow_printf("read_cow_header - failed to read V1 "
				   "header\n");
			goto out;
		}
		*mtime_out = header->v1.mtime;
		*size_out = header->v1.size;
		*sectorsize_out = header->v1.sectorsize;
		*bitmap_offset_out = sizeof(header->v1);
		file = header->v1.backing_file;
	}
	else if(version == 2){
		if(n < sizeof(header->v2)){
			cow_printf("read_cow_header - failed to read V2 "
				   "header\n");
			goto out;
		}
		*mtime_out = ntohl(header->v2.mtime);
		*size_out = ntohll(header->v2.size);
		*sectorsize_out = ntohl(header->v2.sectorsize);
		*bitmap_offset_out = sizeof(header->v2);
		file = header->v2.backing_file;
	}
	else {
		cow_printf("read_cow_header - invalid COW version\n");
		goto out;
	}
	err = -ENOMEM;
	*backing_file_out = cow_strdup(file);
	if(*backing_file_out == NULL){
		cow_printf("read_cow_header - failed to allocate backing "
			   "file\n");
		goto out;
	}
	err = 0;
 out:
	cow_free(header);
	return(err);
}

int init_cow_file(int fd, char *cow_file, char *backing_file, int sectorsize,
		  int *bitmap_offset_out, unsigned long *bitmap_len_out, 
		  int *data_offset_out)
{
	__u64 size, offset;
	char zero = 0;
	int err;

	err = write_cow_header(cow_file, fd, backing_file, sectorsize, &size);
	if(err) 
		goto out;
	
	cow_sizes(size, sectorsize, sizeof(struct cow_header_v2), 
		  bitmap_len_out, data_offset_out);
	*bitmap_offset_out = sizeof(struct cow_header_v2);

	offset = *data_offset_out + size - sizeof(zero);
	err = cow_seek_file(fd, offset);
	if(err != 0){
		cow_printf("cow bitmap lseek failed : errno = %d\n", errno);
		goto out;
	}

	/* does not really matter how much we write it is just to set EOF 
	 * this also sets the entire COW bitmap
	 * to zero without having to allocate it 
	 */
	err = cow_write_file(fd, &zero, sizeof(zero));
	if(err != sizeof(zero)){
		err = -EINVAL;
		cow_printf("Write of bitmap to new COW file '%s' failed, "
			   "errno = %d\n", cow_file, errno);
		goto out;
	}

	return(0);

 out:
	return(err);
}

/*
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
