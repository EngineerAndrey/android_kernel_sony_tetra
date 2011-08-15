 /*
  *
  *
  *  AVS (Advaptive Voltage Scaling) driver for BCM  Kona based Chips
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License version 2 as
  * published by the Free Software Foundation.
  */

/*******************************************************************************
* Copyright 2010,2011 Broadcom Corporation.  All rights reserved.
*
*	@file	arch/arm/plat-kona/kona_avs.c
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

#include <linux/kernel.h>
#include <linux/err.h>
//#include <plat/bcm_otp.h>
#include <plat/kona_avs.h>
#include <linux/platform_device.h>


#define KONA_AVS_DEBUG

 /*Should we move this to avs_param ?? */
 #define MONITOR_VAL_MASK       0xFF
 #define MONITOR_VAL0_SHIFT     8
 #define MONITOR_VAL1_SHIFT     16
 #define MONITOR_VAL2_SHIFT     24
 #define MONITOR_VAL3_SHIFT     0

 #define MIN(x,y) (x < y)? x : y

struct avs_info
{
    u32 monitor_val0;
    u32 monitor_val1;
    u32 monitor_val2;
    u32 monitor_val3;

    u32 silicon_type;
    u32 svt_silicon_type;
    u32 lvt_silicon_type;

    u32* volt_tbl;
    struct kona_avs_pdata *pdata;
};

struct avs_info avs_info = {.silicon_type = SILICON_TYPE_SLOW,};


module_param_named(silicon_type, avs_info.silicon_type, int, S_IRUGO);
module_param_named(svt_silicon_type, avs_info.svt_silicon_type, int, S_IRUGO);
module_param_named(lvt_silicon_type, avs_info.lvt_silicon_type, int, S_IRUGO);

module_param_named(avs_mon_val0, avs_info.monitor_val0, int, S_IRUGO);
module_param_named(avs_mon_val1, avs_info.monitor_val1, int, S_IRUGO);
module_param_named(avs_mon_val2, avs_info.monitor_val2, int, S_IRUGO);
module_param_named(avs_mon_val3, avs_info.monitor_val3, int, S_IRUGO);

#if defined(KONA_AVS_DEBUG)

struct otp_val
{

    u32 val0;
    u32 val1;
};

static int otp_read(int row, struct otp_val* otp_val)
{
       if(row < 0)
            return -EINVAL;
        otp_val->val0 = (123 << MONITOR_VAL0_SHIFT) | (200 << MONITOR_VAL1_SHIFT) | (175 << MONITOR_VAL2_SHIFT) ;
        otp_val->val1 =  234;

        return 0;
}

#endif

u32 kona_avs_get_solicon_type()
{
    return avs_info.silicon_type;
}
EXPORT_SYMBOL(kona_avs_get_solicon_type);

u32* kona_avs_get_volt_table()
{
    return avs_info.volt_tbl;
}
EXPORT_SYMBOL(kona_avs_get_volt_table);

int kona_avs_get_mon_val(struct avs_info* avs_inf_ptr)
{
    struct otp_val  otp_val;
    int ret;
    ret = otp_read(avs_inf_ptr->pdata->otp_row,&otp_val);
    if(!ret)
    {

        avs_inf_ptr->monitor_val0 = (otp_val.val0 >> MONITOR_VAL0_SHIFT) & MONITOR_VAL_MASK;
        avs_inf_ptr->monitor_val1 = (otp_val.val0 >> MONITOR_VAL1_SHIFT) & MONITOR_VAL_MASK;
        avs_inf_ptr->monitor_val2 = (otp_val.val0 >> MONITOR_VAL2_SHIFT) & MONITOR_VAL_MASK;
        avs_inf_ptr->monitor_val3 = (otp_val.val1 >> MONITOR_VAL3_SHIFT) & MONITOR_VAL_MASK;
    }
    return ret;
}

static u32 kona_avs_get_svt_type(struct avs_info* avs_inf_ptr)
{
    int i;

    int svt_pmos_inx = -1;
    int svt_nmos_inx = -1;

    struct kona_avs_pdata *pdata =  avs_inf_ptr->pdata;

    for(i = 0; i < pdata->pmos_bin_size; i++)
    {
        if(avs_inf_ptr->monitor_val3 >= pdata->svt_pmos_bin[i] &&
                avs_inf_ptr->monitor_val3 < pdata->svt_pmos_bin[i+1])
        {
           svt_pmos_inx = i;
           break;
        }
    }

    for(i = 0; i < pdata->nmos_bin_size; i++)
    {
        if(avs_inf_ptr->monitor_val2 >= pdata->svt_nmos_bin[i] &&
                avs_inf_ptr->monitor_val2 < pdata->svt_nmos_bin[i+1])
        {
           svt_nmos_inx = i;
           break;
        }
    }

    if(svt_nmos_inx == -1 || svt_pmos_inx == -1)
        return SILICON_TYPE_SLOW;

    return pdata->svt_silicon_type_lut[svt_pmos_inx][svt_nmos_inx];
}



static u32 kona_avs_get_lvt_type(struct avs_info* avs_inf_ptr)
{
    int i;

    int lvt_pmos_inx = -1;
    int lvt_nmos_inx = -1;

    struct kona_avs_pdata *pdata =  avs_inf_ptr->pdata;

    for(i = 0; i < pdata->pmos_bin_size; i++)
    {
        if(avs_inf_ptr->monitor_val1 >= pdata->lvt_pmos_bin[i] &&
                avs_inf_ptr->monitor_val0 < pdata->lvt_pmos_bin[i+1])
        {
           lvt_pmos_inx = i;
           break;
        }
    }

    for(i = 0; i < pdata->nmos_bin_size; i++)
    {
        if(avs_inf_ptr->monitor_val0 >= pdata->lvt_nmos_bin[i] &&
                avs_inf_ptr->monitor_val0 < pdata->lvt_nmos_bin[i+1])
        {
           lvt_nmos_inx = i;
           break;
        }
    }

    if(lvt_nmos_inx == -1 || lvt_pmos_inx == -1)
        return SILICON_TYPE_SLOW;

    return pdata->lvt_silicon_type_lut[lvt_pmos_inx][lvt_nmos_inx];
}

static int kona_avs_drv_probe(struct platform_device *pdev)
{
    int ret = 0;
	struct kona_avs_pdata *pdata = pdev->dev.platform_data;

	pr_info("%s\n", __func__);

	if(!pdata)
	{
		pr_info("%s : invalid paltform data !!\n", __func__);
		ret = -EPERM;
		goto error;
	}

	avs_info.pdata = pdata;

    ret = kona_avs_get_mon_val(&avs_info);
    if(!ret)
        goto error;

    avs_info.svt_silicon_type = kona_avs_get_svt_type(&avs_info);
    avs_info.lvt_silicon_type = kona_avs_get_lvt_type(&avs_info);

    avs_info.silicon_type = MIN(avs_info.lvt_silicon_type,avs_info.svt_silicon_type);
    avs_info.volt_tbl = pdata->volt_table[avs_info.silicon_type];

    if(pdata->silicon_type_notify)
        pdata->silicon_type_notify(avs_info.silicon_type);
error:
	return ret;
}

static int __devexit kona_avs_drv_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver kona_avs_drv =
{
	.probe  =  kona_avs_drv_probe,
	.remove = __devexit_p(kona_avs_drv_remove),
	.driver = { .name =	 "kona-avs",},
};

static int __init kona_avs_drv_init(void)
{
	return platform_driver_register(&kona_avs_drv);
}

fs_initcall(kona_avs_drv_init);
static void __exit kona_avs_drv_exit(void)
{
	platform_driver_unregister(&kona_avs_drv);
}

module_exit(kona_avs_drv_exit);

MODULE_ALIAS("platform:kona_avs_drv");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Adaptive Voltage Scaling driver for Broadcom Kona based Chipsets");
