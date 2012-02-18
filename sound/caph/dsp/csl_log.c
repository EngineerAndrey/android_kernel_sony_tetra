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
*   @file   csl_log.c
*
*   @brief  This file contains CSL (DSP) API to audio logging interface
*
****************************************************************************/
#include <string.h>
#include "mobcom_types.h"
#include "shared.h"
#include "resultcode.h"
#include "csl_log.h"

extern AP_SharedMem_t	*vp_shared_mem;



//*********************************************************************
/**
*
*   CSL_LOG_Start starts audio logging for specified stream.
* 
*   @param    streamNumber	(in)	stream number 1:4
*   @param    controlInfo	(in)	control information
*
**********************************************************************/
Result_t CSL_LOG_Start(UInt16 streamNumber, UInt16 controlInfo)
{

	Result_t res = RESULT_OK;

	switch(streamNumber)
	{
		case 1:
			vp_shared_mem->shared_audio_stream_0_crtl = controlInfo;

			break;

		case 2:
			vp_shared_mem->shared_audio_stream_1_crtl = controlInfo;

			break;

		case 3:
			vp_shared_mem->shared_audio_stream_2_crtl = controlInfo;

			break;

		case 4:
			vp_shared_mem->shared_audio_stream_3_crtl = controlInfo;

			break;

		default:
			res = RESULT_ERROR;
			break;

	}
	return res;

} // CSL_LOG_Start


//*********************************************************************
/**
*
*   CSL_LOG_Start stops audio logging for specified stream.
* 
*   @param    streamNumber	(in)	stream number 1:4
*
**********************************************************************/
Result_t CSL_LOG_Stop(UInt16 streamNumber, UInt8 *flag)
{
	Result_t res = RESULT_OK;

	switch(streamNumber)
	{
		case 1:
			vp_shared_mem->shared_audio_stream_0_crtl = 0;

			break;

		case 2:
			vp_shared_mem->shared_audio_stream_1_crtl = 0;

			break;

		case 3:
			vp_shared_mem->shared_audio_stream_2_crtl = 0;

			break;

		case 4:
			vp_shared_mem->shared_audio_stream_3_crtl = 0;

			break;

		default:
			res = RESULT_ERROR;
			break;

	}

	if ( vp_shared_mem->shared_audio_stream_0_crtl == 0
		&& vp_shared_mem->shared_audio_stream_1_crtl == 0
		&& vp_shared_mem->shared_audio_stream_2_crtl == 0
		&& vp_shared_mem->shared_audio_stream_3_crtl == 0
		)
	{
		*flag = 1; //inform to audio driver for shut down
	}

	return res;
} // CSL_LOG_Stop


//*********************************************************************
/**
*
*   CSL_LOG_Read reads data from shared memory for audio logging.
* 
*   @param    streamNumber	(in)	stream number 1:4
*   @param    readIndex		(in)	index of ping-pong buffer 
*   @param    outBuf		(out)	output buffer
*   @param    controlInfo	(out)	logging control information
*   @return   UInt32				number of bytes read to the buffer
*
**********************************************************************/
UInt32 CSL_LOG_Read(UInt16 streamNumber, UInt16 readIndex, UInt8 *outBuf, UInt16 *controlInfo)
{
 UInt16 size;    //number of 16-bit words
 Audio_Logging_Buf_t* ptr;
 UInt16 sampleRate;	//To indicate 8kHz or 16kHz samplerate

	switch(streamNumber)
	{
		case 1:
			*controlInfo = vp_shared_mem->shared_audio_stream_0_crtl;
			ptr = (Audio_Logging_Buf_t *)&vp_shared_mem->shared_audio_stream_0[readIndex].param[0];
			sampleRate = vp_shared_mem->shared_audio_stream_0[readIndex].rate;

			break;

		case 2:
			*controlInfo = vp_shared_mem->shared_audio_stream_1_crtl;
			ptr = (Audio_Logging_Buf_t *)&vp_shared_mem->shared_audio_stream_1[readIndex].param[0];
			sampleRate = vp_shared_mem->shared_audio_stream_1[readIndex].rate;

			break;

		case 3:
			*controlInfo = vp_shared_mem->shared_audio_stream_2_crtl;
			ptr = (Audio_Logging_Buf_t *)&vp_shared_mem->shared_audio_stream_2[readIndex].param[0];
			sampleRate = vp_shared_mem->shared_audio_stream_2[readIndex].rate;

			break;

		case 4:
			*controlInfo = vp_shared_mem->shared_audio_stream_3_crtl;
			ptr = (Audio_Logging_Buf_t *)&vp_shared_mem->shared_audio_stream_3[readIndex].param[0];
			sampleRate = vp_shared_mem->shared_audio_stream_3[readIndex].rate;

			break;

		default:
			return 0;

	}
	
	/* if nothing to read */
	if(*controlInfo == 0)
	{
		return 0;

	} 


	/* check ctrl point */
	// 0x101 and 0x201 are only compressed frame cases
	if (*controlInfo == 0x101 || *controlInfo == 0x201)
	{
		// Compressed frame captured
		size = LOG_COMPRESSED_SIZE; //162
	}
	else
	{	//Sample rate is 16kHz
		if(sampleRate == 16000)
		{// PCM frame
			size = LOG_WB_SIZE-2; //642
		}
		//Sample rate is 8kHz
		else
		{
			size = LOG_NB_SIZE; //320
		}

	}
			

	/* get logging data */
	memcpy(outBuf, ptr, size);

	if(sampleRate == 16000)
	{
		*(outBuf+640) = (UInt8)(sampleRate&0xf);
		*(outBuf+641) = (UInt8)((sampleRate>>4)&0xf);
		return (size+2); //Sample rate is 16kHz
	}
	else
		return size; //Sample rate is 8kHz

} // CSL_LOG_Read

