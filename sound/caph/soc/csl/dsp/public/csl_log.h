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
*   @file   csl_log.h
*
*   @brief  This file contains CSL (DSP) API to audio logging interface
*
****************************************************************************/
#ifndef _CSL_LOG_H_
#define _CSL_LOG_H_

// ---- Include Files -------------------------------------------------------
#include "mobcom_types.h"

/**
 * @addtogroup CSL audio logging interface
 * @{
 */

#define	LOG_COMPRESSED_SIZE		162
#define	LOG_NB_SIZE				320
#define	LOG_WB_SIZE				642


// ---- Function Declarations -----------------------------------------
//*********************************************************************
/**
*
*   CSL_LOG_Start starts audio logging for specified stream.
* 
*   @param    streamNumber	(in)	stream number 1:4
*   @param    controlInfo	(in)	control information
*   @return   Result_t			RESULT_ERROR or RESULT_OK
*
**********************************************************************/
Result_t CSL_LOG_Start(UInt16 streamNumber, UInt16 controlInfo);

//*********************************************************************
/**
*
*   CSL_LOG_Start stops audio logging for specified stream.
* 
*   @param    streamNumber	(in)	stream number 1:4
*   @param    flag		(out)	shut down information   
*   @return   Result_t			RESULT_ERROR or RESULT_OK
*
**********************************************************************/
Result_t CSL_LOG_Stop(UInt16 streamNumber, UInt8 *flag);

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
UInt32 CSL_LOG_Read(UInt16 streamNumber, UInt16 readIndex, UInt8 *outBuf, UInt16 *controlInfo);

/** @} */

#endif //_CSL_LOG_H_
