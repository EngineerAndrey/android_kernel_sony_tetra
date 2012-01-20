/*******************************************************************************************
Copyright 2009 - 2010 Broadcom Corporation.  All rights reserved.                                */

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
*  @file   chal_caph_audioh_analogmic.c
*
*  @brief  chal layer driver for caph audioh device driver
*
****************************************************************************/

#include "io_map.h"
#include "chal_caph.h"
#include "chal_caph_audioh.h"
#include "chal_caph_audioh_int.h"
#include "brcm_rdb_sysmap.h"
#include "brcm_rdb_audioh.h"
#include "brcm_rdb_util.h"
#include "brcm_rdb_aci.h"
#include "brcm_rdb_auxmic.h"
#include "brcm_rdb_padctrlreg.h"

//****************************************************************************
//                        G L O B A L   S E C T I O N
//****************************************************************************

//****************************************************************************
// global variable definitions
//****************************************************************************


//****************************************************************************
//                         L O C A L   S E C T I O N
//****************************************************************************

//****************************************************************************
// local macro declarations
//****************************************************************************

#define  READ_REG32(reg)            ( *((volatile UInt32 *) (reg)) )
#define  WRITE_REG32(reg, value)   	( *((volatile UInt32 *) (reg)) = (UInt32) (value) )

//****************************************************************************
// local typedef declarations
//****************************************************************************



//****************************************************************************
// local variable definitions
//****************************************************************************


//****************************************************************************
// local function declarations
//****************************************************************************



//******************************************************************************
// local function definitions
//******************************************************************************




//============================================================================
//
// Function Name: CHAL_HANDLE chal_audio_init(cUInt32 baseAddr)
//
// Description:   Standard Init entry point for cHal
//                first function to call.
//
// Parameters:
//                handle     ---  the Hera audio handle
//                mic_input  ---  the annlog microphone select
// Return:        none
//
//============================================================================

cVoid chal_audio_mic_input_select(CHAL_HANDLE handle, UInt16 mic_input)
{

    cUInt32 regVal = 0;

    cUInt32 base =    ((ChalAudioCtrlBlk_t*)handle)->audioh_base;

    cUInt32 reg_val;

    reg_val = BRCM_READ_REG(base, AUDIOH_ADC_CTL);
    reg_val &= ~(AUDIOH_ADC_CTL_AMIC_EN_MASK);

    // if(mic_input == CHAL_AUDIO_ENABLE)
    {
       reg_val |= AUDIOH_ADC_CTL_AMIC_EN_MASK;
    }

    /* Set the required setting */
    BRCM_WRITE_REG(base,  AUDIOH_ADC_CTL, reg_val);


	// ACI control for analog microphone

	// WRITE_REG32(0x3500E0D4, 0xC0);
	regVal = READ_REG32((ACI_BASE_ADDR+ACI_ADC_CTRL_OFFSET));
    regVal |= ACI_ADC_CTRL_AUDIORX_VREF_PWRUP_MASK;
    regVal |= ACI_ADC_CTRL_AUDIORX_BIAS_PWRUP_MASK;
    WRITE_REG32((ACI_BASE_ADDR+ACI_ADC_CTRL_OFFSET), regVal);

	/* enable AUXMIC */
	
	// WRITE_REG32(0x3500E014, 0x01);
	regVal = READ_REG32((AUXMIC_BASE_ADDR+AUXMIC_AUXEN_OFFSET));
	regVal |= AUXMIC_AUXEN_MICAUX_EN_MASK;
	WRITE_REG32((AUXMIC_BASE_ADDR+AUXMIC_AUXEN_OFFSET), regVal);

	/* disable AUXMIC force power down */
	
	regVal = READ_REG32((AUXMIC_BASE_ADDR+AUXMIC_F_PWRDWN_OFFSET));
	regVal &= ~AUXMIC_F_PWRDWN_FORCE_PWR_DWN_MASK;
	WRITE_REG32((AUXMIC_BASE_ADDR+AUXMIC_F_PWRDWN_OFFSET), regVal);


    return;
}

//============================================================================
//
// Function Name: cVoid chal_audio_mic_pga(CHAL_HANDLE handle, int gain)
//
// Description:   Set gain on analog microphone path
//
// Parameters:
//                handle ---  the Hera audio handle
//				  gain   ---  the gain value
// Return:        none
//
//============================================================================
cVoid chal_audio_mic_pga(CHAL_HANDLE handle, int gain)
{
	cUInt32 base =    ((ChalAudioCtrlBlk_t*)handle)->audioh_base;
    cUInt32 reg_val;
	
	reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VRX1);
	reg_val &= ~AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_GAINCTRL_MASK;

	if(gain > 0x3f)	gain = 0x3f;
	if(gain < 0x00) gain = 0;

	reg_val |= (gain << AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_GAINCTRL_SHIFT);

    BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VRX1, reg_val);

	return;
}

//============================================================================
//
// Function Name: chal_audio_mic_mute(CHAL_HANDLE handle,  Boolean mute_ctrl)
//
// Description:  Mute the ANALOG MIC and AUX MIC signals on the DATA line
//
// Parameters:   handle      : the voice input path handle.
//               mute_ctrl   : mute control
//
// Return:       None.
//
//============================================================================
cVoid chal_audio_mic_mute(CHAL_HANDLE handle, Boolean mute_ctrl)
{
    cUInt32 base =    ((ChalAudioCtrlBlk_t*)handle)->audioh_base;
    cUInt32 reg_val;

	reg_val  = BRCM_READ_REG(base, AUDIOH_AUDIORX_VRX1);
	reg_val &= ~(AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_ADCRST_MASK);

	if(mute_ctrl == TRUE)
	{
    	reg_val |= AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_ADCRST_MASK;
	}
    else
    {
    	reg_val &=~(AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_ADCRST_MASK);
    }

	BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VRX1, reg_val);

}



//============================================================================
//
// Function Name: cVoid chal_audio_mic_adc_standby(CHAL_HANDLE handle, Boolean standby)
//
// Description:   control ADC standby state on analog microphone path
//
// Parameters:
//                handle  ---  the Hera audio handle
//				  standby ---  the gain value
// Return:        none
//
//============================================================================

cVoid chal_audio_mic_adc_standby(CHAL_HANDLE handle, Boolean standby)
{

	return;
}

//============================================================================
//
// Function Name: cVoid chal_audio_mic_pwrctrl(CHAL_HANDLE handle, Boolean pwronoff)
//
// Description:   power on/off analog microphone path
//
// Parameters:
//                handle   ---  the Hera audio handle
//				  pwronoff ---  on or off selection
// Return:        none
//
//============================================================================

cVoid chal_audio_mic_pwrctrl(CHAL_HANDLE handle, Boolean pwronoff)
{
    cUInt32 base =    ((ChalAudioCtrlBlk_t*)handle)->audioh_base;

    cUInt32 reg_val;

    if(pwronoff == TRUE)
    {
        //0. powerup ACI VREF, BIAS (should be done by caller before)

        //1. power up BiasCore
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_BIAS);
        reg_val |= (AUDIOH_AUDIORX_BIAS_AUDIORX_BIAS_PWRUP_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_BIAS, reg_val);

        //2. power up AUDIORX_REF, and fast settle, others "0"
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VREF);
        reg_val |= (AUDIOH_AUDIORX_VREF_AUDIORX_VREF_PWRUP_MASK);
        reg_val |= (AUDIOH_AUDIORX_VREF_AUDIORX_VREF_FASTSETTLE_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VREF, reg_val);

        //3.  enable AUXMIC
        //4. disable AUXMIC force power down

        //5.  turn on everything and all default to "zero"
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VRX1);
        reg_val &= ~(AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_PWRDN_MASK);
        reg_val &= ~(AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_CMBUF_PWRDN_MASK);
        reg_val &= ~(AUDIOH_AUDIORX_VRX1_AUDIORX_APMCLK_PWRDN_MASK);
        reg_val &= ~(AUDIOH_AUDIORX_VRX1_AUDIORX_LDO_DIG_PWRDN_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VRX1, reg_val);

        //6. power up MAIN MIC
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VMIC);
        reg_val &= ~(AUDIOH_AUDIORX_VMIC_AUDIORX_MIC_PWRDN_MASK);
        reg_val |= (AUDIOH_AUDIORX_VMIC_AUDIORX_MIC_EN_MASK);
        reg_val &= ~(AUDIOH_AUDIORX_VMIC_AUDIORX_VMIC_CTRL_MASK);
        reg_val |= (3 << AUDIOH_AUDIORX_VMIC_AUDIORX_VMIC_CTRL_SHIFT);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VMIC, reg_val);

        // power up AUDIORX_REF, others "0"
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VREF);
        reg_val &= (AUDIOH_AUDIORX_VREF_AUDIORX_VREF_FASTSETTLE_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VREF, reg_val);

        // AUDIORX_VRX2/AUDIORX_VMIC
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VRX2, 0x00);

    }
    else
    {

        // power down AUDIORX_REF, others "0"
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VREF);
        reg_val |= (AUDIOH_AUDIORX_VREF_AUDIORX_VREF_FASTSETTLE_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VREF, reg_val);

        //6. power down MAIN MIC
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VMIC);
        reg_val |= (AUDIOH_AUDIORX_VMIC_AUDIORX_MIC_PWRDN_MASK);
        reg_val &= ~(AUDIOH_AUDIORX_VMIC_AUDIORX_MIC_EN_MASK);
        reg_val |= (AUDIOH_AUDIORX_VMIC_AUDIORX_VMIC_CTRL_MASK);
        reg_val |= (0 << AUDIOH_AUDIORX_VMIC_AUDIORX_VMIC_CTRL_SHIFT);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VMIC, reg_val);

        //5.  turn off everything
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VRX1);
        reg_val |= (AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_PWRDN_MASK);
        reg_val |= (AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_CMBUF_PWRDN_MASK);
        reg_val |= (AUDIOH_AUDIORX_VRX1_AUDIORX_APMCLK_PWRDN_MASK);
        reg_val |= (AUDIOH_AUDIORX_VRX1_AUDIORX_LDO_DIG_PWRDN_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VRX1, reg_val);

        //2. power down AUDIORX_REF, and fast settle
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VREF);
        reg_val &= ~(AUDIOH_AUDIORX_VREF_AUDIORX_VREF_PWRUP_MASK);
        reg_val &= ~(AUDIOH_AUDIORX_VREF_AUDIORX_VREF_FASTSETTLE_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VREF, reg_val);

        //1. power down BiasCore
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_BIAS);
        reg_val &= ~(AUDIOH_AUDIORX_BIAS_AUDIORX_BIAS_PWRUP_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_BIAS, reg_val);

    }

	return;
}


//============================================================================
//
// Function Name: cVoid chal_audio_hs_mic_pwrctrl(CHAL_HANDLE handle, Boolean pwronoff)
//
// Description:   power on/off headset microphone path
//
// Parameters:
//                handle   ---  the Hera audio handle
//				  pwronoff ---  on or off selection
// Return:        none
//
//============================================================================

cVoid chal_audio_hs_mic_pwrctrl(CHAL_HANDLE handle, Boolean pwronoff)
{
    cUInt32 base =    ((ChalAudioCtrlBlk_t*)handle)->audioh_base;

    cUInt32 reg_val;

    if(pwronoff == TRUE)
    {
        //0. powerup ACI VREF, BIAS (should be done by caller before)

        //1. power up BiasCore
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_BIAS);
        reg_val |= (AUDIOH_AUDIORX_BIAS_AUDIORX_BIAS_PWRUP_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_BIAS, reg_val);

        //2. power up AUDIORX_REF, and fast settle, others "0"
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VREF);
        reg_val |= (AUDIOH_AUDIORX_VREF_AUDIORX_VREF_PWRUP_MASK);
        reg_val |= (AUDIOH_AUDIORX_VREF_AUDIORX_VREF_FASTSETTLE_MASK);
        reg_val |= (AUDIOH_AUDIORX_VREF_AUDIORX_VREF_POWERCYCLE_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VREF, reg_val);

        //3.  enable AUXMIC
        //4. disable AUXMIC force power down

        //5.  turn on everything and all default to "zero"
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VRX1);
        reg_val &= ~(AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_PWRDN_MASK);
        reg_val &= ~(AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_CMBUF_PWRDN_MASK);
        reg_val &= ~(AUDIOH_AUDIORX_VRX1_AUDIORX_APMCLK_PWRDN_MASK);
        reg_val &= ~(AUDIOH_AUDIORX_VRX1_AUDIORX_LDO_DIG_PWRDN_MASK);
        reg_val |= (AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_SEL_MIC1B_MIC2_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VRX1, reg_val);

        //6. power up MAIN MIC
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VMIC);
        reg_val &= ~(AUDIOH_AUDIORX_VMIC_AUDIORX_MIC_PWRDN_MASK);
        reg_val |= (AUDIOH_AUDIORX_VMIC_AUDIORX_MIC_EN_MASK);
        reg_val &= ~(AUDIOH_AUDIORX_VMIC_AUDIORX_VMIC_CTRL_MASK);
        reg_val |= (3 << AUDIOH_AUDIORX_VMIC_AUDIORX_VMIC_CTRL_SHIFT);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VMIC, reg_val);

        // power up AUDIORX_REF, others "0"
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VREF);
        reg_val &= (AUDIOH_AUDIORX_VREF_AUDIORX_VREF_FASTSETTLE_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VREF, reg_val);

        // AUDIORX_VRX2/AUDIORX_VMIC
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VRX2, 0x00);

    }
    else
    {

        // power down AUDIORX_REF, others "0"
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VREF);
        reg_val |= (AUDIOH_AUDIORX_VREF_AUDIORX_VREF_FASTSETTLE_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VREF, reg_val);

        //6. power down MAIN MIC
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VMIC);
        reg_val |= (AUDIOH_AUDIORX_VMIC_AUDIORX_MIC_PWRDN_MASK);
        reg_val &= ~(AUDIOH_AUDIORX_VMIC_AUDIORX_MIC_EN_MASK);
        reg_val |= (AUDIOH_AUDIORX_VMIC_AUDIORX_VMIC_CTRL_MASK);
        reg_val |= (0 << AUDIOH_AUDIORX_VMIC_AUDIORX_VMIC_CTRL_SHIFT);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VMIC, reg_val);

        //5.  turn off everything
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VRX1);
        reg_val &= ~(AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_SEL_MIC1B_MIC2_MASK);
        reg_val |= (AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_PWRDN_MASK);
        reg_val |= (AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_CMBUF_PWRDN_MASK);
        reg_val |= (AUDIOH_AUDIORX_VRX1_AUDIORX_APMCLK_PWRDN_MASK);
        reg_val |= (AUDIOH_AUDIORX_VRX1_AUDIORX_LDO_DIG_PWRDN_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VRX1, reg_val);

        //2. power down AUDIORX_REF, and fast settle
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_VREF);
        reg_val &= ~(AUDIOH_AUDIORX_VREF_AUDIORX_VREF_POWERCYCLE_MASK);
        reg_val &= ~(AUDIOH_AUDIORX_VREF_AUDIORX_VREF_PWRUP_MASK);
        reg_val &= ~(AUDIOH_AUDIORX_VREF_AUDIORX_VREF_FASTSETTLE_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_VREF, reg_val);

        //1. power down BiasCore
        reg_val = BRCM_READ_REG(base, AUDIOH_AUDIORX_BIAS);
        reg_val &= ~(AUDIOH_AUDIORX_BIAS_AUDIORX_BIAS_PWRUP_MASK);
        BRCM_WRITE_REG(base,  AUDIOH_AUDIORX_BIAS, reg_val);

    }

	return;
}

//============================================================================
//
// Function Name: chal_audio_dmic1_pwrctrl(CHAL_HANDLE handle, Boolean pwronoff)
//
// Description:    Set DMIC1 CLK and DATA Pad function
//
// Parameters:
//                handle   ---  the Hera audio handle
//				  pwronoff ---  on or off selection
// Return:        none
//
//============================================================================

cVoid chal_audio_dmic1_pwrctrl(CHAL_HANDLE handle, Boolean pwronoff)
{
#ifndef CENTRALIZED_PADCTRL
    cUInt32  regVal;
    cUInt32   function = 0x4;

    if (pwronoff == TRUE)
	function = 0x0;
    /* Select the function for DMIC0_CLK */
    /* For function = 0 (alt_fn1), this will be set as DMIC1_CLK */
    regVal = READ_REG32((KONA_PAD_CTRL_VA+PADCTRLREG_DMIC0CLK_OFFSET));
    regVal &= (~PADCTRLREG_DMIC0CLK_PINSEL_DMIC0CLK_MASK);
    regVal |= (function << PADCTRLREG_DMIC0CLK_PINSEL_DMIC0CLK_SHIFT);
    WRITE_REG32((KONA_PAD_CTRL_VA+PADCTRLREG_DMIC0CLK_OFFSET), regVal);

    /* Select the function for DMIC0_DATA */
    /* For function = 0 (alt_fn1), this will be set as DMIC1_DATA */
    regVal = READ_REG32((KONA_PAD_CTRL_VA+PADCTRLREG_DMIC0DQ_OFFSET));
    regVal &= (~PADCTRLREG_DMIC0DQ_PINSEL_DMIC0DQ_MASK);
    regVal |= (function << PADCTRLREG_DMIC0DQ_PINSEL_DMIC0DQ_SHIFT);
    WRITE_REG32((KONA_PAD_CTRL_VA+PADCTRLREG_DMIC0DQ_OFFSET), regVal);
#endif //#ifndef CENTRALIZED_PADCTRL
}

//============================================================================
//
// Function Name: chal_audio_dmic2_pwrctrl(CHAL_HANDLE handle, Boolean pwronoff)
//
// Description:    Set DMIC2 CLK and DATA Pad function
//
// Parameters:
//                handle   ---  the Hera audio handle
//				  pwronoff ---  on or off selection
// Return:        none
//
//============================================================================

cVoid chal_audio_dmic2_pwrctrl(CHAL_HANDLE handle, Boolean pwronoff)
{
#ifndef CENTRALIZED_PADCTRL
    cUInt32  regVal;
    cUInt32  function = 0x0;

    if (pwronoff == TRUE)
	function = 0x4;
 
    /* Select the function for GPIO33 */
    /* For function = 4 (alt_fn5), this will be set as DMIC2_CLK */
regVal = READ_REG32((KONA_PAD_CTRL_VA+PADCTRLREG_GPIO33_OFFSET));
    regVal &= (~PADCTRLREG_GPIO33_PINSEL_GPIO33_MASK);
    regVal |= (function << PADCTRLREG_GPIO33_PINSEL_GPIO33_SHIFT);
WRITE_REG32((KONA_PAD_CTRL_VA+PADCTRLREG_GPIO33_OFFSET), regVal);

    /* Select the function for GPIO34 */
    /* For function = 4 (alt_fn5), this will be set as DMIC2_DATA */
regVal = READ_REG32((KONA_PAD_CTRL_VA+PADCTRLREG_GPIO34_OFFSET));
    regVal &= (~PADCTRLREG_GPIO34_PINSEL_GPIO34_MASK);
    regVal |= (function << PADCTRLREG_GPIO34_PINSEL_GPIO34_SHIFT);
WRITE_REG32((KONA_PAD_CTRL_VA+PADCTRLREG_GPIO34_OFFSET), regVal);
        /* For FPGA no pads are present */
#endif //#ifndef CENTRALIZED_PADCTRL
}




