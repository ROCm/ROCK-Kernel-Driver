/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: hostoptions.c 35 2004-04-09 05:34:32Z roland $
*/

#include <linux/config.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kdev_t.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include <linux/byteorder/generic.h>

#include <asm/processor.h>
#include <asm/scatterlist.h>

#include <net/sock.h>

#include <scsi.h>
#include <hosts.h>
#include "ib_legacy_types.h"
#include "srp_cmd.h"
#include "srptp.h"
#include "srp_host.h"

extern srp_target_t *srp_targets;

#define kWWNLength 8
#define kWWNStringLength (kWWNLength * 2)
#define kGUIDLength 8
#define kGUIDStringLength (kGUIDLength * 2)
#define kDLIDLength 4
#define kDLIDStringLength (kDLIDLength * 2)
#define kTargetIndexLength 4
#define kTargetIndexStringLength (kDLIDLength * 2)

#define kLoadError 1
#define kNoError 0
#define TS_FAILURE -1
#define TS_SUCCESS 0


//  ====================================================================================
//
//  ====================================================================================
void
ConvertToLowerCase (char *stringPtr)
{
  char tempChar;

  tempChar = *stringPtr;

  while (tempChar != 0)
    {
      if ((tempChar >= 'A') && (tempChar <= 'Z'))
	{
	  tempChar += 'a' - 'A';
	  *stringPtr = tempChar;
	}

      stringPtr++;
      tempChar = *stringPtr;
    }
}

//  ====================================================================================
//  This routine copies all the characters from the source string into the dest string
//  up to but not including a delimiter.  It returns the number of characters which I
//  have processed in the source string (including the delimiter character if it
//  exists).  If the delimiter does not exist in the sourc string, the entire source
//  string is copied to the destination string.  If the destination string is too small
//  to hold the characters which need to be copied, then the destination string is
//  filled and the count returned is the number of characters copied to the destination
//  string.
//  ====================================================================================
void
GetString (char *sourceString, char *destString,
	   unsigned long destStringLength, char delimiter,
	   unsigned long *sourceCharsUsedPtr)
{
  unsigned long index;

  for (index = 0; index < destStringLength; index++)
    {
      destString[index] = 0;
    }

  index = 0;

  //  Copy the string fragment into the destination string.
  while ((sourceString[index] != 0) && (sourceString[index] != delimiter)
	 && (index < destStringLength))
    {
      destString[index] = sourceString[index];

      index++;
    }

  //  If the string fragment was too big for the destination string, return a count
  //  of only what fits.
  if (sourceString[index] == delimiter)
    {
      index++;
    }
  else if (index == destStringLength)
    {
      index--;
      destString[index] = 0;
    }

  *sourceCharsUsedPtr = index;
}


int
StringToHex32 (char *stringPtr, tUINT32 *hexptr)
{
  int firsttime = 1;
  long isError = kNoError;
  char *s = stringPtr;
  unsigned char Nibble = 0;

  *hexptr = 0;

  if (*s == '0' && *(s + 1) == 'x')
    s += 2;

  while (*s != '\0')
  {
    if ((*s >= '0') && (*s <= '9'))
	  Nibble = *s - '0';
    else if ((*s >= 'a') && (*s <= 'f'))
	  Nibble = *s - 'a' + 10;
    else
	  isError = kLoadError;

    if (isError == kNoError)
	{
	  if (firsttime)
	    *hexptr = 0;
	  else
	    *hexptr <<= 4;;

      *hexptr |= Nibble;
	}
    else
	  break;

    s++;
    firsttime = 0;
  }
  return isError;
}


int
StringToHex64 (char *stringPtr, uint64_t *hexptr)
{
  int firsttime = 1;
  long isError = kNoError;
  char *s = stringPtr;
  unsigned char Nibble = 0;

  *hexptr = 0;

  if (*s == '0' && *(s + 1) == 'x')
    s += 2;

  while (*s != '\0')
  {
    if ((*s >= '0') && (*s <= '9'))
	  Nibble = *s - '0';
    else if ((*s >= 'a') && (*s <= 'f'))
	  Nibble = *s - 'a' + 10;
    else if ((*s >= 'A') && (*s <= 'F'))
	  Nibble = *s - 'A' + 10;
    else
	  isError = kLoadError;

    if (isError == kNoError)
	{
	  if (firsttime)
	    *hexptr = 0;
	  else
	    *hexptr <<= 4;;

      *hexptr |= Nibble;
	}
    else
	  break;

    s++;
    firsttime = 0;
  }
  return isError;
}

#if 0
tUINT32 parse_parameters( char *parameters )
{
    char *curr_loc;
    unsigned long chars_copied = 0;
    char delimeter;
    char wwn_str[kWWNStringLength+1];
    char guid_str[kGUIDStringLength+1];
    char dlid_str[kDLIDStringLength+1];
    tUINT64 wwn;
    tUINT64 guid;
    tUINT32 dlid;
    long result;
    tUINT32 i;
    extern tUINT32 dlid_conf;

    /* first convert to lower case to make life easier */
    ConvertToLowerCase( parameters );

    curr_loc = parameters;
    i = 0;

    while(( *curr_loc != 0 ) && ( i < max_srp_targets )) {

        printk( "Target %x ", i );

        /* first get the wwn as a string */
        delimeter = '.';
        memset( wwn_str, 0, kWWNStringLength + 1 );

        GetString( curr_loc, wwn_str, kWWNStringLength, delimeter, &chars_copied );

        // printk( "wwn string %s\n", wwn_str );
        // printk( "characters copied %d\n", (tUINT32)chars_copied );
        if ( chars_copied > (kWWNStringLength + 1)) {
            return( TS_FAILURE );
        } else {
            curr_loc += chars_copied;
        }

        result = StringToHex64( wwn_str, &wwn );
        printk("WWPN %llx ", wwn );
        *( tUINT64 *)&(srp_targets[i].service_name) = cpu_to_be64( wwn );

        if ( result != kNoError )
            return( TS_FAILURE );


        delimeter = ':';
        memset( guid_str, 0, kGUIDStringLength + 1 );
        memset( dlid_str, 0, kDLIDStringLength + 1 );

		if (dlid_conf == 0) {
        	GetString( curr_loc, guid_str, kGUIDStringLength, delimeter, &chars_copied );

        	printk( "guid string %s\n", guid_str );
       		printk( "characters copied %d\n", (tUINT32)chars_copied );

       		if ( chars_copied > ( kGUIDStringLength + 1 )) {
            	return( TS_FAILURE );
        	} else {
           	 	curr_loc += chars_copied;
        	}

        	result = StringToHex64( guid_str, &guid );
        	*( tUINT64 *)&(srp_targets[i].guid) = cpu_to_be64( guid );
        	printk("GUID %llx\n", *(tUINT64 *) &srp_targets[i].guid );

        	if ( result != kNoError )
           	 return( TS_FAILURE );
		} else {
        	GetString( curr_loc, dlid_str, kDLIDStringLength, delimeter, &chars_copied );

        	// printk( "dlid string %s\n", dlid_str );
       		// printk( "characters copied %d\n", (tUINT32)chars_copied );

       		if ( chars_copied > ( kDLIDStringLength + 1 )) {
            	return( TS_FAILURE );
        	} else {
           	 	curr_loc += chars_copied;
        	}

        	result = StringToHex32( dlid_str, &dlid );
        	srp_targets[i].iou_path_record[0].dlid =  dlid ;
        	printk("DLID %x\n", srp_targets[i].iou_path_record[0].dlid );

        	if ( result != kNoError )
           	 return( TS_FAILURE );
		}



        i++;
    }

    return( TS_SUCCESS );
}
#endif


tUINT32 parse_target_binding_parameters( char *parameters )
{
    char *curr_loc;
    unsigned long chars_copied = 0;
    char delimeter;
    char wwn_str[kWWNStringLength+1];
    char target_index_str[kTargetIndexStringLength+1];
    uint64_t wwn;
    tUINT32 target_index;
    srp_target_t *target;
    int status;
    long result;
    tUINT32 i;

    /* first convert to lower case to make life easier */
    ConvertToLowerCase( parameters );

    curr_loc = parameters;
    i = 0;

    while(( *curr_loc != 0 ) && ( i < max_srp_targets )) {

        printk( "Target Binding %x ", i );

        /* first get the wwn as a string */
        delimeter = '.';
        memset( wwn_str, 0, kWWNStringLength + 1 );

        GetString( curr_loc, wwn_str, kWWNStringLength, delimeter, &chars_copied );

        if ( chars_copied > (kWWNStringLength + 1)) {
            return( TS_FAILURE );
        } else {
            curr_loc += chars_copied;
        }

        result = StringToHex64( wwn_str, &wwn );

        if ( result != kNoError )
            return( TS_FAILURE );


        delimeter = ':';
        memset( target_index_str, 0, kTargetIndexStringLength + 1 );

        GetString( curr_loc, target_index_str, kTargetIndexStringLength, delimeter, &chars_copied );

       	if ( chars_copied > ( kTargetIndexStringLength + 1 )) {
         	return( TS_FAILURE );
       	} else {
       	 	curr_loc += chars_copied;
       	}

       	result = StringToHex32( target_index_str, &target_index );
        target = &srp_targets[target_index];
        *( tUINT64 *)&(target->service_name) = cpu_to_be64( wwn );
       	printk("to Target %x\n", target_index  );
        status = srp_host_alloc_pkts( target );
        if ( status ) {
            TS_REPORT_FATAL( MOD_SRPTP, "Target %d, packet allocation failure" );
        }
        target->valid = TRUE;

       	if ( result != kNoError )
            return( TS_FAILURE );

        i++;
    }

    return( TS_SUCCESS );
}

void
print_target_bindings(void)
{
	srp_target_t	*target;
    int not_first_entry = FALSE;

	printk("srp_host: target_bindings=");
	for ( target = &srp_targets[0];
		  (target < &srp_targets[max_srp_targets]);
		  target++) {

        if ( target->valid == TRUE ) {

            // don't print colon on first guy
            if ( not_first_entry == TRUE ) {
			    printk(":");
            } else {
                not_first_entry = TRUE;
            }

            printk("%llx.%x",(unsigned long long) cpu_to_be64(target->service_name),target->target_index);
        }
	}

	printk("\n");
}
