/************************************************************************************************/
/*                                                                                              */
/*  Copyright 2010  Broadcom Corporation                                                        */
/*                                                                                              */
/*     Unless you and Broadcom execute a separate written software license agreement governing  */
/*     use of this software, this software is licensed to you under the terms of the GNU        */
/*     General Public License version 2 (the GPL), available at                                 */
/*                                                                                              */
/*          http://www.broadcom.com/licenses/GPLv2.php                                          */
/*                                                                                              */
/*     with the following added to such license:                                                */
/*                                                                                              */
/*     As a special exception, the copyright holders of this software give you permission to    */
/*     link this software with independent modules, and to copy and distribute the resulting    */
/*     executable under terms of your choice, provided that you also meet, for each linked      */
/*     independent module, the terms and conditions of the license of that module.              */
/*     An independent module is a module which is not derived from this software.  The special  */
/*     exception does not apply to any modifications of the software.                           */
/*                                                                                              */
/*     Notwithstanding the above, under no circumstances may you combine this software in any   */
/*     way with any other Broadcom software provided under a license other than the GPL,        */
/*     without Broadcom's express prior written consent.                                        */
/*                                                                                              */
/************************************************************************************************/

#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/ioport.h>
#include <linux/serial_8250.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>

#include <asm/mach/map.h>
#include <asm/pgalloc.h>

#include <linux/serial.h>
#include <linux/serial_core.h>

#include <mach/hardware.h>
#include <mach/io.h>
#include <mach/io_map.h>

#define IO_DESC(va, sz) { .virtual = va, \
                          .pfn = __phys_to_pfn(HW_IO_VIRT_TO_PHYS(va)), \
                          .length = sz, \
                          .type = MT_DEVICE }


static struct map_desc island_io_desc[] __initdata =
{
	/**************************************************
	* NOTE: The following are alphabetically ordered.
	***************************************************/

	IO_DESC( KONA_BINTC_BASE_ADDR, SZ_4K  ),
	IO_DESC( KONA_BSC1_VA, SZ_4K  ),
	IO_DESC( KONA_BSC2_VA, SZ_4K  ),
	IO_DESC( KONA_CHIPREG_VA, SZ_4K	),
	IO_DESC( KONA_D1W_VA, SZ_4K ),
	IO_DESC( KONA_DMAC_NS_VA, SZ_4K	),
	IO_DESC( KONA_DMAC_S_VA, SZ_4K  ),
	IO_DESC( KONA_DMUX_VA, SZ_4K  ),
	IO_DESC( KONA_EDMA_VA, SZ_4K ),
	IO_DESC( KONA_ESW_VA, SZ_1M ),
	IO_DESC( KONA_GICDIST_VA, SZ_4K ),
	IO_DESC( KONA_GPIO2_VA, SZ_4K ),
	IO_DESC( KONA_HSI_VA, SZ_4K ),
	IO_DESC( KONA_IPC_NS_VA, SZ_4K ),
	IO_DESC( KONA_IPC_S_VA, SZ_4K ),
	IO_DESC( KONA_KEK_VA, SZ_4K ),
	IO_DESC( KONA_KPM_CLK_VA, SZ_4K ),
	IO_DESC( KONA_SLV_CLK_VA, SZ_4K ),
	IO_DESC( KONA_L2C_VA, SZ_4K ),
	IO_DESC( KONA_MPHI_VA, SZ_4K ),
	IO_DESC( KONA_NAND_VA, SZ_64K ),
	IO_DESC( KONA_MPU_VA, SZ_4K ),
	IO_DESC( KONA_OTP_VA, SZ_4K ),
	IO_DESC( KONA_PKA_VA, SZ_4K ),
	IO_DESC( KONA_PWM_VA, SZ_4K ),
	IO_DESC( KONA_PMU_BSC_VA, SZ_4K ),
	IO_DESC( KONA_ROOT_RST_VA, SZ_4K ),
	IO_DESC( KONA_RNG_VA, SZ_4K ),

	/*
	* This SCU region also covers MM_ADDR_IO_GICCPU,
	* KONA_PROFTMR aka (GTIM) aka (GLB) aka knllog timer,
	* and KONA_PTIM aka os tick timer
	*/
	IO_DESC( KONA_SCU_VA, SZ_4K ),
	IO_DESC( KONA_SDIO1_VA, SZ_64K ),
	IO_DESC( KONA_SDIO2_VA, SZ_64K ),
	IO_DESC( KONA_SDIO3_VA, SZ_64K ),
	IO_DESC( KONA_SDIO4_VA, SZ_64K ),
	IO_DESC( KONA_SEC_VA, SZ_4K ),
	IO_DESC( KONA_SECWD_VA, SZ_4K ),
	IO_DESC( KONA_SPUM_NS_VA, SZ_64K ),
	IO_DESC( KONA_SPUM_S_VA, SZ_64K ),
	IO_DESC( KONA_SPUM_APB_NS_VA, SZ_4K ),
	IO_DESC( KONA_SPUM_APB_S_VA, SZ_4K ),
	IO_DESC( KONA_SRAM_VA, SZ_256K ),

	IO_DESC( KONA_SSP0_VA, SZ_4K ),
	IO_DESC( KONA_SSP2_VA, SZ_4K ),
	IO_DESC( KONA_SSP3_VA, SZ_4K ),
	IO_DESC( KONA_SSP4_VA, SZ_4K ),
	
	IO_DESC( KONA_SYSTMR_VA, SZ_4K ),  /* 32-bit kona gp timer */
	IO_DESC( KONA_TMR_HUB_VA, SZ_4K ), /* 64-bit hub timer */
	IO_DESC( KONA_TZCFG_VA, SZ_4K ),
	IO_DESC( KONA_UART0_VA, SZ_4K ),
	IO_DESC( KONA_UART1_VA, SZ_4K ),
	IO_DESC( KONA_UART2_VA, SZ_4K ),
	IO_DESC( KONA_UART3_VA, SZ_4K ),
	IO_DESC( KONA_USB_FSHOST_CTRL_VA, SZ_4K ),
	IO_DESC( KONA_USB_HOST_CTRL_VA, SZ_256 ),	/* Could really use SZ_32 if was def'd. Note: 256 not 256K */
	IO_DESC( KONA_USB_HOST_EHCI_VA, SZ_256 ),	/* Includes DWC specific registers, otherwise could use SZ_128 if was def'd */
	IO_DESC( KONA_USB_HOST_OHCI_VA, SZ_256 ),	/* Could really use SZ_128 if was def'd */
	IO_DESC( KONA_USB_HSOTG_CTRL_VA, SZ_4K ),
};



void __init island_map_io(void)
{
	iotable_init(island_io_desc, ARRAY_SIZE(island_io_desc));
}
