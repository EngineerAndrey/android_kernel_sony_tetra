/*
 *  linux/drivers/power/bcm59055_batter.c - Broadcom BCM59055 Battery driver
 *
 *  Copyright (C) 2010 Broadcom, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <linux/slab.h>

#include <linux/mfd/bcm590xx/bcm59055_A0.h>

#include <linux/mfd/bcm590xx/core.h>
#include <linux/mfd/bcm590xx/pmic.h>

#include <linux/workqueue.h>

struct bcm59055_battery_data {
	// struct power_supply battery;
	// struct power_supply ac;

	struct bcm590xx *bcm590xx;
	struct power_supply battery;
	struct power_supply wall;
	// struct power_supply usb;

	enum power_supply_type power_src;
	struct delayed_work charger_insert_wq;
	struct delayed_work batt_lvl_wq;
#ifdef CONFIG_HAS_WAKELOCK
	// struct wake_lock batt_monitor_wl;
	// struct wake_lock usb_charger_wl;
#endif
};

struct voltage_percentage   
{
    unsigned int voltage ;
    unsigned int percentage ;
} ; 


#define MAX_VOLTAGES   8 

struct voltage_percentage vp_table[MAX_VOLTAGES] = 
{
    { 3200 , 10 },
    { 3800 , 20 },
    { 3900 , 30 },
    { 3950 , 40 },
    { 4000 , 50 },
    { 4100 , 70 },
    { 4150 , 80 },
    { 4200 , 100 },
} ;

/* Battery values exported in /sys/class/power_supply/battery */
#define BATT_TECHNOLOGY	 (POWER_SUPPLY_TECHNOLOGY_UNKNOWN)
#define BATT_VOLT	(1200)
#define BATT_HEALTH	(1)	/* Good */
#define BATT_PRESENT	(1)
#define BATT_STATUS	(4)	/* Full */
#define BATT_TEMP	(58)

/* AC power values exported in /sys/class/power_supply/ac */
#define AC_ONLINE	(1)

unsigned int g_battery_percentage = 0 ;

static enum power_supply_property bcm59055_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static enum power_supply_property bcm59055_wall_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static int bcm59055_get_battery_voltage_per(struct bcm590xx *bcm590xx, unsigned int *percentage )  ;

static int bcm59055_wall_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = AC_ONLINE;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int bcm59055_get_battery_voltage_per(struct bcm590xx *bcm590xx, unsigned int *percentage ) 
{
	unsigned int regval = 0 ;
	unsigned int regval1 = 0 ;
	unsigned int millivolt_mul = 1000 ;
	unsigned int i = 0 ;

    regval = bcm590xx_reg_read(bcm590xx,BCM59055_REG_ADCCTRL3 ) ;

	if ( !(regval & BCM59055_INVALID_ADCVAL ) )
	{
		regval  = regval & BCM59055_REG_ADCCTRL3_VALID_BITS ;
		 
        regval1 = bcm590xx_reg_read(bcm590xx,BCM59055_REG_ADCCTRL4 ) ;

        regval = ( regval << 8 ) | ( regval1 ) ;

    	regval = ( regval * millivolt_mul * 48) / ( 1024 * 10 ) ;

		i = 0 ;
		while ( i < MAX_VOLTAGES ) 
		{
            if ( regval <= vp_table[i].voltage ) 
            {
                *percentage = vp_table[i].percentage ;
				break ;
            }
            i++ ;
		}
    }
	return 0 ;    
}


static void bcm59055_batt_lvl_wq(struct work_struct *work)
{
    struct bcm59055_battery_data *battery_data = container_of(work, struct bcm59055_battery_data, batt_lvl_wq.work);

    bcm59055_get_battery_voltage_per(battery_data->bcm590xx, &g_battery_percentage) ;

	schedule_delayed_work(&battery_data->batt_lvl_wq, msecs_to_jiffies(30000));
}


static int bcm59055_battery_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{
	int ret = 0;

	printk(" bcm59055_battery_get_property called with %d, battery_percentage is %d \n", psp, g_battery_percentage ) ;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = BATT_STATUS;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = BATT_HEALTH;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = BATT_PRESENT;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = BATT_TECHNOLOGY;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = g_battery_percentage ;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = BATT_TEMP;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = BATT_VOLT;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

void bcm59055_initialize_charging( struct bcm590xx *bcm59055 )
{
    bcm590xx_reg_write(bcm59055, BCM59055_REG_MBCCTRL6, 0x08 ) ;
    bcm590xx_reg_write(bcm59055, BCM59055_REG_MBCCTRL8, 0x07 ) ;
    bcm590xx_reg_write(bcm59055, BCM59055_REG_MBCCTRL9, 0x01 ) ;
}

void bcm59055_start_charging(struct bcm590xx *bcm59055 )
{
    bcm590xx_reg_write(bcm59055, BCM59055_REG_MBCCTRL3, 5 ) ;
    bcm590xx_reg_write_slave1(0, 0x44) ;
    return ;
}

static void bcm59055_power_isr(int intr, void *data)
{
	struct bcm59055_battery_data *battery_data = data;
	struct bcm590xx *bcm59055 = battery_data->bcm590xx;
	struct bcm590xx_battery_pdata *pdata = bcm59055->pdata->battery_pdata;

	switch (intr) {
	case BCM59055_IRQID_INT2_CHGINS:
		printk("%s Wall Charger inserted interrupt \n", __func__);
		if (pdata && pdata->can_start_charging && !pdata->can_start_charging(NULL))
			printk ("charging not started\n");
		else {
			printk ("charging started\n");
			bcm59055_start_charging(bcm59055);
		}
		break;

	case BCM59055_IRQID_INT2_CHGRM:
		printk("%s Wall Charger REMOVED interrupt \n", __func__);
        bcm590xx_reg_write_slave1(0, 0x46) ;
		break;

	}
}

int bcm59055_init_charger(struct bcm59055_battery_data *battery_data)
{
	struct bcm590xx *bcm59055 = battery_data->bcm590xx;

	printk("######## Init charging called \n" ) ;

	bcm59055_initialize_charging(bcm59055 ) ;

    bcm590xx_request_irq(bcm59055, BCM59055_IRQID_INT2_CHGINS, true, bcm59055_power_isr, battery_data);	/*EOC charge interrupt */
	bcm590xx_request_irq(bcm59055, BCM59055_IRQID_INT2_CHGRM, true, bcm59055_power_isr, battery_data);	/*WAC connected interrupt */
    return 0 ;
}

static int bcm59055_battery_probe(struct platform_device *pdev)
{
	int ret;
	struct bcm59055_battery_data *battery_data;
	struct bcm590xx *bcm59055 = dev_get_drvdata(pdev->dev.parent);  // From debugger make sure we get this information correctly.
	struct bcm590xx_battery_pdata *battery_pdata;

	battery_pdata = bcm59055->pdata->battery_pdata;

	battery_data = kzalloc(sizeof(struct bcm59055_battery_data), GFP_KERNEL);
	if (battery_data == NULL) {
		printk(KERN_ERR"%s : Failed to allocate memory for bcm59055_battery_data\n", __func__);
		ret = -ENOMEM;
		goto err_data_alloc;
	}

	battery_data->bcm590xx = bcm59055;

	// INIT_DELAYED_WORK(&battery_data->charger_insert_wq, bcm59055_charger_wq); Implement bcm59055_charger_wq
	INIT_DELAYED_WORK(&battery_data->batt_lvl_wq, bcm59055_batt_lvl_wq); 

	platform_set_drvdata(pdev, battery_data);

	bcm59055_init_charger(battery_data) ;

	battery_data->battery.name = "bcm59055-battery";
	battery_data->battery.type = POWER_SUPPLY_TYPE_BATTERY;
	battery_data->battery.properties = bcm59055_battery_props;
	battery_data->battery.num_properties = ARRAY_SIZE(bcm59055_battery_props);
	battery_data->battery.get_property = bcm59055_battery_get_property;

	battery_data->wall.name = "bcm59055-wall";
	battery_data->wall.type = POWER_SUPPLY_TYPE_MAINS;
	battery_data->wall.properties = bcm59055_wall_props;
	battery_data->wall.num_properties = ARRAY_SIZE(bcm59055_wall_props);
	battery_data->wall.get_property = bcm59055_wall_get_property;

	/* If it were a real device, get platform device resources here
	 * and ioremap the register space + register an interrupt if needed
	 */

	/* Register AC power supply */
	ret = power_supply_register(&pdev->dev, &battery_data->wall);
	if (ret) {
		printk(KERN_ERR"%s : Failed to register WALL power supply\n", __func__); 
		goto err_ac_register;
	}

	/* Register battery power supply */
	ret = power_supply_register(&pdev->dev, &battery_data->battery);
	if (ret) {
		printk(KERN_ERR"%s : Failed to register battery power supply\n", __func__); 
		goto err_battery_register;
	}

	schedule_delayed_work(&battery_data->batt_lvl_wq, 0 );
	return 0;

err_battery_register:
	power_supply_unregister(&battery_data->wall);
err_ac_register:
	kfree(battery_data);
err_data_alloc:
	return ret;
}

static int bcm59055_battery_remove(struct platform_device *pdev)
{
	struct bcm59055_battery_data *data = platform_get_drvdata(pdev);

	power_supply_unregister(&data->battery);
	power_supply_unregister(&data->wall);
	kfree(data);

	return 0;
}

static struct platform_driver bcm59055_battery_driver = {
	.probe		= bcm59055_battery_probe,
	.remove		= bcm59055_battery_remove,
	.driver = {
		.name = "bcm59055-battery"
	}
};

static int __init bcm59055_battery_init(void)
{
	return platform_driver_register(&bcm59055_battery_driver);
}

static void __exit bcm59055_battery_exit(void)
{
	platform_driver_unregister(&bcm59055_battery_driver);
}

module_init(bcm59055_battery_init);
module_exit(bcm59055_battery_exit);
