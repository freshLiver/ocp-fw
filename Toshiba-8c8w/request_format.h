//////////////////////////////////////////////////////////////////////////////////
// request_format.h for Cosmos+ OpenSSD
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
// File Name: request_format.h
//
// Version: v1.0.0
//
// Description:
//   - define parameters, data structure of request
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#ifndef REQUEST_FORMAT_H_
#define REQUEST_FORMAT_H_

#include "nvme/nvme.h"

#define REQ_TYPE_SLICE    0x0
#define REQ_TYPE_NAND     0x1 // flash or flash DMA operation
#define REQ_TYPE_NVME_DMA 0x2 // host DMA

/**
 * The queue type of a request pool entry. Each of these macros, except for NONE, has a
 * corresponding queue defined in `request_queue.h`.
 */

#define REQ_QUEUE_TYPE_NONE                    0x0
#define REQ_QUEUE_TYPE_FREE                    0x1
#define REQ_QUEUE_TYPE_SLICE                   0x2
#define REQ_QUEUE_TYPE_BLOCKED_BY_BUF_DEP      0x3
#define REQ_QUEUE_TYPE_BLOCKED_BY_ROW_ADDR_DEP 0x4
#define REQ_QUEUE_TYPE_NVME_DMA                0x5
#define REQ_QUEUE_TYPE_NAND                    0x6

/**
 * The command codes of a request pool entry.
 */

#define REQ_CODE_WRITE         0x00
#define REQ_CODE_READ          0x08 // read trigger
#define REQ_CODE_READ_TRANSFER 0x09 // read transfer
#define REQ_CODE_ERASE         0x0C
#define REQ_CODE_RESET         0x0D // currently only used in FTL initialization stage
#define REQ_CODE_SET_FEATURE   0x0E // currently only used in FTL initialization stage
#define REQ_CODE_FLUSH         0x0F
#define REQ_CODE_RxDMA         0x10
#define REQ_CODE_TxDMA         0x20

#define REQ_CODE_OCSSD_PHY_TYPE_BASE 0xA0
#define REQ_CODE_OCSSD_PHY_WRITE     0xA0
#define REQ_CODE_OCSSD_PHY_READ      0xA8
#define REQ_CODE_OCSSD_PHY_ERASE     0xAC

/**
 * These values are for the 2 bits flag `SSD_REQ_FORMAT::REQ_OPTION::dataBufFormat`. The
 * flag will be used for checking the meaning of `SSD_REQ_FORMAT::dataBufInfo`.
 *
 * If the flag is set to `REQ_OPT_DATA_BUF_ENTRY` or `REQ_OPT_DATA_BUF_TEMP_ENTRY`, this
 * means that the data stored in the `dataBufInfo` is a index of the specified buffer.
 *
 * If the flag is set to `REQ_OPT_DATA_BUF_ADDR`, then `dataBufInfo` is the address of its
 * data buffer.
 *
 * If the flag is set to `REQ_OPT_DATA_BUF_NONE`, it means that we don't need to allocate
 * a data buffer entry for this request (e.g., ERASE, RESET, SET_FEATURE).
 */

#define REQ_OPT_DATA_BUF_ENTRY      0 // the data stored in `dataBufFormat` is buffer index.
#define REQ_OPT_DATA_BUF_TEMP_ENTRY 1 // the data stored in `dataBufFormat` is buffer index.
#define REQ_OPT_DATA_BUF_ADDR       2 // the data stored in `dataBufFormat` is buffer address.
#define REQ_OPT_DATA_BUF_NONE       3 // for ERASE, RESET, SET_FEATURE (no buffer needed).

#define REQ_OPT_NAND_ADDR_VSA     0 // the data stored in `nandInfo` is Virtual Slice Address.
#define REQ_OPT_NAND_ADDR_PHY_ORG 1 // the data stored in `nandInfo` is Physical Flash Info.

#define REQ_OPT_NAND_ECC_OFF 0
#define REQ_OPT_NAND_ECC_ON  1

#define REQ_OPT_NAND_ECC_WARNING_OFF 0
#define REQ_OPT_NAND_ECC_WARNING_ON  1

#define REQ_OPT_WRAPPING_NONE 0
#define REQ_OPT_WRAPPING_REQ  1

// no need to check dep for this request, may be used by information related operations.
#define REQ_OPT_ROW_ADDR_DEPENDENCY_NONE 0
// we should check dep for this request, basically was used by NAND I/O operations.
#define REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK 1

/**
 * @brief for the 1 bit flag `REQ_OPTION::blockSpace`.
 *
 * These two block spaces will be used to generate the row address of a corresponding
 * request (check the function `GenerateNandRowAddr()`).
 *
 * The blocks in a plane, or LUN in this project, are separated into 2 spaces:
 *
 * @todo
 *
 * - main space:
 * - extended space:
 *
 * For request that should be apply to all the blocks, like SET_FEATURE, RESET and READ
 * (for finding bad blocks), we will set the block space to `REQ_OPT_BLOCK_SPACE_TOTAL`.
 *
 * Check `ftl_config.h` and `request_translation.c` for details.
 */

#define REQ_OPT_BLOCK_SPACE_MAIN  0 // main blocks only
#define REQ_OPT_BLOCK_SPACE_TOTAL 1 // main blocks and extended blocks

#define LOGICAL_SLICE_ADDR_NONE 0xffffffff

/**
 * @brief The real address or entry index of the request.
 *
 * This structure used to point the data buffer entry of this request. It may be a real
 * data buffer address if the `REQ_OPTION::dataBufFormat` is `REQ_OPT_DATA_BUF_ADDR`, or
 * a request entry index if the `REQ_OPTION::dataBufFormat` is `REQ_OPT_DATA_BUF_ENTRY`
 * or `REQ_OPT_DATA_BUF_TEMP_ENTRY`.
 */
typedef struct _DATA_BUF_INFO
{
    union
    {
        unsigned int addr;  // real buffer address
        unsigned int entry; // data buffer entry index
    };
} DATA_BUF_INFO, *P_DATA_BUF_INFO;

typedef struct _NVME_DMA_INFO
{
    unsigned int startIndex : 16;
    unsigned int nvmeBlockOffset : 16;
    unsigned int numOfNvmeBlock : 16;
    unsigned int reqTail : 8;
    unsigned int reserved0 : 8;
    unsigned int overFlowCnt;
} NVME_DMA_INFO, *P_NVME_DMA_INFO;

typedef struct _NAND_INFO
{
    union
    {
        unsigned int virtualSliceAddr; // TODO location vector?
        struct
        {
            unsigned int physicalCh : 4;
            unsigned int physicalWay : 4;
            unsigned int physicalBlock : 16;
            unsigned int phyReserved0 : 8;
        };
    };
    union
    {
        unsigned int programmedPageCnt;
        struct
        {
            unsigned int physicalPage : 16;
            unsigned int phyReserved1 : 16;
        };
    };
} NAND_INFO, *P_NAND_INFO;

typedef struct _REQ_OPTION
{
    /**
     * @brief Type of address stored in the `SSD_REQ_FORMAT::dataBufInfo`.
     *
     * REQ_OPT_DATA_BUF_(ENTRY|TEMP_ENTRY|ADDR|NONE)
     */
    unsigned int dataBufFormat : 2;

    /**
     * @brief Type of address stored in the `SSD_REQ_FORMAT::nandInfo`.
     *
     * 0 for VSA, 1 for PHY
     */
    unsigned int nandAddr : 2;
    unsigned int nandEcc : 1;                // 0 for OFF, 1 for ON
    unsigned int nandEccWarning : 1;         // 0 for OFF, 1 for ON
    unsigned int rowAddrDependencyCheck : 1; // 0 for NONE, 1 for CHECK
    unsigned int blockSpace : 1;             // 0 for MAIN, 1 for TOTAL
    unsigned int reserved0 : 24;
} REQ_OPTION, *P_REQ_OPTION; /* NOTE: 32 bits */

/**
 * @brief The structure of a slice command
 *
 * This structure can split into 4 parts:
 *
 * @todo
 *
 * - address: The logical address of the underlying slice command.
 * - request type:
 *
 *      As described in the structure of request pool `REQ_POOL`, this structure is used
 *      for distinguishing the type of requests.
 *
 * - request info
 * - relation between requests:
 *
 */
typedef struct _SSD_REQ_FORMAT
{
    unsigned int reqType : 4;         // the type of this request
    unsigned int reqQueueType : 4;    // the type of request queue where this request is stored
    unsigned int reqCode : 8;         // READ/WRITE/RxDMA/TxDMA/etc. (check REQ_CODE_*)
    unsigned int nvmeCmdSlotTag : 16; //

    unsigned int logicalSliceAddr;

    REQ_OPTION reqOpt;         //
    DATA_BUF_INFO dataBufInfo; // request data buffer entry info of this request
    NVME_DMA_INFO nvmeDmaInfo; //
    NAND_INFO nandInfo;        // address info of this NAND request

    unsigned int prevReq : 16;         // the request pool index of prev request queue entry
    unsigned int nextReq : 16;         // the request pool index of next request queue entry
    unsigned int prevBlockingReq : 16; // the request pool index of prev blocking request queue entry
    unsigned int nextBlockingReq : 16; // the request pool index of next blocking request queue entry

    // 4 4 8+4+12+8 8 Bytes

} SSD_REQ_FORMAT, *P_SSD_REQ_FORMAT;

#endif /* REQUEST_FORMAT_H_ */
