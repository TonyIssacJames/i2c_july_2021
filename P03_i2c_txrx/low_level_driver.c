#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/platform_data/i2c-omap.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include "i2c_char.h"

/* 
 * Assignment for sending the multiple bytes and reading
 * the Eeprom contents
 */

#define OMAP_I2C0_BASE      0x44E0B000
#define OMAP_I2C0_REG_SIZE  0x1000        //4KB

static struct omap_i2c_dev i2c_dev;

inline void omap_i2c_write_reg(struct omap_i2c_dev *i2c_dev,
				      int reg, u16 val)
{
	__raw_writew(val, i2c_dev->base + i2c_dev->regs[reg]);
}

inline u16 omap_i2c_read_reg(struct omap_i2c_dev *i2c_dev, int reg)
{
	return __raw_readw(i2c_dev->base + i2c_dev->regs[reg]);
}

inline void omap_i2c_ack_stat(struct omap_i2c_dev *dev, u16 stat)
{
	omap_i2c_write_reg(dev, OMAP_I2C_STAT_REG, stat);
}

/*
 * Waiting on Bus Busy
 */
int omap_i2c_wait_for_bb(struct omap_i2c_dev *dev)
{
	unsigned long timeout;

	timeout = jiffies + OMAP_I2C_TIMEOUT;
	while (omap_i2c_read_reg(dev, OMAP_I2C_STAT_REG) & OMAP_I2C_STAT_BB) {
		if (time_after(jiffies, timeout)) {
			printk("timeout waiting for bus ready\n");
			return -ETIMEDOUT;
		}
		msleep(1);
	}

	return 0;
}

void flush_fifo(struct omap_i2c_dev *dev)
{
	unsigned long timeout;
	u32 status;

	timeout = jiffies + OMAP_I2C_TIMEOUT;
	while ((status = omap_i2c_read_reg(dev, OMAP_I2C_STAT_REG)) & OMAP_I2C_STAT_RRDY) {
		omap_i2c_read_reg(dev, OMAP_I2C_DATA_REG);
		omap_i2c_ack_stat(dev, OMAP_I2C_STAT_RRDY);
		if (time_after(jiffies, timeout)) {
			printk(KERN_ALERT "timeout waiting for bus ready\n");
			break;
		}
		msleep(1);
	}
}

u16 wait_for_event(struct omap_i2c_dev *dev)
{
	unsigned long timeout = jiffies + OMAP_I2C_TIMEOUT;
	u16 status;

	while (!((status = omap_i2c_read_reg(dev, OMAP_I2C_STAT_REG)) & 
				(OMAP_I2C_STAT_ROVR | OMAP_I2C_STAT_XUDF | 
				 OMAP_I2C_STAT_XRDY | OMAP_I2C_STAT_RRDY | 
				 OMAP_I2C_STAT_ARDY | OMAP_I2C_STAT_NACK | 
				 OMAP_I2C_STAT_AL))) {
		if (time_after(jiffies, timeout)) {
			printk("time-out waiting for event\n");
			omap_i2c_write_reg(dev, OMAP_I2C_STAT_REG, 0XFFFF);
			return 0;
		}
		mdelay(1);
	}
	return status;
}

int i2c_transmit(struct i2c_msg *msg, size_t count, int rx_follows)
{
	/* 
	 * To be copied from previous assignment. 
	 * Make sure to send only 2 bytes 
	 * Byte 0 - Eeprom address high
	 * Byte 1 - Eerprom address low
	 * So, in order to read the eeprom from address 0x0060, byte 0 = 0x00, byte 1 = 0x60
	 */
	//http://www.merkles.com/Using_I2C/6_i2cWriteRead.html
	/* Set the threshold to 0 and clear buffers */
	u16 status, cnt = msg->len, w;
	int i2c_error = 0;
	/* Initialize the loop variable */
	int k = 7;
	int tb = 0;

	//Set the TX FIFO Threshold to 0 and clear the FIFO's
	omap_i2c_write_reg(&i2c_dev, OMAP_I2C_BUF_REG, 0);
	//TODO: Update the slave addresss register with 0x50
	omap_i2c_write_reg(&i2c_dev, OMAP_I2C_SA_REG, msg->addr);
	//TODO: Update the count register with 1
	omap_i2c_write_reg(&i2c_dev, OMAP_I2C_CNT_REG, cnt);

	printk("##### Sending %d byte(s) on the I2C bus ####\n", cnt);

	/*
	 * TODO: Update the configuration register (OMAP_I2C_CON_REG) to start the
	 * transaction with master mode and direction as transmit. Also, enable the
	 * I2c module and set the start and stop bits.
	 * The naming convention for the bits is OMAP_I2C_<REG NAME>_<BIT NAME>
	 * So, for start bit, the macro is OMAP_I2C_CON_STT. Check I2c_char.h for other bits
	 */
	w = OMAP_I2C_CON_MST | OMAP_I2C_CON_EN| OMAP_I2C_CON_STT| OMAP_I2C_CON_STP;
	
	if(!rx_follows) // if recieve follows this transmission then dont generate stop
	{
		w |= OMAP_I2C_CON_STP;
	}
	
	if(!(msg->flags & I2C_M_RD))//if not read, set transmit bit in config register
	{
		w |= OMAP_I2C_CON_TRX; 		
	}


	omap_i2c_write_reg(&i2c_dev, OMAP_I2C_CON_REG, w);

	tb = 0; //transmitted bytes
	while (k--)
	{
		printk("-------------- k = %d, tb = %d\n", k, tb);
		// Wait for status to be updated
		status = wait_for_event(&i2c_dev);
		if (status == 0) {
			i2c_error = -ETIMEDOUT;
			goto wr_exit;
		}
		//TODO: Check the status to verify if XRDY is received
		//TODO: Update the data register with data to be transmitted
		//TODO: Clear the XRDY event with omap_i2c_ack_stat
		if (status & OMAP_I2C_STAT_XRDY) {
			printk("Got XRDY\n");
			omap_i2c_write_reg(&i2c_dev, OMAP_I2C_DATA_REG, msg->buf[tb]);
			omap_i2c_ack_stat(&i2c_dev, OMAP_I2C_STAT_XRDY);
			tb++; //increse transmitted bytes
			continue;   
		}

		//TODO: Check the status to verify if ARDY is received
		//TODO: Clear the XRDY event with omap_i2c_ack_stat
		if (status& OMAP_I2C_STAT_ARDY) {	
			printk("Got ARDY\n");
			omap_i2c_ack_stat(&i2c_dev, OMAP_I2C_STAT_ARDY);
			break;
		}
	}
	if (k <= 0) {
		printk("TX Timed out\n");
		i2c_error = -ETIMEDOUT;
	}
wr_exit:
	printk("Why it reached the exit\n");
	flush_fifo(&i2c_dev);
	omap_i2c_write_reg(&i2c_dev, OMAP_I2C_STAT_REG, 0XFFFF);
	return i2c_error;
}

int i2c_receive(struct i2c_msg *msg, size_t count)
{	
	/* Set the threshold to 0 and clear buffers */
	u16 status, cnt = msg->len, w;
	int i2c_error = 0;
	/* Initialize the loop variable */
	int k = 7;
	u8 a = 0;
	int rb = 0; //recieved bytes

	ENTER();

	//Set the FIFO Threshold to 0 and clear the FIFO's
	omap_i2c_write_reg(&i2c_dev, OMAP_I2C_BUF_REG, 0);
	//TODO: Update the slave addresss register with 0x50
	omap_i2c_write_reg(&i2c_dev, OMAP_I2C_SA_REG, msg->addr);
	//TODO: Update the count register
	omap_i2c_write_reg(&i2c_dev, OMAP_I2C_CNT_REG, cnt);
	
	printk("##### Receiving %d byte(s) over the I2C bus ####\n", cnt);


	/*
	 * TODO: Update the configuration register (OMAP_I2C_CON_REG) to start the
	 * transaction with master mode and direction as receive. Also, enable the
	 * I2c module and set the start and stop bits.
	 * The naming convention for the bits is OMAP_I2C_<REG NAME>_<BIT NAME>
	 * So, for start bit, the macro is OMAP_I2C_CON_STT. Check I2c_char.h for other bits
	 */
	w = OMAP_I2C_CON_MST | OMAP_I2C_CON_EN| OMAP_I2C_CON_STT ;
	

	if(!(msg->flags & I2C_M_RD))//if not read, set transmit bit in config register
	{
		w |= OMAP_I2C_CON_TRX; 		
	}


	omap_i2c_write_reg(&i2c_dev, OMAP_I2C_CON_REG, w);
	
	rb = 0;

	while (k--) {
		printk("-------------- k = %d, rb = %d\n", k, rb);
		// Wait for status to be updated
		status = wait_for_event(&i2c_dev);
		if (status == 0) {
			i2c_error = -ETIMEDOUT;
			goto wr_exit;
		}
		//TODO: Check the status to verify if RRDY is received
		//TODO: Read the data register contents into variable 'a'
		//TODO: Clear the RRDY event with omap_i2c_ack_stat
		if (status& OMAP_I2C_STAT_RRDY) {
			printk("Got RRDY\n");
			a = 0;
			a = omap_i2c_read_reg(&i2c_dev, OMAP_I2C_DATA_REG);
			printk("Received %x\n", a);
			omap_i2c_ack_stat(&i2c_dev, OMAP_I2C_STAT_RRDY);
			rb++;
			continue;   
		}

		//TODO: Check the status to verify if ARDY is received
		//TODO: Clear the ARDY event with omap_i2c_ack_stat
		if (status & OMAP_I2C_STAT_ARDY) {	
			printk("Got ARDY\n");
			omap_i2c_ack_stat(&i2c_dev, OMAP_I2C_STAT_ARDY);
			break;
		}
	}
	if (k <= 0) {
		printk("TX Timed out\n");
		i2c_error = -ETIMEDOUT;
	}
wr_exit:
	flush_fifo(&i2c_dev);
	omap_i2c_write_reg(&i2c_dev, OMAP_I2C_STAT_REG, 0XFFFF);
	return i2c_error;

	return 0;
}

static void omap_i2c_set_speed(struct omap_i2c_dev *dev)
{
	u16 psc = 0;
	unsigned long fclk_rate = 48000;
	unsigned long internal_clk = 24000; // 24MHz Recommended as per TRM
	unsigned long scl, scll, sclh;
	
	/* Compute prescaler divisor */
	psc = fclk_rate / internal_clk;
	//TODO: Update the prescalar register with psc - 1
	omap_i2c_write_reg(dev, OMAP_I2C_PSC_REG, psc-1);
	// Hard coding the speed to 400KHz
	dev->speed = 400;
	scl = internal_clk / dev->speed;
	// 50% duty cycle
        scl /= 2;
        scll = scl - 7;
        sclh = scl - 5;

	//TODO: Update the SCL low and high registers as per above calculations
	omap_i2c_write_reg(dev, OMAP_I2C_SCLL_REG, scll);
	omap_i2c_write_reg(dev, OMAP_I2C_SCLH_REG, sclh);
}

int omap_i2c_init(struct omap_i2c_dev *dev)
{
	omap_i2c_set_speed(dev);
	omap_i2c_write_reg(dev, OMAP_I2C_CON_REG, 0);

	/* Take the I2C module out of reset: */
	omap_i2c_write_reg(dev, OMAP_I2C_CON_REG, OMAP_I2C_CON_EN);
	
	// TODO: Update the 'iestate' field with desired events such as XRDY
	dev->iestate = OMAP_I2C_IE_XRDY|OMAP_I2C_IE_ARDY|\
				   OMAP_I2C_IE_RRDY| OMAP_I2C_IE_NACK;
	// TODO: Update the OMAP_I2C_IE_REG register
	omap_i2c_write_reg(dev, OMAP_I2C_IE_REG, dev->iestate);
	
	flush_fifo(dev);
	omap_i2c_write_reg(dev, OMAP_I2C_STAT_REG, 0XFFFF);
	omap_i2c_wait_for_bb(dev);

	return 0;
}

static int __init omap_i2c_init_driver(void)
{
	int ret;
	/* Char interface related initialization */
	i2c_dev.i2c_class = NULL;
	// TODO: Create the class with name i2cdrv
	i2c_dev.i2c_class = class_create(THIS_MODULE, "i2cdrv");
	
	if(!i2c_dev.i2c_class)
	{
		printk(KERN_ALERT "Creating class i2cdrv failed\n");
		return -1;
	}
	// TODO: Initialize the character driver interface
	ret =  chrdrv_init(&i2c_dev);
	if(ret < 0)
	{
		class_destroy(i2c_dev.i2c_class);
		printk(KERN_ALERT "Initialize the character driver interface failed\n");
		return -1;
	}
	/*
	 * TODO: Get the virtual address for the i2c0 base address and store it
	 * in 'base' field of omap_i2c_dev. 
	 * Use API void __iomem* ioremap((resource_size_t offset, unsigned long size)
	*/
	i2c_dev.base = ioremap(OMAP_I2C0_BASE, OMAP_I2C0_REG_SIZE);
	
	i2c_dev.regs = (u8 *)reg_map_ip_v2;
	omap_i2c_init(&i2c_dev);

	return 0;
}

static void __exit omap_i2c_exit_driver(void)
{
	// TODO: De-initialize the character driver interface
	chrdrv_exit(&i2c_dev);
	// TODO: Delete the i2cdrv class
	class_destroy(i2c_dev.i2c_class);
}

module_init(omap_i2c_init_driver);
module_exit(omap_i2c_exit_driver);

MODULE_AUTHOR("Embitude Trainings <info@embitude.in>");
MODULE_DESCRIPTION("Low level I2C driver");
MODULE_LICENSE("GPL");
