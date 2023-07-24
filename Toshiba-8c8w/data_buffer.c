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
#include "debug.h"

#include "memory_map.h"
#include "data_buffer.h"

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
 * - logicalSliceAddr: not belongs to any request yet, thus just point to LSA_NONE (0xffffffff)
 * - prevEntry points to prev element of the `dataBuf` array (except for head)
 * - nextEntry points to next element of the `dataBuf` array (except for tail)
 * - hashPrevEntry points to DATA_BUF_NONE, because it doesn't belongs to any bucket yet
 * - hashNextEntry points to DATA_BUF_NONE, because it doesn't belongs to any bucket yet
 * - dirty flag is not set
 * - blockingReqTail: no blocking request at the beginning, thus points to none
 * - dontCache: this buffer entry should not be cached (be inserted into hash list)
 *
 * There are `16 x NUM_DIES` entries in the `dataBufHashTable`, and all the elements will
 * be initialized to empty bucket, so:
 *
 * - headEntry and tailEntry both point to DATA_BUF_NONE (0xffff = 65535)
 *
 * There are `NUM_DIES` entries in the `tempDataBuf`, and all the elements of it will be
 * initialized to:
 *
 * - blockingReqTail: no blocking request at the beginning, thus points to none
 *
 * And the head/tail of `dataBufLruList` points to the first/last entry of `dataBuf`, this
 * means the initial `dataBufLruList` contains all the data buffer entries, therefore the
 * data buffer should be allocated from the last element of `dataBuf` array.
 */
void InitDataBuf()
{
    int bufEntry;

    dataBufMapPtr       = (P_DATA_BUF_MAP)DATA_BUFFER_MAP_ADDR;
    dataBufHashTablePtr = (P_DATA_BUF_HASH_TABLE)DATA_BUFFFER_HASH_TABLE_ADDR;
    tempDataBufMapPtr   = (P_TEMPORARY_DATA_BUF_MAP)TEMPORARY_DATA_BUFFER_MAP_ADDR;

    for (bufEntry = 0; bufEntry < AVAILABLE_DATA_BUFFER_ENTRY_COUNT; bufEntry++)
    {
        dataBufMapPtr->dataBuf[bufEntry].logicalSliceAddr = LSA_NONE;
        dataBufMapPtr->dataBuf[bufEntry].prevEntry        = bufEntry - 1;
        dataBufMapPtr->dataBuf[bufEntry].nextEntry        = bufEntry + 1;
        dataBufMapPtr->dataBuf[bufEntry].dirty            = DATA_BUF_CLEAN;
        dataBufMapPtr->dataBuf[bufEntry].phyReq           = DATA_BUF_FOR_LOG_REQ;
        dataBufMapPtr->dataBuf[bufEntry].dontCache        = DATA_BUF_KEEP_CACHE;
        dataBufMapPtr->dataBuf[bufEntry].blockingReqTail  = REQ_SLOT_TAG_NONE;

        dataBufHashTablePtr->dataBufHash[bufEntry].headEntry = DATA_BUF_NONE;
        dataBufHashTablePtr->dataBufHash[bufEntry].tailEntry = DATA_BUF_NONE;
        dataBufMapPtr->dataBuf[bufEntry].hashPrevEntry       = DATA_BUF_NONE;
        dataBufMapPtr->dataBuf[bufEntry].hashNextEntry       = DATA_BUF_NONE;
    }

    dataBufMapPtr->dataBuf[0].prevEntry                                     = DATA_BUF_NONE;
    dataBufMapPtr->dataBuf[AVAILABLE_DATA_BUFFER_ENTRY_COUNT - 1].nextEntry = DATA_BUF_NONE;
    dataBufLruList.headEntry                                                = 0;
    dataBufLruList.tailEntry = AVAILABLE_DATA_BUFFER_ENTRY_COUNT - 1;

    for (bufEntry = 0; bufEntry < AVAILABLE_TEMPORARY_DATA_BUFFER_ENTRY_COUNT; bufEntry++)
        tempDataBufMapPtr->tempDataBuf[bufEntry].blockingReqTail = REQ_SLOT_TAG_NONE;
}

void FlushDataBuf(uint32_t cmdSlotTag)
{
    uint32_t iCh, iWay, iDie, iPBlk, iPage, iBufEntry, iReqEntry, vsa;
    P_DATA_BUF_ENTRY bufEntry;

    // traverse data buf LRU list from LRU entry to MRU entry
    for (iBufEntry = BUF_TAIL_IDX(); iBufEntry != DATA_BUF_NONE; iBufEntry = BUF_PREV_IDX(iBufEntry))
    {
        // get corresponding buffer entry
        bufEntry = BUF_ENTRY(iBufEntry);

        // flush buffer entry
        if (bufEntry->dirty == DATA_BUF_DIRTY && bufEntry->dontCache == DATA_BUF_KEEP_CACHE)
        {
            if (bufEntry->phyReq)
            {
                // FIXME: we should program a page once before that page being erased
                vsa   = bufEntry->logicalSliceAddr;
                iDie  = VSA2VDIE(vsa);
                iCh   = VDIE2PCH(iDie);
                iWay  = VDIE2PWAY(iDie);
                iPBlk = VSA2VBLK(vsa);
                iPage = VSA2VPAGE(vsa);

                iReqEntry = GetFromFreeReqQ();

                REQ_ENTRY(iReqEntry)->reqType                       = REQ_TYPE_NAND;
                REQ_ENTRY(iReqEntry)->reqCode                       = REQ_CODE_WRITE;
                REQ_ENTRY(iReqEntry)->nvmeCmdSlotTag                = cmdSlotTag;
                REQ_ENTRY(iReqEntry)->logicalSliceAddr              = bufEntry->logicalSliceAddr;
                REQ_ENTRY(iReqEntry)->reqOpt.dataBufFormat          = REQ_OPT_DATA_BUF_ENTRY;
                REQ_ENTRY(iReqEntry)->reqOpt.nandAddr               = REQ_OPT_NAND_ADDR_PHY_ORG;
                REQ_ENTRY(iReqEntry)->reqOpt.nandEcc                = REQ_OPT_NAND_ECC_ON;
                REQ_ENTRY(iReqEntry)->reqOpt.nandEccWarning         = REQ_OPT_NAND_ECC_WARNING_ON;
                REQ_ENTRY(iReqEntry)->reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_NONE;
                REQ_ENTRY(iReqEntry)->reqOpt.blockSpace             = REQ_OPT_BLOCK_SPACE_TOTAL;
                REQ_ENTRY(iReqEntry)->dataBufInfo.entry             = iBufEntry;
                REQ_ENTRY(iReqEntry)->nandInfo.physicalCh           = iCh;
                REQ_ENTRY(iReqEntry)->nandInfo.physicalWay          = iWay;
                REQ_ENTRY(iReqEntry)->nandInfo.physicalBlock        = iPBlk;
                REQ_ENTRY(iReqEntry)->nandInfo.physicalPage         = iPage;

                pr_info("Req[%u]: Write C/W[%u/%u].PBlk[%u].Page[%u]", iReqEntry, iCh, iWay, iPBlk, iPage);
            }
            else
            {
                iReqEntry = GetFromFreeReqQ();
                vsa       = AddrTransWrite(bufEntry->logicalSliceAddr);

                REQ_ENTRY(iReqEntry)->reqType                       = REQ_TYPE_NAND;
                REQ_ENTRY(iReqEntry)->reqCode                       = REQ_CODE_WRITE;
                REQ_ENTRY(iReqEntry)->nvmeCmdSlotTag                = cmdSlotTag;
                REQ_ENTRY(iReqEntry)->logicalSliceAddr              = bufEntry->logicalSliceAddr;
                REQ_ENTRY(iReqEntry)->reqOpt.dataBufFormat          = REQ_OPT_DATA_BUF_ENTRY;
                REQ_ENTRY(iReqEntry)->reqOpt.nandAddr               = REQ_OPT_NAND_ADDR_VSA;
                REQ_ENTRY(iReqEntry)->reqOpt.nandEcc                = REQ_OPT_NAND_ECC_ON;
                REQ_ENTRY(iReqEntry)->reqOpt.nandEccWarning         = REQ_OPT_NAND_ECC_WARNING_ON;
                REQ_ENTRY(iReqEntry)->reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
                REQ_ENTRY(iReqEntry)->reqOpt.blockSpace             = REQ_OPT_BLOCK_SPACE_MAIN;
                REQ_ENTRY(iReqEntry)->dataBufInfo.entry             = iBufEntry;
                REQ_ENTRY(iReqEntry)->nandInfo.virtualSliceAddr     = vsa;
            }

            UpdateDataBufEntryInfoBlockingReq(iBufEntry, iReqEntry);
            SelectLowLevelReqQ(iReqEntry);

            bufEntry->dirty = DATA_BUF_CLEAN;
        }
    }
}

/**
 * @brief Get the data buffer entry index of the given request.
 *
 * Try to find the data buffer entry of the given request (with same `logicalSliceAddr`)
 * by traversing the correspoding bucket of the given request.
 *
 * If the request found, the corresponding data buffer entry become the Most Recently Used
 * entry and should be moved to the head of LRU list.
 *
 * @param reqSlotTag the request pool entry index of the request to be check
 */
unsigned int CheckDataBufHit(unsigned int reqSlotTag)
{
    unsigned int bufEntry, logicalSliceAddr;

    // get the bucket index of the given request
    logicalSliceAddr = REQ_LSA(reqSlotTag);
    bufEntry         = H_BUF_HEAD_IDX(FindDataBufHashTableEntry(logicalSliceAddr));

    // don't take phy addr buf for logical addr, and vice versa
    bool isPhyReq =
        REQ_CODE_IS(reqSlotTag, REQ_CODE_OCSSD_PHY_WRITE) || REQ_CODE_IS(reqSlotTag, REQ_CODE_OCSSD_PHY_READ);

    // traverse the bucket and try to find the data buffer entry of target request
    while (bufEntry != DATA_BUF_NONE)
    {
        if ((BUF_LSA(bufEntry) == logicalSliceAddr) && (BUF_ENTRY(bufEntry)->phyReq == isPhyReq))
        {
            pr_info("%s Req[%u]: Hit Buf[%u]!", isPhyReq ? "Phy" : "Log", reqSlotTag, bufEntry);

            // remove from the LRU list before making it MRU
            if ((!BUF_ENTRY_IS_HEAD(bufEntry)) && (!BUF_ENTRY_IS_TAIL(bufEntry)))
            {
                // body of LRU list
                BUF_PREV_ENTRY(bufEntry)->nextEntry = BUF_NEXT_IDX(bufEntry);
                BUF_NEXT_ENTRY(bufEntry)->prevEntry = BUF_PREV_IDX(bufEntry);
            }
            else if ((!BUF_ENTRY_IS_HEAD(bufEntry)) && BUF_ENTRY_IS_TAIL(bufEntry))
            {
                // tail of LRU list, modify the LRU tail
                BUF_PREV_ENTRY(bufEntry)->nextEntry = DATA_BUF_NONE;
                dataBufLruList.tailEntry            = BUF_PREV_IDX(bufEntry);
            }
            else if (BUF_ENTRY_IS_HEAD(bufEntry) && (!BUF_ENTRY_IS_TAIL(bufEntry)))
            {
                // head of LRU list, modify the LRU head
                BUF_NEXT_ENTRY(bufEntry)->prevEntry = DATA_BUF_NONE;
                dataBufLruList.headEntry            = BUF_NEXT_IDX(bufEntry);
            }
            else
            {
                // the only entry in LRU list, make LRU list empty
                dataBufLruList.tailEntry = DATA_BUF_NONE;
                dataBufLruList.headEntry = DATA_BUF_NONE;
            }

            // make this entry the MRU entry (move to the head of LRU list)
            if (BUF_HEAD_IDX() != DATA_BUF_NONE)
            {
                BUF_ENTRY(bufEntry)->prevEntry = DATA_BUF_NONE;
                BUF_ENTRY(bufEntry)->nextEntry = BUF_HEAD_IDX();
                BUF_HEAD_ENTRY()->prevEntry    = bufEntry;
                dataBufLruList.headEntry       = bufEntry;
            }
            else
            {
                BUF_ENTRY(bufEntry)->prevEntry = DATA_BUF_NONE;
                BUF_ENTRY(bufEntry)->nextEntry = DATA_BUF_NONE;
                dataBufLruList.headEntry       = bufEntry;
                dataBufLruList.tailEntry       = bufEntry;
            }

            return bufEntry;
        }

        // move to next entry in the same hash bucket
        bufEntry = BUF_ENTRY(bufEntry)->hashNextEntry;
    }

    return DATA_BUF_FAIL;
}

/**
 * @brief Retrieve a LRU data buffer entry from the LRU list.
 *
 * Choose the LRU data buffer entry from the LRU list, and return it's index.
 *
 * If the `evictedEntry` is `DATA_BUF_NONE`, that's an error or the `dataBufLruList` is
 * empty.
 *
 * If the `evictedEntry`, namely the last entry of LRU list, exists and:
 *
 * - it's also the head of LRU list:
 *
 *      this means that there is only entry in the LRU list, therefore we don't need to
 *      modify the prev/next entry of `evictedEntry`.
 *
 * - it's the body of LRU list:
 *
 *      we have to make the prev entry be the new tail of LRU list, and make the evicted
 *      entry be the new head of the LRU list.
 *
 * After the evicted entry being moved from the tail of LRU entry to head, we have to call
 * the function `SelectiveGetFromDataBufHashList` to remove the `evictedEntry` from its
 * bucket of hash table.
 */
unsigned int AllocateDataBuf()
{
    unsigned int evictedEntry = dataBufLruList.tailEntry;

    if (evictedEntry == DATA_BUF_NONE)
        assert(!"[WARNING] There is no valid buffer entry [WARNING]");

    if (dataBufMapPtr->dataBuf[evictedEntry].prevEntry != DATA_BUF_NONE)
    {
        dataBufMapPtr->dataBuf[dataBufMapPtr->dataBuf[evictedEntry].prevEntry].nextEntry = DATA_BUF_NONE;
        dataBufLruList.tailEntry = dataBufMapPtr->dataBuf[evictedEntry].prevEntry;

        dataBufMapPtr->dataBuf[evictedEntry].prevEntry             = DATA_BUF_NONE;
        dataBufMapPtr->dataBuf[evictedEntry].nextEntry             = dataBufLruList.headEntry;
        dataBufMapPtr->dataBuf[dataBufLruList.headEntry].prevEntry = evictedEntry;
        dataBufLruList.headEntry                                   = evictedEntry;
    }
    else
    {
        dataBufMapPtr->dataBuf[evictedEntry].prevEntry = DATA_BUF_NONE;
        dataBufMapPtr->dataBuf[evictedEntry].nextEntry = DATA_BUF_NONE;
        dataBufLruList.headEntry                       = evictedEntry;
        dataBufLruList.tailEntry                       = evictedEntry;
    }

    SelectiveGetFromDataBufHashList(evictedEntry);

    return evictedEntry;
}

/**
 * @brief Append the request to the blocking queue of the specified data buffer entry.
 *
 * In current fw implementation, a slice request may be split into several sub-requests,
 * and these sub-requests can share the same data buffer since they must be executed in
 * correct order.
 *
 * However, since the scheduler may reorder the requests base on the priority, the fw must
 * make sure these sub-requests are executed in the correct order. To do this, each data
 * buffer entry maintains a blocking request queue (`DATA_BUF_ENTRY::blockingReqTail`).
 * Whenever a new request is created, its request entry index will be appended to the
 * blocking queue of the data buffer allocated for it.
 *
 * @sa `ReqTransSliceToLowLevel()`, `CheckBufDep()`, `SelectLowLevelReqQ()` and `DATA_BUF_ENTRY`.
 *
 * @param byfEntry The data buffer entry index of the newly created request.
 * @param reqSlotTag The request pool entry index of the newly created request.
 */
void UpdateDataBufEntryInfoBlockingReq(unsigned int bufEntry, unsigned int reqSlotTag)
{
    // don't forget to update the original tail blocking request if it exists
    if (dataBufMapPtr->dataBuf[bufEntry].blockingReqTail != REQ_SLOT_TAG_NONE)
    {
        reqPoolPtr->reqPool[reqSlotTag].prevBlockingReq = dataBufMapPtr->dataBuf[bufEntry].blockingReqTail;
        reqPoolPtr->reqPool[reqPoolPtr->reqPool[reqSlotTag].prevBlockingReq].nextBlockingReq = reqSlotTag;
    }

    dataBufMapPtr->dataBuf[bufEntry].blockingReqTail = reqSlotTag;
}

/**
 * @brief Retrieve the index of temp buffer entry of the target die.
 *
 * By default, there is only `NUM_DIES` entries in the `tempDataBuf`, so we can just use
 * the serial number of each die to determine which temp entry should be used.
 *
 * @param dieNo an unique number of the specified die
 */
unsigned int AllocateTempDataBuf(unsigned int dieNo) { return dieNo; }

/**
 * @brief Append the request to the blocking queue specified by given temp buffer entry.
 *
 * Similar to `UpdateDataBufEntryInfoBlockingReq()`, but the data buffer is temp buffer.
 *
 * @param byfEntry the temp data buffer entry index of the specified request
 * @param reqSlotTag the request pool entry index of the request to be
 */
void UpdateTempDataBufEntryInfoBlockingReq(unsigned int bufEntry, unsigned int reqSlotTag)
{
    // don't forget to update the original tail blocking request if it exists
    if (tempDataBufMapPtr->tempDataBuf[bufEntry].blockingReqTail != REQ_SLOT_TAG_NONE)
    {
        reqPoolPtr->reqPool[reqSlotTag].prevBlockingReq = tempDataBufMapPtr->tempDataBuf[bufEntry].blockingReqTail;
        reqPoolPtr->reqPool[reqPoolPtr->reqPool[reqSlotTag].prevBlockingReq].nextBlockingReq = reqSlotTag;
    }

    tempDataBufMapPtr->tempDataBuf[bufEntry].blockingReqTail = reqSlotTag;
}

/**
 * @brief Insert the given data buffer entry into the hash table.
 *
 * Insert the given data buffer entry into the tail of hash table bucket specified by the
 * `logicalSliceAddr` of the given entry, and modify the tail (head as well, if needed) of
 * target hash table bucket.
 *
 * @note If the `dontCached` flag of the buffer entry is set to true, this insert process
 * will be skipped, since the buffer entry should not be inserted into any hash bucket.
 *
 * @param bufEntry the index of the data buffer entry to be inserted
 */
void PutToDataBufHashList(unsigned int bufEntry)
{
    unsigned int hashEntry;

    // this buffer entry may not want to be cached (in hash list)
    if (BUF_ENTRY(bufEntry)->dontCache == DATA_BUF_SKIP_CACHE)
    {
        pr_debug("Buf[%u] should not be cached, skipped", bufEntry);
        BUF_ENTRY(bufEntry)->hashPrevEntry = DATA_BUF_NONE;
        BUF_ENTRY(bufEntry)->hashNextEntry = DATA_BUF_NONE;
        ASSERT(0, "Currently all buffer entries should be cached");
        return;
    }

    hashEntry = FindDataBufHashTableEntry(dataBufMapPtr->dataBuf[bufEntry].logicalSliceAddr);

    if (dataBufHashTablePtr->dataBufHash[hashEntry].tailEntry != DATA_BUF_NONE)
    {
        dataBufMapPtr->dataBuf[bufEntry].hashPrevEntry = dataBufHashTablePtr->dataBufHash[hashEntry].tailEntry;
        dataBufMapPtr->dataBuf[bufEntry].hashNextEntry = REQ_SLOT_TAG_NONE;
        dataBufMapPtr->dataBuf[dataBufHashTablePtr->dataBufHash[hashEntry].tailEntry].hashNextEntry = bufEntry;
        dataBufHashTablePtr->dataBufHash[hashEntry].tailEntry                                       = bufEntry;
    }
    else
    {
        dataBufMapPtr->dataBuf[bufEntry].hashPrevEntry        = REQ_SLOT_TAG_NONE;
        dataBufMapPtr->dataBuf[bufEntry].hashNextEntry        = REQ_SLOT_TAG_NONE;
        dataBufHashTablePtr->dataBufHash[hashEntry].headEntry = bufEntry;
        dataBufHashTablePtr->dataBufHash[hashEntry].tailEntry = bufEntry;
    }
}

/**
 * @brief Remove the given data buffer entry from the hash table.
 *
 * We may need to modify the head/tail index of corresponding bucket specified by the
 * `logicalSliceAddr` of the given data buffer entry.
 *
 * @note If the `dontCached` flag of the buffer entry is set to true, this remove process
 * will be skipped, since the buffer entry should not exist in any hash bucket.
 *
 * @param bufEntry the index of the data buffer entry to be removed
 */
void SelectiveGetFromDataBufHashList(unsigned int bufEntry)
{
    // this buffer entry may not in cache (hash list)
    if (BUF_ENTRY(bufEntry)->dontCache == DATA_BUF_SKIP_CACHE)
    {
        pr_debug("Buf[%u] not cached, skipped", bufEntry);
        BUF_ENTRY(bufEntry)->hashPrevEntry = DATA_BUF_NONE;
        BUF_ENTRY(bufEntry)->hashNextEntry = DATA_BUF_NONE;
        ASSERT(0, "Currently all buffer entries should be cached");
        return;
    }

    if (dataBufMapPtr->dataBuf[bufEntry].logicalSliceAddr != LSA_NONE)
    {
        unsigned int prevBufEntry, nextBufEntry, hashEntry;

        prevBufEntry = dataBufMapPtr->dataBuf[bufEntry].hashPrevEntry;
        nextBufEntry = dataBufMapPtr->dataBuf[bufEntry].hashNextEntry;
        hashEntry    = FindDataBufHashTableEntry(dataBufMapPtr->dataBuf[bufEntry].logicalSliceAddr);

        // body, no need to modify the head or tail index of this bucket
        if ((nextBufEntry != DATA_BUF_NONE) && (prevBufEntry != DATA_BUF_NONE))
        {
            dataBufMapPtr->dataBuf[prevBufEntry].hashNextEntry = nextBufEntry;
            dataBufMapPtr->dataBuf[nextBufEntry].hashPrevEntry = prevBufEntry;
        }

        // tail, don't forget to modify the tail index of this bucket
        else if ((nextBufEntry == DATA_BUF_NONE) && (prevBufEntry != DATA_BUF_NONE))
        {
            dataBufMapPtr->dataBuf[prevBufEntry].hashNextEntry    = DATA_BUF_NONE;
            dataBufHashTablePtr->dataBufHash[hashEntry].tailEntry = prevBufEntry;
        }

        // head, don't forget to modify the head index of this bucket
        else if ((nextBufEntry != DATA_BUF_NONE) && (prevBufEntry == DATA_BUF_NONE))
        {
            dataBufMapPtr->dataBuf[nextBufEntry].hashPrevEntry    = DATA_BUF_NONE;
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
