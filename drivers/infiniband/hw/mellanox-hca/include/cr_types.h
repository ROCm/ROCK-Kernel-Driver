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

#ifndef H_CR_TYPES_H
#define H_CR_TYPES_H


/* Macros to use with device's header file */

#define MT_BIT_OFFSET(object_struct,reg_path) \
    ((MT_offset_t) &( ((struct object_struct *)(0))-> reg_path ))

#define MT_BIT_SIZE(object_struct,reg_path) \
    ((MT_size_t) sizeof( ((struct object_struct *)(0))-> reg_path ))
    
#define MT_BIT_OFFSET_SIZE(object_struct,reg_path) \
    MT_BIT_OFFSET(object_struct,reg_path),MT_BIT_SIZE(object_struct,reg_path)

#undef MT_BYTE_OFFSET
#define MT_BYTE_OFFSET(object_struct,reg_path) \
    ((MT_offset_t) (MT_BIT_OFFSET(object_struct,reg_path)/8))

#define MT_BYTE_SIZE(object_struct,reg_path) \
    ((MT_size_t) MT_BIT_SIZE(object_struct,reg_path)/8)

#define MT_BYTE_OFFSET_SIZE(object_struct,reg_path) \
    MT_BYTE_OFFSET(object_struct,reg_path),MT_BYTE_SIZE(object_struct,reg_path)

typedef u_int8_t pseudo_bit_t;

#endif

