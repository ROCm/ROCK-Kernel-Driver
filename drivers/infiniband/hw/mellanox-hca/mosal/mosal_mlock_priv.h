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
#ifndef H_MOSAL_MLOCK_PRIV_H
#define H_MOSAL_MLOCK_PRIV_H

/********************************************************************************
 * Function: 
 *          MOSAL_mlock_init
 *
 * Arguments:
 *          void
 * Returns:
 *  MT_OK, 
 *  appropriate error code otherwise
 *
 * Description:
 *   Initializes data structures needed for MOSAL_mlock/MOSAL_munlock functions
 *
 ********************************************************************************/
call_result_t MOSAL_mlock_init(void);


/********************************************************************************
 * Function: 
 *          MOSAL_mlock_cleanup
 *
 * Arguments:
 *          void
 * Returns:
 *          void
 * Description:
 *   Cleans data structures needed for MOSAL_mlock/MOSAL_munlock functions
 *
 ********************************************************************************/
void MOSAL_mlock_cleanup(void);



#endif
