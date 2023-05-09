//////////////////////////////////////////////////////////////////////////////////
// data_buffer.h for Cosmos+ OpenSSD
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
// Module Name: Data Buffer Manager
// File Name: data_buffer.h
//
// Version: v1.0.0
//
// Description:
//   - define parameters, data structure and functions of data buffer manager
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#ifndef DATA_BUFFER_H_
#define DATA_BUFFER_H_

#include "stdint.h"
#include "ftl_config.h"
#include "memory_map.h"

// the data buffer entries for each die, default 16
#define AVAILABLE_DATA_BUFFER_ENTRY_COUNT           (16 * USER_DIES)
#define AVAILABLE_TEMPORARY_DATA_BUFFER_ENTRY_COUNT (USER_DIES)

#define DATA_BUF_NONE  0xffff
#define DATA_BUF_FAIL  0xffff
#define DATA_BUF_DIRTY 1 // the buffer entry is not clean
#define DATA_BUF_CLEAN 0 // the buffer entry is not dirty

#define DATA_BUF_FOR_PHY_REQ 1 // the data buffer entry is used for physical addr (OC) request
#define DATA_BUF_FOR_LOG_REQ 0 // the data buffer entry is used for logical addr request

#define DATA_BUF_SKIP_CACHE 1 // this buffer entry should not be cached in hash list
#define DATA_BUF_KEEP_CACHE 0 // this buffer entry should be cached in hash list (default)

#define FindDataBufHashTableEntry(logicalSliceAddr) ((logicalSliceAddr) % AVAILABLE_DATA_BUFFER_ENTRY_COUNT)

/**
 * @brief The structure of the data buffer entry.
 *
 * Since only a limited number of data buffer entries could be maintained in DRAM, the fw
 * must reuse the data buffer entries by maintaining a LRU list which is consist of data
 * buffer entries.
 *
 * The nodes of the LRU list are linked using the two members `prevEntry` and `nextEntry`
 * of this structure.
 *
 *  - When a data buffer entry is allocated to a request, the allocated data buffer entry
 *    will be move to the head of the LRU list.
 *
 *  - When there is no free data buffer entry can be used to serve the newly created slice
 *    request, the fw will reuse the tail entry of the LRU list, and evict the data if the
 *    flag `dirty` of that entry is marked as true.
 *
 *  - Besides storing the data needed by the requests, the data buffer entries are also
 *    used as cache to speed up the read/write requests. When a new request is created
 *    the fw should first check if there is any data buffer entry cached the data needed
 *    by the new request (check `ReqTransSliceToLowLevel()` and `CheckDataBufHit()` for
 *    details).
 *
 *    However, if we simply traverse the LRU list every time we want to check the data
 *    buffer, it may be too slow. Therefore, in current implementation, the fw maintains
 *    a hash table to reduce the overhead for searching the target data buffer entry that
 *    owns the data of target LBA. And the nodes of the hash table are linked together by
 *    using the two members `hashPrevEntry` and `hashNextEntry` of this structure.
 *
 * @sa `AllocateDataBuf()`, `CheckDataBufHit()`, `EvictDataBufEntry()`.
 *
 * Also, since a data buffer may be shared by several requests, every data buffer entry
 * maintains a blocking request queue (by using `SSD_REQ_FORMAT::(prev|next)BlockingReq`,
 * and `DATA_BUF_ENTRY::blockingReqTail`) to make sure the requests will be executed in
 * correct order (check `UpdateDataBufEntryInfoBlockingReq()` for details).
 *
 * @sa `UpdateDataBufEntryInfoBlockingReq()`.
 */
typedef struct _DATA_BUF_ENTRY
{
    unsigned int logicalSliceAddr;     // the LSA of the request that owns this buffer
    unsigned int prevEntry : 16;       // the index of pref entry in the dataBuf
    unsigned int nextEntry : 16;       // the index of next entry in the dataBuf
    unsigned int blockingReqTail : 16; // the request pool entry index of the last blocking request
    unsigned int hashPrevEntry : 16;   // the index of the prev data buffer entry in the bucket
    unsigned int hashNextEntry : 16;   // the index of the next data buffer entry in the bucket
    unsigned int dirty : 1;            // whether this data buffer entry is dirty or not (clean)
    unsigned int phyReq : 1;           // treat LSA as physical address
    unsigned int dontCache : 1;        // do not cache (insert into hash list) this buffer
    unsigned int reserved0 : 13;
} DATA_BUF_ENTRY, *P_DATA_BUF_ENTRY;

/**
 * @brief The structure of data buffer table.
 *
 * A fixed-sized 1D data buffer array. Used for storing a slice command and managing the
 * relation between data buffer entries.
 */
typedef struct _DATA_BUF_MAP
{
    DATA_BUF_ENTRY dataBuf[AVAILABLE_DATA_BUFFER_ENTRY_COUNT];
} DATA_BUF_MAP, *P_DATA_BUF_MAP;

/**
 * @brief The structure of LRU list that records the head and tail data buffer entry index
 * of the LRU list.
 *
 * This structure only records the head and tail index, therefore we need to manage the
 * relation between data buffer entries by maintaining the `prevEntry` and `nextEntry` of
 * the data buffer entries in this LRU list.
 */
typedef struct _DATA_BUF_LRU_LIST
{
    unsigned int headEntry : 16; // the index of MRU data buffer entry
    unsigned int tailEntry : 16; // the index of LRU data buffer entry
} DATA_BUF_LRU_LIST, *P_DATA_BUF_LRU_LIST;

/**
 * @brief The structure of data buffer bucket that records the head and tail data buffer
 * entry index in the bucket.
 *
 * Similar to the LRU list, this structure only records the head and tail index, therefore
 * we must manage the relation between data buffer entries in this bucket by maintaining
 * the `hashPrevEntry` and `hashNextEntry` of the data buffer entries.
 */
typedef struct _DATA_BUF_HASH_ENTRY
{
    unsigned int headEntry : 16; // the first data buffer entry in this bucket
    unsigned int tailEntry : 16; // the last data buffer entry in this bucket
} DATA_BUF_HASH_ENTRY, *P_DATA_BUF_HASH_ENTRY;

/**
 * @brief The structure of data buffer hash table.
 *
 * A fixed-sized 1D data buffer bucket array. Used for fast finding the data buffer entry
 * of a given request by the `logicalSliceAddr`.
 */
typedef struct _DATA_BUF_HASH_TABLE
{
    DATA_BUF_HASH_ENTRY dataBufHash[AVAILABLE_DATA_BUFFER_ENTRY_COUNT];
} DATA_BUF_HASH_TABLE, *P_DATA_BUF_HASH_TABLE;

typedef struct _TEMPORARY_DATA_BUF_ENTRY
{
    unsigned int blockingReqTail : 16;
    unsigned int reserved0 : 16;
} TEMPORARY_DATA_BUF_ENTRY, *P_TEMPORARY_DATA_BUF_ENTRY;

/**
 * @brief The structure of the temp data buffer table.
 *
 * A fixed-sized 1D temp data buffer array. The number of buffer entries is exactly the
 * number of dies, therefore just choose the target entry by using the dieNo when
 * allocating a temp buffer entry. (check the implementation of `AllocateTempDataBuf`).
 */
typedef struct _TEMPORARY_DATA_BUF_MAP
{
    TEMPORARY_DATA_BUF_ENTRY tempDataBuf[AVAILABLE_TEMPORARY_DATA_BUFFER_ENTRY_COUNT];
} TEMPORARY_DATA_BUF_MAP, *P_TEMPORARY_DATA_BUF_MAP;

void InitDataBuf();
void FlushDataBuf(uint32_t cmdSlotTag);
unsigned int CheckDataBufHit(unsigned int reqSlotTag);
unsigned int AllocateDataBuf();
void UpdateDataBufEntryInfoBlockingReq(unsigned int bufEntry, unsigned int reqSlotTag);

unsigned int AllocateTempDataBuf(unsigned int dieNo);
void UpdateTempDataBufEntryInfoBlockingReq(unsigned int bufEntry, unsigned int reqSlotTag);

void PutToDataBufHashList(unsigned int bufEntry);
void SelectiveGetFromDataBufHashList(unsigned int bufEntry);

extern P_DATA_BUF_MAP dataBufMapPtr;
extern DATA_BUF_LRU_LIST dataBufLruList;
extern P_DATA_BUF_HASH_TABLE dataBufHashTable;
extern P_TEMPORARY_DATA_BUF_MAP tempDataBufMapPtr;

/* -------------------------------------------------------------------------- */
/*                   util macros for data buffer related ops                  */
/* -------------------------------------------------------------------------- */

#define BUF_ENTRY(iEntry)      (&dataBufMapPtr->dataBuf[(iEntry)])
#define BUF_HEAD_IDX()         (dataBufLruList.headEntry)
#define BUF_TAIL_IDX()         (dataBufLruList.tailEntry)
#define BUF_HEAD_ENTRY()       (BUF_ENTRY(BUF_HEAD_IDX()))
#define BUF_TAIL_ENTRY()       (BUF_ENTRY(BUF_TAIL_IDX()))
#define BUF_PREV_IDX(iEntry)   (BUF_ENTRY((iEntry))->prevEntry)
#define BUF_NEXT_IDX(iEntry)   (BUF_ENTRY((iEntry))->nextEntry)
#define BUF_PREV_ENTRY(iEntry) (BUF_ENTRY(BUF_PREV_IDX((iEntry))))
#define BUF_NEXT_ENTRY(iEntry) (BUF_ENTRY(BUF_NEXT_IDX((iEntry))))

#define BUF_ENTRY_IS_HEAD(iEntry) (BUF_PREV_IDX((iEntry)) == DATA_BUF_NONE)
#define BUF_ENTRY_IS_TAIL(iEntry) (BUF_NEXT_IDX((iEntry)) == DATA_BUF_NONE)

#define H_BUF_ENTRY(iEntry)      (&dataBufHashTablePtr->dataBufHash[(iEntry)])
#define H_BUF_HEAD_ENTRY(iEntry) (BUF_ENTRY(H_BUF_ENTRY((iEntry))->headEntry))
#define H_BUF_HEAD_IDX(iEntry)   (H_BUF_ENTRY((iEntry))->headEntry)

#define BUF_LSA(iEntry) (BUF_ENTRY((iEntry))->logicalSliceAddr)

#define BUF_DATA_ENTRY2ADDR(iEntry)  (DATA_BUFFER_BASE_ADDR + ((iEntry)*BYTES_PER_DATA_REGION_OF_SLICE))
#define BUF_SPARE_ENTRY2ADDR(iEntry) (SPARE_DATA_BUFFER_BASE_ADDR + ((iEntry)*BYTES_PER_SPARE_REGION_OF_SLICE))

#endif /* DATA_BUFFER_H_ */
