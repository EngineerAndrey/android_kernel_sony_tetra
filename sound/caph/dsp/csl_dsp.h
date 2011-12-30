//*********************************************************************
//
// (c)1999-2011 Broadcom Corporation
//
// Unless you and Broadcom execute a separate written software license agreement governing use of this software,
// this software is licensed to you under the terms of the GNU General Public License version 2,
// available at http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
//
//*********************************************************************
/**
*
*   @file   csl_dsp.h
*
*   @brief  This file contains AP interface functions to DSP
*
****************************************************************************/
#ifndef _CSL_DSP_H_
#define _CSL_DSP_H_

#include "mobcom_types.h"

typedef void (*VPUCaptureStatusCB_t)(UInt16 bufferIndex);
typedef void (*VPURenderStatusCB_t)(UInt16 bufferIndex);
typedef void (*WBAMRRenderStatusCB_t)(UInt16 status, UInt16 dsp_read_index, UInt16 dsp_write_index);
typedef void (*USBStatusCB_t)(UInt16 param1, UInt16 param2, UInt16 param3);
typedef void (*VOIFStatusCB_t)(UInt16 param1, UInt16 param2);
typedef void (*ARM2SPRenderStatusCB_t)(UInt16 bufferPosition);
typedef void (*ARM2SP2RenderStatusCB_t)(UInt16 bufferPosition);
typedef void (*WBAMRCaptureStatusCB_t)(UInt16 size, UInt16 index);
typedef void (*MainAMRStatusCB_t)(UInt16 codecType);
typedef void (*VoIPStatusCB_t)(void);
typedef void (*UserStatusCB_t)(UInt32 param1, UInt32 param2, UInt32 param3);
typedef void (*AudioLogStatusCB_t)(UInt16 bufferIndex);
typedef void (*AudioEnableDoneStatusCB_t)(UInt16 enabled_audio_path);

//*********************************************************************
/**
*
*   VPSHAREDMEM_Init initializes AP interface to DSP.
*
*   @param    dsp_shared_mem (in)	AP shared memory address 
* 
**********************************************************************/
void VPSHAREDMEM_Init(UInt32 *dsp_shared_mem);

//*********************************************************************
/**
*
*   CSL_RegisterVPUCaptureStatusHandler registers VPU capture status handler.
*
*   @param    callbackFunction	(in)	callback function to register 
* 
**********************************************************************/
void CSL_RegisterVPUCaptureStatusHandler(VPUCaptureStatusCB_t callbackFunction);

//*********************************************************************
/**
*
*   CSL_RegisterVPURenderStatusHandler registers VPU render status handler.
*
*   @param    callbackFunction	(in)	callback function to register 
* 
**********************************************************************/
void CSL_RegisterVPURenderStatusHandler(VPURenderStatusCB_t callbackFunction);

//*********************************************************************
/**
*
*   CSL_RegisterUSBStatusHandler registers USB status handler.
*
*   @param    callbackFunction	(in)	callback function to register 
* 
**********************************************************************/
void CSL_RegisterUSBStatusHandler(USBStatusCB_t callbackFunction);

//*********************************************************************
/**
*
*   CSL_RegisterVOIFStatusHandler registers VOIF status handler.
*
*   @param    callbackFunction	(in)	callback function to register 
* 
**********************************************************************/
void CSL_RegisterVOIFStatusHandler(VOIFStatusCB_t callbackFunction);

//*********************************************************************
/**
*
*   CSL_RegisterARM2SPRenderStatusHandler registers main ARM2SP render 
*	status handler.
*
*   @param    callbackFunction	(in)	callback function to register 
* 
**********************************************************************/
void CSL_RegisterARM2SPRenderStatusHandler(ARM2SPRenderStatusCB_t callbackFunction);

//*********************************************************************
/**
*
*   CSL_RegisterARM2SP2RenderStatusHandler registers main ARM2SP2 render 
*	status handler.
*
*   @param    callbackFunction	(in)	callback function to register 
* 
**********************************************************************/
void CSL_RegisterARM2SP2RenderStatusHandler(ARM2SP2RenderStatusCB_t callbackFunction);

//*********************************************************************
/**
*
*   CSL_RegisterMainAMRStatusHandler registers main AMR status handler.
*
*   @param    callbackFunction	(in)	callback function to register 
* 
**********************************************************************/
void CSL_RegisterMainAMRStatusHandler(MainAMRStatusCB_t callbackFunction);

//*********************************************************************
/**
*
*   CSL_RegisterAudioLogHandler registers VoIP status handler.
*
*   @param    callbackFunction	(in)	callback function to register 
* 
**********************************************************************/
void CSL_RegisterVoIPStatusHandler(VoIPStatusCB_t callbackFunction);

#if defined(ENABLE_SPKPROT)
//*********************************************************************
/**
*
*   CSL_RegisterAudioLogHandler registers user status handler.
*
*   @param    callbackFunction	(in)	callback function to register 
* 
**********************************************************************/
void CSL_RegisterUserStatusHandler(UserStatusCB_t callbackFunction);
#endif

//*********************************************************************
/**
*
*   CSL_RegisterAudioLogHandler registers audio logging status handler.
*
*   @param    callbackFunction	(in)	callback function to register 
* 
**********************************************************************/
void CSL_RegisterAudioLogHandler(AudioLogStatusCB_t callbackFunction);

//*********************************************************************
/**
*
*   CSL_RegisterAudioEnableDoneHandler registers audio enable done 
*   status handler.
*
*   @param    callbackFunction	(in)	callback function to register 
* 
**********************************************************************/
void CSL_RegisterAudioEnableDoneHandler(AudioEnableDoneStatusCB_t callbackFunction);


//*********************************************************************
/**
*
*   AP_ProcessStatus processes VPU status message from DSP on AP.
*
* 
**********************************************************************/
void AP_ProcessStatus(void);

/*****************************************************************************************/
/**
* 
* Function Name: AUDIO_Return_IHF_48kHz_buffer_base_address
*
*   @note     This function returns the base address to the shared memory buffer where
*             the ping-pong 48kHz data would be stored for AADMAC to pick them up
*             for IHF case. This base address needs to be programmed to the 
*             AADMAC_CTRL1 register.
*                                                                                         
*   @return   ptr (UInt32 *) Pointer to the base address of shared memory ping-pong buffer 
*                            for transferring 48kHz speech data from DSP to AADMAC for IHF.
*
**/
/*******************************************************************************************/
UInt32 *AUDIO_Return_IHF_48kHz_buffer_base_address(void);

#endif //_CSL_DSP_H_
