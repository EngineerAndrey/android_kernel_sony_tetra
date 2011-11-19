/************************************************************************************************/
/*                                                                                              */
/*  Copyright 2011  Broadcom Corporation                                                        */
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

/**
*
* @file   audio_pmu_adapt.h
* @brief  Audio PMU adaptation for various PMU devices 
*
******************************************************************************/

#ifndef __AUDIO_PMU_ADAPT_H__
#define __AUDIO_PMU_ADAPT_H__

#ifdef NO_PMU

#include "bcmpmu_audio.h"
#define AUDIO_PMU_INIT() NULL
#define AUDIO_PMU_HS_SET_GAIN(a, b) NULL
#define AUDIO_PMU_HS_POWER(a) NULL
#define AUDIO_PMU_IHF_SET_GAIN(a) NULL
#define AUDIO_PMU_IHF_POWER(a) NULL
#define AUDIO_PMU_DEINIT() NULL
#else
#ifdef CONFIG_BCM59055_AUDIO

#include "linux/broadcom/bcm59055-audio.h"
#define AUDIO_PMU_INIT bcm59055_audio_init
#define AUDIO_PMU_HS_SET_GAIN bcm59055_hs_set_gain
#define AUDIO_PMU_HS_POWER bcm59055_hs_power
#define AUDIO_PMU_IHF_SET_GAIN bcm59055_ihf_set_gain
#define AUDIO_PMU_IHF_POWER bcm59055_ihf_power
#define AUDIO_PMU_DEINIT bcm59055_audio_deinit

#elif defined(CONFIG_BCMPMU_AUDIO)

#include "bcmpmu_audio.h"
#define AUDIO_PMU_INIT bcmpmu_audio_init
#define AUDIO_PMU_HS_SET_GAIN bcmpmu_hs_set_gain
#define AUDIO_PMU_HS_POWER bcmpmu_hs_power
#define AUDIO_PMU_IHF_SET_GAIN bcmpmu_ihf_set_gain
#define AUDIO_PMU_IHF_POWER bcmpmu_ihf_power
#define AUDIO_PMU_DEINIT bcmpmu_audio_deinit

#endif


/********************************************************************
*  @brief  Convert Headset gain dB value to PMU-format gain value
*
*  @param  Headset gain dB galue
*
*  @return PMU_HS_Gain_t PMU-format gain value
*
****************************************************************************/
UInt32 map2pmu_hs_gain_fromDB( Int16 db_gain );

/********************************************************************
*  @brief  Convert IHF gain dB value to PMU-format gain value
*
*  @param  IHF gain dB galue
*
*  @return PMU_HS_Gain_t PMU-format gain value
*
****************************************************************************/
UInt32 map2pmu_ihf_gain_fromDB( Int16 db_gain );


/********************************************************************
*  @brief  Convert Headset gain dB value to PMU-format gain value
*
*  @param  Headset gain (Q13.2 dB)
*
*  @return PMU_HS_Gain_t PMU-format gain value
*
****************************************************************************/
UInt32 map2pmu_hs_gain_fromQ13dot2( Int16 gain );

/********************************************************************
*  @brief  Convert IHF gain dB value to PMU-format gain value
*
*  @param  IHF gain (Q13.2 dB)
*
*  @return PMU_HS_Gain_t PMU-format gain value
*
****************************************************************************/
UInt32 map2pmu_ihf_gain_fromQ13dot2( Int16 gain );


#endif  //#if !defined(NO_PMU)

#endif	//__AUDIO_PMU_ADAPT_H__

