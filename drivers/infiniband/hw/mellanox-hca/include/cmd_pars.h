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


 
/*-------------------------------------------------------------------
 * Command line Arguments processing definition 
 *-------------------------------------------------------------------								 
 * $Id: cmd_pars.h,v 1.1.1.1 2004/02/24 19:46:16 roland Exp $
 * Kholodenko Alex 15.07.2001
 *-------------------------------------------------------------------
*/

#ifndef CMD_PARS_H_INCLUDED
#define CMD_PARS_H_INCLUDED 


#include <mtl_common.h>



//-----------------------------------------------------------------------------
// Constants and Defines
//-----------------------------------------------------------------------------
#define MAX_STR_OPT_SZ     128           // Maximum size of lvalue option string.    
#define MAX_STR_VAL_SZ     128           // Maximum size of rvalue option string.    
#define MAX_OPT_LST_LEN    ('z' - 'A')  // Maximum number of element in option list.
#define ASCII_TABLE_SIZE   256          // Range of ascii charaters.
#define MAX_APP_TITLE_LEN  256
#define MAX_COMB_LEN       32           // Maximum size of combination string.      


#define show_title(optlst)  do { printf("%s (V%d.%d.%d - %s)\n\n", (optlst)->title, (optlst)->version.major, \
        (optlst)->version.minor,  (optlst)->version.subminor, __DATE__); } while(0)

#define opt_print_error(optlst, err_str, A...)   show_title(optlst); printf(err_str, ## A); \
                                            printf("--help gives usage information.\n"); 



//-----------------------------------------------------------------------------
// Types Definition
//-----------------------------------------------------------------------------
typedef enum  { OPT_TYPE_NONE, OPT_TYPE_NUM, OPT_TYPE_STR, OPT_TYPE_ENUM }  optype_t;

typedef struct {
	char      m_option;	     	// One character option
	char*     mm_option;        // String option
    optype_t  optype;           // Type of this option.
	char*     help_desc;	  	// Help string

   	unsigned int  case_code;

    int  def_num_val;
    char def_str_val[MAX_STR_VAL_SZ];


} opt_desc_t; 


typedef struct {
    char major;
    char minor;
    char subminor;
} cmd_opt_ver_t;



typedef struct {
    char title[MAX_APP_TITLE_LEN];

    cmd_opt_ver_t version;


    // List of possible options. 
    opt_desc_t opt_desc_list[MAX_OPT_LST_LEN];

    // List of combination groups.
    char  comb_list[MAX_OPT_LST_LEN][MAX_COMB_LEN];

}  cmd_opt_list_t;


typedef struct {
   bool       valid;

   optype_t   optype;

   union {
           int  num;
           char str[MAX_STR_VAL_SZ];
   } value;

   int case_code;

} optel_t;



//-----------------------------------------------------------------------------
// Global Variables
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Global Function Definition
//-----------------------------------------------------------------------------
int  cmdopt_parser(int argc, char** argv,  cmd_opt_list_t * opt_list, optel_t ** opt_lst);


//-----------------------------------------------------------------------------
// Local Function Definition
//-----------------------------------------------------------------------------

#endif /* H_CMD_PARS_H */


