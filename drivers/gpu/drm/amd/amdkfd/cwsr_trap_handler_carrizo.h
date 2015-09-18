/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#if 0
  HW (CARRIZO) source code for CWSR trap handler
 "var G8SR_WDMEM_HWREG_OFFSET = 0 \n\
 var G8SR_WDMEM_SGPR_OFFSET  = 128  // in bytes \n\
  \n\
 // Keep definition same as the app shader, These 2 time stamps are part of the app shader... Should before any Save and after restore. \n\
  \n\
 var G8SR_DEBUG_TIMESTAMP = 0 \n\
 var G8SR_DEBUG_TS_SAVE_D_OFFSET = 40*4  // ts_save_d timestamp offset relative to SGPR_SR_memory_offset \n\
 var s_g8sr_ts_save_s	 = s[34:35]   // save start \n\
 var s_g8sr_ts_sq_save_msg  = s[36:37]	 // The save shader send SAVEWAVE msg to spi \n\
 var s_g8sr_ts_spi_wrexec   = s[38:39]	 // the SPI write the sr address to SQ \n\
 var s_g8sr_ts_save_d	 = s[40:41]   // save end \n\
 var s_g8sr_ts_restore_s = s[42:43]   // restore start \n\
 var s_g8sr_ts_restore_d = s[44:45]   // restore end \n\
  \n\
 var G8SR_VGPR_SR_IN_DWX4 = 0 \n\
 var G8SR_SAVE_BUF_RSRC_WORD1_STRIDE_DWx4 = 0x00100000	  // DWx4 stride is 4*4Bytes \n\
 var G8SR_RESTORE_BUF_RSRC_WORD1_STRIDE_DWx4  = G8SR_SAVE_BUF_RSRC_WORD1_STRIDE_DWx4 \n\
  \n\
 /*************************************************************************/ \n\
 /*					 control on how to run the shader					  */ \n\
 /*************************************************************************/ \n\
 //any hack that needs to be made to run this code in EMU (either becasue various EMU code are not ready or no compute save & restore in EMU run) \n\
 var EMU_RUN_HACK					 =	 0 \n\
 var EMU_RUN_HACK_RESTORE_NORMAL	 =	 0 \n\
 var EMU_RUN_HACK_SAVE_NORMAL_EXIT	 =	 0 \n\
 var	 EMU_RUN_HACK_SAVE_SINGLE_WAVE	 =	 0 \n\
 var EMU_RUN_HACK_SAVE_FIRST_TIME	 =	 0					 //for interrupted restore in which the first save is through EMU_RUN_HACK \n\
 var EMU_RUN_HACK_SAVE_FIRST_TIME_TBA_LO =	 0					 //for interrupted restore in which the first save is through EMU_RUN_HACK \n\
 var EMU_RUN_HACK_SAVE_FIRST_TIME_TBA_HI =	 0					 //for interrupted restore in which the first save is through EMU_RUN_HACK \n\
 var SAVE_LDS						 =	 1 \n\
 var WG_BASE_ADDR_LO					 =   0x9000a000 \n\
 var WG_BASE_ADDR_HI					 =	 0x0 \n\
 var WAVE_SPACE 					 =	 0x5000 			 //memory size that each wave occupies in workgroup state mem \n\
 var CTX_SAVE_CONTROL				 =	 0x0 \n\
 var CTX_RESTORE_CONTROL			 =	 CTX_SAVE_CONTROL \n\
 var SIM_RUN_HACK					 =	 0					 //any hack that needs to be made to run this code in SIM (either becasue various RTL code are not ready or no compute save & restore in RTL run) \n\
 var	 SGPR_SAVE_USE_SQC				 =	 1					 //use SQC D$ to do the write \n\
 var USE_MTBUF_INSTEAD_OF_MUBUF 	 =	 0					 //becasue TC EMU curently asserts on 0 of // overload DFMT field to carry 4 more bits of stride for MUBUF opcodes \n\
 var SWIZZLE_EN 					 =	 0					 //whether we use swizzled buffer addressing \n\
  \n\
 /**************************************************************************/ \n\
 /*			 variables								       */ \n\
 /**************************************************************************/ \n\
 var SQ_WAVE_STATUS_INST_ATC_SHIFT  = 23 \n\
 var SQ_WAVE_STATUS_INST_ATC_MASK   = 0x00800000 \n\
  \n\
 var SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT	 = 12 \n\
 var SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE		 = 9 \n\
 var SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT	 = 8 \n\
 var SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE	 = 6 \n\
 var SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT	 = 24 \n\
 var SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE	 = 3						 //FIXME  sq.blk still has 4 bits at this time while SQ programming guide has 3 bits \n\
  \n\
 var	 SQ_WAVE_TRAPSTS_SAVECTX_MASK	 =	 0x400 \n\
 var SQ_WAVE_TRAPSTS_EXCE_MASK	     =	 0x1FF			// Exception mask \n\
 var	 SQ_WAVE_TRAPSTS_SAVECTX_SHIFT	 =	 10 \n\
 var	 SQ_WAVE_TRAPSTS_MEM_VIOL_MASK	 =	 0x100 \n\
 var	 SQ_WAVE_TRAPSTS_MEM_VIOL_SHIFT  =	 8		  \n\
 var	 SQ_WAVE_TRAPSTS_PRE_SAVECTX_MASK	 =	 0x3FF \n\
 var	 SQ_WAVE_TRAPSTS_PRE_SAVECTX_SHIFT	 =	 0x0 \n\
 var	 SQ_WAVE_TRAPSTS_PRE_SAVECTX_SIZE	 =	 10 \n\
 var	 SQ_WAVE_TRAPSTS_POST_SAVECTX_MASK	 =	 0xFFFFF800	  \n\
 var	 SQ_WAVE_TRAPSTS_POST_SAVECTX_SHIFT	 =	 11 \n\
 var	 SQ_WAVE_TRAPSTS_POST_SAVECTX_SIZE	 =	 21	  \n\
  \n\
 var SQ_WAVE_IB_STS_RCNT_SHIFT			 =	 16					 //FIXME \n\
 var SQ_WAVE_IB_STS_RCNT_SIZE			 =	 4					 //FIXME \n\
 var SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT	 =	 15					 //FIXME \n\
 var SQ_WAVE_IB_STS_FIRST_REPLAY_SIZE	 =	 1					 //FIXME \n\
 var SQ_WAVE_IB_STS_RCNT_FIRST_REPLAY_MASK_NEG	 = 0x00007FFF	 //FIXME \n\
   \n\
 var	 SQ_BUF_RSRC_WORD1_ATC_SHIFT		 =	 24 \n\
 var	 SQ_BUF_RSRC_WORD3_MTYPE_SHIFT	 =	 27 \n\
  \n\
 /*	 Save	     */ \n\
 var	 S_SAVE_BUF_RSRC_WORD1_STRIDE		 =	 0x00040000		 //stride is 4 bytes  \n\
 var	 S_SAVE_BUF_RSRC_WORD3_MISC			 =	 0x00807FAC			 //SQ_SEL_X/Y/Z/W, BUF_NUM_FORMAT_FLOAT, (0 for MUBUF stride[17:14] when ADD_TID_ENABLE and BUF_DATA_FORMAT_32 for MTBUF), ADD_TID_ENABLE \n\
  \n\
 var	 S_SAVE_SPI_INIT_ATC_MASK			 =	 0x08000000			 //bit[27]: ATC bit \n\
 var	 S_SAVE_SPI_INIT_ATC_SHIFT			 =	 27 \n\
 var	 S_SAVE_SPI_INIT_MTYPE_MASK			 =	 0x70000000			 //bit[30:28]: Mtype \n\
 var	 S_SAVE_SPI_INIT_MTYPE_SHIFT			 =	 28 \n\
 var	 S_SAVE_SPI_INIT_FIRST_WAVE_MASK	 =	 0x04000000			 //bit[26]: FirstWaveInTG \n\
 var	 S_SAVE_SPI_INIT_FIRST_WAVE_SHIFT	 =	 26 \n\
  \n\
 var S_SAVE_PC_HI_RCNT_SHIFT				 =	 28					 //FIXME  check with Brian to ensure all fields other than PC[47:0] can be used \n\
 var S_SAVE_PC_HI_RCNT_MASK				 =   0xF0000000 		 //FIXME \n\
 var S_SAVE_PC_HI_FIRST_REPLAY_SHIFT		 =	 27					 //FIXME \n\
 var S_SAVE_PC_HI_FIRST_REPLAY_MASK		 =	 0x08000000			 //FIXME \n\
  \n\
 var	 s_save_spi_init_lo				 =	 exec_lo \n\
 var s_save_spi_init_hi 			 =	 exec_hi \n\
  \n\
						 //tba_lo and tba_hi need to be saved/restored \n\
 var	 s_save_pc_lo			 =	 ttmp0			 //{TTMP1, TTMP0} = {3¡¯h0,pc_rewind[3:0], HT[0],trapID[7:0], PC[47:0]} \n\
 var	 s_save_pc_hi			 =	 ttmp1 \n\
 var s_save_exec_lo			 =	 ttmp2 \n\
 var s_save_exec_hi			 =	 ttmp3 \n\
 var	 s_save_status			 =	 ttmp4 \n\
 var	 s_save_trapsts 		 =	 ttmp5			 //not really used until the end of the SAVE routine \n\
 var s_save_xnack_mask_lo	 =	 ttmp6 \n\
 var s_save_xnack_mask_hi	 =	 ttmp7 \n\
 var	 s_save_buf_rsrc0		 =	 ttmp8 \n\
 var	 s_save_buf_rsrc1		 =	 ttmp9 \n\
 var	 s_save_buf_rsrc2		 =	 ttmp10 \n\
 var	 s_save_buf_rsrc3		 =	 ttmp11 \n\
  \n\
 var s_save_mem_offset		 =	 tma_lo 			  \n\
 var s_save_alloc_size		 =	 s_save_trapsts 		 //conflict \n\
 var s_save_tmp 	     =	 s_save_buf_rsrc2	 //shared with s_save_buf_rsrc2  (conflict: should not use mem access with s_save_tmp at the same time) \n\
 var s_save_m0				 =	 tma_hi 				  \n\
  \n\
 /*	 Restore     */ \n\
 var	 S_RESTORE_BUF_RSRC_WORD1_STRIDE		 =	 S_SAVE_BUF_RSRC_WORD1_STRIDE  \n\
 var	 S_RESTORE_BUF_RSRC_WORD3_MISC			 =	 S_SAVE_BUF_RSRC_WORD3_MISC		   \n\
  \n\
 var	 S_RESTORE_SPI_INIT_ATC_MASK			     =	 0x08000000			 //bit[27]: ATC bit \n\
 var	 S_RESTORE_SPI_INIT_ATC_SHIFT			 =	 27 \n\
 var	 S_RESTORE_SPI_INIT_MTYPE_MASK			 =	 0x70000000			 //bit[30:28]: Mtype \n\
 var	 S_RESTORE_SPI_INIT_MTYPE_SHIFT 		 =	 28 \n\
 var	 S_RESTORE_SPI_INIT_FIRST_WAVE_MASK		 =	 0x04000000			 //bit[26]: FirstWaveInTG \n\
 var	 S_RESTORE_SPI_INIT_FIRST_WAVE_SHIFT	     =	 26 \n\
  \n\
 var S_RESTORE_PC_HI_RCNT_SHIFT 			 =	 S_SAVE_PC_HI_RCNT_SHIFT \n\
 var S_RESTORE_PC_HI_RCNT_MASK				 =   S_SAVE_PC_HI_RCNT_MASK \n\
 var S_RESTORE_PC_HI_FIRST_REPLAY_SHIFT 	 =	 S_SAVE_PC_HI_FIRST_REPLAY_SHIFT \n\
 var S_RESTORE_PC_HI_FIRST_REPLAY_MASK		 =	 S_SAVE_PC_HI_FIRST_REPLAY_MASK \n\
  \n\
 var s_restore_spi_init_lo		     =	 exec_lo \n\
 var s_restore_spi_init_hi		     =	 exec_hi \n\
  \n\
 var s_restore_mem_offset		 =	 ttmp2 \n\
 var s_restore_alloc_size		 =	 ttmp3 \n\
 var s_restore_tmp		 =   ttmp6				 //tba_lo/hi need to be restored \n\
 var s_restore_mem_offset_save	 =	 s_restore_tmp		 //no conflict \n\
  \n\
 var s_restore_m0			 =	 s_restore_alloc_size	 //no conflict \n\
  \n\
 var s_restore_mode			 =	 ttmp7 \n\
  \n\
 var	 s_restore_pc_lo	     =	 ttmp0			  \n\
 var	 s_restore_pc_hi	     =	 ttmp1 \n\
 var s_restore_exec_lo		 =	 tma_lo 				 //no conflict \n\
 var s_restore_exec_hi		 =	 tma_hi 				 //no conflict \n\
 var	 s_restore_status	     =	 ttmp4			  \n\
 var	 s_restore_trapsts	     =	 ttmp5 \n\
 var s_restore_xnack_mask_lo	 =	 xnack_mask_lo \n\
 var s_restore_xnack_mask_hi	 =	 xnack_mask_hi \n\
 var	 s_restore_buf_rsrc0		 =	 ttmp8 \n\
 var	 s_restore_buf_rsrc1		 =	 ttmp9 \n\
 var	 s_restore_buf_rsrc2		 =	 ttmp10 \n\
 var	 s_restore_buf_rsrc3		 =	 ttmp11 \n\
  \n\
 /**************************************************************************/ \n\
 /*			 trap handler entry points				       */ \n\
 /**************************************************************************/ \n\
 /* Shader Main*/ \n\
  \n\
 shader main \n\
   asic(CARRIZO) \n\
   type(CS) \n\
    \n\
     if ((EMU_RUN_HACK) && (!EMU_RUN_HACK_RESTORE_NORMAL))					 //hack to use trap_id for determining save/restore \n\
	 //FIXME VCCZ un-init assertion s_getreg_b32	 s_save_status, hwreg(HW_REG_STATUS)			 //save STATUS since we will change SCC \n\
	 s_and_b32 s_save_tmp, s_save_pc_hi, 0xffff0000 			 //change SCC \n\
	 s_cmp_eq_u32 s_save_tmp, 0x007e0000						 //Save: trap_id = 0x7e. Restore: trap_id = 0x7f.   \n\
	 s_cbranch_scc0 L_JUMP_TO_RESTORE							 //do not need to recover STATUS here  since we are going to RESTORE \n\
	 //FIXME  s_setreg_b32	 hwreg(HW_REG_STATUS),	 s_save_status		 //need to recover STATUS since we are going to SAVE	  \n\
	 s_branch L_SKIP_RESTORE									 //NOT restore, SAVE actually \n\
     else	  \n\
	 s_branch L_SKIP_RESTORE									 //NOT restore. might be a regular trap or save \n\
     end \n\
 \n\
 L_JUMP_TO_RESTORE: \n\
     s_branch L_RESTORE 											 // restore \n\
      \n\
 L_SKIP_RESTORE: \n\
      \n\
     s_getreg_b32	 s_save_status, hwreg(HW_REG_STATUS)								 //save STATUS since we will change SCC \n\
     s_getreg_b32	 s_save_trapsts, hwreg(HW_REG_TRAPSTS)							  \n\
     s_and_b32		 s_save_trapsts, s_save_trapsts, SQ_WAVE_TRAPSTS_SAVECTX_MASK	 //check whether this is for save   \n\
     s_cbranch_scc1	 L_SAVE 								 //this is the operation for save \n\
     \n\
     // *********	Handle non-CWSR traps	    *******************\n\
     s_getreg_b32	 s_save_trapsts, hwreg(HW_REG_TRAPSTS)					 \n\
	 s_and_b32		 s_save_trapsts, s_save_trapsts, SQ_WAVE_TRAPSTS_EXCE_MASK // Check whether it is an exception \n\
	 s_cbranch_scc1  L_EXCP_CASE   // Exception, jump back to the shader program directly. \n\
	 s_add_u32	 ttmp0, ttmp0, 4   // S_TRAP case, add 4 to ttmp0  \n\
 L_EXCP_CASE: \n\
	 s_and_b32	 ttmp1, ttmp1, 0xFFFF \n\
	 s_rfe_b64	 [ttmp0, ttmp1] \n\
     // *********	    End handling of non-CWSR traps   *******************\n\
 \n\
 /**************************************************************************/ \n\
 /*			 save routine							       */ \n\
 /**************************************************************************/ \n\
  \n\
 L_SAVE:  \n\
  \n\
 if G8SR_DEBUG_TIMESTAMP \n\
     s_memrealtime	 s_g8sr_ts_save_s \n\
     s_waitcnt lgkmcnt(0)	  //FIXME, will cause xnack?? \n\
 end \n\
  \n\
     //check whether there is mem_viol \n\
     s_getreg_b32	 s_save_trapsts, hwreg(HW_REG_TRAPSTS)			  \n\
     s_and_b32	 s_save_trapsts, s_save_trapsts, SQ_WAVE_TRAPSTS_MEM_VIOL_MASK			  \n\
     s_cbranch_scc0	 L_NO_PC_REWIND \n\
      \n\
     //if so, need rewind PC assuming GDS operation gets NACKed \n\
     s_mov_b32	     s_save_tmp, 0															 //clear mem_viol bit \n\
     s_setreg_b32	 hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_MEM_VIOL_SHIFT, 1), s_save_tmp	 //clear mem_viol bit  \n\
     s_and_b32		 s_save_pc_hi, s_save_pc_hi, 0x0000ffff    //pc[47:32] \n\
     s_sub_u32		 s_save_pc_lo, s_save_pc_lo, 8		   //pc[31:0]-8 \n\
     s_subb_u32 	 s_save_pc_hi, s_save_pc_hi, 0x0		   // -scc \n\
      \n\
 L_NO_PC_REWIND: \n\
     s_mov_b32	     s_save_tmp, 0															 //clear saveCtx bit \n\
     s_setreg_b32	 hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_SAVECTX_SHIFT, 1), s_save_tmp		 //clear saveCtx bit	\n\
      \n\
     s_mov_b32		 s_save_xnack_mask_lo,	 xnack_mask_lo									 //save XNACK_MASK   \n\
     s_mov_b32		 s_save_xnack_mask_hi,	 xnack_mask_hi	  //save XNACK must before any memory operation \n\
     s_getreg_b32	 s_save_tmp, hwreg(HW_REG_IB_STS, SQ_WAVE_IB_STS_RCNT_SHIFT, SQ_WAVE_IB_STS_RCNT_SIZE)					 //save RCNT \n\
     s_lshl_b32 	 s_save_tmp, s_save_tmp, S_SAVE_PC_HI_RCNT_SHIFT \n\
     s_or_b32		 s_save_pc_hi, s_save_pc_hi, s_save_tmp \n\
     s_getreg_b32	 s_save_tmp, hwreg(HW_REG_IB_STS, SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT, SQ_WAVE_IB_STS_FIRST_REPLAY_SIZE)	 //save FIRST_REPLAY \n\
     s_lshl_b32 	 s_save_tmp, s_save_tmp, S_SAVE_PC_HI_FIRST_REPLAY_SHIFT \n\
     s_or_b32		 s_save_pc_hi, s_save_pc_hi, s_save_tmp \n\
     s_getreg_b32	 s_save_tmp, hwreg(HW_REG_IB_STS)										 //clear RCNT and FIRST_REPLAY in IB_STS \n\
     s_and_b32		 s_save_tmp, s_save_tmp, SQ_WAVE_IB_STS_RCNT_FIRST_REPLAY_MASK_NEG \n\
      \n\
     s_setreg_b32	 hwreg(HW_REG_IB_STS), s_save_tmp \n\
      \n\
     /* 	 inform SPI the readiness and wait for SPI's go signal */ \n\
     s_mov_b32		 s_save_exec_lo, exec_lo												 //save EXEC and use EXEC for the go signal from SPI \n\
     s_mov_b32		 s_save_exec_hi, exec_hi \n\
     s_mov_b64		 exec,	 0x0																 //clear EXEC to get ready to receive \n\
      \n\
 if G8SR_DEBUG_TIMESTAMP \n\
     s_memrealtime  s_g8sr_ts_sq_save_msg \n\
     s_waitcnt lgkmcnt(0) \n\
 end \n\
  \n\
     if (EMU_RUN_HACK) \n\
      \n\
     else \n\
	 s_sendmsg	 sendmsg(MSG_SAVEWAVE)	//send SPI a message and wait for SPI's write to EXEC	\n\
     end \n\
      \n\
   L_SLEEP:		  \n\
     s_sleep 0x2		// sleep 1 (64clk) is not enough for 8 waves per SIMD, which will cause SQ hang, since the 7,8th wave could not get arbit to exec inst, while other waves are stuck into the sleep-loop and waiting for wrexec!=0 \n\
      \n\
     if (EMU_RUN_HACK) \n\
     else \n\
	 s_cbranch_execz L_SLEEP							  \n\
     end \n\
      \n\
 if G8SR_DEBUG_TIMESTAMP \n\
     s_memrealtime  s_g8sr_ts_spi_wrexec \n\
     s_waitcnt lgkmcnt(0) \n\
 end \n\
  \n\
     /*      setup Resource Contants	*/ \n\
     if ((EMU_RUN_HACK) && (!EMU_RUN_HACK_SAVE_SINGLE_WAVE))	  \n\
	 //calculate wd_addr using absolute thread id  \n\
	 v_readlane_b32 s_save_tmp, v9, 0 \n\
	 s_lshr_b32 s_save_tmp, s_save_tmp, 6 \n\
	 s_mul_i32 s_save_tmp, s_save_tmp, WAVE_SPACE \n\
	 s_add_i32 s_save_spi_init_lo, s_save_tmp, WG_BASE_ADDR_LO \n\
	 s_mov_b32 s_save_spi_init_hi, WG_BASE_ADDR_HI \n\
	 s_and_b32 s_save_spi_init_hi, s_save_spi_init_hi, CTX_SAVE_CONTROL		  \n\
     else \n\
     end \n\
     if ((EMU_RUN_HACK) && (EMU_RUN_HACK_SAVE_SINGLE_WAVE)) \n\
	 s_add_i32 s_save_spi_init_lo, s_save_tmp, WG_BASE_ADDR_LO \n\
	 s_mov_b32 s_save_spi_init_hi, WG_BASE_ADDR_HI \n\
	 s_and_b32 s_save_spi_init_hi, s_save_spi_init_hi, CTX_SAVE_CONTROL		  \n\
     else \n\
     end \n\
      \n"
 "   s_mov_b32		 s_save_buf_rsrc0,	 s_save_spi_init_lo														 //base_addr_lo \n\
     s_and_b32		 s_save_buf_rsrc1,	 s_save_spi_init_hi, 0x0000FFFF 										 //base_addr_hi \n\
     s_or_b32		 s_save_buf_rsrc1,	 s_save_buf_rsrc1,  S_SAVE_BUF_RSRC_WORD1_STRIDE \n\
     s_mov_b32	     s_save_buf_rsrc2,	 0												 //NUM_RECORDS initial value = 0 (in bytes) although not neccessarily inited \n\
     s_mov_b32		 s_save_buf_rsrc3,	 S_SAVE_BUF_RSRC_WORD3_MISC \n\
     s_and_b32		 s_save_tmp,	     s_save_spi_init_hi, S_SAVE_SPI_INIT_ATC_MASK		  \n\
     s_lshr_b32 	 s_save_tmp,		 s_save_tmp, (S_SAVE_SPI_INIT_ATC_SHIFT-SQ_BUF_RSRC_WORD1_ATC_SHIFT)			 //get ATC bit into position \n\
     s_or_b32		 s_save_buf_rsrc3,	 s_save_buf_rsrc3,  s_save_tmp											 //or ATC \n\
     s_and_b32		 s_save_tmp,	     s_save_spi_init_hi, S_SAVE_SPI_INIT_MTYPE_MASK		  \n\
     s_lshr_b32 	 s_save_tmp,		 s_save_tmp, (S_SAVE_SPI_INIT_MTYPE_SHIFT-SQ_BUF_RSRC_WORD3_MTYPE_SHIFT)	 //get MTYPE bits into position \n\
     s_or_b32		 s_save_buf_rsrc3,	 s_save_buf_rsrc3,  s_save_tmp											 //or MTYPE	  \n\
      \n\
     // Use exec_lo/exec_hi to save tma_lo/tma_hi before using tma_lo/tma_hi \n\
     s_mov_b32	     exec_lo,		 tma_lo \n\
     s_mov_b32	     ttmp5,		 tma_hi \n\
     //FIXME  right now s_save_m0/s_save_mem_offset use tma_lo/tma_hi  (might need to save them before using them?) \n\
     s_mov_b32		 s_save_m0,			 m0																	 //save M0 \n\
      \n\
     /* 	 global mem offset			 */ \n\
     s_mov_b32		 s_save_mem_offset,	 0x0																	 //mem offset initial value = 0 \n\
      \n\
     /* 	 save HW registers	 */ \n\
     ////////////////////////////// \n\
      \n\
   L_SAVE_HWREG: \n\
	 // HWREG SR memory offset : size(VGPR)+size(SGPR) \n\
	get_vgpr_size_bytes(s_save_mem_offset) \n\
	get_sgpr_size_bytes(s_save_tmp) \n\
	s_add_u32 s_save_mem_offset, s_save_mem_offset, s_save_tmp \n\
	 \n\
     s_mov_b32		 s_save_buf_rsrc2, 0x4								 //NUM_RECORDS	 in bytes \n\
     if (SWIZZLE_EN) \n\
	 s_add_u32		 s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0					 //FIXME need to use swizzle to enable bounds checking? \n\
     else \n\
	 s_mov_b32		 s_save_buf_rsrc2,  0x1000000								 //NUM_RECORDS in bytes \n\
     end \n\
      \n\
     write_hwreg_to_mem(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset) 				 //M0 \n\
      \n\
     if ((EMU_RUN_HACK) && (EMU_RUN_HACK_SAVE_FIRST_TIME))	 \n\
	 s_add_u32 s_save_pc_lo, s_save_pc_lo, 4	     //pc[31:0]+4 \n\
	 s_addc_u32 s_save_pc_hi, s_save_pc_hi, 0x0			 //carry bit over \n\
	 s_mov_b32	 tba_lo, EMU_RUN_HACK_SAVE_FIRST_TIME_TBA_LO \n\
	 s_mov_b32	 tba_hi, EMU_RUN_HACK_SAVE_FIRST_TIME_TBA_HI	  \n\
     end \n\
      \n"
 "	 write_hwreg_to_mem(s_save_pc_lo, s_save_buf_rsrc0, s_save_mem_offset)					 //PC \n\
     write_hwreg_to_mem(s_save_pc_hi, s_save_buf_rsrc0, s_save_mem_offset) \n\
     write_hwreg_to_mem(s_save_exec_lo, s_save_buf_rsrc0, s_save_mem_offset)				 //EXEC \n\
     write_hwreg_to_mem(s_save_exec_hi, s_save_buf_rsrc0, s_save_mem_offset) \n\
     // Save the tma_lo and tma_hi content from exec_lo and ttmp5	     \n\
     s_mov_b32		s_save_exec_lo, exec_lo \n\
     s_mov_b32		s_save_exec_hi, ttmp5	\n\
     write_hwreg_to_mem(s_save_status, s_save_buf_rsrc0, s_save_mem_offset)				 //STATUS  \n\
      \n\
     //s_save_trapsts conflicts with s_save_alloc_size \n\
     s_getreg_b32    s_save_trapsts, hwreg(HW_REG_TRAPSTS) \n\
     write_hwreg_to_mem(s_save_trapsts, s_save_buf_rsrc0, s_save_mem_offset)				 //TRAPSTS \n\
      \n\
     write_hwreg_to_mem(s_save_xnack_mask_lo, s_save_buf_rsrc0, s_save_mem_offset)			 //XNACK_MASK_LO \n\
     write_hwreg_to_mem(s_save_xnack_mask_hi, s_save_buf_rsrc0, s_save_mem_offset)			 //XNACK_MASK_HI \n\
      \n\
     //use s_save_tmp would introduce conflict here between s_save_tmp and s_save_buf_rsrc2 \n\
     s_getreg_b32	 s_save_m0, hwreg(HW_REG_MODE)							 //MODE \n\
     write_hwreg_to_mem(s_save_m0, s_save_buf_rsrc0, s_save_mem_offset) \n\
     write_hwreg_to_mem(tba_lo, s_save_buf_rsrc0, s_save_mem_offset)						 //TBA_LO \n\
     write_hwreg_to_mem(tba_hi, s_save_buf_rsrc0, s_save_mem_offset)						 //TBA_HI \n\
     write_hwreg_to_mem(s_save_exec_lo, s_save_buf_rsrc0, s_save_mem_offset)				 //TMA_LO \n\
     write_hwreg_to_mem(s_save_exec_hi, s_save_buf_rsrc0, s_save_mem_offset)				 //TMA_HI \n\
  \n\
     /*      the first wave in the threadgroup	  */ \n\
	 // save fist_wave bits in tba_hi unused bit.26 \n\
     s_and_b32		 s_save_tmp, s_save_spi_init_hi, S_SAVE_SPI_INIT_FIRST_WAVE_MASK     // extract fisrt wave bit \n\
     //s_or_b32        tba_hi, s_save_tmp, tba_hi					 // save first wave bit to tba_hi.bits[26] \n\
     s_mov_b32	      s_save_exec_hi, 0x0				  \n\
     s_or_b32	      s_save_exec_hi, s_save_tmp, s_save_exec_hi			  // save first wave bit to s_save_exec_hi.bits[26] \n\
      \n\
     /* 	 save SGPRs	     */ \n\
	 // Save SGPR before LDS save, then the s0 to s4 can be used during LDS save... \n\
     ////////////////////////////// \n\
      \n\
     // SGPR SR memory offset : size(VGPR)	  \n\
     get_vgpr_size_bytes(s_save_mem_offset) \n\
     // TODO, change RSRC word to rearrange memory layout for SGPRS \n\
      \n\
     s_getreg_b32	 s_save_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE)				 //spgr_size \n\
     s_add_u32		 s_save_alloc_size, s_save_alloc_size, 1 \n\
     s_lshl_b32 	 s_save_alloc_size, s_save_alloc_size, 4						 //Number of SGPRs = (sgpr_size + 1) * 16   (non-zero value)  \n\
      \n\
     if (SGPR_SAVE_USE_SQC) \n\
	 s_lshl_b32		 s_save_buf_rsrc2,	 s_save_alloc_size, 2					 //NUM_RECORDS in bytes  \n\
     else \n\
	 s_lshl_b32		 s_save_buf_rsrc2,	 s_save_alloc_size, 8					 //NUM_RECORDS in bytes (64 threads) \n\
     end \n\
      \n\
     if (SWIZZLE_EN) \n\
	 s_add_u32		 s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0					 //FIXME need to use swizzle to enable bounds checking? \n\
     else \n\
	 s_mov_b32		 s_save_buf_rsrc2,  0x1000000								 //NUM_RECORDS in bytes \n\
     end \n\
      \n\
     // backup s_save_buf_rsrc0,1 to s_save_pc_lo/hi, since write_16sgpr_to_mem function will change the rsrc0 \n\
     //s_mov_b64 s_save_pc_lo, s_save_buf_rsrc0 \n\
     s_mov_b64 s_save_xnack_mask_lo, s_save_buf_rsrc0 \n\
     s_add_u32 s_save_buf_rsrc0, s_save_buf_rsrc0, s_save_mem_offset \n\
      \n\
     s_mov_b32		 m0, 0x0						 //SGPR initial index value =0		  \n\
   L_SAVE_SGPR_LOOP:					  \n\
     // SGPR is allocated in 16 SGPR granularity				  \n\
     s_movrels_b64	 s0, s0     //s0 = s[0+m0], s1 = s[1+m0] \n\
     s_movrels_b64   s2, s2	//s2 = s[2+m0], s3 = s[3+m0] \n\
     s_movrels_b64	 s4, s4     //s4 = s[4+m0], s5 = s[5+m0] \n\
     s_movrels_b64   s6, s6	//s6 = s[6+m0], s7 = s[7+m0] \n\
     s_movrels_b64	 s8, s8     //s8 = s[8+m0], s9 = s[9+m0] \n\
     s_movrels_b64   s10, s10	//s10 = s[10+m0], s11 = s[11+m0] \n\
     s_movrels_b64	 s12, s12   //s12 = s[12+m0], s13 = s[13+m0] \n\
     s_movrels_b64   s14, s14	//s14 = s[14+m0], s15 = s[15+m0] \n\
      \n"
 "	 write_16sgpr_to_mem(s0, s_save_buf_rsrc0, s_save_mem_offset) //PV: the best performance should be using s_buffer_store_dwordx4 \n\
     s_add_u32		 m0, m0, 16														 //next sgpr index \n\
     s_cmp_lt_u32	 m0, s_save_alloc_size											 //scc = (m0 < s_save_alloc_size) ? 1 : 0 \n\
     s_cbranch_scc1	 L_SAVE_SGPR_LOOP									 //SGPR save is complete? \n\
     // restore s_save_buf_rsrc0,1 \n\
     //s_mov_b64 s_save_buf_rsrc0, s_save_pc_lo \n\
     s_mov_b64 s_save_buf_rsrc0, s_save_xnack_mask_lo \n\
      \n\
     /* 	 save first 4 VGPR, then LDS save could use   */ \n\
	 // each wave will alloc 4 vgprs at least... \n\
     ///////////////////////////////////////////////////////////////////////////////////// \n\
      \n\
     s_mov_b32	     s_save_mem_offset, 0 \n\
     s_mov_b32		 exec_lo, 0xFFFFFFFF											 //need every thread from now on \n\
     s_mov_b32		 exec_hi, 0xFFFFFFFF \n\
      \n\
     if (SWIZZLE_EN) \n\
	 s_add_u32		 s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0					 //FIXME need to use swizzle to enable bounds checking? \n\
     else \n\
	 s_mov_b32		 s_save_buf_rsrc2,  0x1000000								 //NUM_RECORDS in bytes \n\
     end \n\
      \n\
     // VGPR Allocated in 4-GPR granularity \n\
      \n\
 if G8SR_VGPR_SR_IN_DWX4 \n\
	 // the const stride for DWx4 is 4*4 bytes \n\
	 s_and_b32 s_save_buf_rsrc1, s_save_buf_rsrc1, 0x0000FFFF   // reset const stride to 0 \n\
	 s_or_b32  s_save_buf_rsrc1, s_save_buf_rsrc1, G8SR_SAVE_BUF_RSRC_WORD1_STRIDE_DWx4  // const stride to 4*4 bytes \n\
	  \n\
	 buffer_store_dwordx4 v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 \n\
	  \n\
	 s_and_b32 s_save_buf_rsrc1, s_save_buf_rsrc1, 0x0000FFFF   // reset const stride to 0 \n\
	 s_or_b32  s_save_buf_rsrc1, s_save_buf_rsrc1, S_SAVE_BUF_RSRC_WORD1_STRIDE  // reset const stride to 4 bytes \n\
 else \n\
	 buffer_store_dword v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 \n\
	 buffer_store_dword v1, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1  offset:256 \n\
	 buffer_store_dword v2, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1  offset:256*2 \n\
	 buffer_store_dword v3, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1  offset:256*3 \n\
 end \n\
  \n\
     /* 	 save LDS	     */ \n\
     ////////////////////////////// \n\
      \n\
   L_SAVE_LDS: \n\
    \n\
	 // Change EXEC to all threads...	  \n\
     s_mov_b32		 exec_lo, 0xFFFFFFFF   //need every thread from now on	  \n\
     s_mov_b32		 exec_hi, 0xFFFFFFFF \n\
      \n\
     s_getreg_b32	 s_save_alloc_size, hwreg(HW_REG_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE)			 //lds_size \n\
     s_and_b32		 s_save_alloc_size, s_save_alloc_size, 0xFFFFFFFF				 //lds_size is zero? \n\
     s_cbranch_scc0	 L_SAVE_LDS_DONE									    //no lds used? jump to L_SAVE_DONE \n\
      \n\
     s_barrier		     //LDS is used? wait for other waves in the same TG  \n\
     //s_and_b32	 s_save_tmp, tba_hi, S_SAVE_SPI_INIT_FIRST_WAVE_MASK		    //exec is still used here	   \n\
     s_and_b32		 s_save_tmp, s_save_exec_hi, S_SAVE_SPI_INIT_FIRST_WAVE_MASK		    //exec is still used here \n\
     s_cbranch_scc0	 L_SAVE_LDS_DONE \n\
      \n\
	 // first wave do LDS save; \n\
	  \n\
     s_lshl_b32 	 s_save_alloc_size, s_save_alloc_size, 6						 //LDS size in dwords = lds_size * 64dw \n\
     s_lshl_b32 	 s_save_alloc_size, s_save_alloc_size, 2						 //LDS size in bytes \n\
     s_mov_b32		 s_save_buf_rsrc2,  s_save_alloc_size							 //NUM_RECORDS in bytes \n\
      \n\
     // LDS at offset: size(VGPR)+SIZE(SGPR)+SIZE(HWREG) \n\
     //  \n\
     get_vgpr_size_bytes(s_save_mem_offset) \n\
     get_sgpr_size_bytes(s_save_tmp) \n\
     s_add_u32	s_save_mem_offset, s_save_mem_offset, s_save_tmp \n\
     s_add_u32 s_save_mem_offset, s_save_mem_offset, get_hwreg_size_bytes() \n\
      \n\
     if (SWIZZLE_EN) \n\
	 s_add_u32		 s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0       //FIXME need to use swizzle to enable bounds checking? \n\
     else \n\
	 s_mov_b32		 s_save_buf_rsrc2,  0x1000000		       //NUM_RECORDS in bytes \n\
     end \n\
      \n\
     s_mov_b32		 m0, 0x0					       //lds_offset initial value = 0 \n\
      \n\
       \n\
 var LDS_DMA_ENABLE = 0 \n\
 var UNROLL = 0 \n\
 if UNROLL==0 && LDS_DMA_ENABLE==1 \n\
	 s_mov_b32  s3, 256*2 \n\
	 s_nop 0 \n\
	 s_nop 0 \n\
	 s_nop 0 \n\
   L_SAVE_LDS_LOOP: \n\
	 //TODO: looks the 2 buffer_store/load clause for s/r will hurt performance.??? \n\
     if (SAVE_LDS)     //SPI always alloc LDS space in 128DW granularity \n\
	    buffer_store_lds_dword s_save_buf_rsrc0, s_save_mem_offset lds:1		// first 64DW \n\
	    buffer_store_lds_dword s_save_buf_rsrc0, s_save_mem_offset lds:1 offset:256 // second 64DW \n\
     end \n\
      \n\
     s_add_u32		 m0, m0, s3											 //every buffer_store_lds does 256 bytes \n\
     s_add_u32		 s_save_mem_offset, s_save_mem_offset, s3							 //mem offset increased by 256 bytes \n\
     s_cmp_lt_u32	 m0, s_save_alloc_size												 //scc=(m0 < s_save_alloc_size) ? 1 : 0 \n\
     s_cbranch_scc1  L_SAVE_LDS_LOOP														 //LDS save is complete? \n\
      \n\
 elsif LDS_DMA_ENABLE==1 && UNROLL==1 // UNROOL  , has ichace miss \n\
       // store from higest LDS address to lowest \n\
       s_mov_b32  s3, 256*2 \n\
       s_sub_u32  m0, s_save_alloc_size, s3 \n\
       s_add_u32 s_save_mem_offset, s_save_mem_offset, m0 \n\
       s_lshr_b32 s_save_alloc_size, s_save_alloc_size, 9   // how many 128 trunks... \n\
       s_sub_u32 s_save_alloc_size, 128, s_save_alloc_size   // store from higheset addr to lowest \n\
       s_mul_i32 s_save_alloc_size, s_save_alloc_size, 6*4   // PC offset increment,  each LDS save block cost 6*4 Bytes instruction \n\
       s_add_u32 s_save_alloc_size, s_save_alloc_size, 3*4   //2is the below 2 inst...//s_addc and s_setpc \n\
       s_nop 0 \n\
       s_nop 0 \n\
       s_nop 0	 //pad 3 dw to let LDS_DMA align with 64Bytes \n\
       s_getpc_b64 s[0:1]			       // reuse s[0:1], since s[0:1] already saved \n\
       s_add_u32   s0, s0,s_save_alloc_size \n\
       s_addc_u32  s1, s1, 0 \n\
       s_setpc_b64 s[0:1] \n\
  \n\
	for var i =0; i< 128; i++     \n\
	     // be careful to make here a 64Byte aligned address, which could improve performance... \n\
	      buffer_store_lds_dword s_save_buf_rsrc0, s_save_mem_offset lds:1 offset:0 	  // first 64DW \n\
	      buffer_store_lds_dword s_save_buf_rsrc0, s_save_mem_offset lds:1 offset:256	    // second 64DW \n\
	      \n\
	  if i!=127 \n\
	     s_sub_u32	m0, m0, s3	// use a sgpr to shrink 2DW-inst to 1DW inst to improve performance , i.e.  pack more LDS_DMA inst to one Cacheline \n\
	     s_sub_u32	s_save_mem_offset, s_save_mem_offset,  s3 \n\
	  end \n\
	end \n\
	 \n\
 else	// BUFFER_STORE \n\
       v_mbcnt_lo_u32_b32 v2, 0xffffffff, 0x0 \n\
       v_mbcnt_hi_u32_b32 v3, 0xffffffff, v2	 // tid \n\
       v_mul_i32_i24 v2, v3, 8	 // tid*8 \n\
       v_mov_b32 v3, 256*2 \n\
       s_mov_b32 m0, 0x10000 \n\
       s_mov_b32 s0, s_save_buf_rsrc3 \n\
       s_and_b32 s_save_buf_rsrc3, s_save_buf_rsrc3, 0xFF7FFFFF    // disable add_tid  \n\
       s_or_b32 s_save_buf_rsrc3, s_save_buf_rsrc3, 0x58000   //DFMT \n\
	\n\
 L_SAVE_LDS_LOOP_VECTOR: \n\
       ds_read_b64 v[0:1], v2	 //x =LDS[a], byte address \n\
       s_waitcnt lgkmcnt(0) \n\
       buffer_store_dwordx2  v[0:1], v2, s_save_buf_rsrc0, s_save_mem_offset offen:1  glc:1  slc:1 \n\
 //	 s_waitcnt vmcnt(0) \n\
	v_add_u32 v2, vcc[0:1], v2, v3 \n\
	v_cmp_lt_u32 vcc[0:1], v2, s_save_alloc_size \n\
	s_cbranch_vccnz L_SAVE_LDS_LOOP_VECTOR \n\
	\n\
       // restore rsrc3 \n\
       s_mov_b32 s_save_buf_rsrc3, s0 \n\
	\n\
 end \n\
  \n\
 L_SAVE_LDS_DONE:	  \n\
  \n\
     /* 	 save VGPRs  - set the Rest VGPRs	     */ \n\
     ////////////////////////////////////////////////////////////////////////////////////// \n\
   L_SAVE_VGPR: \n\
     // VGPR SR memory offset: 0 \n\
     // TODO rearrange the RSRC words to use swizzle for VGPR save... \n\
    \n\
     s_mov_b32	     s_save_mem_offset, (0+256*4)				     // for the rest VGPRs \n\
     s_mov_b32		 exec_lo, 0xFFFFFFFF											 //need every thread from now on \n\
     s_mov_b32		 exec_hi, 0xFFFFFFFF \n\
      \n\
     s_getreg_b32	 s_save_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE)					 //vpgr_size \n\
     s_add_u32		 s_save_alloc_size, s_save_alloc_size, 1 \n\
     s_lshl_b32 	 s_save_alloc_size, s_save_alloc_size, 2						 //Number of VGPRs = (vgpr_size + 1) * 4    (non-zero value)   //FIXME for GFX, zero is possible  \n\
     s_lshl_b32 	 s_save_buf_rsrc2,  s_save_alloc_size, 8						 //NUM_RECORDS in bytes (64 threads*4) \n\
     if (SWIZZLE_EN) \n\
	 s_add_u32		 s_save_buf_rsrc2, s_save_buf_rsrc2, 0x0					 //FIXME need to use swizzle to enable bounds checking? \n\
     else \n\
	 s_mov_b32		 s_save_buf_rsrc2,  0x1000000								 //NUM_RECORDS in bytes \n\
     end \n\
      \n\
     // VGPR Allocated in 4-GPR granularity \n\
      \n\
 if G8SR_VGPR_SR_IN_DWX4 \n\
	 // the const stride for DWx4 is 4*4 bytes \n\
	 s_and_b32 s_save_buf_rsrc1, s_save_buf_rsrc1, 0x0000FFFF   // reset const stride to 0 \n\
	 s_or_b32  s_save_buf_rsrc1, s_save_buf_rsrc1, G8SR_SAVE_BUF_RSRC_WORD1_STRIDE_DWx4  // const stride to 4*4 bytes \n\
	  \n\
	 s_mov_b32	   m0, 4     // skip first 4 VGPRs \n\
	 s_cmp_lt_u32	   m0, s_save_alloc_size \n\
	 s_cbranch_scc0    L_SAVE_VGPR_LOOP_END      // no more vgprs \n\
	  \n\
	 s_set_gpr_idx_on  m0, 0x1   // This will change M0 \n\
	 s_add_u32	   s_save_alloc_size, s_save_alloc_size, 0x1000  // because above inst change m0 \n\
 L_SAVE_VGPR_LOOP: \n\
	 v_mov_b32	   v0, v0   // v0 = v[0+m0] \n\
	 v_mov_b32	   v1, v1 \n\
	 v_mov_b32	   v2, v2 \n\
	 v_mov_b32	   v3, v3 \n\
	  \n\
	   \n\
	 buffer_store_dwordx4 v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 \n\
	 s_add_u32	   m0, m0, 4 \n\
	 s_add_u32	   s_save_mem_offset, s_save_mem_offset, 256*4 \n\
	 s_cmp_lt_u32	   m0, s_save_alloc_size \n\
     s_cbranch_scc1	 L_SAVE_VGPR_LOOP												 //VGPR save is complete? \n\
     s_set_gpr_idx_off \n\
 L_SAVE_VGPR_LOOP_END: \n\
  \n\
	 s_and_b32 s_save_buf_rsrc1, s_save_buf_rsrc1, 0x0000FFFF   // reset const stride to 0 \n\
	 s_or_b32  s_save_buf_rsrc1, s_save_buf_rsrc1, S_SAVE_BUF_RSRC_WORD1_STRIDE  // reset const stride to 4 bytes \n\
 else \n\
     // VGPR store using dw burst	  \n\
     s_mov_b32		   m0, 0x4   //VGPR initial index value =0 \n\
     s_cmp_lt_u32      m0, s_save_alloc_size \n\
     s_cbranch_scc0    L_SAVE_VGPR_END \n\
      \n\
     s_set_gpr_idx_on	 m0, 0x1 //M0[7:0] = M0[7:0] and M0[15:12] = 0x1 \n\
     s_add_u32		 s_save_alloc_size, s_save_alloc_size, 0x1000					 //add 0x1000 since we compare m0 against it later	  \n\
      \n\
   L_SAVE_VGPR_LOOP:										  \n\
     v_mov_b32		 v0, v0 			 //v0 = v[0+m0]   \n\
     v_mov_b32		 v1, v1 			 //v0 = v[0+m0]   \n\
     v_mov_b32		 v2, v2 			 //v0 = v[0+m0]   \n\
     v_mov_b32		 v3, v3 			 //v0 = v[0+m0]   \n\
	  \n\
     if(USE_MTBUF_INSTEAD_OF_MUBUF)	   \n\
	 tbuffer_store_format_x v0, v0, s_save_buf_rsrc0, s_save_mem_offset format:BUF_NUM_FORMAT_FLOAT format: BUF_DATA_FORMAT_32 slc:1 glc:1 \n\
     else \n\
	 buffer_store_dword v0, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1 \n\
	 buffer_store_dword v1, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1  offset:256 \n\
	 buffer_store_dword v2, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1  offset:256*2 \n\
	 buffer_store_dword v3, v0, s_save_buf_rsrc0, s_save_mem_offset slc:1 glc:1  offset:256*3 \n\
     end \n\
      \n\
     s_add_u32		 m0, m0, 4														 //next vgpr index \n\
     s_add_u32		 s_save_mem_offset, s_save_mem_offset, 256*4						 //every buffer_store_dword does 256 bytes \n\
     s_cmp_lt_u32	 m0,	 s_save_alloc_size											 //scc = (m0 < s_save_alloc_size) ? 1 : 0 \n\
     s_cbranch_scc1	 L_SAVE_VGPR_LOOP												 //VGPR save is complete? \n\
     s_set_gpr_idx_off \n\
 end \n\
      \n\
 L_SAVE_VGPR_END: \n\
  \n\
     /*     S_PGM_END_SAVED  */ 							 //FIXME  graphics ONLY \n\
     if ((EMU_RUN_HACK) && (!EMU_RUN_HACK_SAVE_NORMAL_EXIT))	  \n\
	 s_and_b32 s_save_pc_hi, s_save_pc_hi, 0x0000ffff    //pc[47:32] \n\
	 s_add_u32 s_save_pc_lo, s_save_pc_lo, 4	     //pc[31:0]+4 \n\
	 s_addc_u32 s_save_pc_hi, s_save_pc_hi, 0x0			 //carry bit over \n\
	 s_rfe_b64 s_save_pc_lo 			     //Return to the main shader program \n\
     else \n\
     end \n\
      \n"
 "// Save Done timestamp  \n\
 if G8SR_DEBUG_TIMESTAMP \n\
	  s_memrealtime  s_g8sr_ts_save_d \n\
	 // SGPR SR memory offset : size(VGPR)	  \n\
	 get_vgpr_size_bytes(s_save_mem_offset) \n\
	 s_add_u32 s_save_mem_offset, s_save_mem_offset, G8SR_DEBUG_TS_SAVE_D_OFFSET \n\
	 s_waitcnt lgkmcnt(0)	      //FIXME, will cause xnack?? \n\
	 // Need reset rsrc2?? \n\
	 s_mov_b32 m0, s_save_mem_offset \n\
	 s_mov_b32 s_save_buf_rsrc2,  0x1000000 				 //NUM_RECORDS in bytes \n\
	 s_buffer_store_dwordx2 s_g8sr_ts_save_d, s_save_buf_rsrc0, m0		 glc:1	  \n\
 end \n\
  \n\
      \n\
     s_branch	 L_END_PGM \n\
      \n\
  \n\
 /**************************************************************************/ \n\
 /*			 restore routine						       */ \n\
 /**************************************************************************/ \n\
  \n\
 L_RESTORE: \n\
     /*      Setup Resource Contants	*/ \n\
     if ((EMU_RUN_HACK) && (!EMU_RUN_HACK_RESTORE_NORMAL)) \n\
	 //calculate wd_addr using absolute thread id \n\
	 v_readlane_b32 s_restore_tmp, v9, 0 \n\
	 s_lshr_b32 s_restore_tmp, s_restore_tmp, 6 \n\
	 s_mul_i32 s_restore_tmp, s_restore_tmp, WAVE_SPACE \n\
	 s_add_i32 s_restore_spi_init_lo, s_restore_tmp, WG_BASE_ADDR_LO \n\
	 s_mov_b32 s_restore_spi_init_hi, WG_BASE_ADDR_HI \n\
	 s_and_b32 s_restore_spi_init_hi, s_restore_spi_init_hi, CTX_RESTORE_CONTROL	  \n\
     else \n\
     end \n\
      \n\
 if G8SR_DEBUG_TIMESTAMP \n\
	  s_memrealtime  s_g8sr_ts_restore_s \n\
	 s_waitcnt lgkmcnt(0)	      //FIXME, will cause xnack?? \n\
	 // tma_lo/hi are sgpr 110, 111, which will not used for 112 SGPR allocated case... \n\
	 s_mov_b32 s_restore_pc_lo, s_g8sr_ts_restore_s[0] \n\
	 s_mov_b32 s_restore_pc_hi, s_g8sr_ts_restore_s[1]   //backup ts to ttmp0/1, sicne exec will be finally restored.. \n\
 end \n\
  \n\
     s_mov_b32		 s_restore_buf_rsrc0,	 s_restore_spi_init_lo															 //base_addr_lo \n\
     s_and_b32		 s_restore_buf_rsrc1,	 s_restore_spi_init_hi, 0x0000FFFF												 //base_addr_hi \n\
     s_or_b32		 s_restore_buf_rsrc1,	 s_restore_buf_rsrc1,  S_RESTORE_BUF_RSRC_WORD1_STRIDE \n\
     s_mov_b32	     s_restore_buf_rsrc2,	 0														 //NUM_RECORDS initial value = 0 (in bytes) \n\
     s_mov_b32		 s_restore_buf_rsrc3,	 S_RESTORE_BUF_RSRC_WORD3_MISC \n\
     s_and_b32		 s_restore_tmp, 	 s_restore_spi_init_hi, S_RESTORE_SPI_INIT_ATC_MASK		  \n\
     s_lshr_b32 	 s_restore_tmp, 		 s_restore_tmp, (S_RESTORE_SPI_INIT_ATC_SHIFT-SQ_BUF_RSRC_WORD1_ATC_SHIFT)		 //get ATC bit into position \n\
     s_or_b32		 s_restore_buf_rsrc3,	 s_restore_buf_rsrc3,  s_restore_tmp												 //or ATC \n\
     s_and_b32		 s_restore_tmp, 	 s_restore_spi_init_hi, S_RESTORE_SPI_INIT_MTYPE_MASK		  \n\
     s_lshr_b32 	 s_restore_tmp, 		 s_restore_tmp, (S_RESTORE_SPI_INIT_MTYPE_SHIFT-SQ_BUF_RSRC_WORD3_MTYPE_SHIFT)	 //get MTYPE bits into position \n\
     s_or_b32		 s_restore_buf_rsrc3,	 s_restore_buf_rsrc3,  s_restore_tmp												 //or MTYPE \n\
      \n\
     /* 	 global mem offset			 */ \n\
 //	 s_mov_b32		 s_restore_mem_offset, 0x0								 //mem offset initial value = 0 \n\
      \n\
     /*      the first wave in the threadgroup	  */ \n\
     s_and_b32		 s_restore_tmp, s_restore_spi_init_hi, S_RESTORE_SPI_INIT_FIRST_WAVE_MASK			  \n\
     s_cbranch_scc0	 L_RESTORE_VGPR \n\
      \n\
     /* 	 restore LDS	     */ \n\
     ////////////////////////////// \n\
   L_RESTORE_LDS: \n\
    \n\
     s_mov_b32		 exec_lo, 0xFFFFFFFF													 //need every thread from now on   //be consistent with SAVE although can be moved ahead \n\
     s_mov_b32		 exec_hi, 0xFFFFFFFF \n\
      \n"
 "   s_getreg_b32	 s_restore_alloc_size, hwreg(HW_REG_LDS_ALLOC,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT,SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE) 			 //lds_size \n\
     s_and_b32		 s_restore_alloc_size, s_restore_alloc_size, 0xFFFFFFFF 				 //lds_size is zero? \n\
     s_cbranch_scc0	 L_RESTORE_VGPR 														 //no lds used? jump to L_RESTORE_VGPR \n\
     s_lshl_b32 	 s_restore_alloc_size, s_restore_alloc_size, 6							 //LDS size in dwords = lds_size * 64dw \n\
     s_lshl_b32 	 s_restore_alloc_size, s_restore_alloc_size, 2							 //LDS size in bytes \n\
     s_mov_b32		 s_restore_buf_rsrc2,	 s_restore_alloc_size							 //NUM_RECORDS in bytes \n\
      \n\
     // LDS at offset: size(VGPR)+SIZE(SGPR)+SIZE(HWREG) \n\
     //  \n\
     get_vgpr_size_bytes(s_restore_mem_offset) \n\
     get_sgpr_size_bytes(s_restore_tmp) \n\
     s_add_u32	s_restore_mem_offset, s_restore_mem_offset, s_restore_tmp \n\
     s_add_u32	s_restore_mem_offset, s_restore_mem_offset, get_hwreg_size_bytes()	      //FIXME, Check if offset overflow??? \n\
      \n"
 "	 if (SWIZZLE_EN) \n\
	 s_add_u32		 s_restore_buf_rsrc2, s_restore_buf_rsrc2, 0x0						 //FIXME need to use swizzle to enable bounds checking? \n\
     else \n\
	 s_mov_b32		 s_restore_buf_rsrc2,  0x1000000									 //NUM_RECORDS in bytes \n\
     end \n\
     s_mov_b32		 m0, 0x0																 //lds_offset initial value = 0 \n\
      \n\
   L_RESTORE_LDS_LOOP:									  \n\
     if (SAVE_LDS) \n\
	 buffer_load_dword	 v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset lds:1		    // first 64DW \n\
	 buffer_load_dword	 v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset lds:1 offset:256	    // second 64DW \n\
     end \n\
     s_add_u32		 m0, m0, 256*2								     // 128 DW \n\
     s_add_u32		 s_restore_mem_offset, s_restore_mem_offset, 256*2	     //mem offset increased by 128DW \n\
     s_cmp_lt_u32	 m0, s_restore_alloc_size				     //scc=(m0 < s_restore_alloc_size) ? 1 : 0 \n\
     s_cbranch_scc1  L_RESTORE_LDS_LOOP 													 //LDS restore is complete? \n\
      \n\
     /* 	 restore VGPRs	     */ \n\
     ////////////////////////////// \n\
   L_RESTORE_VGPR: \n\
	 // VGPR SR memory offset : 0	  \n\
     s_mov_b32		 s_restore_mem_offset, 0x0 \n\
     s_mov_b32		 exec_lo, 0xFFFFFFFF													 //need every thread from now on   //be consistent with SAVE although can be moved ahead \n\
     s_mov_b32		 exec_hi, 0xFFFFFFFF \n\
      \n\
     s_getreg_b32	 s_restore_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE)	 //vpgr_size \n\
     s_add_u32		 s_restore_alloc_size, s_restore_alloc_size, 1 \n\
     s_lshl_b32 	 s_restore_alloc_size, s_restore_alloc_size, 2							 //Number of VGPRs = (vgpr_size + 1) * 4    (non-zero value) \n\
     s_lshl_b32 	 s_restore_buf_rsrc2,  s_restore_alloc_size, 8						     //NUM_RECORDS in bytes (64 threads*4) \n\
     if (SWIZZLE_EN) \n\
	 s_add_u32		 s_restore_buf_rsrc2, s_restore_buf_rsrc2, 0x0						 //FIXME need to use swizzle to enable bounds checking? \n\
     else \n\
	 s_mov_b32		 s_restore_buf_rsrc2,  0x1000000									 //NUM_RECORDS in bytes \n\
     end \n\
      \n\
 if G8SR_VGPR_SR_IN_DWX4 \n\
      get_vgpr_size_bytes(s_restore_mem_offset) \n\
      s_sub_u32 	s_restore_mem_offset, s_restore_mem_offset, 256*4 \n\
       \n\
      // the const stride for DWx4 is 4*4 bytes \n\
      s_and_b32 s_restore_buf_rsrc1, s_restore_buf_rsrc1, 0x0000FFFF   // reset const stride to 0 \n\
      s_or_b32	s_restore_buf_rsrc1, s_restore_buf_rsrc1, G8SR_RESTORE_BUF_RSRC_WORD1_STRIDE_DWx4  // const stride to 4*4 bytes \n\
  \n\
      s_mov_b32 	m0, s_restore_alloc_size \n\
      s_set_gpr_idx_on	m0, 0x8    // Note.. This will change m0 \n\
       \n\
 L_RESTORE_VGPR_LOOP: \n\
      buffer_load_dwordx4 v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset slc:1 glc:1 \n\
      s_waitcnt vmcnt(0) \n\
      s_sub_u32 	m0, m0, 4 \n\
      v_mov_b32 	v0, v0	 // v[0+m0] = v0 \n\
      v_mov_b32 	v1, v1 \n\
      v_mov_b32 	v2, v2 \n\
      v_mov_b32 	v3, v3 \n\
      s_sub_u32 	s_restore_mem_offset, s_restore_mem_offset, 256*4 \n\
      s_cmp_eq_u32	m0, 0x8000 \n\
      s_cbranch_scc0	L_RESTORE_VGPR_LOOP \n\
      s_set_gpr_idx_off \n\
       \n\
      s_and_b32 s_restore_buf_rsrc1, s_restore_buf_rsrc1, 0x0000FFFF   // reset const stride to 0 \n\
      s_or_b32	s_restore_buf_rsrc1, s_restore_buf_rsrc1, S_RESTORE_BUF_RSRC_WORD1_STRIDE  // const stride to 4*4 bytes \n\
       \n\
 else \n\
     // VGPR load using dw burst \n\
     s_mov_b32		 s_restore_mem_offset_save, s_restore_mem_offset	 // restore start with v1, v0 will be the last \n\
     s_add_u32		 s_restore_mem_offset, s_restore_mem_offset, 256*4 \n\
     s_mov_b32		 m0, 4							 //VGPR initial index value = 1 \n\
     s_set_gpr_idx_on  m0, 0x8						 //M0[7:0] = M0[7:0] and M0[15:12] = 0x8 \n\
     s_add_u32		 s_restore_alloc_size, s_restore_alloc_size, 0x8000						 //add 0x8000 since we compare m0 against it later	  \n\
      \n\
   L_RESTORE_VGPR_LOOP: 									  \n\
     if(USE_MTBUF_INSTEAD_OF_MUBUF)	   \n\
	 tbuffer_load_format_x v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset format:BUF_NUM_FORMAT_FLOAT format: BUF_DATA_FORMAT_32 slc:1 glc:1 \n\
     else \n\
	 buffer_load_dword v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset	 slc:1 glc:1	  \n\
	 buffer_load_dword v1, v0, s_restore_buf_rsrc0, s_restore_mem_offset	 slc:1 glc:1	 offset:256 \n\
	 buffer_load_dword v2, v0, s_restore_buf_rsrc0, s_restore_mem_offset	 slc:1 glc:1	 offset:256*2 \n\
	 buffer_load_dword v3, v0, s_restore_buf_rsrc0, s_restore_mem_offset	 slc:1 glc:1	 offset:256*3 \n\
     end \n\
     s_waitcnt		 vmcnt(0)																 //ensure data ready \n\
     v_mov_b32		 v0, v0 																 //v[0+m0] = v0 \n\
     v_mov_b32	     v1, v1 \n\
     v_mov_b32	     v2, v2 \n\
     v_mov_b32	     v3, v3 \n\
     s_add_u32		 m0, m0, 4																 //next vgpr index \n\
     s_add_u32		 s_restore_mem_offset, s_restore_mem_offset, 256*4							 //every buffer_load_dword does 256 bytes \n\
     s_cmp_lt_u32	 m0,	 s_restore_alloc_size												 //scc = (m0 < s_restore_alloc_size) ? 1 : 0 \n\
     s_cbranch_scc1	 L_RESTORE_VGPR_LOOP														 //VGPR restore (except v0) is complete? \n\
     s_set_gpr_idx_off \n\
									 /* VGPR restore on v0 */ \n\
     if(USE_MTBUF_INSTEAD_OF_MUBUF)	   \n\
	 tbuffer_load_format_x v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save format:BUF_NUM_FORMAT_FLOAT format: BUF_DATA_FORMAT_32 slc:1 glc:1 \n\
     else \n\
	 buffer_load_dword v0, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save	 slc:1 glc:1	  \n\
	 buffer_load_dword v1, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save	 slc:1 glc:1	 offset:256 \n\
	 buffer_load_dword v2, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save	 slc:1 glc:1	 offset:256*2 \n\
	 buffer_load_dword v3, v0, s_restore_buf_rsrc0, s_restore_mem_offset_save	 slc:1 glc:1	 offset:256*3 \n\
     end \n\
      \n\
 end \n\
      \n\
     /* 	 restore SGPRs	     */ \n\
     ////////////////////////////// \n\
      \n\
     // SGPR SR memory offset : size(VGPR)	  \n\
     get_vgpr_size_bytes(s_restore_mem_offset) \n\
     get_sgpr_size_bytes(s_restore_tmp) \n\
     s_add_u32 s_restore_mem_offset, s_restore_mem_offset, s_restore_tmp \n\
     s_sub_u32 s_restore_mem_offset, s_restore_mem_offset, 16*4     // restore SGPR from S[n] to S[0], by 16 sgprs group \n\
     // TODO, change RSRC word to rearrange memory layout for SGPRS \n\
      \n\
     s_getreg_b32	 s_restore_alloc_size, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE)				 //spgr_size \n\
     s_add_u32		 s_restore_alloc_size, s_restore_alloc_size, 1 \n\
     s_lshl_b32 	 s_restore_alloc_size, s_restore_alloc_size, 4							 //Number of SGPRs = (sgpr_size + 1) * 16   (non-zero value) \n\
      \n\
     if (SGPR_SAVE_USE_SQC) \n\
	 s_lshl_b32		 s_restore_buf_rsrc2,	 s_restore_alloc_size, 2					 //NUM_RECORDS in bytes  \n\
     else \n\
	 s_lshl_b32		 s_restore_buf_rsrc2,	 s_restore_alloc_size, 8					 //NUM_RECORDS in bytes (64 threads) \n\
     end \n\
     if (SWIZZLE_EN) \n\
	 s_add_u32		 s_restore_buf_rsrc2, s_restore_buf_rsrc2, 0x0						 //FIXME need to use swizzle to enable bounds checking? \n\
     else \n\
	 s_mov_b32		 s_restore_buf_rsrc2,  0x1000000									 //NUM_RECORDS in bytes \n\
     end \n\
      \n\
     /* If 112 SGPRs ar allocated, 4 sgprs are not used TBA(108,109),TMA(110,111), \n\
	However, we are safe to restore these 4 SGPRs anyway, since TBA,TMA will later be restored by HWREG \n\
     */ \n\
     s_mov_b32 m0, s_restore_alloc_size \n\
      \n\
  L_RESTORE_SGPR_LOOP:	\n\
     read_16sgpr_from_mem(s0, s_restore_buf_rsrc0, s_restore_mem_offset)  //PV: further performance improvement can be made \n\
     s_waitcnt		 lgkmcnt(0)																 //ensure data ready \n\
      \n\
     s_sub_u32 m0, m0, 16    // Restore from S[n] to S[0] \n\
      \n\
     s_movreld_b64	 s0, s0 	 //s[0+m0] = s0 \n\
     s_movreld_b64	 s2, s2 \n\
     s_movreld_b64	 s4, s4 \n\
     s_movreld_b64	 s6, s6 \n\
     s_movreld_b64	 s8, s8 \n\
     s_movreld_b64	 s10, s10 \n\
     s_movreld_b64	 s12, s12 \n\
     s_movreld_b64	 s14, s14 \n\
      \n\
     s_cmp_eq_u32	 m0, 0				 //scc = (m0 < s_restore_alloc_size) ? 1 : 0 \n\
     s_cbranch_scc0	 L_RESTORE_SGPR_LOOP		 //SGPR restore (except s0) is complete? \n\
      \n\
     /* 	 restore HW registers	 */ \n\
     ////////////////////////////// \n\
   L_RESTORE_HWREG: \n\
    \n\
     \n\
 if G8SR_DEBUG_TIMESTAMP \n\
       s_mov_b32 s_g8sr_ts_restore_s[0], s_restore_pc_lo \n\
       s_mov_b32 s_g8sr_ts_restore_s[1], s_restore_pc_hi \n\
 end \n\
  \n\
     // HWREG SR memory offset : size(VGPR)+size(SGPR) \n\
     get_vgpr_size_bytes(s_restore_mem_offset) \n\
     get_sgpr_size_bytes(s_restore_tmp) \n\
     s_add_u32 s_restore_mem_offset, s_restore_mem_offset, s_restore_tmp \n\
      \n\
       \n\
     s_mov_b32		 s_restore_buf_rsrc2, 0x4												 //NUM_RECORDS	 in bytes \n\
     if (SWIZZLE_EN) \n\
	 s_add_u32		 s_restore_buf_rsrc2, s_restore_buf_rsrc2, 0x0						 //FIXME need to use swizzle to enable bounds checking? \n\
     else \n\
	 s_mov_b32		 s_restore_buf_rsrc2,  0x1000000									 //NUM_RECORDS in bytes \n\
     end \n\
      \n\
     read_hwreg_from_mem(s_restore_m0, s_restore_buf_rsrc0, s_restore_mem_offset)					 //M0 \n\
     read_hwreg_from_mem(s_restore_pc_lo, s_restore_buf_rsrc0, s_restore_mem_offset)				 //PC \n\
     read_hwreg_from_mem(s_restore_pc_hi, s_restore_buf_rsrc0, s_restore_mem_offset) \n\
     read_hwreg_from_mem(s_restore_exec_lo, s_restore_buf_rsrc0, s_restore_mem_offset)				 //EXEC \n\
     read_hwreg_from_mem(s_restore_exec_hi, s_restore_buf_rsrc0, s_restore_mem_offset) \n\
     read_hwreg_from_mem(s_restore_status, s_restore_buf_rsrc0, s_restore_mem_offset)				 //STATUS \n\
     read_hwreg_from_mem(s_restore_trapsts, s_restore_buf_rsrc0, s_restore_mem_offset)				 //TRAPSTS \n\
     read_hwreg_from_mem(xnack_mask_lo, s_restore_buf_rsrc0, s_restore_mem_offset)					 //XNACK_MASK_LO \n\
     read_hwreg_from_mem(xnack_mask_hi, s_restore_buf_rsrc0, s_restore_mem_offset)					 //XNACK_MASK_HI \n\
     read_hwreg_from_mem(s_restore_mode, s_restore_buf_rsrc0, s_restore_mem_offset)				 //MODE \n\
     read_hwreg_from_mem(tba_lo, s_restore_buf_rsrc0, s_restore_mem_offset)						 //TBA_LO \n\
     read_hwreg_from_mem(tba_hi, s_restore_buf_rsrc0, s_restore_mem_offset)						 //TBA_HI \n\
     \n\
     s_waitcnt		 lgkmcnt(0)																						 //from now on, it is safe to restore STATUS and IB_STS \n\
      \n\
     s_and_b32 s_restore_pc_hi, s_restore_pc_hi, 0x0000ffff	 //pc[47:32]	    //Do it here in order not to affect STATUS \n\
      \n\
     //for normal save & restore, the saved PC points to the next inst to execute, no adjustment needs to be made, otherwise: \n\
     if ((EMU_RUN_HACK) && (!EMU_RUN_HACK_RESTORE_NORMAL)) \n\
	 s_add_u32 s_restore_pc_lo, s_restore_pc_lo, 8		  //pc[31:0]+8	   //two back-to-back s_trap are used (first for save and second for restore) \n\
	 s_addc_u32	 s_restore_pc_hi, s_restore_pc_hi, 0x0		  //carry bit over \n\
     end  \n\
     if ((EMU_RUN_HACK) && (EMU_RUN_HACK_RESTORE_NORMAL))		\n\
	 s_add_u32 s_restore_pc_lo, s_restore_pc_lo, 4		  //pc[31:0]+4	   // save is hack through s_trap but restore is normal \n\
	 s_addc_u32	 s_restore_pc_hi, s_restore_pc_hi, 0x0		  //carry bit over \n\
     end \n\
      \n\
     s_mov_b32		 m0,		 s_restore_m0 \n\
     s_mov_b32		 exec_lo,	 s_restore_exec_lo \n\
     s_mov_b32		 exec_hi,	 s_restore_exec_hi \n\
      \n\
     read_hwreg_from_mem(tma_lo, s_restore_buf_rsrc0, s_restore_mem_offset)					     //tma_lo \n\
     read_hwreg_from_mem(tma_hi, s_restore_buf_rsrc0, s_restore_mem_offset)					     //tma_hi \n\
     s_waitcnt		 lgkmcnt(0)																	 //from now on, it is safe to restore STATUS and IB_STS \n\
     \n\
     s_and_b32		 s_restore_m0, SQ_WAVE_TRAPSTS_PRE_SAVECTX_MASK, s_restore_trapsts \n\
     s_setreg_b32	 hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_PRE_SAVECTX_SHIFT, SQ_WAVE_TRAPSTS_PRE_SAVECTX_SIZE), s_restore_m0 \n\
     s_and_b32		 s_restore_m0, SQ_WAVE_TRAPSTS_POST_SAVECTX_MASK, s_restore_trapsts \n\
     s_lshr_b32 	 s_restore_m0, s_restore_m0, SQ_WAVE_TRAPSTS_POST_SAVECTX_SHIFT \n\
     s_setreg_b32	 hwreg(HW_REG_TRAPSTS, SQ_WAVE_TRAPSTS_POST_SAVECTX_SHIFT, SQ_WAVE_TRAPSTS_POST_SAVECTX_SIZE), s_restore_m0 \n\
     //s_setreg_b32	 hwreg(HW_REG_TRAPSTS),  s_restore_trapsts	//don't overwrite SAVECTX bit as it may be set through external SAVECTX during restore \n\
     s_setreg_b32	 hwreg(HW_REG_MODE),	 s_restore_mode \n\
     //reuse s_restore_m0 as a temp register \n\
     s_and_b32		 s_restore_m0, s_restore_pc_hi, S_SAVE_PC_HI_RCNT_MASK \n\
     s_lshr_b32 	 s_restore_m0, s_restore_m0, S_SAVE_PC_HI_RCNT_SHIFT \n\
     s_lshl_b32 	 s_restore_m0, s_restore_m0, SQ_WAVE_IB_STS_RCNT_SHIFT \n\
     s_mov_b32		 s_restore_tmp, 0x0																				 //IB_STS is zero \n\
     s_or_b32		 s_restore_tmp, s_restore_tmp, s_restore_m0 \n\
     s_and_b32		 s_restore_m0, s_restore_pc_hi, S_SAVE_PC_HI_FIRST_REPLAY_MASK \n\
     s_lshr_b32 	 s_restore_m0, s_restore_m0, S_SAVE_PC_HI_FIRST_REPLAY_SHIFT \n\
     s_lshl_b32 	 s_restore_m0, s_restore_m0, SQ_WAVE_IB_STS_FIRST_REPLAY_SHIFT \n\
     s_or_b32		 s_restore_tmp, s_restore_tmp, s_restore_m0 \n\
     s_and_b32	     s_restore_m0, s_restore_status, SQ_WAVE_STATUS_INST_ATC_MASK  \n\
     s_lshr_b32 	 s_restore_m0, s_restore_m0, SQ_WAVE_STATUS_INST_ATC_SHIFT     \n\
     s_setreg_b32	 hwreg(HW_REG_IB_STS),	 s_restore_tmp \n\
      \n\
     s_and_b64	 vcc, vcc, vcc	// Restore STATUS.VCCZ, not writable by s_setreg_b32 \n\
     s_setreg_b32	 hwreg(HW_REG_STATUS),	 s_restore_status     // SCC is included, which is changed by previous salu \n\
      \n\
     s_barrier													 //barrier to ensure the readiness of LDS before access attemps from any other wave in the same TG //FIXME not performance-optimal at this time \n\
      \n\
 if G8SR_DEBUG_TIMESTAMP \n\
      s_memrealtime s_g8sr_ts_restore_d \n\
     s_waitcnt lgkmcnt(0) \n\
 end	  \n\
      \n\
 //	 s_rfe_b64 s_restore_pc_lo					 //Return to the main shader program and resume execution \n\
     s_rfe_restore_b64	s_restore_pc_lo, s_restore_m0		 // s_restore_m0[0] is used to set STATUS.inst_atc  \n\
      \n\
       \n\
 /**************************************************************************/ \n\
 /*			 the END								       */ \n\
 /**************************************************************************/	  \n\
 L_END_PGM:	  \n\
     s_endpgm_saved \n\
      \n\
 end	  \n\
  \n"
 "  \n\
 /**************************************************************************/ \n\
 /*			 the helper functions							   */ \n\
 /**************************************************************************/ \n\
  \n\
 //Only for save hwreg to mem \n\
 function write_hwreg_to_mem(s, s_rsrc, s_mem_offset) \n\
	 s_mov_b32 exec_lo, m0					 //assuming exec_lo is not needed anymore from this point on \n\
	 s_mov_b32 m0, s_mem_offset \n\
	 s_buffer_store_dword s, s_rsrc, m0		 glc:0	  \n\
	 s_add_u32		 s_mem_offset, s_mem_offset, 4 \n\
	 s_mov_b32	 m0, exec_lo \n\
 end \n\
  \n\
 //Only for save hwreg to mem \n\
 function write_tma_to_mem(s, s_rsrc, offset_imm) \n\
	 s_mov_b32 exec_lo, m0					 //assuming exec_lo is not needed anymore from this point on \n\
	 s_mov_b32 m0, offset_imm \n\
	 s_buffer_store_dword s, s_rsrc, m0		 glc:0	  \n\
	 s_mov_b32	 m0, exec_lo \n\
 end \n\
  \n\
 // HWREG are saved before SGPRs, so all HWREG could be use. \n\
 function write_16sgpr_to_mem(s, s_rsrc, s_mem_offset) \n\
  \n\
	 s_buffer_store_dwordx4 s[0], s_rsrc, 0  glc:0	  \n\
	 s_buffer_store_dwordx4 s[4], s_rsrc, 16  glc:0   \n\
	 s_buffer_store_dwordx4 s[8], s_rsrc, 32  glc:0   \n\
	 s_buffer_store_dwordx4 s[12], s_rsrc, 48 glc:0   \n\
	 s_add_u32	 s_rsrc[0], s_rsrc[0], 4*16 \n\
	 s_addc_u32		 s_rsrc[1], s_rsrc[1], 0x0			   // +scc \n\
 end \n\
  \n"
 "function read_hwreg_from_mem(s, s_rsrc, s_mem_offset) \n\
     s_buffer_load_dword s, s_rsrc, s_mem_offset	 glc:1 \n\
     s_add_u32		 s_mem_offset, s_mem_offset, 4 \n\
 end \n\
  \n\
 function read_16sgpr_from_mem(s, s_rsrc, s_mem_offset) \n\
     s_buffer_load_dwordx16 s, s_rsrc, s_mem_offset		 glc:1 \n\
     s_sub_u32		 s_mem_offset, s_mem_offset, 4*16 \n\
 end \n\
  \n\
 function get_lds_size_bytes(s_lds_size_byte) \n\
     // SQ LDS granularity is 64DW, while PGM_RSRC2.lds_size is in granularity 128DW	   \n\
     s_getreg_b32   s_lds_size_byte, hwreg(HW_REG_LDS_ALLOC, SQ_WAVE_LDS_ALLOC_LDS_SIZE_SHIFT, SQ_WAVE_LDS_ALLOC_LDS_SIZE_SIZE) 		 // lds_size \n\
     s_lshl_b32     s_lds_size_byte, s_lds_size_byte, 8 					 //LDS size in dwords = lds_size * 64 *4Bytes	 // granularity 64DW \n\
 end \n\
  \n\
 function get_vgpr_size_bytes(s_vgpr_size_byte) \n\
     s_getreg_b32   s_vgpr_size_byte, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_VGPR_SIZE_SIZE)  //vpgr_size \n\
     s_add_u32	    s_vgpr_size_byte, s_vgpr_size_byte, 1 \n\
     s_lshl_b32     s_vgpr_size_byte, s_vgpr_size_byte, (2+8) //Number of VGPRs = (vgpr_size + 1) * 4 * 64 * 4	 (non-zero value)   //FIXME for GFX, zero is possible  \n\
 end \n\
  \n\
 function get_sgpr_size_bytes(s_sgpr_size_byte) \n\
     s_getreg_b32   s_sgpr_size_byte, hwreg(HW_REG_GPR_ALLOC,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SHIFT,SQ_WAVE_GPR_ALLOC_SGPR_SIZE_SIZE)  //spgr_size \n\
     s_add_u32	    s_sgpr_size_byte, s_sgpr_size_byte, 1 \n\
     s_lshl_b32     s_sgpr_size_byte, s_sgpr_size_byte, 6 //Number of SGPRs = (sgpr_size + 1) * 16 *4	(non-zero value)  \n\
 end \n\
  \n\
 function get_hwreg_size_bytes \n\
     return 128 //HWREG size 128 bytes \n\
 end\n";

#endif

static const uint32_t cwsr_trap_carrizo_hex[] = {
	0xbf820001, 0xbf820128,
	0xb8f4f802, 0xb8f5f803,
	0x8675ff75, 0x00000400,
	0xbf850008, 0xb8f5f803,
	0x8675ff75, 0x000001ff,
	0xbf850001, 0x80708470,
	0x8671ff71, 0x0000ffff,
	0xbe801f70, 0xb8f5f803,
	0x8675ff75, 0x00000100,
	0xbf840006, 0xbefa0080,
	0xb97a0203, 0x8671ff71,
	0x0000ffff, 0x80f08870,
	0x82f18071, 0xbefa0080,
	0xb97a0283, 0xbef60068,
	0xbef70069, 0xb8fa1c07,
	0x8e7a9c7a, 0x87717a71,
	0xb8fa03c7, 0x8e7a9b7a,
	0x87717a71, 0xb8faf807,
	0x867aff7a, 0x00007fff,
	0xb97af807, 0xbef2007e,
	0xbef3007f, 0xbefe0180,
	0xbf900004, 0xbf8e0002,
	0xbf88fffe, 0xbef8007e,
	0x8679ff7f, 0x0000ffff,
	0x8779ff79, 0x00040000,
	0xbefa0080, 0xbefb00ff,
	0x00807fac, 0x867aff7f,
	0x08000000, 0x8f7a837a,
	0x877b7a7b, 0x867aff7f,
	0x70000000, 0x8f7a817a,
	0x877b7a7b, 0xbefe006e,
	0xbef5006f, 0xbeef007c,
	0xbeee0080, 0xb8ee2a05,
	0x806e816e, 0x8e6e8a6e,
	0xb8fa1605, 0x807a817a,
	0x8e7a867a, 0x806e7a6e,
	0xbefa0084, 0xbefa00ff,
	0x01000000, 0xbefe007c,
	0xbefc006e, 0xc0601bfc,
	0x0000007c, 0x806e846e,
	0xbefc007e, 0xbefe007c,
	0xbefc006e, 0xc0601c3c,
	0x0000007c, 0x806e846e,
	0xbefc007e, 0xbefe007c,
	0xbefc006e, 0xc0601c7c,
	0x0000007c, 0x806e846e,
	0xbefc007e, 0xbefe007c,
	0xbefc006e, 0xc0601cbc,
	0x0000007c, 0x806e846e,
	0xbefc007e, 0xbefe007c,
	0xbefc006e, 0xc0601cfc,
	0x0000007c, 0x806e846e,
	0xbefc007e, 0xbef2007e,
	0xbef30075, 0xbefe007c,
	0xbefc006e, 0xc0601d3c,
	0x0000007c, 0x806e846e,
	0xbefc007e, 0xb8f5f803,
	0xbefe007c, 0xbefc006e,
	0xc0601d7c, 0x0000007c,
	0x806e846e, 0xbefc007e,
	0xbefe007c, 0xbefc006e,
	0xc0601dbc, 0x0000007c,
	0x806e846e, 0xbefc007e,
	0xbefe007c, 0xbefc006e,
	0xc0601dfc, 0x0000007c,
	0x806e846e, 0xbefc007e,
	0xb8eff801, 0xbefe007c,
	0xbefc006e, 0xc0601bfc,
	0x0000007c, 0x806e846e,
	0xbefc007e, 0xbefe007c,
	0xbefc006e, 0xc0601b3c,
	0x0000007c, 0x806e846e,
	0xbefc007e, 0xbefe007c,
	0xbefc006e, 0xc0601b7c,
	0x0000007c, 0x806e846e,
	0xbefc007e, 0xbefe007c,
	0xbefc006e, 0xc0601cbc,
	0x0000007c, 0x806e846e,
	0xbefc007e, 0xbefe007c,
	0xbefc006e, 0xc0601cfc,
	0x0000007c, 0x806e846e,
	0xbefc007e, 0x867aff7f,
	0x04000000, 0xbef30080,
	0x8773737a, 0xb8ee2a05,
	0x806e816e, 0x8e6e8a6e,
	0xb8f51605, 0x80758175,
	0x8e758475, 0x8e7a8275,
	0xbefa00ff, 0x01000000,
	0xbef60178, 0x80786e78,
	0xbefc0080, 0xbe802b00,
	0xbe822b02, 0xbe842b04,
	0xbe862b06, 0xbe882b08,
	0xbe8a2b0a, 0xbe8c2b0c,
	0xbe8e2b0e, 0xc06a003c,
	0x00000000, 0xc06a013c,
	0x00000010, 0xc06a023c,
	0x00000020, 0xc06a033c,
	0x00000030, 0x8078c078,
	0x82798079, 0x807c907c,
	0xbf0a757c, 0xbf85ffeb,
	0xbef80176, 0xbeee0080,
	0xbefe00c1, 0xbeff00c1,
	0xbefa00ff, 0x01000000,
	0xe0724000, 0x6e1e0000,
	0xe0724100, 0x6e1e0100,
	0xe0724200, 0x6e1e0200,
	0xe0724300, 0x6e1e0300,
	0xbefe00c1, 0xbeff00c1,
	0xb8f54306, 0x8675c175,
	0xbf84002c, 0xbf8a0000,
	0x867aff73, 0x04000000,
	0xbf840028, 0x8e758675,
	0x8e758275, 0xbefa0075,
	0xb8ee2a05, 0x806e816e,
	0x8e6e8a6e, 0xb8fa1605,
	0x807a817a, 0x8e7a867a,
	0x806e7a6e, 0x806eff6e,
	0x00000080, 0xbefa00ff,
	0x01000000, 0xbefc0080,
	0xd28c0002, 0x000100c1,
	0xd28d0003, 0x000204c1,
	0xd1060002, 0x00011103,
	0x7e0602ff, 0x00000200,
	0xbefc00ff, 0x00010000,
	0xbe80007b, 0x867bff7b,
	0xff7fffff, 0x877bff7b,
	0x00058000, 0xd8ec0000,
	0x00000002, 0xbf8c007f,
	0xe0765000, 0x6e1e0002,
	0x32040702, 0xd0c9006a,
	0x0000eb02, 0xbf87fff7,
	0xbefb0000, 0xbeee00ff,
	0x00000400, 0xbefe00c1,
	0xbeff00c1, 0xb8f52a05,
	0x80758175, 0x8e758275,
	0x8e7a8875, 0xbefa00ff,
	0x01000000, 0xbefc0084,
	0xbf0a757c, 0xbf840015,
	0xbf11017c, 0x8075ff75,
	0x00001000, 0x7e000300,
	0x7e020301, 0x7e040302,
	0x7e060303, 0xe0724000,
	0x6e1e0000, 0xe0724100,
	0x6e1e0100, 0xe0724200,
	0x6e1e0200, 0xe0724300,
	0x6e1e0300, 0x807c847c,
	0x806eff6e, 0x00000400,
	0xbf0a757c, 0xbf85ffef,
	0xbf9c0000, 0xbf8200d0,
	0xbef8007e, 0x8679ff7f,
	0x0000ffff, 0x8779ff79,
	0x00040000, 0xbefa0080,
	0xbefb00ff, 0x00807fac,
	0x8676ff7f, 0x08000000,
	0x8f768376, 0x877b767b,
	0x8676ff7f, 0x70000000,
	0x8f768176, 0x877b767b,
	0x8676ff7f, 0x04000000,
	0xbf84001e, 0xbefe00c1,
	0xbeff00c1, 0xb8f34306,
	0x8673c173, 0xbf840019,
	0x8e738673, 0x8e738273,
	0xbefa0073, 0xb8f22a05,
	0x80728172, 0x8e728a72,
	0xb8f61605, 0x80768176,
	0x8e768676, 0x80727672,
	0x8072ff72, 0x00000080,
	0xbefa00ff, 0x01000000,
	0xbefc0080, 0xe0510000,
	0x721e0000, 0xe0510100,
	0x721e0000, 0x807cff7c,
	0x00000200, 0x8072ff72,
	0x00000200, 0xbf0a737c,
	0xbf85fff6, 0xbef20080,
	0xbefe00c1, 0xbeff00c1,
	0xb8f32a05, 0x80738173,
	0x8e738273, 0x8e7a8873,
	0xbefa00ff, 0x01000000,
	0xbef60072, 0x8072ff72,
	0x00000400, 0xbefc0084,
	0xbf11087c, 0x8073ff73,
	0x00008000, 0xe0524000,
	0x721e0000, 0xe0524100,
	0x721e0100, 0xe0524200,
	0x721e0200, 0xe0524300,
	0x721e0300, 0xbf8c0f70,
	0x7e000300, 0x7e020301,
	0x7e040302, 0x7e060303,
	0x807c847c, 0x8072ff72,
	0x00000400, 0xbf0a737c,
	0xbf85ffee, 0xbf9c0000,
	0xe0524000, 0x761e0000,
	0xe0524100, 0x761e0100,
	0xe0524200, 0x761e0200,
	0xe0524300, 0x761e0300,
	0xb8f22a05, 0x80728172,
	0x8e728a72, 0xb8f61605,
	0x80768176, 0x8e768676,
	0x80727672, 0x80f2c072,
	0xb8f31605, 0x80738173,
	0x8e738473, 0x8e7a8273,
	0xbefa00ff, 0x01000000,
	0xbefc0073, 0xc031003c,
	0x00000072, 0x80f2c072,
	0xbf8c007f, 0x80fc907c,
	0xbe802d00, 0xbe822d02,
	0xbe842d04, 0xbe862d06,
	0xbe882d08, 0xbe8a2d0a,
	0xbe8c2d0c, 0xbe8e2d0e,
	0xbf06807c, 0xbf84fff1,
	0xb8f22a05, 0x80728172,
	0x8e728a72, 0xb8f61605,
	0x80768176, 0x8e768676,
	0x80727672, 0xbefa0084,
	0xbefa00ff, 0x01000000,
	0xc0211cfc, 0x00000072,
	0x80728472, 0xc0211c3c,
	0x00000072, 0x80728472,
	0xc0211c7c, 0x00000072,
	0x80728472, 0xc0211bbc,
	0x00000072, 0x80728472,
	0xc0211bfc, 0x00000072,
	0x80728472, 0xc0211d3c,
	0x00000072, 0x80728472,
	0xc0211d7c, 0x00000072,
	0x80728472, 0xc0211a3c,
	0x00000072, 0x80728472,
	0xc0211a7c, 0x00000072,
	0x80728472, 0xc0211dfc,
	0x00000072, 0x80728472,
	0xc0211b3c, 0x00000072,
	0x80728472, 0xc0211b7c,
	0x00000072, 0x80728472,
	0xbf8c007f, 0x8671ff71,
	0x0000ffff, 0xbefc0073,
	0xbefe006e, 0xbeff006f,
	0xc0211bbc, 0x00000072,
	0x80728472, 0xc0211bfc,
	0x00000072, 0x80728472,
	0xbf8c007f, 0x867375ff,
	0x000003ff, 0xb9734803,
	0x867375ff, 0xfffff800,
	0x8f738b73, 0xb973a2c3,
	0xb977f801, 0x8673ff71,
	0xf0000000, 0x8f739c73,
	0x8e739073, 0xbef60080,
	0x87767376, 0x8673ff71,
	0x08000000, 0x8f739b73,
	0x8e738f73, 0x87767376,
	0x8673ff74, 0x00800000,
	0x8f739773, 0xb976f807,
	0x86ea6a6a, 0xb974f802,
	0xbf8a0000, 0x95807370,
	0xbf9b0000, 0x00000000,
	0x00000000, 0x00000000,
};

