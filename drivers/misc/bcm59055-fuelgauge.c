/*******************************************************************************
* Copyright 2010 Broadcom Corporation.  All rights reserved.
*
* 	@file	drivers/misc/bcm59055-fuelgauge.c
*
* Unless you and Broadcom execute a separate written software license agreement
* governing use of this software, this software is licensed to you under the
* terms of the GNU General Public License version 2, available at
* http://www.gnu.org/copyleft/gpl.html (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a license
* other than the GPL, without Broadcom's express prior written consent.
*******************************************************************************/

/*
*
*****************************************************************************
*
*  bcm59055-fuelgauge.c
*
*  PURPOSE:
*
*     This implements the driver for the Fuel Gauge on BCM59055 PMU chip.
*
*  NOTES:
*
*
*****************************************************************************/

/* ---- Include Files ---------------------------------------------------- */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/mfd/bcm590xx/core.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>
#include <mach/irqs.h>
#include <linux/stringify.h>
#include <linux/broadcom/bcm59055-fuelgauge.h>

struct bcm59055_fg {
	struct bcm590xx *bcm59055;
	int mode;
	bool enable;
};
static struct bcm59055_fg *bcm59055_fg;

int bcm59055_fg_enable(void)
{
	struct bcm590xx *bcm59055 = bcm59055_fg->bcm59055;
	u8 reg;
	int ret;
	pr_debug("Inside %s\n", __func__);
	if (bcm59055_fg->enable) {
		pr_info("%s: Fuel Gauge is already enable\n", __func__);
		return -EPERM;
	}
	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGCTRL1);
	reg |= FGHOSTEN;
	ret = bcm590xx_reg_write(bcm59055, BCM59055_REG_FGCTRL1, reg);
	if (!ret)
		bcm59055_fg->enable = true;
	return ret;
}
EXPORT_SYMBOL(bcm59055_fg_enable);

int bcm59055_fg_disable(void)
{
	struct bcm590xx *bcm59055 = bcm59055_fg->bcm59055;
	u8 reg;
	int ret;
	pr_debug("Inside %s\n", __func__);
	if (!bcm59055_fg->enable) {
			pr_info("%s: Fuel Gauge is already disable\n", __func__);
			return -EPERM;
	}
	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGCTRL1);
	reg &= ~FGHOSTEN;
	ret = bcm590xx_reg_write(bcm59055, BCM59055_REG_FGCTRL1, reg);
	if (!ret)
			bcm59055_fg->enable = false;
	return ret;
}
EXPORT_SYMBOL(bcm59055_fg_disable);

int bcm59055_fg_set_cont_mode(void)
{
	struct bcm590xx *bcm59055 = bcm59055_fg->bcm59055;
	u8 reg;
	int ret;
	pr_debug("Inside %s\n", __func__);
	if (bcm59055_fg->mode == CONTINUOUS_MODE) {
			pr_info("%s: Fuel Gauge is already in continuous mode\n",
				__func__);
			return -EPERM;
	}
	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGOPMODCTRL);
	reg &= ~FGSYNCMODE;
	ret = bcm590xx_reg_write(bcm59055, BCM59055_REG_FGOPMODCTRL, reg);
	if (!ret)
			bcm59055_fg->mode = CONTINUOUS_MODE;
	return ret;
}
EXPORT_SYMBOL(bcm59055_fg_set_cont_mode);

int bcm59055_fg_set_sync_mode(bool modulator_on)
{
	struct bcm590xx *bcm59055 = bcm59055_fg->bcm59055;
	u8 reg;
	int ret;
	pr_debug("Inside %s\n", __func__);
	if (bcm59055_fg->mode == SYNCHRONOUS_MODE) {
			pr_info("%s: Fuel Gauge is already in synchronous mode\n",
				__func__);
			return -EPERM;
	}
	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGOPMODCTRL);
	/* Set the PC2 PC1 combination..FG should on only if PC1 = 1 */
	reg |= (FGOPMODCRTL1 | FGOPMODCRTL3);
	reg &= ~(FGOPMODCRTL0 | FGOPMODCRTL2);
	reg |= FGSYNCMODE;		/* change the mode */
	if (modulator_on)
		reg |= FGMODON;
	else
		reg &= ~FGMODON;
	ret = bcm590xx_reg_write(bcm59055, BCM59055_REG_FGOPMODCTRL, reg);
	if (!ret)
			bcm59055_fg->mode = SYNCHRONOUS_MODE;
	return ret;
}
EXPORT_SYMBOL(bcm59055_fg_set_sync_mode);

int bcm59055_fg_enable_modulator(bool enable)
{
	struct bcm590xx *bcm59055 = bcm59055_fg->bcm59055;
	u8 reg;
	int ret;
	pr_debug("Inside %s\n", __func__);

	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGOPMODCTRL);

	if (enable)
		reg |= FGMODON;
	else
		reg &= ~FGMODON;
	ret = bcm590xx_reg_write(bcm59055, BCM59055_REG_FGOPMODCTRL, reg);
	return ret;
}
EXPORT_SYMBOL(bcm59055_fg_enable_modulator);

int bcm59055_fg_offset_cal(bool longcal)
{
	struct bcm590xx *bcm59055 = bcm59055_fg->bcm59055;
	u8 reg, calbit;
	int ret;
	pr_debug("Inside %s\n", __func__);
	if (longcal)
		calbit = LONGCAL;
	else
		calbit = FGCAL;
	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGCTRL2);
	reg |= calbit;
	ret = bcm590xx_reg_write(bcm59055, BCM59055_REG_FGOPMODCTRL, reg);
	while (reg & calbit) {
		pr_info("%s: Calibration is in process\n", __func__);
		reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGCTRL2);
	}
	return ret;
}
EXPORT_SYMBOL(bcm59055_fg_offset_cal);

int bcm59055_fg_1point_cal(void)
{
	struct bcm590xx *bcm59055 = bcm59055_fg->bcm59055;
	u8 reg;
	int ret;
	pr_debug("Inside %s\n", __func__);

	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGCTRL2);
	reg |= FG1PTCAL;
	ret = bcm590xx_reg_write(bcm59055, BCM59055_REG_FGOPMODCTRL, reg);
	while (reg & FG1PTCAL) {
		pr_info("%s: 1 point Calibration is in process\n", __func__);
		reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGCTRL2);
	}
	return ret;
}
EXPORT_SYMBOL(bcm59055_fg_1point_cal);

int bcm59055_fg_force_cal(void)
{
	struct bcm590xx *bcm59055 = bcm59055_fg->bcm59055;
	u8 reg;
	int ret;
	pr_debug("Inside %s\n", __func__);

	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGCTRL2);
	reg |= FGFORCECAL;
	ret = bcm590xx_reg_write(bcm59055, BCM59055_REG_FGOPMODCTRL, reg);
	while (reg & FGFORCECAL) {
		pr_info("%s: Force Calibration is in process\n", __func__);
		reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGCTRL2);
	}
	return ret;
}
EXPORT_SYMBOL(bcm59055_fg_force_cal);

int bcm59055_fg_set_comb_rate(int rate)
{
	struct bcm590xx *bcm59055 = bcm59055_fg->bcm59055;
	u8 reg;
	int ret;
	pr_debug("Inside %s\n", __func__);
	if (rate < FG_COMB_RATE_2HZ && rate > FG_COMB_RATE_16HZ) {
		pr_info("%s: Invalid rate\n", __func__);
		return -EINVAL;
	}
	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGOCICCTRL1);
	reg &= ~FG_COMB_RATE_MASK;
	reg |= rate;
	ret = bcm590xx_reg_write(bcm59055, BCM59055_REG_FGOCICCTRL1, reg);
	return ret;
}
EXPORT_SYMBOL(bcm59055_fg_set_comb_rate);

int bcm59055_fg_init_read(void)
{
	struct bcm590xx *bcm59055 = bcm59055_fg->bcm59055;
	u8 reg;
	int ret;
	pr_debug("Inside %s\n", __func__);
	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGCTRL2);
	reg |= FGFRZREAD;
	ret = bcm590xx_reg_write(bcm59055, BCM59055_REG_FGCTRL2, reg);
	udelay(2);
	return ret;
}
EXPORT_SYMBOL(bcm59055_fg_init_read);

int bcm59055_fg_read_accm(void)
{
	struct bcm590xx *bcm59055 = bcm59055_fg->bcm59055;
	u8 reg;
	int val;
	pr_debug("Inside %s\n", __func__);

	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGACCM1);
	if (!(reg & FGRDVALID)) {
		pr_info("%s: Accumulator value is invalid..try later\n", __func__);
		return -EINVAL;
	}
	val = reg << 24;
	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGACCM2);
	val |= reg << 16;
	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGACCM3);
	val |= reg << 8;
	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGACCM4);
	val |= reg;
	pr_info("%s: Accumulator value 0x%x\n", __func__, val);
	return val;
}
EXPORT_SYMBOL(bcm59055_fg_read_accm);

int bcm59055_fg_read_count(void)
{
	struct bcm590xx *bcm59055 = bcm59055_fg->bcm59055;
	u8 reg;
	int val;
	pr_debug("Inside %s\n", __func__);

	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGCNT1);
	val = reg << 8;
	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGCNT2);
	val |= reg;

	pr_info("%s: Sample count %d\n", __func__, val);
	return val;
}
EXPORT_SYMBOL(bcm59055_fg_read_count);

int bcm59055_fg_reset(void)
{
	struct bcm590xx *bcm59055 = bcm59055_fg->bcm59055;
	u8 reg;
	int ret;
	pr_debug("Inside %s\n", __func__);

	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGCTRL2);
	reg |= FGRESET;
	ret = bcm590xx_reg_write(bcm59055, BCM59055_REG_FGCTRL2, reg);
	return ret;
}
EXPORT_SYMBOL(bcm59055_fg_reset);

static int __devinit bcm59055_fg_probe(struct platform_device *pdev)
{
	struct bcm590xx *bcm59055 = dev_get_drvdata(pdev->dev.parent);
	struct bcm59055_fg *priv_data;
	u8 reg;

	pr_info("BCM59055 Fuel Gauge Driver\n");
	priv_data = kzalloc(sizeof(struct bcm59055_fg ), GFP_KERNEL);
	if (!priv_data) {
		pr_info("%s: Memory can not be allocated!!\n",
			__func__);
		return -ENOMEM;
	}
	priv_data->bcm59055 = bcm59055;
	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGCTRL1);
	if (reg & FGHOSTEN)
		priv_data->enable = true;
	else
		priv_data->enable = false;
	reg = bcm590xx_reg_read(bcm59055, BCM59055_REG_FGOPMODCTRL);
	if (reg & FGSYNCMODE)
		priv_data->mode = SYNCHRONOUS_MODE;
	else
		priv_data->mode = CONTINUOUS_MODE;
	bcm59055_fg = priv_data;
	return 0;
}



static int __devexit bcm59055_fg_remove(struct platform_device *pdev)
{
	struct bcm59055_fg *priv_data = platform_get_drvdata(pdev);
	kfree(priv_data);
	return 0;
}

struct platform_driver fg_driver = {
	.probe = bcm59055_fg_probe,
	.remove = __devexit_p(bcm59055_fg_remove),
	.driver = {
		   .name = "bcm590xx-fg",
		   }
};

/****************************************************************************
*
*  bcm59055_fg_init
*
*     Called to perform module initialization when the module is loaded
*
***************************************************************************/

static int __init bcm59055_fg_init(void)
{
	platform_driver_register(&fg_driver);
	/* initialize semaphore for ADC access control */
	return 0;
}				/* bcm59055_fg_init */

/****************************************************************************
*
*  bcm59055_fg_exit
*
*       Called to perform module cleanup when the module is unloaded.
*
***************************************************************************/

static void __exit bcm59055_fg_exit(void)
{
	platform_driver_unregister(&fg_driver);

}				/* bcm59055_fg_exit */

subsys_initcall(bcm59055_fg_init);
module_exit(bcm59055_fg_exit);

MODULE_AUTHOR("TKG");
MODULE_DESCRIPTION("BCM59055 FUEL GAUGE Driver");
