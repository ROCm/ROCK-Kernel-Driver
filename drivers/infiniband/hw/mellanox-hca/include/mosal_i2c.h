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

#ifndef H_MOSAL_I2C_H
#define H_MOSAL_I2C_H

#include <mtl_common.h>


#define I2C_MAX_DEV_NAME   17      /* TBD: MOSAL name?     */


/* Forward declaration of device handle */
typedef struct MOSAL_i2c_dev_st * MOSAL_i2c_devh_t;


/* Forwad declaration of device */
typedef struct MOSAL_i2c_dev_st MOSAL_i2c_dev_t;



/***************************************************************************************************
 * Function:  MOSAL_I2C_master_receive
 *
 * Description: master receive data
 *
 * Parameters:
 *             dev_h             (IN)                 Device handle.
 *             slv_addr          (IN)                 The address of the receipent
 *             buffer            (OUT)  (LEN @count)  Pointer where to put data received
 *             count             (IN)                 Number of bytes to be receive
 *             bytes_received_p  (OUT)                Pointer to var to receive number of bytes received
 *             sendSTOP          (IN)                 Send stop after transaction
 *             key               (IN)                 Key to autenticate the caller.
 *			   
 * Returns: MT_OK    success
 *          MT_ERROR failure to initialize
 * 
 * Notes:
 *
 **************************************************************************************************/
call_result_t MOSAL_I2C_master_receive(MOSAL_i2c_devh_t dev_h,  u_int8_t slv_addr,
                                       void *buffer, u_int16_t count, u_int32_t *bytes_received_p,  MT_bool sendSTOP, u_int32_t key);

/***************************************************************************************************
 *    
 *  Function(MOSAL): MOSAL_I2C_master_transmit
 *
 *  Description:  master transmit data
 *
 *
 *  Parameters: 
 *              dev_h           (IN)                  I2C device handle.
 *              slv_addr        (IN)                  The address of the receipent.
 *              buffer          (IN)    (LEN @count)  Pointer to data to be transmitted.
 *              count           (IN)                  Number of bytes to be sent.
 *              bytes_sent_p    (OUT)                 Pointer to var to receive number of bytes sent
 *              sendSTOP        (IN)                  Flag when 1 send STOP - otherwise don't send STOP
 *              key_p           (IN/OUT)              Pointer to key. 
 *			   
 * Returns: MT_OK     success
 *          MT_ERROR  failure to initialize
 * 
 *****************************************************************************************/
call_result_t MOSAL_I2C_master_transmit(MOSAL_i2c_devh_t dev_h, unsigned char slv_addr, void *buffer,
                                       int count, int *bytes_sent_p,  MT_bool sendSTOP, u_int32_t * const key_p);

/***************************************************************************************************
 * Function: MOSAL_I2C_send_stop
 *
 * Description: send STOP to the i2c bus
 *
 * Parameters: 
 *             dev_h        (IN)     device handle
 *             key          (IN)     pointer to a key previously received from master transmit
 *			   
 * Returns: MT_OK    success
 *          MT_ERROR failure to initialize
 * 
 * Notes:
 *
 **************************************************************************************************/
call_result_t MOSAL_I2C_send_stop(MOSAL_i2c_devh_t dev_h, const u_int32_t key);



/*****************************************************************************************
 * Function: MOSAL_I2C_read
 *
 * 
 * Description: Read from i2c device.
 *
 * 
 * Parameters:
 *              dev_h       (IN)                 Handle of device to use.
 *              i2c_addr    (IN)                 I2C bus address of target device.
 *              addr        (IN)                 Offset in target device for read start.
 *              data        (OUT)  (LEN @length) Buffer in local memory where data read will be written.
 *              length      (IN)                 Number of bytes to read.
 *                 
 *
 * Returns: MT_OK    success
 *          MT_ERROR failed to read
 *
 *****************************************************************************************/
call_result_t MOSAL_I2C_read(MOSAL_i2c_devh_t dev_h, u_int16_t i2c_addr, u_int32_t addr,
                             u_int8_t* data, u_int32_t length);


/*****************************************************************************************
 * Function: MOSAL_I2C_write
 * 
 * Description: write to i2c device.
 *
 * 
 * Parameters:
 *              dev_h       (IN)                Handle of device to use.
 *              i2c_addr    (IN)                I2C bus address of target device.
 *              addr        (IN)                Offset in target device for write start.
 *              data        (IN) (LEN @length)  Buffer in local memory where data to be written is found
 *              length      (IN)                Number of bytes to read.
 *                 
 *
 * Returns: MT_OK    success
 *          MT_ERROR failed to write
 *
 *****************************************************************************************/
call_result_t MOSAL_I2C_write(MOSAL_i2c_devh_t dev_h, u_int16_t i2c_addr,
                              u_int32_t addr, u_int8_t *data, u_int32_t length);


/*****************************************************************************************
 * Function: MOSAL_I2C_open
 * 
 * Description: Open I2C device returning handle to it. 
 *
 * 
 * Parameters:
 *              name        (IN)   (LEN s)       Device name.
 *              dev_h       (OUT)                Handle of device to use.
 *                 
 *
 * Returns: MT_OK     success
 *          MT_ENODEV not device register for this name 
 *          MT_ERROR  generic error
 *
 *****************************************************************************************/
call_result_t MOSAL_I2C_open(char * name, MOSAL_i2c_devh_t * dev_h);


/*****************************************************************************************
 * Function: MOSAL_I2C_close
 * 
 * Description: close a previously opened device.
 *
 * 
 * Parameters:
 *              dev_h       (IN)                Handle of device to use.
 *                 
 *
 * Returns: MT_OK     success
 *          MT_ENODEV Inavlid handle
 *          MT_ERROR  generic error
 *
 *****************************************************************************************/
call_result_t MOSAL_I2C_close(MOSAL_i2c_devh_t dev_h);





/*****************************************************************************************
 *
 * Function (Kernel only): MOSAL_I2C_add_dev
 *
 * Description: Register an i2c device.
 * 
 * Parameters: 
 *              devst   (IN)   Device struture.
 *              devh    (OUT)  Return device handle.
 *
 * Returns: MT_OK
 *          MT_ENOMEM
 *
 *
 * Notes: this function should be called only from the modules registering the device
 *        inside the kernel.
 *****************************************************************************************/
call_result_t MOSAL_I2C_add_dev(MOSAL_i2c_dev_t * devst, MOSAL_i2c_devh_t *devh);

/*****************************************************************************************
 *
 * Function (Kernel only): MOSAL_I2C_del_dev
 *
 * Description: Deregister I2C device.
 * 
 * Parameters: 
 *              devh   (IN)   Device struture.
 *
 * Returns: MT_OK
 *          MT_EINVAL - invalid device.
 *
 * Notes: this function should be called only from the modules registering the device
 *        inside the kernel.
 *****************************************************************************************/
call_result_t MOSAL_I2C_del_dev(MOSAL_i2c_devh_t devh);

   
#endif /* H_MOSAL_I2C_H */
