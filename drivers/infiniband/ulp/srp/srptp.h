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

  $Id: srptp.h 33 2004-04-09 03:58:41Z roland $
*/

#ifndef _SRPTP_H_
#define _SRPTP_H_

/*
 * Return values for SRPTP functions
 */
#define SRPTP_SUCCESS  0
#define SRPTP_FAILURE -1
/* got CM reject with reason "stale connection; Retry conn to same dlid */
#define SRPTP_RETRY_STALE_CONNECTION 2
/* got CM reject with reason "redirect req to specified port";
 * Retry conn to given port */
#define SRPTP_RETRY_REDIRECT_CONNECTION 3
/* Responder rejeced us */
#define SRPTP_HARD_REJECT 4
/* max size of SRP packets */
#define SRP_CMD_PKT_DIRECT_ADDRESS_SIZE   256

/*
 * Called by a host SRP driver to get the CA GUID.
 * OUT: 8 byte array containing the guid
 * OUT: 2-d array with ports gids
 */
extern int srptp_refresh_hca_info (void);

extern int srptp_init_module(void);

extern void srptp_cleanup_module(void);

#endif /* _SRPTP_H_ */
