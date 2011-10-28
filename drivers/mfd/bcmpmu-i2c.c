/*****************************************************************************
*  Copyright 2001 - 2008 Broadcom Corporation.  All rights reserved.
*
*  Unless you and Broadcom execute a separate written software license
*  agreement governing use of this software, this software is licensed to you
*  under the terms of the GNU General Public License version 2, available at
*  http://www.gnu.org/licenses/old-license/gpl-2.0.html (the "GPL").
*
*  Notwithstanding the above, under no circumstances may you combine this
*  software in any way with any other Broadcom software provided under a
*  license other than the GPL, without Broadcom's express prior written
*  consent.
*
*****************************************************************************/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include <linux/mfd/bcmpmu.h>

struct bcmpmu_i2c {
	struct bcmpmu *bcmpmu;
	struct mutex i2c_mutex;
	struct i2c_client *i2c_client;
	struct i2c_client *i2c_client1;
	int pagesize;
};

static int bcmpmu_i2c_read_device(struct bcmpmu *bcmpmu, int reg, unsigned int *val, unsigned int msk)
{
	struct bcmpmu_reg_map map;
	int err;
	struct bcmpmu_i2c *acc = (struct bcmpmu_i2c *)bcmpmu->accinfo;

	if (reg >= PMU_REG_MAX) return -ENODEV;
	map = bcmpmu->regmap[reg];
	if ((map.addr == 0) && (map.mask == 0))  return -ENODEV;

	mutex_lock(&acc->i2c_mutex);
	if (map.map == 0)
		err = i2c_smbus_read_byte_data(acc->i2c_client, map.addr);
	else if (map.map == 1)
		err = i2c_smbus_read_byte_data(acc->i2c_client1, map.addr);
	else err = -ENODEV;
	mutex_unlock(&acc->i2c_mutex);

	if (err < 0) return err;
	err = err & msk;
	err = err & map.mask;
	*val = err;
	return 0;
}

static int bcmpmu_i2c_write_device(struct bcmpmu *bcmpmu, int reg, unsigned int value, unsigned int msk)
{
	struct bcmpmu_reg_map map;
	int err;
	struct bcmpmu_i2c *acc = (struct bcmpmu_i2c *)bcmpmu->accinfo;

	if (reg >= PMU_REG_MAX) return -ENODEV;
	map = bcmpmu->regmap[reg];
	if ((map.addr == 0) && (map.mask == 0))  return -ENODEV;

	mutex_lock(&acc->i2c_mutex);
	if (map.map == 0)
		err = i2c_smbus_read_byte_data(acc->i2c_client, map.addr);
	else if (map.map == 1)
		err = i2c_smbus_read_byte_data(acc->i2c_client1, map.addr);
	else err = -ENODEV;
	if (err < 0) goto err;

	err = err & ~msk;
	err = err & ~map.mask;
	value = value | err;

	if (map.map == 0)
		err = i2c_smbus_write_byte_data(acc->i2c_client, map.addr, value);
	else if (map.map == 1)
		err = i2c_smbus_write_byte_data(acc->i2c_client1, map.addr, value);
	else err = -ENODEV;
err:
	mutex_unlock(&acc->i2c_mutex);
	return err;
}

static int bcmpmu_i2c_read_device_direct(struct bcmpmu *bcmpmu, int map, int addr, unsigned int *val, unsigned int msk)
{
	int err;
	struct bcmpmu_i2c *acc = (struct bcmpmu_i2c *)bcmpmu->accinfo;
	if ((addr == 0) && (msk == 0))  return -ENODEV;

	mutex_lock(&acc->i2c_mutex);
	if (map == 0)
		err = i2c_smbus_read_byte_data(acc->i2c_client, addr);
	else if (map == 1)
		err = i2c_smbus_read_byte_data(acc->i2c_client1, addr);
	else err = -ENODEV;
	mutex_unlock(&acc->i2c_mutex);

	if (err < 0) return err;
	err = err & msk;
	*val = err;
	return 0;
}

static int bcmpmu_i2c_write_device_direct(struct bcmpmu *bcmpmu, int map, int addr, unsigned int val, unsigned int msk)
{
	int err;
	u8 value = (u8)val;
	struct bcmpmu_i2c *acc = (struct bcmpmu_i2c *)bcmpmu->accinfo;
	if ((addr == 0) && (msk == 0))  return -ENODEV;

	mutex_lock(&acc->i2c_mutex);
	if (map == 0)
		err = i2c_smbus_read_byte_data(acc->i2c_client, addr);
	else if (map == 1)
		err = i2c_smbus_read_byte_data(acc->i2c_client1, addr);
	else err = -ENODEV;
	if (err < 0) goto err;

	err = err & ~msk;
	value = value | err;

	if (map == 0)
		err = i2c_smbus_write_byte_data(acc->i2c_client, addr, value);
	else if (map == 1)
		err = i2c_smbus_write_byte_data(acc->i2c_client1, addr, value);
	else err = -ENODEV;
err:
	mutex_unlock(&acc->i2c_mutex);
	return err;
}

static int bcmpmu_i2c_read_device_direct_bulk(struct bcmpmu *bcmpmu, int map, int addr, unsigned int *val, int len)
{
	int err;
	struct bcmpmu_i2c *acc = (struct bcmpmu_i2c *)bcmpmu->accinfo;
	u8 *uval = (u8 *)val;
	int i;
	
	if (addr + len > acc->pagesize) return -ENODEV;

	mutex_lock(&acc->i2c_mutex);
	if (map == 0)
		err = i2c_smbus_read_i2c_block_data(acc->i2c_client, addr, len, uval);
	else if (map == 1)
		err = i2c_smbus_read_i2c_block_data(acc->i2c_client1, addr, len, uval);
	else err = -ENODEV;
	mutex_unlock(&acc->i2c_mutex);

	for (i = len; i > 0; i--)
		val[i-1] = (unsigned int)uval[i-1];

	if (err < 0) return err;
	return 0;
}

static int bcmpmu_i2c_write_device_direct_bulk(struct bcmpmu *bcmpmu, int map, int addr, unsigned int *val, int len)
{
	int err;
	struct bcmpmu_i2c *acc = (struct bcmpmu_i2c *)bcmpmu->accinfo;
	u8 *uval = (u8 *)val;
	int i;
	
	if (addr + len > acc->pagesize) return -ENODEV;

	for (i = 0; i < len; i++)
		uval[i] = (u8)val[i];

	mutex_lock(&acc->i2c_mutex);
	if (map == 0)
		err = i2c_smbus_write_i2c_block_data(acc->i2c_client, addr, len, uval);
	else if (map == 1)
		err = i2c_smbus_write_i2c_block_data(acc->i2c_client1, addr, len, uval);
	else err = -ENODEV;
	mutex_unlock(&acc->i2c_mutex);

	if (err < 0) return err;
	return 0;
}


static struct platform_device bcmpmu_core_device = {
	.name 			= "bcmpmu_core",
	.id			= -1,
	.dev.platform_data 	= NULL,
};

static int bcmpmu_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct bcmpmu *bcmpmu;
	int ret = 0;
	struct bcmpmu_platform_data *pdata;
	struct i2c_client *clt;
	struct i2c_adapter *adp;
	struct bcmpmu_i2c *bcmpmu_i2c;
		
	pdata = (struct bcmpmu_platform_data *)i2c->dev.platform_data;
	
	printk(KERN_INFO "%s called\n", __func__);
	
	bcmpmu = kzalloc(sizeof(struct bcmpmu), GFP_KERNEL);
	if (bcmpmu == NULL) {
		printk(KERN_ERR "%s: failed to alloc mem.\n", __func__);
		kfree(i2c);
		ret = -ENOMEM;
		goto err;
	}

	bcmpmu_i2c = kzalloc(sizeof(struct bcmpmu_i2c), GFP_KERNEL);
	if (bcmpmu_i2c == NULL) {
		printk(KERN_ERR "%s: failed to alloc mem.\n", __func__);
		ret = -ENOMEM;
		goto err;
	}

	i2c_set_clientdata(i2c, bcmpmu);
	bcmpmu->dev = &i2c->dev;
	bcmpmu_i2c->i2c_client = i2c;

	adp = i2c_get_adapter(pdata->i2c_adapter_id);
	clt = i2c_new_device(adp, pdata->i2c_board_info_map1);
	if (!clt)
		printk(KERN_ERR "%s: add new device for map1 failed\n", __func__);

	bcmpmu_i2c->i2c_client1 = clt;
	mutex_init(&bcmpmu_i2c->i2c_mutex);
	
	bcmpmu->read_dev = bcmpmu_i2c_read_device;
	bcmpmu->write_dev = bcmpmu_i2c_write_device;
	bcmpmu->read_dev_drct = bcmpmu_i2c_read_device_direct;
	bcmpmu->write_dev_drct = bcmpmu_i2c_write_device_direct;
	bcmpmu->read_dev_bulk = bcmpmu_i2c_read_device_direct_bulk;
	bcmpmu->write_dev_bulk = bcmpmu_i2c_write_device_direct_bulk;
	bcmpmu->pdata = pdata;
	bcmpmu_i2c->pagesize = pdata->i2c_pagesize;

	bcmpmu->accinfo = bcmpmu_i2c;

	bcmpmu_core_device.dev.platform_data = bcmpmu;
	platform_device_register(&bcmpmu_core_device);

	return ret;

err:
	kfree(bcmpmu->accinfo);
	kfree(bcmpmu);
	return ret;
}

static int bcmpmu_i2c_remove(struct i2c_client *i2c)
{
	struct bcmpmu *bcmpmu = i2c_get_clientdata(i2c);

	platform_device_unregister(&bcmpmu_core_device);
	kfree(bcmpmu->accinfo);
	kfree(bcmpmu);

	return 0;
}

static const struct i2c_device_id bcmpmu_i2c_id[] = {
       { "bcmpmu", 0 },
       { }
};
MODULE_DEVICE_TABLE(i2c, bcmpmu_i2c_id);


static struct i2c_driver bcmpmu_i2c_driver = {
	.driver = {
		   .name = "bcmpmu",
		   .owner = THIS_MODULE,
	},
	.probe = bcmpmu_i2c_probe,
	.remove = bcmpmu_i2c_remove,
	.id_table = bcmpmu_i2c_id,
};

static int __init bcmpmu_i2c_init(void)
{
	return i2c_add_driver(&bcmpmu_i2c_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(bcmpmu_i2c_init);

static void __exit bcmpmu_i2c_exit(void)
{
	i2c_del_driver(&bcmpmu_i2c_driver);
}
module_exit(bcmpmu_i2c_exit);

MODULE_DESCRIPTION("I2C support for BCM590XX PMIC");
MODULE_LICENSE("GPL");
