/*
  Structure of the proc filesystem:
  /proc/dasd/
  /proc/dasd/devices                  # List of devices
  /proc/dasd/ddabcd                   # Device node for devno abcd            
  /proc/dasd/ddabcd1                  # Device node for partition abcd  
  /proc/dasd/abcd                     # Device information for devno abcd
*/

#include <linux/proc_fs.h>

#include <linux/dasd.h>

#include "dasd_types.h"

int dasd_proc_read_devices ( char *, char **, off_t, int);
#ifdef DASD_PROFILE
extern int dasd_proc_read_statistics ( char *, char **, off_t, int);
extern int dasd_proc_read_debug ( char *, char **, off_t, int);
#endif /* DASD_PROFILE */

static struct proc_dir_entry *dasd_proc_root_entry;

void
dasd_proc_init ( void )
{
	dasd_proc_root_entry = proc_mkdir("dasd", NULL);
	create_proc_info_entry("devices",0,&dasd_proc_root_entry,dasd_proc_read_devices);
#ifdef DASD_PROFILE
	create_proc_info_entry("statistics",0,&dasd_proc_root_entry,dasd_proc_read_statistics);
	create_proc_info_entry("debug",0,&dasd_proc_root_entry,dasd_proc_read_debug);
#endif /* DASD_PROFILE */
}


int 
dasd_proc_read_devices ( char * buf, char **start, off_t off, int len)
{
	int i;
	len = sprintf ( buf, "dev# MAJ minor node        Format\n");
	for ( i = 0; i < DASD_MAX_DEVICES; i++ ) {
		dasd_information_t *info = dasd_info[i];
		if ( ! info ) 
			continue;
		if ( len >= PAGE_SIZE - 80 )
			len += sprintf ( buf + len, "terminated...\n");
		len += sprintf ( buf + len,
				 "%04X %3d %5d /dev/dasd%c",
				 dasd_info[i]->info.devno,
				 DASD_MAJOR,
				 i << PARTN_BITS,
				 'a' + i );
                if (info->flags == DASD_INFO_FLAGS_NOT_FORMATTED) {
			len += sprintf ( buf + len, "    n/a");
		} else {
			len += sprintf ( buf + len, " %6d", 
					 info->sizes.bp_block);
		}
		len += sprintf ( buf + len, "\n");
	} 
	return len;
}


void 
dasd_proc_add_node (int di) 
{
}
