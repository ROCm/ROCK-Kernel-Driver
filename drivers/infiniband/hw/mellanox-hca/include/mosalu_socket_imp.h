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

#ifndef _MOSALU_SOCKET_IMP_H
#define _MOSALU_SOCKET_IMP_H

#ifndef __KERNEL__

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>


typedef enum {MOSAL_PF_INET=PF_INET, MOSAL_PF_LOCAL=PF_UNIX } MOSAL_socket_domain_t;
typedef enum {MOSAL_SOCK_STREAM=SOCK_STREAM, MOSAL_SOCK_DGRAM= SOCK_DGRAM } MOSAL_socket_type_t;
typedef enum {MOSAL_IPPROTO_TCP=IPPROTO_TCP, MOSAL_IPPROTO_IP=IPPROTO_IP  } MOSAL_socket_protocol_t;
typedef enum {MOSAL_SOL_SOCKET=SOL_SOCKET, MOSAL_SOL_IPPROTO_TCP=IPPROTO_TCP } MOSAL_socket_optlevel_t;
typedef enum {MOSAL_SO_REUSEADDR=SO_REUSEADDR } MOSAL_socket_optname_t;


typedef int	MOSAL_socklen_t;
typedef struct sockaddr MOSAL_sockaddr_t;

#endif

#endif
