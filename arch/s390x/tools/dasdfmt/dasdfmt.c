/*
 *
 * dasdfmt.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Corporation
 *    Author(s): Utz Bacher, <utz.bacher@de.ibm.com>
 *
 *  Device-in-use-checks by Fritz Elfert, <felfert@to.com>
 *
 * Still to do:
 *   detect non-switch parameters ("dasdfmt -n 170 XY") and complain about them 
 */

/* #define _LINUX_BLKDEV_H */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <mntent.h>
#define __KERNEL__ /* we want to use kdev_t and not have to define it */
#include <linux/kdev_t.h>
#undef __KERNEL__

#include <linux/fs.h>
#include <asm/dasd.h>
#include <linux/hdreg.h>

#define EXIT_MISUSE 1
#define EXIT_BUSY 2
#define TEMPFILENAME "/tmp/ddfXXXXXX"
#define TEMPFILENAMECHARS 8  /* 8 characters are fixed in all temp filenames */
#define SLASHDEV "/dev/"
#define PROC_DASD_DEVICES "/proc/dasd/devices"
/* _PATH_MOUNTED is /etc/mtab - /proc/mounts does not show root-fs correctly */
#define PROC_MOUNTS _PATH_MOUNTED
#define PROC_SWAPS "/proc/swaps"
#define DASD_DRIVER_NAME "dasd"
#define LABEL_LENGTH 10
#define PROC_LINE_LENGTH 80
#define ERR_LENGTH 80

#define MAX_FILELEN NAME_MAX+PATH_MAX

#define GIVEN_DEVNO 1
#define GIVEN_MAJOR 2
#define GIVEN_MINOR 4

#define CHECK_START 1
#define CHECK_END 2
#define CHECK_BLKSIZE 4
#define CHECK_ALL ~0

#define ERRMSG(x...) {fflush(stdout);fprintf(stderr,x);}
#define ERRMSG_EXIT(ec,x...) {fflush(stdout);fprintf(stderr,x);exit(ec);}

#define CHECK_SPEC_MAX_ONCE(i,str) \
	{if (i>1) \
		ERRMSG_EXIT(EXIT_MISUSE,"%s: " str " " \
			"can only be specified once\n",prog_name);}

#define PARSE_PARAM_INTO(x,param,base,str) \
	{x=(int)strtol(param,&endptr,base); \
	if (*endptr) \
		ERRMSG_EXIT(EXIT_MISUSE,"%s: " str " " \
			"is in invalid format\n",prog_name);}

char *prog_name;/*="dasdfmt";*/
char tempfilename[]=TEMPFILENAME;

__u8 _ascebc[256] =
{
 /*00 NUL   SOH   STX   ETX   EOT   ENQ   ACK   BEL */
     0x00, 0x01, 0x02, 0x03, 0x37, 0x2D, 0x2E, 0x2F,
 /*08  BS    HT    LF    VT    FF    CR    SO    SI */
 /*              ->NL                               */
     0x16, 0x05, 0x15, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
 /*10 DLE   DC1   DC2   DC3   DC4   NAK   SYN   ETB */
     0x10, 0x11, 0x12, 0x13, 0x3C, 0x3D, 0x32, 0x26,
 /*18 CAN    EM   SUB   ESC    FS    GS    RS    US */
 /*                               ->IGS ->IRS ->IUS */
     0x18, 0x19, 0x3F, 0x27, 0x22, 0x1D, 0x1E, 0x1F,
 /*20  SP     !     "     #     $     %     &     ' */
     0x40, 0x5A, 0x7F, 0x7B, 0x5B, 0x6C, 0x50, 0x7D,
 /*28   (     )     *     +     ,     -    .      / */
     0x4D, 0x5D, 0x5C, 0x4E, 0x6B, 0x60, 0x4B, 0x61,
 /*30   0     1     2     3     4     5     6     7 */
     0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
 /*38   8     9     :     ;     <     =     >     ? */
     0xF8, 0xF9, 0x7A, 0x5E, 0x4C, 0x7E, 0x6E, 0x6F,
 /*40   @     A     B     C     D     E     F     G */
     0x7C, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
 /*48   H     I     J     K     L     M     N     O */
     0xC8, 0xC9, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6,
 /*50   P     Q     R     S     T     U     V     W */
     0xD7, 0xD8, 0xD9, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6,
 /*58   X     Y     Z     [     \     ]     ^     _ */
     0xE7, 0xE8, 0xE9, 0xBA, 0xE0, 0xBB, 0xB0, 0x6D,
 /*60   `     a     b     c     d     e     f     g */
     0x79, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
 /*68   h     i     j     k     l     m     n     o */
     0x88, 0x89, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,
 /*70   p     q     r     s     t     u     v     w */
     0x97, 0x98, 0x99, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
 /*78   x     y     z     {     |     }     ~    DL */
     0xA7, 0xA8, 0xA9, 0xC0, 0x4F, 0xD0, 0xA1, 0x07,
 /*80*/
     0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
 /*88*/
     0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
 /*90*/
     0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
 /*98*/
     0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
 /*A0*/
     0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
 /*A8*/
     0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
 /*B0*/
     0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
 /*B8*/
     0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
 /*C0*/
     0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
 /*C8*/
     0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
 /*D0*/
     0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
 /*D8*/
     0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
 /*E0        sz						*/
     0x3F, 0x59, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
 /*E8*/
     0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
 /*F0*/
     0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
 /*F8*/
     0x90, 0x3F, 0x3F, 0x3F, 0x3F, 0xEA, 0x3F, 0xFF
};

void convert_label(char *str)
{
	int i;
	for (i=0;i<LABEL_LENGTH;i++) str[i]=_ascebc[str[i]];
}

void
exit_usage(int exitcode)
{
#ifdef RANGE_FORMATTING
	printf("Usage: %s [-htvyLV] [-l <label>] [-b <blocksize>] [<range>] " \
		"<diskspec>\n\n",prog_name);
#else /* RANGE_FORMATTING */
	printf("Usage: %s [-htvyLV] [-l <label>] [-b <blocksize>] " \
		"<diskspec>\n\n",prog_name);
#endif /* RANGE_FORMATTING */
	printf("       -t means testmode\n");
	printf("       -v means verbose mode\n");
	printf("       -V means print version\n");
	printf("       -L means don't write disk label\n");
	printf("       <label> is a label which is converted to EBCDIC and " \
		"written to disk\n");
	printf("       <blocksize> has to be power of 2 and at least 512\n");
#ifdef RANGE_FORMATTING
	printf("       <range> is either\n");
	printf("           -s <start_track> -e <end_track>\n");
	printf("       or\n");
	printf("           -r <start_track>-<end_track>\n");
#endif /* RANGE_FORMATTING */
	printf("       and <diskspec> is either\n");
	printf("           -f /dev/dasdX\n");
	printf("       or\n");
	printf("           -n <s390-devnr>\n");
	exit(exitcode);
}

void
get_xno_from_xno(int *devno,kdev_t *major_no,kdev_t *minor_no,int mode)
{
	FILE *file;
	int d,rc;
	kdev_t mi,ma;
	int mi_i,ma_i; /* for scanf :-( */
	char line[PROC_LINE_LENGTH];

	file=fopen(PROC_DASD_DEVICES,"r");
	if (file==NULL)
		ERRMSG_EXIT(EXIT_FAILURE,"%s: failed to open " \
			PROC_DASD_DEVICES ": %s (do you have the /proc " \
			"filesystem enabled?)\n",prog_name,strerror(errno));

	/*	fgets(line,sizeof(line),file); omit first line */ 
	while (fgets(line,sizeof(line),file)!=NULL) {
		rc=sscanf(line,"%X %*[(A-Z)] at (%d:%d)",&d,&ma_i,&mi_i);
		ma=ma_i;
		mi=mi_i;
		if ( (rc==3) &&
			!((d!=*devno)&&(mode&GIVEN_DEVNO)) &&
			!((ma!=*major_no)&&(mode&GIVEN_MAJOR)) &&
			!((mi!=*minor_no)&&(mode&GIVEN_MINOR)) ) {
			*devno=d;
			*major_no=ma;
			*minor_no=mi;
			/* yes, this is a quick exit, but the easiest way */
			fclose(file);
			return;
		}
	}
	fclose(file);

	ERRMSG_EXIT(EXIT_FAILURE,"%s: failed to find device in the /proc " \
		"filesystem (are you sure to have the right param line?)\n",
		prog_name);
}

char *
get_devname_from_devno(int devno,int verbosity)
{
	kdev_t major_no,minor_no;
	kdev_t file_major,file_minor;
	struct stat stat_buf;
	int rc;
	int found;
	char *devname;
	char tmpname[MAX_FILELEN];

	DIR *dp;
	struct dirent *direntp;

	/**** get minor number ****/
	get_xno_from_xno(&devno,&major_no,&minor_no,GIVEN_DEVNO);

	/**** get device file ****/
	if ((dp=opendir(SLASHDEV)) == NULL)
		ERRMSG_EXIT(EXIT_FAILURE,"%s: unable to read " SLASHDEV \
			"\n",prog_name);
	found=0;
	while ((direntp=readdir(dp)) != NULL) {
		strcpy(tmpname,SLASHDEV);
		strcat(tmpname,direntp->d_name);
		rc=stat(tmpname,&stat_buf);
		if (!rc) {
			file_major=MAJOR(stat_buf.st_rdev);
			file_minor=MINOR(stat_buf.st_rdev);
			if ((file_major==major_no) && (file_minor==minor_no)) {
				found=1;
				break;
			}
		}
	}
	if (found) {
		devname=malloc(strlen(direntp->d_name));
		strcpy(devname,tmpname);
	}
	rc=closedir(dp);
	if (rc<0) ERRMSG("%s: unable to close directory " SLASHDEV \
		"; continuing\n",prog_name);
	if (found)
		return devname;

	if (verbosity>=1)
		printf("I didn't find device node in " SLASHDEV \
			"; trying to create a temporary node\n");

	/**** get temp file and create device node *****/
	rc=mkstemp(tempfilename);
	if (rc==-1)
		ERRMSG_EXIT(EXIT_FAILURE,"%s: failed to get temporary " \
			"filename: %s\n",prog_name,strerror(errno));
	close(rc);
	rc=unlink(tempfilename);
	
	rc=mknod(tempfilename,S_IFBLK|0600,MKDEV(major_no,minor_no));
	if (rc)
		ERRMSG_EXIT(EXIT_FAILURE,"%s: failed to create temporary " \
			"device node %s: %s\n",prog_name,tempfilename,
			strerror(errno));
	return tempfilename;
}

char *
check_param(int mode,format_data_t data)
{
	char *s;

	if (NULL==(s=malloc(ERR_LENGTH)))
		ERRMSG_EXIT(EXIT_FAILURE,"%s: not enough memory.\n",prog_name);

	if ((mode&CHECK_START)&&(data.start_unit<0)) {
		strcpy(s,"start track must be greater than zero");
		goto exit;
	}
	if ((mode&CHECK_END)&&(data.stop_unit<-1)) {
		strcpy(s,"end track must be -1 or greater than zero");
		goto exit;
	}
	if ((mode&CHECK_END)&&(data.start_unit>data.stop_unit)&&
		(data.stop_unit!=-1)) {
		strcpy(s,"end track must be higher than start track");
		goto exit;
	}

	if ((mode&CHECK_BLKSIZE)&&(data.blksize<1)) {
		strcpy(s,"blocksize must be a positive integer");
		goto exit;
	}
	if (mode&CHECK_BLKSIZE) while (data.blksize>0) {
		if ((data.blksize%2)&&(data.blksize!=1)) {
			strcpy(s,"blocksize must be a power of 2");
			goto exit;
		}
		data.blksize/=2;
	}

	free(s);
	return NULL;
exit:
	return s;
}

#define ASK_PRINTOUT printf("Please enter %s",output)
#define ASK_GETBUFFER fgets(buffer,sizeof(buffer),stdin)
#define ASK_SCANFORNUMBER(var) rc=sscanf(buffer,"%d%c",&var,&c)
#define ASK_COMPLAIN_FORMAT if ((rc==2)&&(c=='\n')) rc=1; \
	if (rc==-1) rc=1; /* this happens, if enter is pressed */ \
	if (rc!=1) printf(" -- wrong input, try again.\n")
#define ASK_CHECK_PARAM(mode) str=check_param(mode,params); \
		if (str!=NULL) { printf(" -- %s\n",str); rc=0; free(str); }

format_data_t
ask_user_for_data(format_data_t params)
{
	char buffer[20]; /* should be enough for inputing track numbers */
	char c;
	int i,rc;
	char *str;
	char output[60],o2[12];

#ifdef RANGE_FORMATTING
	i=params.start_unit;
	do {
		params.start_unit=i;
		sprintf(output,"the start track of the range to format " \
			"[%d]: ",i);
		ASK_PRINTOUT;
		ASK_GETBUFFER;
		ASK_SCANFORNUMBER(params.start_unit);
		ASK_COMPLAIN_FORMAT;
		ASK_CHECK_PARAM(CHECK_START);
	} while (rc!=1);

	i=params.stop_unit;
	do {
		params.stop_unit=i;
		sprintf(output,"the end track of the range to format [");
		if (i==-1) sprintf(o2,"END]: "); else
			sprintf(o2,"%d]: ",i);
		strcat(output,o2);
		ASK_PRINTOUT;
		ASK_GETBUFFER;
		if ( (!strcasecmp(buffer,"end")) ||
			(!strcasecmp(buffer,"end\n")) ) {
			rc=1;
			params.stop_unit=-1;
		} else {
			ASK_SCANFORNUMBER(params.stop_unit);
			ASK_COMPLAIN_FORMAT;
			ASK_CHECK_PARAM(CHECK_END);
		}
	} while (rc!=1);
#endif /* RANGE_FORMATTING */

	i=params.blksize;
	do {
		params.blksize=i;
		sprintf(output,"the blocksize of the formatting [%d]: ",i);
		ASK_PRINTOUT;
		ASK_GETBUFFER;
		ASK_SCANFORNUMBER(params.blksize);
		ASK_COMPLAIN_FORMAT;
		ASK_CHECK_PARAM(CHECK_BLKSIZE);
	} while (rc!=1);

	return params;
}

/* Check if the device we are going to format is mounted.
 * If true, complain and exit.
 */
void
check_mounted(int major, int minor)
{
	FILE *f;
	int ishift = 0;
	struct mntent *ment;
	struct stat stbuf;
	char line[128];

	/* If whole disk to be formatted ... */
	if ((minor % (1U << DASD_PARTN_BITS)) == 0) {
		/* ... ignore partition-selector */
		minor >>= DASD_PARTN_BITS;
		ishift = DASD_PARTN_BITS;
	}
	/*
	 * first, check filesystems
	 */
	if (!(f = fopen(PROC_MOUNTS, "r")))
		ERRMSG_EXIT(EXIT_FAILURE, "%s: %s\n", PROC_MOUNTS,
			strerror(errno));
	while ((ment = getmntent(f))) {
		if (stat(ment->mnt_fsname, &stbuf) == 0)
			if ((major == MAJOR(stbuf.st_rdev)) &&
				(minor == (MINOR(stbuf.st_rdev)>>ishift))) {
				ERRMSG("%s: device is mounted on %s!!\n",
					prog_name,ment->mnt_dir);
				ERRMSG_EXIT(EXIT_BUSY, "If you really want to "
					"format it, please unmount it.\n");
			}
	}
	fclose(f);
	/*
	 * second, check active swap spaces
	 */
	if (!(f = fopen(PROC_SWAPS, "r")))
		ERRMSG_EXIT(EXIT_FAILURE, PROC_SWAPS " %s", strerror(errno));
	/*
	 * skip header line
	 */
	fgets(line, sizeof(line), f);
	while (fgets(line, sizeof(line), f)) {
		char *p;
		for (p = line; *p && (!isspace(*p)); p++) ;
		*p = '\0';
		if (stat(line, &stbuf) == 0)
			if ((major == MAJOR(stbuf.st_rdev)) &&
				(minor == (MINOR(stbuf.st_rdev)>>ishift))) {
				ERRMSG("%s: the device is in use for "
					"swapping!!\n",prog_name);
				ERRMSG_EXIT(EXIT_BUSY, "If you really want to "
					"format it, please use swapoff %s.\n",
					line);
			}
	}
	fclose(f);
}

void
do_format_dasd(char *dev_name,format_data_t format_params,int testmode,
	int verbosity,int writenolabel,int labelspec,
	char *label,int withoutprompt,int devno)
{
	int fd,rc;
	struct stat stat_buf;
	kdev_t minor_no,major_no;
	int new_blksize;
	unsigned int label_position;
	struct hd_geometry new_geometry;
	char inp_buffer[5]; /* to contain yes */

	fd=open(dev_name,O_RDWR);
	if (fd==-1)
		ERRMSG_EXIT(EXIT_FAILURE,"%s: error opening device %s: " \
			"%s\n",prog_name,dev_name,strerror(errno));

	if (verbosity>=1) {
	}

	rc=stat(dev_name,&stat_buf);
	if (rc) {
		ERRMSG_EXIT(EXIT_FAILURE,"%s: error occured during stat: " \
			"%s\n",prog_name,strerror(errno));
	} else {
		if (!S_ISBLK(stat_buf.st_mode))
			ERRMSG_EXIT(EXIT_FAILURE,"%s: file is not a " \
				"blockdevice.\n",prog_name);
		major_no=MAJOR(stat_buf.st_rdev);
		minor_no=MINOR(stat_buf.st_rdev);
	}
	check_mounted(major_no, minor_no);

	if ((!writenolabel) && (!labelspec)) {
		sprintf(label,"LNX1 x%04x",devno);
	}
	
	if ( ((withoutprompt)&&(verbosity>=1)) ||
		(!withoutprompt) ) {
		get_xno_from_xno(&devno,&major_no,&minor_no,
			GIVEN_MAJOR|GIVEN_MINOR);
		printf("\nI am going to format the device %s in the " \
			"following way:\n",dev_name);
		printf("   Device number of device : 0x%x\n",devno);
		printf("   Major number of device  : %u\n",major_no);
		printf("   Minor number of device  : %u\n",minor_no);
		printf("   Labelling device        : %s\n",(writenolabel)?
			"no":"yes");
		if (!writenolabel)
			printf("   Disk label              : %s\n",label);
#ifdef RANGE_FORMATTING
		printf("   Start track             : %d\n" \
			,format_params.start_unit);
		printf("   End track               : ");
		if (format_params.stop_unit==-1)
			printf("last track of disk\n");
		else
			printf("%d\n",format_params.stop_unit);
#endif /* RANGE_FORMATTING */
		printf("   Blocksize               : %d\n" \
			,format_params.blksize);
		if (testmode) printf("Test mode active, omitting ioctl.\n");
	}

	while (!testmode) {
		if (!withoutprompt) {
			printf("\n--->> ATTENTION! <<---\n");
			printf("All data in the specified range of that " \
				"device will be lost.\nType \"yes\" to " \
				"continue, no will leave the disk untouched: ");
			fgets(inp_buffer,sizeof(inp_buffer),stdin);
			if (strcasecmp(inp_buffer,"yes") &&
				strcasecmp(inp_buffer,"yes\n")) {
				printf("Omitting ioctl call (disk will " \
					"NOT be formatted).\n");
				break;
			}
		}

		if ( !(  (withoutprompt)&&(verbosity<1) ))
			printf("Formatting the device. This may take a " \
				"while (get yourself a coffee).\n");
		rc=ioctl(fd,BIODASDFORMAT,format_params);
		if (rc)
			ERRMSG_EXIT(EXIT_FAILURE,"%s: the dasd driver " \
				"returned with the following error " \
				"message:\n%s\n",prog_name,strerror(errno));
		printf("Finished formatting the device.\n");

		if (!writenolabel) {
			if (verbosity>0)
				printf("Retrieving disk geometry... ");

			rc=ioctl(fd,HDIO_GETGEO,&new_geometry);
			if (rc) {
				ERRMSG("%s: the ioctl call to get geometry " \
					"returned with the following error " \
					"message:\n%s\n",prog_name,
					strerror(errno));
				goto reread;
			}
	

			rc=ioctl(fd,BLKGETSIZE,&new_blksize);
			if (rc) {
				ERRMSG("%s: the ioctl call to get blocksize " \
					"returned with the following error " \
					"message:\n%s\n",prog_name,
					strerror(errno));
				goto reread;
			}
	
			if (verbosity>0) printf("done\n");

			label_position=new_geometry.start*new_blksize;
	
			if (verbosity>0) printf("Writing label... ");
			convert_label(label);
			rc=lseek(fd,label_position,SEEK_SET);
			if (rc!=label_position) {
				ERRMSG("%s: lseek on the device to %i " \
					"failed with the following error " \
					"message:\n%s\n",prog_name,
					label_position,strerror(errno));
				goto reread;
			}
			rc=write(fd,label,LABEL_LENGTH);
			if (rc!=LABEL_LENGTH) {
				ERRMSG("%s: writing the label only wrote %d " \
					"bytes.\n",prog_name,rc);
				goto reread;
			}

			sync();
			sync();

			if (verbosity>0) printf("done\n");
		}
 reread:
		printf("Rereading the partition table... ");
		rc=ioctl(fd,BLKRRPART,NULL);
		if (rc) {
			ERRMSG("%s: error during rereading the partition " \
			       "table: %s.\n",prog_name,strerror(errno));
		} else printf("done.\n");

		break;
	}

	rc=close(fd);
	if (rc)
		ERRMSG("%s: error during close: " \
			"%s; continuing.\n",prog_name,strerror(errno));
}



int main(int argc,char *argv[]) {
	int verbosity;
	int testmode;
	int withoutprompt;
	int writenolabel,labelspec;

	char *dev_name;
	int devno;
	char *dev_filename,*devno_param_str,*range_param_str;
	char *start_param_str,*end_param_str,*blksize_param_str;
	char label[LABEL_LENGTH+1];
	
	format_data_t format_params;

	int rc;
	int oc;
	char *endptr;

	char c1,c2,cbuffer[6]; /* should be able to contain -end plus 1 char */
	int i,i1,i2;
	char *str;

	int start_specified,end_specified,blksize_specified;
	int devfile_specified,devno_specified,range_specified;

	/******************* initialization ********************/
	prog_name=argv[0];

	endptr=NULL;

	/* set default values */
	format_params.start_unit=DASD_FORMAT_DEFAULT_START_UNIT;
	format_params.stop_unit=DASD_FORMAT_DEFAULT_STOP_UNIT;
	format_params.blksize=DASD_FORMAT_DEFAULT_BLOCKSIZE;
	format_params.intensity=DASD_FORMAT_DEFAULT_INTENSITY;
	testmode=0;
	verbosity=0;
	withoutprompt=0;
	writenolabel=0;
	labelspec=0;
	for (i=0;i<LABEL_LENGTH;i++) label[i]=' ';
	label[LABEL_LENGTH]=0;

	start_specified=end_specified=blksize_specified=0;
	devfile_specified=devno_specified=range_specified=0;

	/*************** parse parameters **********************/

	/* avoid error message generated by getopt */
	opterr=0;

#ifdef RANGE_FORMATTING
	while ( (oc=getopt(argc,argv,"r:s:e:b:n:l:f:hLty?vV")) !=EOF) {
#endif /* RANGE_FORMATTING */
	while ( (oc=getopt(argc,argv,"b:n:l:f:hLty?vV")) !=EOF) {
		switch (oc) {
		case 'y':
			withoutprompt=1;
			break;

		case 't':
			testmode=1;
			break;

		case 'v':
			verbosity++;
			break;

		case '?': /* fall-through */
		case ':':
			exit_usage(EXIT_MISUSE);

		case 'h':
			exit_usage(0);

		case 'V':
			printf("%s version 0.99\n",prog_name);
			exit(0);

		case 'l':
			strncpy(label,optarg,LABEL_LENGTH);
			if (strlen(optarg)<LABEL_LENGTH)
				label[strlen(optarg)]=' ';
			labelspec++;
			break;

		case 'L':
			writenolabel++;
			break;

#ifdef RANGE_FORMATTING
		case 's' :
			start_param_str=optarg;
			start_specified++;
			break;
			
		case 'e' :
			end_param_str=optarg;
			end_specified++;
			break;

		case 'r' :
			range_param_str=optarg;
			range_specified++;
			break;
#endif /* RANGE_FORMATTING */

		case 'b' :
			blksize_param_str=optarg;
			blksize_specified++;
			break;
			
		case 'n' :
			devno_param_str=optarg;
			devno_specified++;
			break;
		
		case 'f' :
			dev_filename=optarg;
			devfile_specified++;
			break;
		}
	}

	/******************** checking of parameters **************/

	/* convert range into -s and -e */
	CHECK_SPEC_MAX_ONCE(range_specified,"formatting range");

	while (range_specified) {
		start_specified++;
		end_specified++;

		/* scan for 1 or 2 integers, separated by a dash */
		rc=sscanf(range_param_str,"%d%c%d%c",&i1,&c1,&i2,&c2);
		if ((rc==3)&&(c1=='-')) {
			format_params.start_unit=i1;
			format_params.stop_unit=i2;
			break;
		}
		if (rc==1) {
			format_params.start_unit=i1;
			break;
		}

		/* scan for integer and -END */
		rc=sscanf(range_param_str,"%d%s",&i1,cbuffer);
		if ((rc==2)&&(!strcasecmp(cbuffer,"-END"))) {
			format_params.start_unit=i1;
			format_params.stop_unit=-1;
			break;
		}
		ERRMSG_EXIT(EXIT_MISUSE,"%s: specified range " \
			"is in invalid format\n",prog_name);
	}

	if ((!devfile_specified)&&(!devno_specified))
		ERRMSG_EXIT(EXIT_MISUSE,"%s: device to format " \
			"not specified\n",prog_name);

	if ((devfile_specified+devno_specified)>1)
		ERRMSG_EXIT(EXIT_MISUSE,"%s: device to format " \
			"can only be specified once\n",prog_name);

	if ((!start_specified)&&(!end_specified)&&(!range_specified)&&
		(!blksize_specified)) {
		format_params=ask_user_for_data(format_params);
	}

	CHECK_SPEC_MAX_ONCE(start_specified,"start track");
	CHECK_SPEC_MAX_ONCE(end_specified,"end track");
	CHECK_SPEC_MAX_ONCE(blksize_specified,"blocksize");
	CHECK_SPEC_MAX_ONCE(labelspec,"label");
	CHECK_SPEC_MAX_ONCE(writenolabel,"omit-label-writing flag");

	if (devno_specified)
		PARSE_PARAM_INTO(devno,devno_param_str,16,"device number");
	if (start_specified&&!range_specified)
		PARSE_PARAM_INTO(format_params.start_unit,start_param_str,10,
			"start track");
	if (end_specified&&!range_specified)
		PARSE_PARAM_INTO(format_params.stop_unit,end_param_str,10,
			"end track");
	if (blksize_specified)
		PARSE_PARAM_INTO(format_params.blksize,blksize_param_str,10,
			"blocksize");

	/***********get dev_name *********************/
	dev_name=(devno_specified)?
		get_devname_from_devno(devno,verbosity):
		dev_filename;

	/*** range checking *********/
	str=check_param(CHECK_ALL,format_params);
	if (str!=NULL) ERRMSG_EXIT(EXIT_MISUSE,"%s: %s\n",prog_name,str);

	/******* issue the real command and reread part table *******/
	do_format_dasd(dev_name,format_params,testmode,verbosity,
		writenolabel,labelspec,label,withoutprompt,devno);

	/*************** cleanup ********************************/
	if (strncmp(dev_name,TEMPFILENAME,TEMPFILENAMECHARS)==0) {
		rc=unlink(dev_name);
		if ((rc)&&(verbosity>=1))
			ERRMSG("%s: temporary device node %s could not be " \
				"removed: %s\n",prog_name,dev_name,
				strerror(errno));
	} else {
		if (devno_specified) {
			/* so we have allocated space for the filename */
			free(dev_name);
		}
	}

	return 0;
}
