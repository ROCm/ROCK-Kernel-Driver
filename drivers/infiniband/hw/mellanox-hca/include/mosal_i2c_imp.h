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
 
 



typedef struct 
{
    
    
    call_result_t (*i2c_read)(MOSAL_i2c_devh_t dev_h, u_int16_t i2c_addr, u_int32_t addr,
                              char *data, u_int32_t length);

    call_result_t (*i2c_write)(MOSAL_i2c_devh_t i2c_dev_h, u_int16_t i2c_addr,
                               u_int32_t addr, char *data, u_int32_t length);


    call_result_t (*i2c_master_transmit)(MOSAL_i2c_devh_t dev_h, unsigned char slv_addr, void *buffer,
                                         int count, int *bytes_sent_p,  bool sendSTOP,
                                         u_int32_t *const key_p);

    call_result_t (*i2c_master_receive)(MOSAL_i2c_devh_t dev_h,  u_int8_t slv_addr,
                                        void *buffer, u_int16_t count, u_int32_t *bytes_received_p,
                                        bool sendSTOP, const u_int32_t key);


    call_result_t (*i2c_send_stop)(MOSAL_i2c_devh_t dev_h, const u_int32_t key);
    
} MOSAL_i2c_dev_ops_t;


/**
 * I2C device description structure.
 */
struct MOSAL_i2c_dev_st
{
  u_int16_t base;                  /* base address of card    */
  u_int8_t  own;                   /* own address of the card */
  char      name[I2C_MAX_DEV_NAME];   /* Name of I2C device      */
  void      *priv;
  
  u_int32_t protkey;               /* struct access prot key  */

  MOSAL_i2c_dev_ops_t ops;
};
