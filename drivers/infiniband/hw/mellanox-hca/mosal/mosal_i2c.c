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

#include <mtl_common.h>
#include <mosal_i2c.h>
#include <mosal_i2c_imp.h>

#define I2C_MAX_DEV_NUM     4         /* TBD: do we need more */

#define I2C_SEC_KEY        0x38783244 /* Key used to access struct */

#define MOSAL_I2C_free_entry(dev_p)   (((dev_p)->protkey == 0) ? TRUE : FALSE)
#define MOSAL_I2C_valid_dev(dev_p)    (((dev_p) != NULL && (dev_p)->protkey == I2C_SEC_KEY) ? TRUE : FALSE )


#define MOSAL_I2C_CHECK_RET(dev_p)     if(MOSAL_I2C_valid_dev(dev_p) == FALSE)                 \
                                       {                                                       \
                                          MTL_ERROR2("%s: Invalid device handle.", __func__);  \
                                          return(MT_EINVAL);                                   \
                                       }
                                          
                                          
                                          



/* A table of the I2C devices */ 
static MOSAL_i2c_dev_t i2c_dev_table[I2C_MAX_DEV_NUM] = { { 0 } };


call_result_t MOSAL_I2C_add_dev(MOSAL_i2c_dev_t * dev, MOSAL_i2c_devh_t *dev_h)
{
    u_int8_t count;
    MOSAL_i2c_dev_t * new_dev;

    
    /* Find empty slot in static table */
    for(count = 0; count < I2C_MAX_DEV_NUM; count++)
        if(MOSAL_I2C_free_entry(&i2c_dev_table[count])) break;

    
    if(count == I2C_MAX_DEV_NUM)
    {
        MTL_ERROR2("%s: All couldn't allocate new device", __func__);
        return(MT_ENORSC);
    }

    new_dev = &i2c_dev_table[count];
    

    memcpy(new_dev, dev, sizeof(MOSAL_i2c_dev_t));
    
    new_dev->protkey = I2C_SEC_KEY;

    *dev_h = new_dev;
    
    MTL_TRACE4(": *dev_h 0x%p dev_h 0x%p &dev_h 0x%p\n", *dev_h, dev_h, &dev_h);

    return(MT_OK);
}

call_result_t MOSAL_I2C_del_dev(MOSAL_i2c_devh_t devh)
{
    MOSAL_I2C_CHECK_RET(devh);

    devh->protkey = 0;
    
    return(MT_OK);
}


call_result_t
MOSAL_I2C_open(char  * name, MOSAL_i2c_devh_t * devh)
{
    int i;

    for (i = 0; i < I2C_MAX_DEV_NUM; i++)
    {
        if(MOSAL_I2C_free_entry(&i2c_dev_table[i]))
            continue;


        MTL_ERROR7("%s: current device is %s, looking for %s.\n",  __func__, i2c_dev_table[i].name, name);
        if (!strcmp(name, i2c_dev_table[i].name))
        {
            *devh = &i2c_dev_table[i];
            MTL_TRACE5("Device %s has been found.\n", name);
            MTL_TRACE4("%s: dev_h 0x%p &dev_h 0x%p\n", __func__, devh, &devh);
            return MT_OK;
        }
    }

    MTL_TRACE1("No such device name: %s\n", name);

    return MT_ENORSC;
}


call_result_t MOSAL_I2C_close(MOSAL_i2c_devh_t devh)
{
    return(MT_OK);
}



/**
 * Read from i2c
 * 
 */
call_result_t MOSAL_I2C_read(MOSAL_i2c_devh_t devh, u_int16_t i2c_addr, u_int32_t addr,
                             u_int8_t *data, u_int32_t length)
{
    call_result_t ret; 

    MOSAL_I2C_CHECK_RET(devh);

    MTL_TRACE5("%s(%d): i2c_addr = 0x%x, addr = 0x%x, length = 0x%x.\n", __func__, __LINE__, i2c_addr, addr, length);

    if (devh->ops.i2c_read == 0)
    {
        MTL_ERROR('1',"%s: No read function for given device\n", __func__);
        return(MT_ENOSYS);
    }

    ret = devh->ops.i2c_read(devh,i2c_addr,addr,data,length);

    return(ret);
}


/**
 * Write from i2c
 * 
 */
call_result_t MOSAL_I2C_write(MOSAL_i2c_devh_t devh, u_int16_t i2c_addr,
                              u_int32_t addr, u_int8_t *data, u_int32_t length)
{
    call_result_t ret;

    MOSAL_I2C_CHECK_RET(devh);

    MTL_TRACE5("%s(%d): i2c_addr = 0x%x, addr = 0x%x, length = 0x%x.\n", __func__, __LINE__, i2c_addr, addr, length);


    if (devh->ops.i2c_write == 0)
    {
        MTL_ERROR('1',"%s: No write function for given device\n", __func__);
        return(MT_ENOSYS);
    }

    ret = devh->ops.i2c_write(devh,i2c_addr,addr,data,length);

    return(ret);
}


call_result_t MOSAL_I2C_master_receive(MOSAL_i2c_devh_t devh,  u_int8_t slv_addr,
                                       void *buffer, u_int16_t count, u_int32_t *bytes_received_p,
                                       bool sendSTOP, u_int32_t key)
{
    call_result_t ret;

    MOSAL_I2C_CHECK_RET(devh);
    
    if (devh->ops.i2c_master_receive == 0)
    {
        MTL_ERROR('1',"%s: No receive function for given device\n", __func__);
        return(MT_ENOSYS);
    }

    ret = devh->ops.i2c_master_receive(devh,slv_addr,buffer,count,bytes_received_p,sendSTOP,key);

    return(ret);
}

call_result_t MOSAL_I2C_master_transmit(MOSAL_i2c_devh_t devh, unsigned char slv_addr, void *buffer,
                                       int count, int *bytes_sent_p,  bool sendSTOP, u_int32_t *const key_p)
{
    call_result_t ret;

    MOSAL_I2C_CHECK_RET(devh);
    
    if (devh->ops.i2c_master_transmit == 0)
    {
        MTL_ERROR('1',"%s: No transmit function for given device\n", __func__);
        return(MT_ENOSYS);
    }

    ret = devh->ops.i2c_master_transmit(devh, slv_addr, buffer, count, bytes_sent_p, sendSTOP, key_p);

    return(ret);
}

call_result_t MOSAL_I2C_send_stop(MOSAL_i2c_devh_t devh, const u_int32_t key)
{
    call_result_t ret;

    MOSAL_I2C_CHECK_RET(devh);
    
    if (devh->ops.i2c_send_stop == 0)
    {
        MTL_ERROR('1',"%s: No sendSTOP function for given device\n", __func__);
        return(MT_ENOSYS);
    }

    ret = devh->ops.i2c_send_stop(devh, key);

    return(ret);
}



