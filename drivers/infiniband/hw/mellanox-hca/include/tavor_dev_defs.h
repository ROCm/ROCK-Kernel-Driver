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

#ifndef H_TAVOR_DEV_DEFS_H_
#define H_TAVOR_DEV_DEFS_H_

#define MT23108_DEV_ID 23108
#define MT25208_DEV_ID 25208
/* cr-space ofsetts */
#define TAVOR_HCR_OFFSET_FROM_CR_BASE       \
  MT_BYTE_OFFSET(tavorprm_mt23108_configuration_registers_st,hca_command_interface_register)
/* offset of HCR from cr-space base */
#define TAVOR_ECR_H_OFFSET_FROM_CR_BASE     \
  MT_BYTE_OFFSET(tavorprm_mt23108_configuration_registers_st,ecr_h)
#define TAVOR_ECR_L_OFFSET_FROM_CR_BASE     \
  MT_BYTE_OFFSET(tavorprm_mt23108_configuration_registers_st,ecr_l)
#define TAVOR_CLR_ECR_H_OFFSET_FROM_CR_BASE \
  MT_BYTE_OFFSET(tavorprm_mt23108_configuration_registers_st,clr_ecr_h)
#define TAVOR_CLR_ECR_L_OFFSET_FROM_CR_BASE \
  MT_BYTE_OFFSET(tavorprm_mt23108_configuration_registers_st,clr_ecr_l)
#define TAVOR_CLR_INT_H_OFFSET_FROM_CR_BASE \
  MT_BYTE_OFFSET(tavorprm_mt23108_configuration_registers_st,clr_int_h)
#define TAVOR_CLR_INT_L_OFFSET_FROM_CR_BASE \
  MT_BYTE_OFFSET(tavorprm_mt23108_configuration_registers_st,clr_int_l)



#endif /* H_TAVOR_DEV_DEFS_H_ */
