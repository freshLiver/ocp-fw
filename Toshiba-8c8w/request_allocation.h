//////////////////////////////////////////////////////////////////////////////////
// request_allocation.h for Cosmos+ OpenSSD
// Copyright (c) 2017 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// This file is part of Cosmos+ OpenSSD.
//
// Cosmos+ OpenSSD is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// Cosmos+ OpenSSD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Cosmos+ OpenSSD; see the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Company: ENC Lab. <http://enc.hanyang.ac.kr>
// Engineer: Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: Request Allocator
// File Name: request_allocation.h
//
// Version: v1.0.0
//
// Description:
//   - define parameters, data structure and functions of request allocator
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#ifndef REQUEST_ALLOCATION_H_
#define REQUEST_ALLOCATION_H_

#include "ftl_config.h"
#include "request_format.h"
#include "request_queue.h"

/**
 * @todo why 128?
 * @warning typo OUNTSTANDING -> OUTSTANDING
 */
#define AVAILABLE_OUNTSTANDING_REQ_COUNT ((USER_DIES)*128) // regardless of request type

#define REQ_SLOT_TAG_NONE 0xffff // no request pool entry, used for checking tail entry
#define REQ_SLOT_TAG_FAIL 0xffff // request pool entry not found, used for return error

/**
 * @brief The request entries pool for both NVMe and NAND requests.
 *
 * A 1D fixed-sized array used for managing the request info and relation between requests.
 * Each entry in the pool is represented in the structure `SSD_REQ_FORMAT`.
 *
 * Unlike the schematic in the paper, this queue is shared by both host and flash
 * operations, so the structure of `SSD_REQ_FORMAT` contains some members for
 * distinguishing which type is the request.
 */
typedef struct _REQ_POOL
{
    SSD_REQ_FORMAT reqPool[AVAILABLE_OUNTSTANDING_REQ_COUNT];
} REQ_POOL, *P_REQ_POOL;

void InitReqPool();

void PutToFreeReqQ(unsigned int reqSlotTag);
unsigned int GetFromFreeReqQ();

void PutToSliceReqQ(unsigned int reqSlotTag);
unsigned int GetFromSliceReqQ();

void PutToBlockedByBufDepReqQ(unsigned int reqSlotTag);
void SelectiveGetFromBlockedByBufDepReqQ(unsigned int reqSlotTag);

void PutToBlockedByRowAddrDepReqQ(unsigned int reqSlotTag, unsigned int chNo, unsigned int wayNo);
void SelectiveGetFromBlockedByRowAddrDepReqQ(unsigned int reqSlotTag, unsigned int chNo, unsigned int wayNo);

void PutToNvmeDmaReqQ(unsigned int reqSlotTag);
void SelectiveGetFromNvmeDmaReqQ(unsigned int regSlotTag);

void PutToNandReqQ(unsigned int reqSlotTag, unsigned chNo, unsigned wayNo);
void GetFromNandReqQ(unsigned int chNo, unsigned int wayNo, unsigned int reqStatus, unsigned int reqCode);

extern P_REQ_POOL reqPoolPtr;
extern FREE_REQUEST_QUEUE freeReqQ;
extern SLICE_REQUEST_QUEUE sliceReqQ;
extern BLOCKED_BY_BUFFER_DEPENDENCY_REQUEST_QUEUE blockedByBufDepReqQ;
extern BLOCKED_BY_ROW_ADDR_DEPENDENCY_REQUEST_QUEUE blockedByRowAddrDepReqQ[USER_CHANNELS][USER_WAYS];
extern NVME_DMA_REQUEST_QUEUE nvmeDmaReqQ;
extern NAND_REQUEST_QUEUE nandReqQ[USER_CHANNELS][USER_WAYS];

extern unsigned int notCompletedNandReqCnt;
extern unsigned int blockedReqCnt;

/* -------------------------------------------------------------------------- */
/*                  util macros for request pool related ops                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Get the request entry by entry index.
 *
 * @param index Index of request pool entry.
 * @return The address of request pool entry.
 */
#define REQ_ENTRY(iEntry) (&reqPoolPtr->reqPool[(iEntry)])
#define REQ_BUF(iEntry)   (REQ_ENTRY((iEntry))->dataBufInfo.entry)
#define REQ_LSA(iEntry)   (REQ_ENTRY((iEntry))->logicalSliceAddr)
#define REQ_VSA(iEntry)   (REQ_ENTRY((iEntry))->nandInfo.virtualSliceAddr)

/**
 * @brief Check the request code of the given request pool entry index
 *
 * @param idx the request pool entry index of the request to be checked
 * @param code the supposed request code of this request
 * @return bool true if `reqType` == `type`, otherwise false
 */
#define REQ_CODE_IS(idx, code) (REQ_ENTRY((idx))->reqCode == (code))

/**
 * @brief Check the request type of the given request pool entry index
 *
 * @param idx the request pool entry index of the request to be checked
 * @param type the supposed type of this request
 * @return bool true if `reqType` == `type`, otherwise false
 */
#define REQ_TYPE_IS(idx, type) (REQ_ENTRY((idx))->reqType == (type))

/**
 * @brief Check the request queue type of the given request pool entry index
 *
 * @param idx the request pool entry index of the request to be checked
 * @param qType the supposed queue type of this request
 * @return bool true if `reqType` == `type`, otherwise false
 */
#define REQ_QUEUE_TYPE_IS(idx, qType) (REQ_ENTRY((idx))->reqQueueType == (qType))

#endif /* REQUEST_ALLOCATION_H_ */
