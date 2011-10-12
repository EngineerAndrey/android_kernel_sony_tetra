/*****************************************************************************
* Copyright 2011 Broadcom Corporation.  All rights reserved.
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

#ifndef __VC_WIFIHDMI_DEFS_H__INCLUDED__
#define __VC_WIFIHDMI_DEFS_H__INCLUDED__

// FourCC code used for VCHI connection
#define VC_WIFIHDMI_SERVER_NAME MAKE_FOURCC("WOHS")
#define VC_WIFIHDMI_NOTIFY_NAME MAKE_FOURCC("WOHN")

// Maximum message length
#define VC_WIFIHDMI_MAX_MSG_LEN (sizeof( VC_WIFIHDMI_MSG_UNION_T ) + \
                                 sizeof( VC_WIFIHDMI_MSG_HDR_T ))
#define VC_WIFIHDMI_MAX_RSP_LEN (sizeof( VC_WIFIHDMI_MSG_UNION_T ))

#define VC_WIFIHDMI_MAX_DATA_LEN  2048 
#define VC_WIFIHDMI_RESOURCE_NAME 32

// All message types supported
typedef enum
{
   // HOST->VC
   VC_WIFIHDMI_MSG_TYPE_SET,
   VC_WIFIHDMI_MSG_TYPE_UNSET,
   VC_WIFIHDMI_MSG_TYPE_REC,
   VC_WIFIHDMI_MSG_TYPE_STATS,

   VC_WIFIHDMI_MSG_TYPE_SKT_IN,
   VC_WIFIHDMI_MSG_TYPE_SKT_DSC,
   VC_WIFIHDMI_MSG_TYPE_SKT_DATA,
   VC_WIFIHDMI_MSG_TYPE_SKT_END,

   VC_WIFIHDMI_MSG_TYPE_START,
   VC_WIFIHDMI_MSG_TYPE_STOP,

   // VC->HOST
   VC_WIFIHDMI_MSG_TYPE_SKT_OPEN,
   VC_WIFIHDMI_MSG_TYPE_TX_DATA,

   VC_WIFIHDMI_MSG_TYPE_MAX

} VC_WIFIHDMI_MSG_TYPE;

// Message header for all messages in HOST->VC direction
typedef struct
{
   int32_t type;      // Message type (VC_SMCT_MSG_TYPE)
   uint32_t trans_id; // Transaction identifier (unique)
   uint8_t body[0];   // Pointer to message body (if exists)

} VC_WIFIHDMI_MSG_HDR_T;

// Generic result for a request (VC->HOST)
typedef struct
{
   uint32_t trans_id;       // Transaction identifier
   int32_t success;         // Action status

} VC_WIFIHDMI_RESULT_T;

// Socket operation result for a request (VC->HOST)
typedef struct
{
   uint32_t trans_id;       // Transaction identifier
   int32_t  success;        // Action status
   uint32_t handle;

} VC_WIFIHDMI_SKT_RES_T;

// Request to set a buffer for a specific purpose (HOST->VC)
typedef struct
{
   uint32_t res_handle;             // Handle reference
   uint32_t res_inthdl;             // Internal handle reference
   uint32_t res_size;               // Size of resource

} VC_WIFIHDMI_SET_T;

// Request to recycle a buffer for a specific purpose (HOST->VC)
typedef struct
{
   uint32_t res_handle;             // Handle reference

} VC_WIFIHDMI_REC_T;

// Request to process a notification (VC->HOST)
typedef struct
{
   uint32_t res_handle;             // Handle reference
   uint32_t res_size;               // Payload to ship out
   uint32_t res_socket;             // "Socket" on which to send this payload

} VC_WIFIHDMI_TX_DATA_T;

// Mode request used with start/stop command (HOST->VC)
typedef struct
{
   uint32_t loopback;
   uint32_t wifihdmi;

} VC_WIFIHDMI_MODE_T;

// Socket connection information (HOST->VC)
typedef struct
{
   uint32_t handle;
   uint32_t address;
   uint16_t port;

} VC_WIFIHDMI_SKT_T;

// Socket connection data (HOST->VC)
typedef struct
{
   uint32_t handle;
   uint32_t data_len;
   uint32_t data_handle;

} VC_WIFIHDMI_SKT_DATA_T;

// Socket action information (VC->HOST)
typedef struct
{
   uint32_t socket_handle;
   uint16_t socket_port;
   uint16_t socket_send_only;

} VC_WIFIHDMI_SKT_ACTION_T;

// Miscellaneous stats collected on VC to be queried by HOST
typedef struct
{
   uint32_t trans_id;       // Transaction identifier

   uint32_t tx_cnt;         // Transmit counter (queued up and signaled to host)
   uint32_t tx_miss_cnt;    // Transmit miss counter (failed to queue up to host)

   uint32_t tx_rec_cnt;     // Transmit recycled counter
   uint32_t tx_busy_cnt;    // Transmit busied counter

} VC_WIFIHDMI_STATS_T;

// Union of ALL messages
typedef union
{
   VC_WIFIHDMI_SET_T         set;
   VC_WIFIHDMI_REC_T         rec;
   VC_WIFIHDMI_TX_DATA_T     txdata;
   VC_WIFIHDMI_STATS_T       stats;
   VC_WIFIHDMI_RESULT_T      result;
   VC_WIFIHDMI_MODE_T        mode;
   VC_WIFIHDMI_SKT_RES_T     skt_res;
   VC_WIFIHDMI_SKT_T         skt;
   VC_WIFIHDMI_SKT_DATA_T    skt_data;
   VC_WIFIHDMI_SKT_ACTION_T  skt_action;

} VC_WIFIHDMI_MSG_UNION_T;

#endif /* __VC_WIFIHDMI_DEFS_H__INCLUDED__ */

