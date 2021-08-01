#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/platform_data/serial-omap.h>
#include "i2c_char.h"

static int my_open(struct inode *i, struct file *f)
{
	return 0;
}

static int my_close(struct inode *i, struct file *f)
{
	return 0;
}

static ssize_t my_read(struct file *f, char __user *buf, size_t count, loff_t *off)
{
	// TODO: Add the code here
	struct i2c_msg msg[2];
	unsigned char buff1[3];
	unsigned char buff2[4];

	msg[0].addr = 0x50;
	msg[0].buf = buff1;
	buff1[0] = 0x00;
	buff1[1] = 0x00;
	msg[0].len = 2;
	msg[0].flags = 0;

	msg[1].addr = 0x50;
	msg[1].buf = buff2;
	buff2[0] = 0x00;
	buff2[1] = 0x00;
	buff2[2] = 0x00;
	buff2[3] = 0x00;
	msg[1].len = 4;
	msg[1].flags = I2C_M_RD;
	
	// TODO: Add the code here
	printk("Printing count %d\n", count);
	i2c_transmit(&msg[0], count, 1);
	/* the last parameters tell if a recieve follows or not,
	   if a i2c_rx is followed stop bit is not generated for the tx
	*/
	i2c_receive(&msg[1], count);
	return 0;
}

static ssize_t my_write(struct file *f, const char __user *buf, size_t count, loff_t *off)
{
	struct i2c_msg msg;
	unsigned char buff[3];
	

	msg.addr = 0x50;
	msg.buf = buff;
	buff[0] = 0x01;
	buff[1] = 0x00;
	buff[2] = 0xee;
	msg.len = 3;
	msg.flags = 0;

	// TODO: Add the code here
	printk("Printing count %d\n", count);
	i2c_transmit(&msg, count, 0);
	return count;
}

static struct file_operations driver_fops =
{
	.owner = THIS_MODULE,
	.open = my_open,
	.release = my_close,
	.read = my_read,
	.write = my_write
};

int chrdrv_init(struct omap_i2c_dev *i2c_dev)
{
	static int i2c_num = 0;
	struct device *dev_ret = NULL;
	int init_result = alloc_chrdev_region(&i2c_dev->devt, 0, 1, "i2c_drv");

	if (0 > init_result)
	{
		printk(KERN_ALERT "Device Registration failed\n");
		return -1;
	}
	printk("Major Nr: %d\n", MAJOR(i2c_dev->devt));

	// TODO: Create the device file
	
	dev_ret = device_create(i2c_dev->i2c_class, NULL, i2c_dev->devt, NULL, "myi2c");



	if (!dev_ret)
	{
		unregister_chrdev_region(i2c_dev->devt, 1);
	
		printk(KERN_ALERT "Device File Creation failed failed\n");
		return -1;
	}
	printk("Is device created: %x\n", dev_ret);
	
	// TODO: Register the file_operations
	cdev_init(&i2c_dev->cdev, &driver_fops);
	
	init_result = cdev_add(&i2c_dev->cdev, i2c_dev->devt, 1);
	if (0 > init_result)
	{
		cdev_del(&i2c_dev->cdev);
		device_destroy(i2c_dev->i2c_class, i2c_dev->devt);
		unregister_chrdev_region(i2c_dev->devt, 1);

		printk(KERN_ALERT "Registering  File Operations failed\n");
		return -1;
	}
	
	return 0;
}

void chrdrv_exit(struct omap_i2c_dev *i2c_dev)
{
	// TODO: Delete the device file
	// TODO: Unregister file operations
	cdev_del(&i2c_dev->cdev);
	device_destroy(i2c_dev->i2c_class, i2c_dev->devt);
	
	// TODO: Unregister character driver
	unregister_chrdev_region(i2c_dev->devt, 1);
	
			
		
	printk(KERN_ALERT "Uneregister Char Driver\n");
}

