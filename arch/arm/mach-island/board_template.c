/*****************************************************************************
* Copyright 2003 - 2011 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

/*
 * A board template for adding devices and pass their associated board
 * dependent confgiurations as platform_data into the drivers
 *
 * This file needs to be included by the board specific source code
 */

#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/serial_8250.h>
#include <linux/i2c.h>
#include <linux/i2c-kona.h>

#include <asm/memory.h>
#include <asm/sizes.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <asm/hardware/gic.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/kona.h>
#include <mach/dma_mmap.h>
#include <mach/sdma.h>
#include <mach/sdio_platform.h>
#include <mach/usbh_cfg.h>

#include <sdio_settings.h>

#include <i2c_settings.h>
#include <usbh_settings.h>

#if defined(CONFIG_BCMBLT_RFKILL) || defined(CONFIG_BCMBLT_RFKILL_MODULE)
#include <linux/broadcom/bcmblt-rfkill.h>
#include <bcmblt_rfkill_settings.h>
#endif


#if defined(CONFIG_TOUCHSCREEN_EGALAX_I2C) || defined(CONFIG_TOUCHSCREEN_EGALAX_I2C_MODULE)
#include <linux/i2c/egalax_i2c_ts.h>
#include <egalax_i2c_ts_settings.h>
#endif

#if defined(CONFIG_SENSORS_BMA150) || defined(CONFIG_SENSORS_BMA150_MODULE)
#include <linux/bma150.h>
#include <sensors_bma150_i2c_settings.h>
#endif

#if defined(CONFIG_SENSORS_BH1715) || defined(CONFIG_SENSORS_BH1715_MODULE)
#include <linux/bh1715.h>
#include <bh1715_i2c_settings.h>
#endif

#if defined(CONFIG_SENSORS_MPU3050) || defined(CONFIG_SENSORS_MPU3050_MODULE)
#include <linux/mpu3050.h>
#include <mpu3050_i2c_settings.h>
#endif

#if defined(CONFIG_BMP18X_I2C) || defined(CONFIG_BMP18X_I2C_MODULE)
#include <linux/bmp18x.h>
#include <bmp18x_i2c_settings.h>
#endif

#if defined(CONFIG_NET_ISLAND)
#include <mach/net_platform.h>
#include <net_settings.h>
#endif

#if defined(CONFIG_MAX3353) || defined(CONFIG_MAX3353_MODULE)
#include <otg_settings.h>
#include <linux/i2c/max3353.h>
#endif

#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
#include <leds_gpio_settings.h>
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
#include <gpio_keys_settings.h>
#endif

#if defined(CONFIG_KEYBOARD_KONA) || defined(CONFIG_KEYBOARD_KONA_MODULE)
#include <linux/kona_keypad.h>
#include <keymap_settings.h>
#endif

#include "island.h"
#include "common.h"

#ifndef ISLAND_BOARD_ID
#error ISLAND_BOARD_ID needs to be defined in board_xxx.c
#endif

/*
 * Since this board template is included by each board_xxx.c. We concatenate
 * ISLAND_BOARD_ID to help debugging when multiple boards are compiled into
 * a single image
 */
#define concatenate_again(a, b) a ## b
#define concatenate(a, b) concatenate_again(a, b)

/* number of SDIO devices */
#define MAX_SDIO_DEVICES      3

/*
 * The SDIO index starts from 1 in CHAL, which is really not by convention
 * Re-define them here to avoid confusions
 */
#define PHYS_ADDR_SDIO0        SDIO1_BASE_ADDR
#define PHYS_ADDR_SDIO1        SDIO2_BASE_ADDR
#define PHYS_ADDR_SDIO2        SDIO3_BASE_ADDR
#define SDIO_CORE_REG_SIZE     0x10000


/* number of I2C adapters (hosts/masters) */
#define MAX_I2C_ADAPS    3

/*
 * The BSC (I2C) index starts from 1 in CHAL, which is really not by
 * convention. Re-define them here to avoid confusions
 */
#define PHYS_ADDR_BSC0         BSC1_BASE_ADDR
#define PHYS_ADDR_BSC1         BSC2_BASE_ADDR
#define PHYS_ADDR_BSC2         PMU_BSC_BASE_ADDR
#define BSC_CORE_REG_SIZE      0x100

#define USBH_EHCI_CORE_REG_SIZE    0x90
#define USBH_OHCI_CORE_REG_SIZE    0x1000
#define USBH_DWC_REG_OFFSET        USBH_EHCI_CORE_REG_SIZE
#define USBH_DWC_BASE_ADDR         (EHCI_BASE_ADDR + USBH_DWC_REG_OFFSET)
#define USBH_DWC_CORE_REG_SIZE     0x20
#define USBH_CTRL_REG_OFFSET       0x8000
#define USBH_CTRL_BASE_ADDR        (EHCI_BASE_ADDR + USBH_CTRL_REG_OFFSET)
#define USBH_CTRL_CORE_REG_SIZE    0x20

#define OTG_CTRL_CORE_REG_SIZE     0x100

static struct resource sdio0_resource[] = {
	[0] = {
		.start = PHYS_ADDR_SDIO0,
		.end = PHYS_ADDR_SDIO0 + SDIO_CORE_REG_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = BCM_INT_ID_SDIO0,
		.end = BCM_INT_ID_SDIO0,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource sdio1_resource[] = {
	[0] = {
		.start = PHYS_ADDR_SDIO1,
		.end = PHYS_ADDR_SDIO1 + SDIO_CORE_REG_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = BCM_INT_ID_SDIO1,
		.end = BCM_INT_ID_SDIO1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource sdio2_resource[] = {
        [0] = {
                .start = PHYS_ADDR_SDIO2,
                .end = PHYS_ADDR_SDIO2 + SDIO_CORE_REG_SIZE - 1,
                .flags = IORESOURCE_MEM,
        },
        [1] = {
                .start = BCM_INT_ID_SDIO_NAND,
                .end = BCM_INT_ID_SDIO_NAND,
                .flags = IORESOURCE_IRQ,
        },
};

static struct sdio_platform_cfg sdio_param[] =
#ifdef HW_SDIO_PARAM
	HW_SDIO_PARAM;
#else
	{};
#endif

static struct platform_device sdio_devices[MAX_SDIO_DEVICES] =
{
   { /* SDIO0 */
      .name = "sdhci",
      .id = 0,
      .resource = sdio0_resource,
      .num_resources	= ARRAY_SIZE(sdio0_resource),
   },
   { /* SDIO1 */
      .name = "sdhci",
      .id = 1,
      .resource = sdio1_resource,
      .num_resources	= ARRAY_SIZE(sdio1_resource),
   },
   { /* SDIO2 */
      .name = "sdhci",
      .id = 2,
      .resource = sdio2_resource,
      .num_resources    = ARRAY_SIZE(sdio1_resource),
   },
};

#if defined(CONFIG_NET_ISLAND)
static struct island_net_hw_cfg island_net_data =
#ifdef HW_CFG_ISLAND_NET
   HW_CFG_ISLAND_NET;
#else
{
   .addrPhy0 = 0,
   .addrPhy1 = 1,
   .gpioPhy0 = -1,
   .gpioPhy1 = -1,
};
#endif

static struct platform_device net_device =
{
   .name = "island-net",
   .id = -1,
   .dev =
   {
      .platform_data = &island_net_data,
   },
};
#endif /* CONFIG_NET_ISLAND */

static struct bsc_adap_cfg i2c_adap_param[] =
#ifdef HW_I2C_ADAP_PARAM
	HW_I2C_ADAP_PARAM;
#else
	{};
#endif

static struct resource i2c0_resource[] = {
   [0] =
   {
      .start = PHYS_ADDR_BSC0,
      .end = PHYS_ADDR_BSC0 + BSC_CORE_REG_SIZE - 1,
      .flags = IORESOURCE_MEM,
   },
   [1] = 
   {
      .start = BCM_INT_ID_I2C0,
      .end = BCM_INT_ID_I2C0,
      .flags = IORESOURCE_IRQ,
   },
};

static struct resource i2c1_resource[] = {
   [0] =
   {
      .start = PHYS_ADDR_BSC1,
      .end = PHYS_ADDR_BSC1 + BSC_CORE_REG_SIZE - 1,
      .flags = IORESOURCE_MEM,
   },
   [1] = 
   {
      .start = BCM_INT_ID_I2C1,
      .end = BCM_INT_ID_I2C1,
      .flags = IORESOURCE_IRQ,
   },
};

static struct resource i2c2_resource[] = {
   [0] =
   {
      .start = PHYS_ADDR_BSC2,
      .end = PHYS_ADDR_BSC2 + BSC_CORE_REG_SIZE - 1,
      .flags = IORESOURCE_MEM,
   },
   [1] =
   {
      .start = BCM_INT_ID_PM_I2C,
      .end = BCM_INT_ID_PM_I2C,
      .flags = IORESOURCE_IRQ,
   },
};

static struct platform_device i2c_adap_devices[MAX_I2C_ADAPS] =
{
   {  /* for BSC0 */
      .name = "bsc-i2c",
      .id = 0,
      .resource = i2c0_resource,
      .num_resources	= ARRAY_SIZE(i2c0_resource),
   },
   {  /* for BSC1 */
      .name = "bsc-i2c",
      .id = 1,
      .resource = i2c1_resource,
      .num_resources	= ARRAY_SIZE(i2c1_resource),
   },
   {  /* for PMBSC */
      .name = "bsc-i2c",
      .id = 2,
      .resource = i2c2_resource,
      .num_resources	= ARRAY_SIZE(i2c2_resource),
   },
};

static struct usbh_cfg usbh_param =
#ifdef HW_USBH_PARAM
	HW_USBH_PARAM;
#else
	{};
#endif

static struct resource usbh_resource[] = {
	[0] = {
		.start = USBH_CTRL_BASE_ADDR,
		.end = USBH_CTRL_BASE_ADDR + USBH_CTRL_CORE_REG_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device usbh_device =
{
	.name = "usbh",
	.id = -1,
	.resource = usbh_resource,
	.num_resources = ARRAY_SIZE(usbh_resource),
	.dev = {
		.platform_data = &usbh_param,
	},
};

static u64 ehci_dmamask = DMA_BIT_MASK(32);

static struct resource usbh_ehci_resource[] = {
	[0] = {
		.start = EHCI_BASE_ADDR,
		.end = EHCI_BASE_ADDR + USBH_EHCI_CORE_REG_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = BCM_INT_ID_ULPI_EHCI,
		.end = BCM_INT_ID_ULPI_EHCI,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device usbh_ehci_device =
{
	.name = "bcm-ehci",
	.id = 0,
	.resource = usbh_ehci_resource,
	.num_resources = ARRAY_SIZE(usbh_ehci_resource),
	.dev = {
		.dma_mask = &ehci_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static u64 ohci_dmamask = DMA_BIT_MASK(32);

static struct resource usbh_ohci_resource[] = {
	[0] = {
		.start = OHCI_BASE_ADDR,
		.end = OHCI_BASE_ADDR + USBH_OHCI_CORE_REG_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = BCM_INT_ID_ULPI_OHCI,
		.end = BCM_INT_ID_ULPI_OHCI,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device usbh_ohci_device =
{
	.name = "bcm-ohci",
	.id = 0,
	.resource = usbh_ohci_resource,
	.num_resources = ARRAY_SIZE(usbh_ohci_resource),
	.dev = {
		.dma_mask = &ohci_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

#if defined(CONFIG_TOUCHSCREEN_EGALAX_I2C) || defined(CONFIG_TOUCHSCREEN_EGALAX_I2C_MODULE)
static struct egalax_i2c_ts_cfg egalax_i2c_param =
{
	.id = -1,
	.gpio = {
		.reset = -1,
		.event = -1,
	},
};

static struct i2c_board_info egalax_i2c_boardinfo[] =
{
	{
		.type = "egalax_i2c",
		.addr = 0x04,
		.platform_data = &egalax_i2c_param,
	},
};
#endif

#if defined(CONFIG_MAX3353) || defined(CONFIG_MAX3353_MODULE)
static struct max3353_platform_data max3353_info = {
	.mode = HW_OTG_MAX3353_MODE,
};

static struct i2c_board_info max3353_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO(MAX3353_DRIVER_NAME, MAX3353_I2C_ADDR_BASE),
		.platform_data  = &max3353_info,
	},
};
#endif

#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
#define board_leds_gpio_device concatenate(ISLAND_BOARD_ID, _leds_gpio_device)
static struct platform_device board_leds_gpio_device = {
   .name = "leds-gpio",
   .id = -1,
   .dev = {
      .platform_data = &leds_gpio_data,
   },
};
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
#define board_gpio_keys_device concatenate(ISLAND_BOARD_ID, _gpio_keys_device)
static struct platform_device board_gpio_keys_device = {
   .name = "gpio-keys",
   .id = -1,
   .dev = {
      .platform_data = &gpio_keys_data,
   },
};
#endif

#if defined(CONFIG_KEYBOARD_KONA) || defined(CONFIG_KEYBOARD_KONA_MODULE)

#define board_keypad_keymap concatenate(ISLAND_BOARD_ID, _keypad_keymap)
static struct KEYMAP board_keypad_keymap[] = HW_DEFAULT_KEYMAP;

#define board_keypad_pwroff concatenate(ISLAND_BOARD_ID, _keypad_pwroff)
static unsigned int board_keypad_pwroff[] = HW_DEFAULT_POWEROFF;

#define board_keypad_param concatenate(ISLAND_BOARD_ID, _keypad_param)
static struct KEYPAD_DATA board_keypad_param =
{
    .active_mode = 0,
    .keymap      = board_keypad_keymap,
    .keymap_cnt  = ARRAY_SIZE(board_keypad_keymap),
    .pwroff      = board_keypad_pwroff,
    .pwroff_cnt  = ARRAY_SIZE(board_keypad_pwroff),
    .clock       = "gpiokp_apb_clk",
};

#define board_keypad_device_resource concatenate(ISLAND_BOARD_ID, _keypad_device_resource)
static struct resource board_keypad_device_resource[] = {
    [0] = {
        .start = KEYPAD_BASE_ADDR,
        .end   = KEYPAD_BASE_ADDR + 0xD0,
        .flags = IORESOURCE_MEM,
    },
    [1] = {
        .start = BCM_INT_ID_KEYPAD,
        .end   = BCM_INT_ID_KEYPAD,
        .flags = IORESOURCE_IRQ,
    },
};

#define board_keypad_device concatenate(ISLAND_BOARD_ID, _keypad_device)
static struct platform_device board_keypad_device =
{
   .name          = "kona_keypad",
   .id            = -1,
   .resource      = board_keypad_device_resource,
   .num_resources = ARRAY_SIZE(board_keypad_device_resource),
   .dev = {
      .platform_data = &board_keypad_param,
   },
};
#endif

#if defined(CONFIG_KONA_OTG_CP) || defined(CONFIG_KONA_OTG_CP_MODULE)
static struct resource otg_cp_resource[] = {
	[0] = {
		.start = HSOTG_CTRL_BASE_ADDR,
		.end = HSOTG_CTRL_BASE_ADDR + OTG_CTRL_CORE_REG_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = BCM_INT_ID_USB_OTG_DRV_VBUS,
		.end = BCM_INT_ID_USB_OTG_DRV_VBUS,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device otg_cp_device =
{
	.name = "kona-otg-cp",
	.id = -1,
	.resource = otg_cp_resource,
	.num_resources = ARRAY_SIZE(otg_cp_resource),
};
#endif

#if defined(CONFIG_SENSORS_BMA150) || defined(CONFIG_SENSORS_BMA150_MODULE)

#define board_bma150_axis_change concatenate(ISLAND_BOARD_ID, _bma150_axis_change)

#ifdef BMA150_DRIVER_AXIS_SETTINGS
   static struct t_bma150_axis_change board_bma150_axis_change = BMA150_DRIVER_AXIS_SETTINGS;
#endif

static struct i2c_board_info __initdata i2c_bma150_info[] =
{
   {
      I2C_BOARD_INFO(BMA150_DRIVER_NAME, BMA150_DRIVER_SLAVE_NUMBER_0x38),
#ifdef BMA150_DRIVER_AXIS_SETTINGS
      .platform_data  = &board_bma150_axis_change,
#endif
   }, 
};
#endif

#if defined(CONFIG_SENSORS_BH1715) || defined(CONFIG_SENSORS_BH1715_MODULE)
static struct i2c_board_info __initdata i2c_bh1715_info[] =
{
	{
		I2C_BOARD_INFO(BH1715_DRV_NAME, BH1715_I2C_ADDR),
	},
};
#endif

#if defined(CONFIG_SENSORS_MPU3050) || defined(CONFIG_SENSORS_MPU3050_MODULE)

#define board_mpu3050_data concatenate(ISLAND_BOARD_ID, _mpu3050_data)

#ifdef MPU3050_DRIVER_AXIS_SETTINGS
   static struct t_mpu3050_axis_change board_mpu3050_axis_change = MPU3050_DRIVER_AXIS_SETTINGS;
#endif

static struct mpu3050_platform_data board_mpu3050_data = 
{ 
   .gpio_irq_pin = MPU3050_GPIO_IRQ_PIN,
   .scale        = MPU3050_SCALE,
#ifdef MPU3050_DRIVER_AXIS_SETTINGS
   .p_axis_change = &board_mpu3050_axis_change,
#else
   .p_axis_change = 0,
#endif
};

static struct i2c_board_info __initdata i2c_mpu3050_info[] = 
{
	{
		I2C_BOARD_INFO(MPU3050_DRV_NAME, MPU3050_I2C_ADDR),
		.platform_data  = &board_mpu3050_data,
	},
};
#endif

#if defined(CONFIG_BMP18X_I2C) || defined(CONFIG_BMP18X_I2C_MODULE)
static struct i2c_board_info __initdata i2c_bmp18x_info[] = 
{
	{
		I2C_BOARD_INFO(BMP18X_NAME, BMP18X_I2C_ADDRESS),
	},
};
#endif

#if defined(CONFIG_BCMBLT_RFKILL) || defined(CONFIG_BCMBLT_RFKILL_MODULE)
#define board_bcmblt_rfkill_cfg concatenate(ISLAND_BOARD_ID, _bcmblt_rfkill_cfg)
static struct bcmblt_rfkill_platform_data board_bcmblt_rfkill_cfg =
{
#ifdef BCMBLT_RFKILL_GPIO
   .gpio = BCMBLT_RFKILL_GPIO,
#endif
};
#define board_bcmblt_rfkill_device concatenate(ISLAND_BOARD_ID, _bcmblt_rfkill_device)
static struct platform_device board_bcmblt_rfkill_device = 
{
   .name = "bcmblt-rfkill",
   .id = 1,
   .dev =
   {
      .platform_data = &board_bcmblt_rfkill_cfg,
   },
}; 

static void __init board_add_bcmblt_rfkill_device(void)
{
   platform_device_register(&board_bcmblt_rfkill_device);
}
#endif

static void __init add_sdio_device(void)
{
   unsigned int i, id, num_devices;

   num_devices = ARRAY_SIZE(sdio_param);
   if (num_devices > MAX_SDIO_DEVICES)
      num_devices = MAX_SDIO_DEVICES;

   /*
    * Need to register eMMC as the first SDIO device so it grabs mmcblk0 when
    * it's installed. This required for rootfs to be mounted properly
    * 
    * Ask Darwin for why we need to do this
    */
   for (i = 0; i < num_devices; i++)
   {
      id = sdio_param[i].id;
      if (id < MAX_SDIO_DEVICES)
      {
         if (sdio_param[i].devtype == SDIO_DEV_TYPE_EMMC)
         {
            sdio_devices[id].dev.platform_data = &sdio_param[i];
            platform_device_register(&sdio_devices[id]);
         }
      }
   }

   for (i = 0; i < num_devices; i++)
   {
      id = sdio_param[i].id;

      /* skip eMMC as it has been registered */
      if (sdio_param[i].devtype == SDIO_DEV_TYPE_EMMC)
         continue;

      if (id < MAX_SDIO_DEVICES)
      {
         if (sdio_param[i].devtype == SDIO_DEV_TYPE_WIFI)
         {
            struct sdio_wifi_gpio_cfg *wifi_gpio =
               &sdio_param[i].wifi_gpio;

#ifdef HW_WLAN_GPIO_RESET_PIN
            wifi_gpio->reset = HW_WLAN_GPIO_RESET_PIN;
#else
            wifi_gpio->reset = -1;
#endif
#ifdef HW_WLAN_GPIO_SHUTDOWN_PIN
            wifi_gpio->shutdown = HW_WLAN_GPIO_SHUTDOWN_PIN;
#else
            wifi_gpio->shutdown = -1;
#endif
#ifdef HW_WLAN_GPIO_REG_PIN
            wifi_gpio->reg = HW_WLAN_GPIO_REG_PIN;
#else
            wifi_gpio->reg = -1;
#endif
#ifdef HW_WLAN_GPIO_HOST_WAKE_PIN      
            wifi_gpio->host_wake = HW_WLAN_GPIO_HOST_WAKE_PIN;
#else
            wifi_gpio->host_wake = -1;
#endif
         }
         sdio_devices[id].dev.platform_data = &sdio_param[i];
         platform_device_register(&sdio_devices[id]);
      }
   }
}

static void __init add_i2c_device(void)
{
	unsigned int i, num_devices;

	num_devices = ARRAY_SIZE(i2c_adap_param);
	if (num_devices == 0)
		return;
	if (num_devices > MAX_I2C_ADAPS)
 		num_devices = MAX_I2C_ADAPS;

	for (i = 0; i < num_devices; i++) {
		/* DO NOT register the I2C device if it is disabled */
		if (i2c_adap_param[i].disable == 1)
		continue;

		i2c_adap_devices[i].dev.platform_data = &i2c_adap_param[i];
		platform_device_register(&i2c_adap_devices[i]);
	}

#if defined(CONFIG_TOUCHSCREEN_EGALAX_I2C) || defined(CONFIG_TOUCHSCREEN_EGALAX_I2C_MODULE)
#ifdef HW_EGALAX_I2C_BUS_ID
	egalax_i2c_param.id = HW_EGALAX_I2C_BUS_ID;
#endif

#ifdef HW_EGALAX_GPIO_RESET
	egalax_i2c_param.gpio.reset = HW_EGALAX_GPIO_RESET;
#endif

#ifdef HW_EGALAX_GPIO_EVENT
	egalax_i2c_param.gpio.event = HW_EGALAX_GPIO_EVENT;
#endif
	
	egalax_i2c_boardinfo[0].irq =
		gpio_to_irq(egalax_i2c_param.gpio.event);

	i2c_register_board_info(egalax_i2c_param.id, egalax_i2c_boardinfo,
		ARRAY_SIZE(egalax_i2c_boardinfo));
#endif

#if defined(CONFIG_SENSORS_BMA150) || defined(CONFIG_SENSORS_BMA150_MODULE)

   i2c_register_board_info(
#ifdef SENSORS_BMA150_I2C_BUS_ID
      SENSORS_BMA150_I2C_BUS_ID,
#else
      -1,
#endif
      i2c_bma150_info, ARRAY_SIZE(i2c_bma150_info));
#endif

#if defined(CONFIG_SENSORS_BH1715) || defined(CONFIG_SENSORS_BH1715_MODULE)
   i2c_register_board_info(
#ifdef BH1715_I2C_BUS_ID
      BH1715_I2C_BUS_ID,
#else
      -1,
#endif
      i2c_bh1715_info, ARRAY_SIZE(i2c_bh1715_info));
#endif

#if defined(CONFIG_SENSORS_MPU3050) || defined(CONFIG_SENSORS_MPU3050_MODULE)
   i2c_register_board_info(
#ifdef MPU3050_I2C_BUS_ID
      MPU3050_I2C_BUS_ID,  
#else
      -1,
#endif
      i2c_mpu3050_info, ARRAY_SIZE(i2c_mpu3050_info));
#endif

#if defined(CONFIG_BMP18X_I2C) || defined(CONFIG_BMP18X_I2C_MODULE)
			i2c_register_board_info(
#ifdef BMP18X_I2C_BUS_ID
      BMP18X_I2C_BUS_ID,
#else
      -1,
#endif
      i2c_bmp18x_info, ARRAY_SIZE(i2c_bmp18x_info));
#endif
}

#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
#define board_add_led_device concatenate(ISLAND_BOARD_ID, _add_led_device)
static void __init board_add_led_device(void)
{
   platform_device_register(&board_leds_gpio_device);
}
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
#define board_add_keys_device concatenate(ISLAND_BOARD_ID, _add_keyboard_device)
static void __init board_add_keys_device(void)
{
   platform_device_register(&board_gpio_keys_device);
}
#endif

#if defined(CONFIG_KEYBOARD_KONA) || defined(CONFIG_KEYBOARD_KONA_MODULE)
#define board_add_keyboard_kona concatenate(ISLAND_BOARD_ID, _add_keyboard_kona)
static void __init board_add_keyboard_kona(void)
{
   platform_device_register(&board_keypad_device);
}
#endif


static void __init add_usbh_device(void)
{
	/*
	 * Always register the low level USB host device before EHCI/OHCI
	 * devices. Also, always add EHCI device before OHCI
	 */
	platform_device_register(&usbh_device);
	platform_device_register(&usbh_ehci_device);
	platform_device_register(&usbh_ohci_device);
}

static void __init add_usb_otg_device(void)
{
#if defined(CONFIG_KONA_OTG_CP) || defined(CONFIG_KONA_OTG_CP_MODULE)
	platform_device_register(&otg_cp_device);
#endif

#if defined(CONFIG_MAX3353) || defined(CONFIG_MAX3353_MODULE)
#ifdef HW_OTG_MAX3353_I2C_BUS_ID
	max3353_info.id = HW_OTG_MAX3353_I2C_BUS_ID;
#else
	max3353_info.id = -1;
#endif
#ifdef HW_OTG_MAX3353_GPIO_INT
	max3353_info.irq_gpio_num = HW_OTG_MAX3353_GPIO_INT;
#else
	max3353_info.irq_gpio_num = -1;
#endif
	i2c_register_board_info(max3353_info.id, max3353_i2c_boardinfo, ARRAY_SIZE(max3353_i2c_boardinfo));
#endif
}

static void __init add_devices(void)
{
#ifdef HW_SDIO_PARAM
	add_sdio_device();
#endif

#ifdef HW_I2C_ADAP_PARAM
	add_i2c_device();
#endif

#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
        board_add_led_device();
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
        board_add_keys_device();
#endif

#if defined(CONFIG_KEYBOARD_KONA) || defined(CONFIG_KEYBOARD_KONA_MODULE)
        board_add_keyboard_kona();
#endif

#if defined(CONFIG_BCMBLT_RFKILL) || defined(CONFIG_BCMBLT_RFKILL_MODULE)
        board_add_bcmblt_rfkill_device();
#endif

	add_usbh_device();
	add_usb_otg_device();

#ifdef CONFIG_NET_ISLAND
	platform_device_register(&net_device);
#endif
}

static void __init board_init(void)
{
#ifdef CONFIG_MAP_SDMA
	dma_mmap_init();
	sdma_init();
#endif
	/*
	 * Add common platform devices that do not have board dependent HW
	 * configurations
	 */
	board_add_common_devices();

	/* add devices with board dependent HW configurations */
	add_devices();
}

/*
 * Template used by board-xxx.c to create new board instance
 */
#define CREATE_BOARD_INSTANCE(name) \
MACHINE_START(name, #name) \
	.phys_io = IO_START, \
	.io_pg_offst = (IO_BASE >> 18) & 0xFFFC, \
	.map_io = island_map_io, \
	.init_irq = kona_init_irq, \
	.timer  = &kona_timer, \
	.init_machine = board_init, \
MACHINE_END
