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

  $Id: sdp_sock.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_SDP_SOCK_H
#define _TS_SDP_SOCK_H
/*
 * SDP socket protocol/address family for socket() function. For all other
 * functions (e.g bind, connect, etc.) Either AF_INET or AF_INET_SDP can
 * be used with a SDP socket.
 */
#define AF_INET_SDP   26
#define PF_INET_SDP   AF_INET_SDP
/*
 * Socket option level for SDP specific parameters.
 */
#define SOL_SDP   1025
/* ------------------------------------------------------------------------ */
/* Socket options which are SDP specific.                                   */
/* ------------------------------------------------------------------------ */
/*
 * zero copy transfer thresholds. ({get,set}sockopt parameter optval is of
 *                                 type 'int')
 */
#define SDP_ZCOPY_THRSH_SRC  257 /* Threshold for AIO write advertisments */
#define SDP_ZCOPY_THRSH_SNK  258 /* Threshold for AIO read advertisments */
#define SDP_ZCOPY_THRSH      256 /* Convenience for read and write */
/*
 * Default values for SDP specific socket options. (for reference)
 */
#define SDP_ZCOPY_THRSH_SRC_DEFAULT  0x13FF
#define SDP_ZCOPY_THRSH_SNK_DEFAULT  0x13FF

#define SDP_UNBIND           259 /* Unbind socket. For libsdp use */

#endif /* _TS_SDP_SOCK_H */
