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

#ifndef H_MOSAL_GEN_H
#define H_MOSAL_GEN_H

#if !defined(__DARWIN__)
extern MOSAL_IRQ_ID_t cur_max_irql;
#endif

/******************************************************************************
 *  Function (kernel-mode only): MOSAL_init
 *
 *  Description:
 *    Init of mosal
 *
 *  Parameters: 
 *		major(IN) unsigned int
 *			device major number 
 *    khz(IN) cpu frequency in khz
 *
 *  Returns:
 *    call_result_t
 *
 ******************************************************************************/
call_result_t MOSAL_init(unsigned int major);

/******************************************************************************
 *  Function (kernel-mode only): MOSAL_cleanup
 *
 *  Description:
 *    Cleanup of mosal niternal struct
 *
 *  Parameters: 
 *
 *  Returns:
 *    call_result_t
 *
 ******************************************************************************/
void MOSAL_cleanup(void);

/******************************************************************************
 *  Function: MOSAL_is_privileged
 *
 *  Description: Check if current processing context is privileged.
 *    Use this function whenever operation should be limited to privileged
 *    users, e.g., root/administrator etc. The function gets no parameters
 *    as it relies on context of current process/thread. On systems with no
 *    levels of flat hierarchy, this should always return TRUE.
 *
 *  Parameters: (none).
 *
 *  Returns:
 *    MT_bool
 *        TRUE if privileged user/context, FALSE if not.
 *
 ******************************************************************************/
MT_bool MOSAL_is_privileged(void);

/******************************************************************************
 *  Function (kernel-mode only): MOSAL_getpid
 *
 *  Description:
 *    Wrapper to getpid
 *
 *  Parameters: (none)
 *
 *  Returns:
 *    MOSAL_pid_t
 *
 ******************************************************************************/
MOSAL_pid_t MOSAL_getpid(void);

/******************************************************************************
 *  Function (kernel-mode only):
 *		MOSAL_setpid
 *
 *  Description:
 *		set current pid
 *
 *  Parameters: 
 *		pid(IN) MOSAL_pid_t
 *			points to the list head
 *
 *  Returns:
 *
 ******************************************************************************/
void MOSAL_setpid(MOSAL_pid_t pid);

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_letobe64 
 *
 *  Description:
 *    ULONG conversion from little endian to big endian 
 *
 *  Parameters: 
 *    value(IN) u_int64_t
 *
 *  Returns:
 *	  u_int64_t
 *
 ******************************************************************************/
u_int64_t MOSAL_letobe64( u_int64_t value );

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_letobe32 
 *
 *  Description:
 *    ULONG conversion from little endian to big endian 
 *
 *  Parameters: 
 *    value(IN) u_int32_t
 *
 *  Returns:
 *	  u_int32_t
 *
 ******************************************************************************/
u_int32_t MOSAL_letobe32( u_int32_t value );

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_letobe16 
 *
 *  Description:
 *    USHORT conversion from little endian to big endian 
 *
 *  Parameters: 
 *    value(IN) u_int16_t
 *
 *  Returns:
 *	  u_int16_t
 *
 ******************************************************************************/
u_int16_t MOSAL_letobe16( u_int16_t value );




/******************************************************************************
 *  Function (kernel-mode only): MOSAL_betole32
 *
 *  Description:
 *    convert from big endian representation to little endian representation
 *
 *  Parameters:
 *    be(IN) number to be transformed
 *
 *  Returns:
 *    u_int32_t
 *
 ******************************************************************************/
u_int32_t MOSAL_betole32(u_int32_t be);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * 
 * Un-protected double-linked list management
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


/*   Doubly linked list structure.  Can be used as either a list head, or as link words. */




/*   Doubly-linked list manipulation routines.  Implemented as macros */
/*   but logically these are procedures. */



/*  VOID MOSAL_dlist_init_head( PLIST_ENTRY ListHead ); */

#define MOSAL_dlist_init_head(ListHead) 		((ListHead)->Flink = (ListHead)->Blink = (ListHead))


/*   BOOLEAN MOSAL_dlist_is_empty( PLIST_ENTRY ListHead ); */

#define MOSAL_dlist_is_empty(ListHead)		    ((ListHead)->Flink == (ListHead))


/*   PLIST_ENTRY MOSAL_dlist_remove_head(PLIST_ENTRY ListHead); */

/*  !!! Usage: 	only 'el = MOSAL_dlist_remove_head(...)' */
/* 				and NOT return MOSAL_dlist_remove_head(...) or if (MOSAL_dlist_remove_head(...) == ...) !!! */

#define MOSAL_dlist_remove_head(ListHead)   	(ListHead)->Flink; {MOSAL_dlist_remove_entry((ListHead)->Flink)}


/*   PLIST_ENTRY MOSAL_dlist_remove_tail(PLIST_ENTRY ListHead); */

/*  !!! Usage: 	only 'el = MOSAL_dlist_remove_tail(...)' */
/* 				and NOT return MOSAL_dlist_remove_tail(...) or if (MOSAL_dlist_remove_tail(...) == ...) !!! */

#define MOSAL_dlist_remove_tail(ListHead)   	(ListHead)->Blink; {MOSAL_dlist_remove_entry((ListHead)->Blink)}


/*   VOID MOSAL_dlist_remove_entry( PLIST_ENTRY Entry ); */


#define MOSAL_dlist_remove_entry(Entry) {\
    PLIST_ENTRY _EX_Blink;\
    PLIST_ENTRY _EX_Flink;\
    _EX_Flink = (Entry)->Flink;\
    _EX_Blink = (Entry)->Blink;\
    _EX_Blink->Flink = _EX_Flink;\
    _EX_Flink->Blink = _EX_Blink;\
    }


/*   VOID MOSAL_dlist_insert_tail( PLIST_ENTRY ListHead, PLIST_ENTRY Entry ); */


#define MOSAL_dlist_insert_tail(ListHead,Entry) {\
    PLIST_ENTRY _EX_Blink;\
    PLIST_ENTRY _EX_ListHead;\
    _EX_ListHead = (ListHead);\
    _EX_Blink = _EX_ListHead->Blink;\
    (Entry)->Flink = _EX_ListHead;\
    (Entry)->Blink = _EX_Blink;\
    _EX_Blink->Flink = (Entry);\
    _EX_ListHead->Blink = (Entry);\
    }


/*   VOID MOSAL_dlist_insert_head( PLIST_ENTRY ListHead, PLIST_ENTRY Entry ); */


#define MOSAL_dlist_insert_head(ListHead,Entry) {\
    PLIST_ENTRY _EX_Flink;\
    PLIST_ENTRY _EX_ListHead;\
    _EX_ListHead = (ListHead);\
    _EX_Flink = _EX_ListHead->Flink;\
    (Entry)->Flink = _EX_Flink;\
    (Entry)->Blink = _EX_ListHead;\
    _EX_Flink->Blink = (Entry);\
    _EX_ListHead->Flink = (Entry);\
    }


#ifdef MT_KERNEL
typedef enum {MOSAL_IN_ISR=1, MOSAL_IN_DPC, MOSAL_IN_TASK} MOSAL_exec_ctx_t;

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_get_exec_ctx
 *
 *  Description:
 *    get execution context of the current function (e.g. proccess or interrupt)
 *
 *  Parameters: 
 *
 *  Returns:
 *    MT_context_t
 *
 ******************************************************************************/
MOSAL_exec_ctx_t MOSAL_get_exec_ctx(void);


#endif /* MT_KERNEL */

#endif
