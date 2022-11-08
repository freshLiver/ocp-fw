//////////////////////////////////////////////////////////////////////////////////
// data_buffer.c for Cosmos+ OpenSSD
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
// File Name: data_buffer.c
//
// Version: v1.0.0
//
// Description:
//   - manage data buffer used to transfer data between host system and NAND device
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////


#include "xil_printf.h"
#include <assert.h>
#include "memory_map.h"


P_DATA_BUF_MAP dataBufMapPtr;
DATA_BUF_LRU_LIST dataBufLruList;
P_DATA_BUF_HASH_TABLE dataBufHashTablePtr;
P_TEMPORARY_DATA_BUF_MAP tempDataBufMapPtr;

/**
 * @brief Initialization process of the Data buffer.
 * 
 * Three buffer related lists will be initialized in this function
 * 
 * There are `16 x NUM_DIES` entries in the `dataBuf`, and all the elements of `dataBuf`
 * will be initialized to:
 * 
 * - logicalSliceAddr points to LSA_NONE (0xffffffff)
 * - prevEntry points to prev element of the `dataBuf` array (except for head)
 * - nextEntry points to next element of the `dataBuf` array (except for tail)
 * - nextEntry points to next element of the `dataBuf` array (except for tail)
 * - nextEntry points to next element of the `dataBuf` array (except for tail)
 * - hashPrevEntry points to DATA_BUF_NONE, because it doesn't belongs to any bucket yet
 * - hashNextEntry points to DATA_BUF_NONE, because it doesn't belongs to any bucket yet
 * - dirty flag is not set
 * - blockingReqTail: REQ_SLOT_TAG_NONE //TODO
 * 
 * There are `16 x NUM_DIES` entries in the `dataBufHashTable`, and all the elements will
 * be initialized to empty bucket, so:
 * 	
 * - headEntry and tailEntry both point to DATA_BUF_NONE (0xffff = 65535)
 * 
 * There are `NUM_DIES` entries in the `tempDataBuf`, and all the elements of it will be
 * initialized to: 
 * 
 * - blockingReqTail: REQ_SLOT_TAG_NONE //TODO
 * 
 * And the head/tail of `dataBufLruList` points to the first/last entry of `dataBuf`, this
 * means the initial `dataBufLruList` contains all the data buffer entries, therefore the
 * data buffer should be allocated from the last element of `dataBuf` array.
 */
void InitDataBuf()
{
	int bufEntry;

	dataBufMapPtr = (P_DATA_BUF_MAP) DATA_BUFFER_MAP_ADDR;
	dataBufHashTablePtr = (P_DATA_BUF_HASH_TABLE)DATA_BUFFFER_HASH_TABLE_ADDR;
	tempDataBufMapPtr = (P_TEMPORARY_DATA_BUF_MAP)TEMPORARY_DATA_BUFFER_MAP_ADDR;

	for(bufEntry = 0; bufEntry < AVAILABLE_DATA_BUFFER_ENTRY_COUNT; bufEntry++)
	{
		dataBufMapPtr->dataBuf[bufEntry].logicalSliceAddr = LSA_NONE;
		dataBufMapPtr->dataBuf[bufEntry].prevEntry = bufEntry-1;
		dataBufMapPtr->dataBuf[bufEntry].nextEntry = bufEntry+1;
		dataBufMapPtr->dataBuf[bufEntry].dirty = DATA_BUF_CLEAN;
		dataBufMapPtr->dataBuf[bufEntry].blockingReqTail =  REQ_SLOT_TAG_NONE;

		dataBufHashTablePtr->dataBufHash[bufEntry].headEntry = DATA_BUF_NONE;
		dataBufHashTablePtr->dataBufHash[bufEntry].tailEntry = DATA_BUF_NONE;
		dataBufMapPtr->dataBuf[bufEntry].hashPrevEntry = DATA_BUF_NONE;
		dataBufMapPtr->dataBuf[bufEntry].hashNextEntry = DATA_BUF_NONE;
	}

	dataBufMapPtr->dataBuf[0].prevEntry = DATA_BUF_NONE;
	dataBufMapPtr->dataBuf[AVAILABLE_DATA_BUFFER_ENTRY_COUNT - 1].nextEntry = DATA_BUF_NONE;
	dataBufLruList.headEntry = 0 ;
	dataBufLruList.tailEntry = AVAILABLE_DATA_BUFFER_ENTRY_COUNT - 1;

	for(bufEntry = 0; bufEntry < AVAILABLE_TEMPORARY_DATA_BUFFER_ENTRY_COUNT; bufEntry++)
		tempDataBufMapPtr->tempDataBuf[bufEntry].blockingReqTail =  REQ_SLOT_TAG_NONE;
}


/** //TODO 
 * 
 */
unsigned int CheckDataBufHit(unsigned int reqSlotTag)
{
	unsigned int bufEntry, logicalSliceAddr;

	logicalSliceAddr = reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;
	bufEntry = dataBufHashTablePtr->dataBufHash[FindDataBufHashTableEntry(logicalSliceAddr)].headEntry;

	while(bufEntry != DATA_BUF_NONE)
	{
		if(dataBufMapPtr->dataBuf[bufEntry].logicalSliceAddr == logicalSliceAddr)
		{
			if((dataBufMapPtr->dataBuf[bufEntry].nextEntry != DATA_BUF_NONE) && (dataBufMapPtr->dataBuf[bufEntry].prevEntry != DATA_BUF_NONE))
			{
				dataBufMapPtr->dataBuf[dataBufMapPtr->dataBuf[bufEntry].prevEntry].nextEntry = dataBufMapPtr->dataBuf[bufEntry].nextEntry;
				dataBufMapPtr->dataBuf[dataBufMapPtr->dataBuf[bufEntry].nextEntry].prevEntry = dataBufMapPtr->dataBuf[bufEntry].prevEntry;
			}
			else if((dataBufMapPtr->dataBuf[bufEntry].nextEntry == DATA_BUF_NONE) && (dataBufMapPtr->dataBuf[bufEntry].prevEntry != DATA_BUF_NONE))
			{
				dataBufMapPtr->dataBuf[dataBufMapPtr->dataBuf[bufEntry].prevEntry].nextEntry = DATA_BUF_NONE;
				dataBufLruList.tailEntry = dataBufMapPtr->dataBuf[bufEntry].prevEntry;
			}
			else if((dataBufMapPtr->dataBuf[bufEntry].nextEntry != DATA_BUF_NONE) && (dataBufMapPtr->dataBuf[bufEntry].prevEntry== DATA_BUF_NONE))
			{
				dataBufMapPtr->dataBuf[dataBufMapPtr->dataBuf[bufEntry].nextEntry].prevEntry  = DATA_BUF_NONE;
				dataBufLruList.headEntry = dataBufMapPtr->dataBuf[bufEntry].nextEntry;
			}
			else
			{
				dataBufLruList.tailEntry = DATA_BUF_NONE;
				dataBufLruList.headEntry = DATA_BUF_NONE;
			}

			if(dataBufLruList.headEntry != DATA_BUF_NONE)
			{
				dataBufMapPtr->dataBuf[bufEntry].prevEntry = DATA_BUF_NONE;
				dataBufMapPtr->dataBuf[bufEntry].nextEntry = dataBufLruList.headEntry;
				dataBufMapPtr->dataBuf[dataBufLruList.headEntry].prevEntry = bufEntry;
				dataBufLruList.headEntry = bufEntry;
			}
			else
			{
				dataBufMapPtr->dataBuf[bufEntry].prevEntry = DATA_BUF_NONE;
				dataBufMapPtr->dataBuf[bufEntry].nextEntry = DATA_BUF_NONE;
				dataBufLruList.headEntry = bufEntry;
				dataBufLruList.tailEntry = bufEntry;
			}

			return bufEntry;
		}
		else
			bufEntry = dataBufMapPtr->dataBuf[bufEntry].hashNextEntry;
	}

	return DATA_BUF_FAIL;
}


/**
 * Allocate a unused data buffer entry.
 * 
 * This function will retrieve the address of Least Recent Used data buffer entry from the
 * `dataBufLruList`, then the retrieved entry will be the new HEAD of `dataBufLruList`.
 * 
 * If the `dataBufLruList` is empty, that is an error and means all the data buffer
 * entries are used.
 * 
 * If LRU entry is the first element of `dataBuf` array, reset the `dataBufLruList` then
 * insert the evicted entry to `dataBufLruList`, and keep the `prevEntry` and `nextEntry`
 * points to DATA_BUF_NONE.
 * 
 * Otherwise, the evicted entry will be removed from `dataBuf` list, and become the new
 * head of `dataBufLruList` list. Then the new LRU entry will become the `prevEntry` of
 * evicted entry.
 */
unsigned int AllocateDataBuf()
{
	unsigned int evictedEntry = dataBufLruList.tailEntry;

	if(evictedEntry == DATA_BUF_NONE)
		assert(!"[WARNING] There is no valid buffer entry [WARNING]");

	if(dataBufMapPtr->dataBuf[evictedEntry].prevEntry != DATA_BUF_NONE)
	{
		dataBufMapPtr->dataBuf[dataBufMapPtr->dataBuf[evictedEntry].prevEntry].nextEntry = DATA_BUF_NONE;
		dataBufLruList.tailEntry = dataBufMapPtr->dataBuf[evictedEntry].prevEntry;

		dataBufMapPtr->dataBuf[evictedEntry].prevEntry = DATA_BUF_NONE;
		dataBufMapPtr->dataBuf[evictedEntry].nextEntry = dataBufLruList.headEntry;
		dataBufMapPtr->dataBuf[dataBufLruList.headEntry].prevEntry = evictedEntry;
		dataBufLruList.headEntry = evictedEntry;

	}
	else
	{
		// FIXME: why the evicted.next.prev not changed ???
		dataBufMapPtr->dataBuf[evictedEntry].prevEntry = DATA_BUF_NONE;
		dataBufMapPtr->dataBuf[evictedEntry].nextEntry = DATA_BUF_NONE; // FIXME: ???
		dataBufLruList.headEntry = evictedEntry;
		dataBufLruList.tailEntry = evictedEntry; // FIXME: why reset ???
	}

	SelectiveGetFromDataBufHashList(evictedEntry);

	return evictedEntry;
}


/** //TODO
 *
 */
void UpdateDataBufEntryInfoBlockingReq(unsigned int bufEntry, unsigned int reqSlotTag)
{
	if(dataBufMapPtr->dataBuf[bufEntry].blockingReqTail != REQ_SLOT_TAG_NONE)
	{
		reqPoolPtr->reqPool[reqSlotTag].prevBlockingReq = dataBufMapPtr->dataBuf[bufEntry].blockingReqTail;
		reqPoolPtr->reqPool[reqPoolPtr->reqPool[reqSlotTag].prevBlockingReq].nextBlockingReq  = reqSlotTag;
	}

	dataBufMapPtr->dataBuf[bufEntry].blockingReqTail = reqSlotTag;
}


/**
 * By default, there is only `NUM_DIES` entries in the `tempDataBuf`, so we can just use
 * the serial number of each die to determine which temp entry should be used.
 */
unsigned int AllocateTempDataBuf(unsigned int dieNo)
{
	return dieNo;
}


/** //TODO
 *
 */
void UpdateTempDataBufEntryInfoBlockingReq(unsigned int bufEntry, unsigned int reqSlotTag)
{

	if(tempDataBufMapPtr->tempDataBuf[bufEntry].blockingReqTail != REQ_SLOT_TAG_NONE)
	{
		reqPoolPtr->reqPool[reqSlotTag].prevBlockingReq = tempDataBufMapPtr->tempDataBuf[bufEntry].blockingReqTail;
		reqPoolPtr->reqPool[reqPoolPtr->reqPool[reqSlotTag].prevBlockingReq].nextBlockingReq  = reqSlotTag;
	}

	tempDataBufMapPtr->tempDataBuf[bufEntry].blockingReqTail = reqSlotTag;
}

/**/**
 * @brief 
 * 
 */
 * 
 * The inserted entry will become the new tail entry of corresponding bucket. 
 */
void PutToDataBufHashList(unsigned int bufEntry)
{
	unsigned int hashEntry;

	hashEntry = FindDataBufHashTableEntry(dataBufMapPtr->dataBuf[bufEntry].logicalSliceAddr);

	if(dataBufHashTablePtr->dataBufHash[hashEntry].tailEntry != DATA_BUF_NONE)
	{
		dataBufMapPtr->dataBuf[bufEntry].hashPrevEntry = dataBufHashTablePtr->dataBufHash[hashEntry].tailEntry ;
		dataBufMapPtr->dataBuf[bufEntry].hashNextEntry = REQ_SLOT_TAG_NONE;
		dataBufMapPtr->dataBuf[dataBufHashTablePtr->dataBufHash[hashEntry].tailEntry].hashNextEntry = bufEntry;
		dataBufHashTablePtr->dataBufHash[hashEntry].tailEntry = bufEntry;
	}
	else
	{
		dataBufMapPtr->dataBuf[bufEntry].hashPrevEntry = REQ_SLOT_TAG_NONE;
		dataBufMapPtr->dataBuf[bufEntry].hashNextEntry = REQ_SLOT_TAG_NONE;
		dataBufHashTablePtr->dataBufHash[hashEntry].headEntry = bufEntry;
		dataBufHashTablePtr->dataBufHash[hashEntry].tailEntry = bufEntry;
	}
}

/**
 * Remove the evicted entry from the hash table.
 * 
 * We may need to modify the head/tail index of corresponding bucket of hash table.
 */
void SelectiveGetFromDataBufHashList(unsigned int bufEntry)
{
	if(dataBufMapPtr->dataBuf[bufEntry].logicalSliceAddr != LSA_NONE)
	{
		unsigned int prevBufEntry, nextBufEntry, hashEntry;

		prevBufEntry =  dataBufMapPtr->dataBuf[bufEntry].hashPrevEntry;
		nextBufEntry =  dataBufMapPtr->dataBuf[bufEntry].hashNextEntry;
		hashEntry = FindDataBufHashTableEntry(dataBufMapPtr->dataBuf[bufEntry].logicalSliceAddr);

		// body, no need to modify the head or tail index of this bucket
		if((nextBufEntry != DATA_BUF_NONE) && (prevBufEntry != DATA_BUF_NONE))
		{
			dataBufMapPtr->dataBuf[prevBufEntry].hashNextEntry = nextBufEntry;
			dataBufMapPtr->dataBuf[nextBufEntry].hashPrevEntry = prevBufEntry;
		}

		// tail, don't forget to modify the tail index of this bucket
		else if((nextBufEntry == DATA_BUF_NONE) && (prevBufEntry != DATA_BUF_NONE))
		{
			dataBufMapPtr->dataBuf[prevBufEntry].hashNextEntry = DATA_BUF_NONE;
			dataBufHashTablePtr->dataBufHash[hashEntry].tailEntry = prevBufEntry;
		}

		// head, don't forget to modify the head index of this bucket
		else if((nextBufEntry != DATA_BUF_NONE) && (prevBufEntry == DATA_BUF_NONE))
		{
			dataBufMapPtr->dataBuf[nextBufEntry].hashPrevEntry = DATA_BUF_NONE;
			dataBufHashTablePtr->dataBufHash[hashEntry].headEntry = nextBufEntry;
		}

		// bucket with only one entry, both the head and tail index should be modified
		else
		{
			dataBufHashTablePtr->dataBufHash[hashEntry].headEntry = DATA_BUF_NONE;
			dataBufHashTablePtr->dataBufHash[hashEntry].tailEntry = DATA_BUF_NONE;
		}
	}
}




