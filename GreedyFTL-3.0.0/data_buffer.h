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

#include "ftl_config.h"

// Each die 16 entries
#define AVAILABLE_DATA_BUFFER_ENTRY_COUNT           (16 * USER_DIES)
#define AVAILABLE_TEMPORARY_DATA_BUFFER_ENTRY_COUNT (USER_DIES)

#define DATA_BUF_NONE  0xffff
#define DATA_BUF_FAIL  0xffff
#define DATA_BUF_DIRTY 1 // the buffer entry is not clean
#define DATA_BUF_CLEAN 0 // the buffer entry is not dirty

#define FindDataBufHashTableEntry(logicalSliceAddr) ((logicalSliceAddr) % AVAILABLE_DATA_BUFFER_ENTRY_COUNT)

/**
 * @brief The structure of the data buffer entry.
 *
 * Basically following information:
 *
 * - the logical address of the underlying slice command
 * - whether is buffer entry dirty or not
 * - the prev/next entry index of this data buffer entry in the LRU list
 * - the prev/next entry index of this data buffer entry in the data buffer bucket
 * - the request pool entry index of tail request in the blocking request queue
 */
typedef struct _DATA_BUF_ENTRY
{
    unsigned int logicalSliceAddr;     // the logical address of the underlying slice command
    unsigned int prevEntry : 16;       // the index of pref entry in the dataBuf
    unsigned int nextEntry : 16;       // the index of next entry in the dataBuf
    unsigned int blockingReqTail : 16; // the request pool entry index of the last blocking request
    unsigned int hashPrevEntry : 16;   // the index of prev entry in the bucket
    unsigned int hashNextEntry : 16;   // the index of next entry in the bucket
    unsigned int dirty : 1;            // whether this entry is dirty or not (clean)
    unsigned int reserved0 : 15;
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
    unsigned int headEntry : 16; // the MRU entry
    unsigned int tailEntry : 16; // the LRU entry
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

/* TODO */
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

#endif /* DATA_BUFFER_H_ */
