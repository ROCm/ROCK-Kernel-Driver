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

#ifndef _MOSALU_SOCKET_H
#define _MOSALU_SOCKET_H

#ifndef MT_KERNEL

/******************************************************************************
 *  Function 
 *    MOSAL_socket_socket
 *
 *  Description:
 *    create a socket 
 *
 *  Parameters: 
 *		domain  (IN)
 *      type    (IN)
 *      protocol(IN)
 *  Returns:
 *		 the socket on success, otherwise -1
  ******************************************************************************/
int MOSAL_socket_socket(MOSAL_socket_domain_t domain,MOSAL_socket_type_t type,MOSAL_socket_protocol_t protocol);

/******************************************************************************
 *  Function 
 *    MOSAL_socket_close 
 *
 *  Description:
 *    closes the socket
 *
 *  Parameters: 
 *		sock
 *  Returns:
 *  0 on success, -1 otherwise
 ******************************************************************************/
int MOSAL_socket_close(int sock);


/******************************************************************************
 *  Function 
 *    MOSAL_socket_connect 
 *
 *  Description: client
 *    connect to server 
 *
 *  Parameters: 
 *      sock
 *      adrs(IN)     server's adrs details 
 *      len(IN)     sizeof struct adrs
 *  Returns:
 *  0 on success, -1 otherwise
 ******************************************************************************/
int MOSAL_socket_connect(int sock ,const MOSAL_sockaddr_t* adrs,
                                   MOSAL_socklen_t len);


/******************************************************************************
 *  Function 
 *    MOSAL_socket_bind 
 *
 *  Description: server
 *    bind the socket to adrs  
 *
 *  Parameters: 
 *		sock  (IN) 	
 *      adrs (IN)    server's adrs details 
 *      len (IN)      size of struct adrs  
 *  Returns:
 *  0 on success, -1 otherwise
 *
 ******************************************************************************/
int MOSAL_socket_bind(int sock,const MOSAL_sockaddr_t* adrs,
                                MOSAL_socklen_t len); 


/******************************************************************************
 *  Function 
 *    MOSAL_socket_listen 
 *
 *  Description: server
 *    start listening on this socket  
 *
 *  Parameters: 
 *		sock(IN) 		
 *      n     (IN)         length of queue of requests
 *
 *  Returns:
 *  0 on success, -1 otherwise
 ******************************************************************************/
int MOSAL_socket_listen(int sock ,int n); 

/******************************************************************************
 *  Function 
 *    MOSAL_socket_accept 
 *
 *  Description: server
 *    extracts the first connection on the queue of pending connections, creates a new socket with
 *   the  properties of sock, and allocates a new file descriptor.
 *    the socket  
 *
 *  Parameters: 
 *		sock
 *      client_adrs_p(OUT)      adrs of the first connection accepted
 *      len_p(OUT)               sizeof adrs
 *      
 *  Returns:
 *  the new socket on success, -1 otherwise
 ******************************************************************************/
int MOSAL_socket_accept(int sock,MOSAL_sockaddr_t* client_adrs,MOSAL_socklen_t* len_p); 

/******************************************************************************
 *  Function 
 *    MOSAL_socket_send 
 *
 *  Description: 
 *              send len bytes from buffer through socket    
 *  Parameters: 
 *		sock(IN) 
 *      buf
 *      len - num of bytes to send
 *      flags
 *  Returns:   returns the number sent or -1
 *		
 ******************************************************************************/
int MOSAL_socket_send(int sock,const void* buf,int len,int flags);


/******************************************************************************
 *  Function 
 *    MOSAL_socket_recv 
 *
 *  Description: 
 *              recv len bytes from buffer through socket    
 *  Parameters: 
 *		sock(IN) 		        pointer to MOSAL socket object
 *      buf
 *      len - num of bytes to read
 *      flags
 *  Returns:   returns the number read or -1
 ******************************************************************************/
int MOSAL_socket_recv(int sock,void* buf,int len,int flags);


/******************************************************************************
 *  Function 
 *    MOSAL_socket_sendto 
 *
 *  Description: 
 *              send N bytes from buf on socket to peer at adrs adrs.
 *  Parameters: 
 *		sock_p(IN) 		        pointer to MOSAL socket object
 *      buf
 *      n - num of bytes to send
 *      flags
 *      adrs
 *      adrs_len
 *      
 *  Returns:  returns the number sent or -1
 ******************************************************************************/
int MOSAL_socket_sendto (int sock,void *buf, int n,int flags, MOSAL_sockaddr_t* adrs,
            MOSAL_socklen_t adrs_len);

/******************************************************************************
 *  Function 
 *    MOSAL_socket_recvfrom 
 *
 *  Description: 
 *              read N bytes into buf on socket to peer at adrs adrs.
 *              If ADDR is not NULL, fill in *ADDR_LEN bytes of it with tha address of
 *  the sender, and store the actual size of the address in *ADDR_LEN.
 *
 *  Parameters: 
 *		sock(IN) 		        pointer to MOSAL socket object
 *      buf
 *      n - num of bytes to read
 *      flags
 *      adrs
 *      adrs_len
 *      
 *  Returns:  returns the number read or -1
 ******************************************************************************/

int MOSAL_socket_recvfrom (int sock, void *buf, int n, int flags,
			 MOSAL_sockaddr_t* adrs,MOSAL_socklen_t* adrs_len_p);

/******************************************************************************
 *  Function 
 *    MOSAL_socket_setsockopt 
 *
 *  Description: 
 *              set an option on socket or protocol level
 *
 *  Parameters: 
 *		sock(IN) 		        pointer to MOSAL socket object
 *      level(IN)				option level
 *      optname(IN)				option name
 *      optval(IN)				pointer to buffer, containing the option value
 *      optlen(IN)				buffer size
 *  
 *  Returns:  0 on success, -1 otherwise
 ******************************************************************************/
int MOSAL_socket_setsockopt(int sock, MOSAL_socket_optlevel_t level, 
		MOSAL_socket_optname_t optname, const void *optval, int optlen );

/******************************************************************************
 *  Function 
 *    MOSAL_socket_get_last_error 
 *
 *  Description: 
 *              get last error on the socket
 *
 *  Parameters: 
 *  
 *  Returns:  the error number
 ******************************************************************************/
int MOSAL_socket_get_last_error(void);

#endif

#endif

