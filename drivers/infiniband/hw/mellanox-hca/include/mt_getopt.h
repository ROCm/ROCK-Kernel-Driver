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

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/

#ifndef _MT_GETOPT_H
#define _MT_GETOPT_H
/* this version doesn't support : 
 * 1. ordering  2. posixly correct env var 3. long_only var - meaning, that long options are prefixed
 * only by -- and char options are prefixed onlyby -. 
 */  

#ifdef __DARWIN__
#define optarg MTGETOPT_optarg
#define optind MTGETOPT_optind
#define opterr MTGETOPT_opterr
#define optopt MTGETOPT_optopt
#define option MTGETOPT_option

#endif

#if defined(__WIN__) || defined(VXWORKS_OS) || defined(__DARWIN__)



#if defined(__WIN__)

#ifdef MTGETOPT_EXPORTS
#define MTGETOPT_API __declspec(dllexport)
#else
#define MTGETOPT_API __declspec(dllimport)
#endif

#elif  defined(VXWORKS_OS)
#define MTGETOPT_API

#else
#define MTGETOPT_API

#endif

/* the string argument that came with the option */ 
extern MTGETOPT_API char *optarg;

/* Index in ARGV of the next element to be scanned.
   
   When `getopt' returns -1, this is the index of the first of the
   non-option elements that the caller should itself scan.

   Otherwise, `optind' communicates from one call to the next
   how much of ARGV has been scanned so far.  */

extern MTGETOPT_API int optind;

/* Callers store zero here to inhibit the error message `getopt' prints
   for unrecognized options.  */

extern MTGETOPT_API int opterr;

/* Set to an option character which was unrecognized.  */
extern MTGETOPT_API int optopt;

/* Describe the long-named options requested by the application.
   The LONG_OPTIONS argument to getopt_long or getopt_long_only is a vector
   of `struct option' terminated by an element containing a name which is
   zero.

   The field `has_arg' is:
   no_argument		(or 0) if the option does not take an argument,
   required_argument	(or 1) if the option requires an argument,
   optional_argument 	(or 2) if the option takes an optional argument.

   If the field `flag' is not NULL, it points to a variable that is set
   to the value given in the field `val' when the option is found, but
   left unchanged if the option is not found.

   To have a long-named option do something other than set an `int' to
   a compiled-in constant, such as set a value from `optarg', set the
   option's `flag' field to zero and its `val' field to a nonzero
   value (the equivalent single-letter option character, if there is
   one).  For long options that have a zero `flag' field, `getopt'
   returns the contents of the `val' field.  */

struct option
{
  const char *name;
  int has_arg;
  int *flag;
  int val;
};

/* Names for the values of the `has_arg' field of `struct option'.  */

# define no_argument		0
# define required_argument	1
# define optional_argument	2


/* Return the option character from OPTS just read.  Return -1 when
   there are no more options.  For unrecognized options, or options
   missing arguments, `optopt' is set to the option letter, and '?' is
   returned.

   The OPTS string is a list of characters which are recognized option
   letters, optionally followed by colons, specifying that that letter
   takes an argument, to be placed in `optarg'.

   If a letter in OPTS is followed by two colons, its argument is
   optional.  This behavior is specific to the GNU `getopt'.

   The argument `--' causes premature termination of argument
   scanning, explicitly telling `getopt' that there are no more
   options.

   If OPTS begins with `--', then non-option arguments are treated as
   arguments to the option '\0'.  This behavior is specific to the GNU
   `getopt'.  */

extern int MTGETOPT_getopt(int argc, char *const *argv, const char *shortopts);
extern int MTGETOPT_getopt_long(int argc, char *const*argv,
	const char *optstring,
	const struct option *longopts, int *longindex);

#else

#include <getopt.h>

#define MTGETOPT_getopt(arg_c,arg_v,optstring) getopt((arg_c), (arg_v), (optstring))

#define MTGETOPT_getopt_long(arg_c,arg_v,optstring,longopts,longindex) getopt_long((arg_c), (arg_v), (optstring), (longopts), (longindex))

#endif /*WIN */


#endif
