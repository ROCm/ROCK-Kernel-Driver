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
#include "../../../drivers/s390/block/dasd.h" /* uses DASD_PARTN_BITS */
#define __KERNEL__ /* we want to use kdev_t and not have to define it */
#include <linux/kdev_t.h>
#undef __KERNEL__

#define EXIT_MISUSE 1
#define EXIT_BUSY 2
#define TEMPFILENAME "/tmp/ddfXXXXXX"
#define TEMPFILENAMECHARS 8  /* 8 characters are fixed in all temp filenames */
#define IOCTL_COMMAND 'D' << 8
#define SLASHDEV "/dev/"
#define PROC_DASD_DEVICES "/proc/dasd/devices"
#define DASD_DRIVER_NAME "dasd"
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

typedef struct {
	int start_unit;
	int stop_unit;
	int blksize;
} format_data_t;

char prog_name[]="dasd_format";
char tempfilename[]=TEMPFILENAME;

void
exit_usage(int exitcode)
{
	printf("Usage: %s [-htvyV] [-b blocksize] <range> <diskspec>\n\n",
	       prog_name);
	printf("       where <range> is either\n");
	printf("           -s start_track -e end_track\n");
	printf("       or\n");
	printf("           -r start_track-end_track\n");
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

	fgets(line,sizeof(line),file); /* omit first line */
	while (fgets(line,sizeof(line),file)!=NULL) {
		rc=sscanf(line,"%X%d%d",&d,&ma_i,&mi_i);
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
	if (!(f = fopen(_PATH_MOUNTED, "r")))
		ERRMSG_EXIT(EXIT_FAILURE, "%s: %s\n", _PATH_MOUNTED,
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
	if (!(f = fopen("/proc/swaps", "r")))
		ERRMSG_EXIT(EXIT_FAILURE, "/proc/swaps: %s", strerror(errno));
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
	int verbosity,int withoutprompt)
{
	int fd,rc;
	struct stat stat_buf;
	kdev_t minor_no,major_no;
	int devno;
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
	
	if ( ((withoutprompt)&&(verbosity>=1)) ||
		(!withoutprompt) ) {
		get_xno_from_xno(&devno,&major_no,&minor_no,
			GIVEN_MAJOR|GIVEN_MINOR);
		printf("\nI am going to format the device %s in the " \
			"following way:\n",dev_name);
		printf("   Device number of device : 0x%x\n",devno);
		printf("   Major number of device  : %u\n",major_no);
		printf("   Minor number of device  : %u\n",minor_no);
		printf("   Start track             : %d\n" \
			,format_params.start_unit);
		printf("   End track               : ");
		if (format_params.stop_unit==-1)
			printf("last track of disk\n");
		else
			printf("%d\n",format_params.stop_unit);
		printf("   Blocksize               : %d\n" \
			,format_params.blksize);
		if (testmode) printf("Test mode active, omitting ioctl.\n");
	}

	while (!testmode) {
		if (!withoutprompt) {
			printf("\n--->> ATTENTION! <<---\n");
			printf("All data in the specified range of that " \
				"device will be lost.\nType yes to continue" \
				", no will leave the disk untouched: ");
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
		rc=ioctl(fd,IOCTL_COMMAND,format_params);
		if (rc)
			ERRMSG_EXIT(EXIT_FAILURE,"%s: the dasd driver " \
				"returned with the following error " \
				"message:\n%s\n",prog_name,strerror(errno));
		printf("Finished formatting the device.\n");

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

	char *dev_name;
	int devno;
	char *dev_filename,*devno_param_str,*range_param_str;
	char *start_param_str,*end_param_str,*blksize_param_str;
	
	format_data_t format_params;

	int rc;
	int oc;
	char *endptr;

	char c1,c2,cbuffer[6]; /* should be able to contain -end plus 1 char */
	int i1,i2;
	char *str;

	int start_specified,end_specified,blksize_specified;
	int devfile_specified,devno_specified,range_specified;

	/******************* initialization ********************/

	endptr=NULL;

	/* set default values */
	format_params.start_unit=0;
	format_params.stop_unit=-1;
	format_params.blksize=4096;
	testmode=0;
	verbosity=0;
	withoutprompt=0;
	start_specified=end_specified=blksize_specified=0;
	devfile_specified=devno_specified=range_specified=0;

	/*************** parse parameters **********************/

	/* avoid error message generated by getopt */
	opterr=0;

	while ( (oc=getopt(argc,argv,"r:s:e:b:n:f:hty?vV")) !=EOF) {
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

		case 's' :
			start_param_str=optarg;
			start_specified++;
			break;
			
		case 'e' :
			end_param_str=optarg;
			end_specified++;
			break;

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
		case 'r' :
			range_param_str=optarg;
			range_specified++;
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

	/*************** issue the real command *****************/
	do_format_dasd(dev_name,format_params,testmode,verbosity,
		withoutprompt);

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
