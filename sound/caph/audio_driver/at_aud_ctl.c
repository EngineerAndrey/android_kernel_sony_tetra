/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
/*******************************************************************************************
Copyright 2010 - 2011 Broadcom Corporation.  All rights reserved.

Unless you and Broadcom execute a separate written software license agreement
governing use of this software, this software is licensed to you under the
terms of the GNU General Public License version 2, available at
http://www.gnu.org/copyleft/gpl.html (the "GPL").

Notwithstanding the above, under no circumstances may you combine this software
in any way with any other Broadcom software provided under a license other than
the GPL, without Broadcom's express prior written consent.
*******************************************************************************************/

#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/printk.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm_params.h>
#include <sound/pcm.h>
#include <sound/asound.h>
#include <sound/rawmidi.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include "mobcom_types.h"
#include "resultcode.h"
#include "audio_consts.h"

#include "bcm_fuse_sysparm_CIB.h"
#include "audio_ddriver.h"

#include "csl_caph.h"
#include "audio_vdriver.h"
#include "audio_controller.h"
#include "audio_caph.h"
#include "caph_common.h"
#include "auddrv_audlog.h"

#include <linux/io.h>
#include "brcm_rdb_sysmap.h"
#include "brcm_rdb_audioh.h"
#include "brcm_rdb_sdt.h"
#include "brcm_rdb_srcmixer.h"
#include "brcm_rdb_cph_cfifo.h"
#include "brcm_rdb_cph_aadmac.h"
#include "brcm_rdb_cph_ssasw.h"
#include "brcm_rdb_ahintc.h"
#include "brcm_rdb_util.h"

#include "csl_caph_hwctrl.h"

#include "audio_pmu_adapt.h"

extern int AUDDRV_Get_CP_AudioMode(void);

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//Description:
//	AT command handler, handle command AT commands at*maudmode=P1,P2,P3
//Parameters:
//	pChip -- Pointer to chip data structure
//   ParamCount -- Count of parameter array
//	Params  --- P1,P2,...,P6
//
/**
loopback:
at*maudmode=11, 1, 0  //HANDSET  ( 11,  AUDIO_SINK_Enum_t, AUDIO_SOURCE_Enum_t )
at*maudmode=12  //disable loopback

at*maudmode=11, 1, 4  //main mic to IHF
at*maudmode=12

at*maudmode=11,2,1  //headset
at*maudmode=12

**/
//---------------------------------------------------------------------------
int	AtMaudMode(brcm_alsa_chip_t* pChip, Int32	ParamCount, Int32 *Params)
{
    AUDIO_SOURCE_Enum_t mic = AUDIO_SOURCE_ANALOG_MAIN;
    AUDIO_SINK_Enum_t spk = AUDIO_SINK_HANDSET;
    int rtn = 0;  //0 means Ok
    static UInt8 loopback_status = 0, loopback_input = 0, loopback_output = 0;
    Int32 pCurSel[2];

	BCM_AUDIO_DEBUG("%s P1-P6=%ld %ld %ld %ld %ld %ld cnt=%ld\n", __FUNCTION__, Params[0], Params[1], Params[2], Params[3], Params[4], Params[5], ParamCount);

	csl_caph_ControlHWClock(TRUE);

	switch(Params[0])//P1
	{

		case 0:	//at*maudmode 0
			Params[0] = AUDCTRL_GetAudioMode();
			BCM_AUDIO_DEBUG("%s mode %ld \n", __FUNCTION__, Params[0]);
			break;

		case 1:	//at*maudmode 1 mode
			AUDCTRL_GetSrcSinkByMode(Params[1], &mic, &spk);
            pCurSel[0] = pChip->streamCtl[CTL_STREAM_PANEL_VOICECALL-1].iLineSelect[0]; //save current setting
            pCurSel[1] = pChip->streamCtl[CTL_STREAM_PANEL_VOICECALL-1].iLineSelect[1];

            // Update 'VC-SEL' --
			pChip->streamCtl[CTL_STREAM_PANEL_VOICECALL-1].iLineSelect[0] = mic;
			pChip->streamCtl[CTL_STREAM_PANEL_VOICECALL-1].iLineSelect[1] = spk;
#if !defined(USE_NEW_AUDIO_PARAM)
            AUDCTRL_SetAudioMode( Params[1] );
#else
			// need to fill in audio app (check with pcg) or keep the legacy here
            AUDCTRL_SetAudioMode( Params[1], AUDCTRL_GetAudioApp());
#endif
            BCM_AUDIO_DEBUG("%s mic %d spk %d mode %ld \n", __FUNCTION__, mic,spk,Params[1]);
            break;

        case 8:	//at*maudmode=8
            Params[0] = loopback_status;
            BCM_AUDIO_DEBUG("%s loopback status is %d \n", __FUNCTION__, loopback_status);
            break;

        case 9: //at*maudmode=9,x.  x = 0 --> disable; x = other --> enable loopback
            loopback_status = Params[1];
            if (loopback_status > 1) loopback_status = 1;

            AUDCTRL_SetAudioLoopback( loopback_status, loopback_input, loopback_output,1);
            BCM_AUDIO_DEBUG("%s enable loopback from src %d to sink %d \n", __FUNCTION__, loopback_input, loopback_output);
            break;

        case 10: //at*maudmode=10  --> get loopback path
            //Per PCG request
            //if (loopback_output > 2) loopback_output = 2;

            Params[0] = loopback_input;
            Params[1] = loopback_output;
            BCM_AUDIO_DEBUG("%s loopback path is from src %d to sink %d \n", __FUNCTION__, loopback_input, loopback_output);
            break;

        case 11: //at*maudmode=11,x,y  --> set loopback path.  doesn't affect audio mode
            loopback_input = Params[1];  //mic: 0 = default mic, 1 = main mic
            loopback_output = Params[2]; //spk: 0 = handset, 1 = headset, 2 = loud speaker
            /*if ( (loopback_input > 1) || (loopback_output > 2))
            {
                BCM_AUDIO_DEBUG("%s src or sink exceeds its range. Please enter src <= 1 and sink <=2\n", __FUNCTION__);
                rtn = -1;
                break;
            }*/
            if ( ((loopback_input > 4) && (loopback_input != 11))|| ((loopback_output > 2) && (loopback_output != 9) && (loopback_output != 4)) )
            {
                BCM_AUDIO_DEBUG("%s src or sink exceeds its range. Please enter src <= 4 or src = 1, and sink <=2 or sink =9\n", __FUNCTION__);
                rtn = -1;
                break;
            }
            if (loopback_output == 2)  // use IHF
                loopback_output = 4;
            if (loopback_input == 0)  // default mic: use main mic
                loopback_input = 1;

            loopback_status = 1;
            //enable the HW loopback from loopback_input to loopback_output without DSP.
            AUDCTRL_SetAudioLoopback( TRUE, loopback_input, loopback_output,1);

            BCM_AUDIO_DEBUG("%s enable loopback from src %d to sink %d \n", __FUNCTION__, loopback_input, loopback_output);
            break;

        case 12: //at*maudmode=12  --> disable loopback path
            loopback_status = 0;
            AUDCTRL_SetAudioLoopback( FALSE, loopback_input, loopback_output,1);
            //mdelay(100);
            BCM_AUDIO_DEBUG("%s disable loopback from src %d to sink %d \n", __FUNCTION__, loopback_input, loopback_output);
            break;

        case 13: //at*maudmode=13  --> Get call ID
			BCM_AUDIO_DEBUG("%s get call ID is not supported \n", __FUNCTION__);
            rtn = -1;
            break;

        case 14: //at*maudmode=14  --> read current mode and operation
			BCM_AUDIO_DEBUG("%s read current mode and operation is not supported \n", __FUNCTION__);
            rtn = -1;
			break;

        case 15: //at*maudmode=15  --> set current mode and operation
            BCM_AUDIO_DEBUG("%s set current mode and operation is not supported \n", __FUNCTION__);
            rtn = -1;
			break;

		case 99: //at*maudmode=99  --> stop tuning
			break;

		case 100: //at*maudmode=100  --> set external audio amplifer gain in PMU
			//PCG and loadcal currently use Q13p2 gain format
#ifdef CONFIG_BCMPMU_AUDIO
            {
            PMU_AudioGainMapping_t pmu_gain;
            short gain;
            
			gain = (short)Params[3];

			if (Params[1] == 3)
			if( Params[2]==PARAM_PMU_SPEAKER_PGA_LEFT_CHANNEL || Params[2]==PARAM_PMU_SPEAKER_PGA_RIGHT_CHANNEL ) // EXT_SPEAKER_PGA
			{
#if defined(USE_NEW_AUDIO_PARAM)
				if ( (AUDDRV_GetAudioMode()==AUDIO_MODE_HEADSET)
					 || (AUDDRV_GetAudioMode()==AUDIO_MODE_TTY))
#else
				if ( (AUDDRV_GetAudioMode()==AUDIO_MODE_HEADSET) || (AUDDRV_GetAudioMode()==AUDIO_MODE_HEADSET_WB)
					 || (AUDDRV_GetAudioMode()==AUDIO_MODE_TTY) || (AUDDRV_GetAudioMode()==AUDIO_MODE_TTY_WB) )
#endif
				{
					AUDIO_PMU_IHF_POWER(FALSE);
					AUDIO_PMU_HS_POWER(TRUE);
					BCM_AUDIO_DEBUG("%s ext headset speaker gain = %d \n", __FUNCTION__, gain );
					pmu_gain = map2pmu_hs_gain( 25 * gain );
					BCM_AUDIO_DEBUG("%s ext headset speaker gain = %d after lookup \n", __FUNCTION__, pmu_gain.PMU_gain_enum );

					if( Params[2]==PARAM_PMU_SPEAKER_PGA_LEFT_CHANNEL )
						AUDIO_PMU_HS_SET_GAIN(PMU_AUDIO_HS_LEFT, pmu_gain.PMU_gain_enum );
					else
					if( Params[2]==PARAM_PMU_SPEAKER_PGA_RIGHT_CHANNEL )
						AUDIO_PMU_HS_SET_GAIN(PMU_AUDIO_HS_RIGHT, pmu_gain.PMU_gain_enum );

				}
#if defined(USE_NEW_AUDIO_PARAM)
				else if (AUDDRV_GetAudioMode()==AUDIO_MODE_SPEAKERPHONE)
#else
				else if ( (AUDDRV_GetAudioMode()==AUDIO_MODE_SPEAKERPHONE) || (AUDDRV_GetAudioMode()==AUDIO_MODE_SPEAKERPHONE_WB) )
#endif
				{
					AUDIO_PMU_HS_POWER(FALSE);
					AUDIO_PMU_IHF_POWER(TRUE);
					BCM_AUDIO_DEBUG("%s ext IHF speaker gain = %d \n", __FUNCTION__, gain );
					pmu_gain = map2pmu_ihf_gain( 25 * gain );
					BCM_AUDIO_DEBUG("%s ext IHF speaker gain = %d after lookup \n", __FUNCTION__, pmu_gain.PMU_gain_enum );
					AUDIO_PMU_IHF_SET_GAIN( pmu_gain.PMU_gain_enum );
				}
			}
            }
#endif

			break;

		default:
			BCM_AUDIO_DEBUG("%s Unsupported cmd %ld \n", __FUNCTION__, Params[0]);
            rtn = -1;
			break;
	}

	return rtn;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//Description:
//	AT command handler, handle command AT commands at+maudlooptest=P1,P2,P3
//Parameters:
//	pChip -- Pointer to chip data structure
//   ParamCount -- Count of parameter array
//	Params  --- P1,P2,...,P6
//---------------------------------------------------------------------------
int	AtMaudLoopback(brcm_alsa_chip_t* pChip, Int32	ParamCount, Int32 *Params)
{
	BCM_AUDIO_DEBUG("%s P1-P6=%ld %ld %ld %ld %ld %ld cnt=%ld\n", __FUNCTION__, Params[0], Params[1], Params[2], Params[3], Params[4], Params[5], ParamCount);

	return -1;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//Description:
//	AT command handler, handle command AT commands at*maudlog=P1,P2,P3
//Parameters:
//	pChip -- Pointer to chip data structure
//   ParamCount -- Count of parameter array
//	Params  --- P1,P2,...,P6
//---------------------------------------------------------------------------
int	AtMaudLog(brcm_alsa_chip_t* pChip, Int32	ParamCount, Int32 *Params)
{
    int rtn = 0;

	BCM_AUDIO_DEBUG("%s P1-P6=%ld %ld %ld %ld %ld %ld cnt=%ld\n", __FUNCTION__, Params[0], Params[1], Params[2], Params[3], Params[4], Params[5], ParamCount);

    switch(Params[0])//P1
	{
        case 1:	//at*maudlog=1,x,y,z
            rtn= AUDDRV_AudLog_Start(Params[1],Params[2],Params[3],(char *) NULL);
            if (rtn < 0 )
            {
                BCM_AUDIO_DEBUG("\n Couldnt setup channel \n");
                rtn = -1;
            }
			BCM_AUDIO_DEBUG("%s start log on stream %ld, capture pt %ld, consumer %ld \n", __FUNCTION__, Params[1],Params[2],Params[3]);
            break;

        case 2:	//at*maudlog=2,x
            rtn = AUDDRV_AudLog_Stop(Params[1]);
            if (rtn < 0 )
            {
                rtn = -1;
            }
			BCM_AUDIO_DEBUG("%s start log on stream %ld \n", __FUNCTION__, Params[1]);
			break;

        default:
            rtn = -1;
            break;
    }

	return rtn;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//Description:
//	AT command handler, handle command AT commands at*maudtst=P1,P2,P3,P4,P5,P6
//Parameters:
//	pChip -- Pointer to chip data structure
//   ParamCount -- Count of parameter array
//	Params  --- P1,P2,...,P6
//---------------------------------------------------------------------------
int	AtMaudTst(brcm_alsa_chip_t* pChip, Int32	ParamCount, Int32 *Params)
{
	BCM_AUDIO_DEBUG("%s P1-P6=%ld %ld %ld %ld %ld %ld cnt=%ld\n", __FUNCTION__, Params[0], Params[1], Params[2], Params[3], Params[4], Params[5], ParamCount);

	 //this test command argument 100/101 is provided to control the HW clock.In that case, dont enable the clock
	if(Params[0] != 100 && Params[0] != 101)
		csl_caph_ControlHWClock(TRUE);

    switch(Params[0])//P1
    {
#if defined(USE_NEW_AUDIO_PARAM)
		{
		Int32 pCurSel[2];
		AUDIO_SOURCE_Enum_t mic = AUDIO_SOURCE_ANALOG_MAIN;
		AUDIO_SINK_Enum_t spk = AUDIO_SINK_HANDSET;

		case 2:	//at*maudtst 2: return both mode and app for multi app test
			Params[1] = AUDCTRL_GetAudioMode();
			Params[2] = AUDCTRL_GetAudioApp();
			BCM_AUDIO_DEBUG("%s mode %ld app %ld\n", __FUNCTION__, Params[1], Params[2]);
			break;

		case 3:	//at*maudtst 3 mode app: set mode and app for multi app test
			AUDCTRL_GetSrcSinkByMode(Params[1], &mic, &spk);
            pCurSel[0] = pChip->streamCtl[CTL_STREAM_PANEL_VOICECALL-1].iLineSelect[0]; //save current setting
            pCurSel[1] = pChip->streamCtl[CTL_STREAM_PANEL_VOICECALL-1].iLineSelect[1];

            // Update 'VC-SEL' --
			pChip->streamCtl[CTL_STREAM_PANEL_VOICECALL-1].iLineSelect[0] = mic;
			pChip->streamCtl[CTL_STREAM_PANEL_VOICECALL-1].iLineSelect[1] = spk;
			// need to fill in audio app (check with pcg)
            AUDCTRL_SetAudioMode( Params[1], Params[2]);

            BCM_AUDIO_DEBUG("%s mic %d spk %d mode %ld app %ld\n", __FUNCTION__, mic,spk,Params[1], Params[2]);
            break;
		}
#endif
		case 25:
			AUDCTRL_SetPlayVolume(
                      AUDIO_SOURCE_MEM,
                      Params[1],  //	  //speaker channel
                      AUDIO_GAIN_FORMAT_mB,
					  Params[2],  //left volume
                      Params[3],//right volume
                      0);

            BCM_AUDIO_DEBUG( "Set speaker volume left %ld right %ld \n", Params[2], Params[3] );
			break;

		//! typedef enum AUDIO_SINK_Enum_t {
		//!	AUDIO_SINK_HANDSET,
		//!	AUDIO_SINK_HEADSET,
		//!	AUDIO_SINK_HANDSFREE,
		//!	AUDIO_SINK_BTM,  //Bluetooth HFP
		//!	AUDIO_SINK_LOUDSPK,
		//!	AUDIO_SINK_TTY,
		//!	AUDIO_SINK_HAC,
		//!	AUDIO_SINK_USB,
		//!	AUDIO_SINK_BTS,  //Bluetooth A2DP
		//!	AUDIO_SINK_I2S,
		//!	AUDIO_SINK_VIBRA,
		//! -------------------------------------------------------------------------------------
	case 26:
			AUDCTRL_SetPlayMute( AUDIO_SOURCE_UNDEFINED,
				     Params[1],  //speaker channel
				     Params[2],  // mute flag   1 - mute   0 - un-mute
                     0);

			if( Params[2] ==0)
				BCM_AUDIO_DEBUG( "Set speaker un-mute \n" );
			else
				BCM_AUDIO_DEBUG( "Set speaker mute \n" );

			break;

			//! AT*MAUDTST=32, p2, p3
			//!
			//! Function:	 Mute / un-mute microphone of an operation.
			//!
			//! Params: 	 p2 : operation
			//! 			 p3 = mute flag
			//! 				   1 - mute
			//! 				   0 - un-mute
			//! -------------------------------------------------------------------------------------
	case 32:
			AUDCTRL_SetTelephonyMicMute( AUDIO_SOURCE_UNDEFINED, (Boolean) Params[2] );
			break;

	case 33:
	  AUDCTRL_SetTelephonySpkrVolume( AUDIO_SINK_UNDEFINED,
					Params[1],
					AUDIO_GAIN_FORMAT_DSP_VOICE_VOL_GAIN
					);
			break;
	case 34:
	  AUDCTRL_SetPlayVolume(
				AUDIO_SOURCE_I2S,
				AUDIO_SINK_LOUDSPK,
				AUDIO_GAIN_FORMAT_FM_RADIO_DIGITAL_VOLUME_TABLE,
				Params[1],
				0,
				0
				);
			break;
	case 35:
	  AUDCTRL_SetPlayVolume(
				AUDIO_SOURCE_I2S,
				AUDIO_SINK_HEADSET,
				AUDIO_GAIN_FORMAT_FM_RADIO_DIGITAL_VOLUME_TABLE,
				Params[1],
				0,
				0
				);
			break;
	case 37:
		BCM_AUDIO_DEBUG("CP audio mode %d\n", AUDDRV_Get_CP_AudioMode());
		break;

	case 100:
			if(Params[1] == 1)
			{
				BCM_AUDIO_DEBUG( "Enable CAPH clock \n" );
				csl_caph_ControlHWClock(TRUE);
			}
			else if(Params[1] == 0)
			{
				BCM_AUDIO_DEBUG( "Disable CAPH clock \n" );
				csl_caph_ControlHWClock(FALSE);
			}
			break;

	case 101:
			Params[0] = (Int32)csl_caph_QueryHWClock();
			BCM_AUDIO_DEBUG( "csl_caph_QueryHWClock %ld \n",Params[0] );
			break;

	case 121: //at*maudtst=121,x,y  // x=0: EXT_SPEAKER_PGA, x=1:EXT_SPEAKER_PREPGA, x=2: MIC_PGA, y: gain value register value(enum value)
			//PCG on Rhea platform uses at*maudmode=100,3, 
            break;

    case 500: //at*maudtst=500,
            {
			char *address;
            unsigned int value = 0, gain1=0, gain2=0, gain3=0, gain4=0;
            unsigned int index;

#define CHAL_CAPH_SRCM_MAX_FIFOS      15
#define SRCMIXER_MIX_CH_GAIN_CTRL_OFFSET      ( (SRCMIXER_SRC_M1D1_CH1M_GAIN_CTRL_OFFSET - SRCMIXER_SRC_M1D0_CH1M_GAIN_CTRL_OFFSET)/4 ) //4bytes

			address = (char*)ioremap_nocache((UInt32) (AUDIOH_BASE_ADDR + AUDIOH_AUDIORX_VRX1_OFFSET), sizeof(UInt32));
			if(!address)
			{
				pr_err(" address ioremap failed\n");
				return 0;
			}

			value = ioread32(address);

			iounmap(address);

            gain1 = value;
            gain1 &= (AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_GAINCTRL_MASK);
            gain1 >>= (AUDIOH_AUDIORX_VRX1_AUDIORX_VRX_GAINCTRL_SHIFT);

            pr_err(" AUDIOH_AUDIORX_VRX1=0x%x, AMIC_PGA=0x%x \n", value, gain1 );

			address = (char*)ioremap_nocache((UInt32) (AUDIOH_BASE_ADDR + AUDIOH_VIN_FILTER_CTRL_OFFSET), sizeof(UInt32));
			if(!address)
			{
				pr_err(" address ioremap failed\n");
				return 0;
			}

			value = ioread32(address);

			iounmap(address);

            gain1 = value;
            gain1 &= (AUDIOH_VIN_FILTER_CTRL_DMIC1_CIC_BIT_SEL_MASK);
            gain1 >>= (AUDIOH_VIN_FILTER_CTRL_DMIC1_CIC_BIT_SEL_SHIFT);

            gain2 = value;
            gain2 &= (AUDIOH_VIN_FILTER_CTRL_DMIC1_CIC_FINE_SCL_MASK);
            gain2 >>= (AUDIOH_VIN_FILTER_CTRL_DMIC1_CIC_FINE_SCL_SHIFT);

            gain3 = value;
            gain3 &= (AUDIOH_VIN_FILTER_CTRL_DMIC2_CIC_BIT_SEL_MASK);
            gain3 >>= (AUDIOH_VIN_FILTER_CTRL_DMIC2_CIC_BIT_SEL_SHIFT);

            gain4 = value;
            gain4 &= (AUDIOH_VIN_FILTER_CTRL_DMIC2_CIC_FINE_SCL_MASK);
            gain4 >>= (AUDIOH_VIN_FILTER_CTRL_DMIC2_CIC_FINE_SCL_SHIFT);

            pr_err(" AUDIOH_VIN_FILTER_CTRL=0x%x \n", value );
            pr_err("DMIC1_CIC_BIT_SEL=0x%x, DMIC1_CIC_FINE=0x%x \n", gain1, gain2 );
            pr_err("DMIC2_CIC_BIT_SEL=0x%x, DMIC2_CIC_FINE_SCL=0x%x \n", gain3, gain4 );


			address = (char*)ioremap_nocache((UInt32) (AUDIOH_BASE_ADDR + AUDIOH_NVIN_FILTER_CTRL_OFFSET), sizeof(UInt32));
			if(!address)
			{
				pr_err(" address ioremap failed\n");
				return 0;
			}

			value = ioread32(address);

			iounmap(address);

            gain1 = value;
            gain1 &= (AUDIOH_NVIN_FILTER_CTRL_DMIC3_CIC_BIT_SEL_MASK);
            gain1 >>= (AUDIOH_NVIN_FILTER_CTRL_DMIC3_CIC_BIT_SEL_SHIFT);

            gain2 = value;
            gain2 &= (AUDIOH_NVIN_FILTER_CTRL_DMIC3_CIC_FINE_SCL_MASK);
            gain2 >>= (AUDIOH_NVIN_FILTER_CTRL_DMIC3_CIC_FINE_SCL_SHIFT);

            gain3 = value;
            gain3 &= (AUDIOH_NVIN_FILTER_CTRL_DMIC4_CIC_BIT_SEL_MASK);
            gain3 >>= (AUDIOH_NVIN_FILTER_CTRL_DMIC4_CIC_BIT_SEL_SHIFT);

            gain4 = value;
            gain4 &= (AUDIOH_NVIN_FILTER_CTRL_DMIC4_CIC_FINE_SCL_MASK);
            gain4 >>= (AUDIOH_NVIN_FILTER_CTRL_DMIC4_CIC_FINE_SCL_SHIFT);

            pr_err(" AUDIOH_VIN_FILTER_CTRL=0x%x \n", value );
            pr_err("DMIC3_CIC_BIT_SEL=0x%x, DMIC3_CIC_FINE=0x%x \n", gain1, gain2 );
            pr_err("DMIC4_CIC_BIT_SEL=0x%x, DMIC4_CIC_FINE_SCL=0x%x \n", gain3, gain4 );

			//**********
			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D0_CH1M_GAIN_CTRL_OFFSET), sizeof(UInt32));
			if(!address)
			{
				pr_err(" address ioremap failed\n");
				return 0;
			}
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH1M_GAIN_CTRL_SRC_M1D0_CH1M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH1M_GAIN_CTRL_SRC_M1D0_CH1M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH1M_GAIN_CTRL_SRC_M1D0_CH1M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH1M_GAIN_CTRL_SRC_M1D0_CH1M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D0, channel 1, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D0, channel 2, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D0_CH3M_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D0, channel 3, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D0_CH4M_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D0, channel 4, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D0_CH5L_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D0, channel 5 left, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D0_CH5R_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D0, channel 5 right, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D0_CH6L_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D0, channel 6 left, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D0_CH6R_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D0, channel 6 right, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D0_CH7L_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D0, channel 7 left, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D0_CH7R_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D0, channel 7 right, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			//**********
            address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D1_CH1M_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH1M_GAIN_CTRL_SRC_M1D0_CH1M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH1M_GAIN_CTRL_SRC_M1D0_CH1M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH1M_GAIN_CTRL_SRC_M1D0_CH1M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH1M_GAIN_CTRL_SRC_M1D0_CH1M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D1, channel 1, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D1_CH2M_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D1, channel 2, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D1_CH3M_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D1, channel 3, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D1_CH4M_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D1, channel 4, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D1_CH5L_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D1, channel 5 left, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D1_CH5R_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D1, channel 5 right, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D1_CH6L_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D1, channel 6 left, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D1_CH6R_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D1, channel 6 right, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D1_CH7L_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D1, channel 7 left, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + SRCMIXER_SRC_M1D1_CH7R_GAIN_CTRL_OFFSET), sizeof(UInt32));
			value = ioread32(address);
			iounmap(address);

            gain1 = value;
            gain1 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_MASK);
            gain1 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_GAIN_RAMPSTEP_SHIFT);

            gain2 = value;
            gain2 &= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_MASK);
            gain2 >>= (SRCMIXER_SRC_M1D0_CH2M_GAIN_CTRL_SRC_M1D0_CH2M_TARGET_GAIN_SHIFT);

            pr_err("MIXER 1 D1, channel 7 right, Target_Gain=0x%x, Gain_RampStep=0x%x \n", gain2, gain1 );

			//**********
			for ( index = 0x00000160; index <= 0x00000270; index=index+4 )
			{
			    address = (char*)ioremap_nocache((UInt32) (SRCMIXER_BASE_ADDR + index), sizeof(UInt32));
			    value = ioread32(address);
			    iounmap(address);
                pr_err("SRCMIXER_BASE_ADDR + offset 0x%x, value = 0x%x \n", index, value );
			}

        }
        break;
	case 1000: //at*maudtst=1000,addr,len
		{
			u32 value, index, phy_addr, size;
			char *addr;

			if(Params[2]==0) size = 1;
			else size = Params[2] >> 2;

			phy_addr = Params[1];

			addr = ioremap_nocache(phy_addr, sizeof(u32));
			if(!addr)
			{
				pr_err(" addr ioremap failed\n");
				return 0;
			}

			pr_err("Read phy_addr 0x%08x (virtual %p), size = 0x%08x bytes\n", phy_addr, addr, size<<2);
			iounmap(addr);

			for ( index = 0; index < size; index++ )
			{
				addr = ioremap_nocache(phy_addr, sizeof(u32));
				if(addr)
				{
					value = ioread32(addr);
					iounmap(addr);
					pr_err("[%08x] = %08x\n", phy_addr, value);
				}
				phy_addr += 4;
			}
        }
        break;

        default:
            BCM_AUDIO_DEBUG("%s Not supported command \n", __FUNCTION__);
            return -1;
    }
	return 0;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//Description:
//	AT command handler, handle command AT commands at*maudvol=P1,P2,P3
//Parameters:
//	pChip -- Pointer to chip data structure
//   ParamCount -- Count of parameter array
//	Params  --- P1,P2,...,P6
//---------------------------------------------------------------------------
int	AtMaudVol(brcm_alsa_chip_t* pChip, Int32	ParamCount, Int32 *Params)
{
	int *pVolume;
	int mode, vol;

	BCM_AUDIO_DEBUG("%s P1-P6=%ld %ld %ld %ld %ld %ld cnt=%ld\n", __FUNCTION__, Params[0], Params[1], Params[2], Params[3], Params[4], Params[5], ParamCount);

	csl_caph_ControlHWClock(TRUE);

	switch(Params[0])//P1
	{
	case 6:	//at*maudvol=6
		mode = AUDCTRL_GetAudioMode();
		//Get volume from driver. Range -36 ~ 0 dB in Driver and DSP:
		Params[0] = AUDCTRL_GetTelephonySpkrVolume( AUDIO_GAIN_FORMAT_mB );
		Params[0] = Params[0]/100;  //dB
		Params[0] += AUDIO_GetParmAccessPtr()[mode].voice_volume_max;//Range 0~36 dB shown in PCG
		//or
		//pVolume = pChip->streamCtl[CTL_STREAM_PANEL_VOICECALL -1].ctlLine[mode].iVolume;
		//Params[0] = pVolume[0];
		BCM_AUDIO_DEBUG("%s pVolume[0] %ld \n", __FUNCTION__, Params[0]);
		return 0;

	case 7: //at*maudvol=7,x    Range 0~36 dB in PCG
		mode = AUDCTRL_GetAudioMode();
		//mode = pChip->streamCtl[CTL_STREAM_PANEL_VOICECALL-1].iLineSelect[1];
		pVolume = pChip->streamCtl[CTL_STREAM_PANEL_VOICECALL -1].ctlLine[mode].iVolume;
		pVolume[0] = Params[1];
		pVolume[1] = Params[1];
		vol = Params[1];
		vol -= AUDIO_GetParmAccessPtr()[mode].voice_volume_max;//Range -36 ~ 0 dB in DSP
		AUDCTRL_SetTelephonySpkrVolume(	AUDIO_SINK_UNDEFINED,
							(vol*100),   //Params[1] in dB
							AUDIO_GAIN_FORMAT_mB
							);

		BCM_AUDIO_DEBUG("%s pVolume[0] %d mode=%d vol %d \n", __FUNCTION__, pVolume[0],mode, vol);
		return 0;

	default:
		BCM_AUDIO_DEBUG("%s Unsupported cmd %ld \n", __FUNCTION__, Params[0]);
		break;
	}
	return -1;
}



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//Description:
//	Kernel AT command handler and gernal purpose controls
//Parameters
//	cmdIndex -- command index coresponding to AT comand
//	ParamCount	 -- count of array Params
//	Params -- Parameter array
//Return
//   0 on success, -1 otherwise
//------------------------------------------------------------------------------------------
int	AtAudCtlHandler_put(Int32 cmdIndex, brcm_alsa_chip_t* pChip, Int32	ParamCount, Int32 *Params)
{
    int rtn = 0;

	BCM_AUDIO_DEBUG("AT-AUD-put ctl=%ld ParamCount= %ld [%ld %ld %ld %ld %ld %ld %ld]\n", cmdIndex, ParamCount, Params[0],Params[1],Params[2], Params[3],Params[4],Params[5],Params[6]);

	switch(cmdIndex)
	{
		case AT_AUD_CTL_INDEX:
			{
				int count = sizeof(pChip->i32AtAudHandlerParms)/sizeof(pChip->i32AtAudHandlerParms[0]);
				if(count>ParamCount)
					count = ParamCount;
				memcpy(pChip->i32AtAudHandlerParms, Params, sizeof(pChip->i32AtAudHandlerParms[0])*count);
				return 0;
			}
		case AT_AUD_CTL_DBG_LEVEL:
			gAudioDebugLevel = Params[0];
			return 0;
		case AT_AUD_CTL_HANDLER:
			break;//go next ..
		default:
			return -1;//unlikely

	}

	switch(Params[0])
	{

		case AT_AUD_HANDLER_MODE:
			rtn = AtMaudMode(pChip,ParamCount-1, &Params[1]);
            break;
		case AT_AUD_HANDLER_VOL:
			rtn = AtMaudVol(pChip,ParamCount-1, &Params[1]);
            break;
		case AT_AUD_HANDLER_TST:
			rtn = AtMaudTst(pChip,ParamCount-1, &Params[1]);
            break;
		case AT_AUD_HANDLER_LOG:
			rtn = AtMaudLog(pChip,ParamCount-1, &Params[1]);
            break;
		case AT_AUD_HANDLER_LBTST:
			rtn = AtMaudLoopback(pChip,ParamCount-1, &Params[1]);
            break;
		default:
			BCM_AUDIO_DEBUG("%s Unsupported handler %ld \n", __FUNCTION__, Params[0]);
            rtn = -1;
			break;
	}

	return rtn;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//Description:
//	Kernel AT command handler and gernal purpose controls
//Parameters
//	cmdIndex -- command index coresponding to AT comand
//	ParamCount	 -- count of array Params
//	Params -- Parameter array
//Return
//   0 on success, -1 otherwise
//------------------------------------------------------------------------------------------
int	AtAudCtlHandler_get(Int32 cmdIndex, brcm_alsa_chip_t* pChip, Int32	ParamCount, Int32 *Params)
{
	int count = sizeof(pChip->i32AtAudHandlerParms)/sizeof(pChip->i32AtAudHandlerParms[0]);
    int rtn = 0;

	BCM_AUDIO_DEBUG("AT-AUD-get ctl=%ld ParamCount= %ld [%ld %ld %ld %ld %ld %ld %ld]\n", cmdIndex, ParamCount, Params[0],Params[1],Params[2], Params[3],Params[4],Params[5],Params[6]);

	switch(cmdIndex)
	{

		case AT_AUD_CTL_HANDLER:
		case AT_AUD_CTL_INDEX:
			{
				if(count>ParamCount)
					count = ParamCount;
				memcpy(Params, pChip->i32AtAudHandlerParms, sizeof(pChip->i32AtAudHandlerParms[0])*count);
				if(cmdIndex==AT_AUD_CTL_INDEX)
					return 0;
				else
					break; //continue
			}
		case AT_AUD_CTL_DBG_LEVEL:
			Params[0] = gAudioDebugLevel;
			return 0;
		default:
			return -1;//unlikely

	}

	switch(Params[0])
	{
		case AT_AUD_HANDLER_MODE:
			rtn = AtMaudMode(pChip,ParamCount-1, &Params[1]);
            break;
		case AT_AUD_HANDLER_VOL:
			rtn = AtMaudVol(pChip,ParamCount-1, &Params[1]);
            break;
		case AT_AUD_HANDLER_TST:
			rtn = AtMaudTst(pChip,ParamCount-1, &Params[1]);
            break;
		case AT_AUD_HANDLER_LOG:
			rtn = AtMaudLog(pChip,ParamCount-1, &Params[1]);
            break;
		case AT_AUD_HANDLER_LBTST:
			rtn = AtMaudLoopback(pChip,ParamCount-1, &Params[1]);
            break;
		default:
			BCM_AUDIO_DEBUG("%s Unsupported handler %ld \n", __FUNCTION__, Params[0]);
            rtn = -1;
			break;
	}

	return rtn;
}

