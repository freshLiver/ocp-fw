//////////////////////////////////////////////////////////////////////////////////
// request_allocation.c for Cosmos+ OpenSSD
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
// File Name: request_allocation.c
//
// Version: v1.0.0
//
// Description:
//   - allocate requests to each request queue
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

P_REQ_POOL reqPoolPtr;
FREE_REQUEST_QUEUE freeReqQ;
SLICE_REQUEST_QUEUE sliceReqQ;
BLOCKED_BY_BUFFER_DEPENDENCY_REQUEST_QUEUE blockedByBufDepReqQ;
BLOCKED_BY_ROW_ADDR_DEPENDENCY_REQUEST_QUEUE blockedByRowAddrDepReqQ[USER_CHANNELS][USER_WAYS];
NVME_DMA_REQUEST_QUEUE nvmeDmaReqQ;
NAND_REQUEST_QUEUE nandReqQ[USER_CHANNELS][USER_WAYS];

unsigned int notCompletedNandReqCnt;
unsigned int blockedReqCnt;

/**
 * @brief Initialize the request pool and the request queues.
 *
 * At the beginning, all the requests are stored in the `freeReqQ`, therefore the size of
 * `freeReqQ` should equal to the size of request pool, and the others request queue will
 * be initialized to be empty.
 *
 * After all the queues are initialized, we next have to initialize the request pool:
 *
 * - all the entries should belongs to `freeReqQ` by default
 * - all the entries should be connected in serial order
 * - no request exists at the beginning, therefore:
 *      - the number of not completed NAND request is zero
 *      - the number of blocking requests is zero
 *      - both `prevBlockingReq` and `nextBlockingReq` should be NONE
 */
void InitReqPool()
{
    int chNo, wayNo, reqSlotTag;

    reqPoolPtr = (P_REQ_POOL)REQ_POOL_ADDR; // revise address

    freeReqQ.headReq = 0;
    freeReqQ.tailReq = AVAILABLE_OUNTSTANDING_REQ_COUNT - 1;

    sliceReqQ.headReq = REQ_SLOT_TAG_NONE;
    sliceReqQ.tailReq = REQ_SLOT_TAG_NONE;
    sliceReqQ.reqCnt  = 0;

    blockedByBufDepReqQ.headReq = REQ_SLOT_TAG_NONE;
    blockedByBufDepReqQ.tailReq = REQ_SLOT_TAG_NONE;
    blockedByBufDepReqQ.reqCnt  = 0;

    nvmeDmaReqQ.headReq = REQ_SLOT_TAG_NONE;
    nvmeDmaReqQ.tailReq = REQ_SLOT_TAG_NONE;
    nvmeDmaReqQ.reqCnt  = 0;

    for (chNo = 0; chNo < USER_CHANNELS; chNo++)
        for (wayNo = 0; wayNo < USER_WAYS; wayNo++)
        {
            blockedByRowAddrDepReqQ[chNo][wayNo].headReq = REQ_SLOT_TAG_NONE;
            blockedByRowAddrDepReqQ[chNo][wayNo].tailReq = REQ_SLOT_TAG_NONE;
            blockedByRowAddrDepReqQ[chNo][wayNo].reqCnt  = 0;

            nandReqQ[chNo][wayNo].headReq = REQ_SLOT_TAG_NONE;
            nandReqQ[chNo][wayNo].tailReq = REQ_SLOT_TAG_NONE;
            nandReqQ[chNo][wayNo].reqCnt  = 0;
        }

    for (reqSlotTag = 0; reqSlotTag < AVAILABLE_OUNTSTANDING_REQ_COUNT; reqSlotTag++)
    {
        reqPoolPtr->reqPool[reqSlotTag].reqQueueType    = REQ_QUEUE_TYPE_FREE;
        reqPoolPtr->reqPool[reqSlotTag].prevBlockingReq = REQ_SLOT_TAG_NONE;
        reqPoolPtr->reqPool[reqSlotTag].nextBlockingReq = REQ_SLOT_TAG_NONE;
        reqPoolPtr->reqPool[reqSlotTag].prevReq         = reqSlotTag - 1;
        reqPoolPtr->reqPool[reqSlotTag].nextReq         = reqSlotTag + 1;
    }

    reqPoolPtr->reqPool[0].prevReq                                    = REQ_SLOT_TAG_NONE;
    reqPoolPtr->reqPool[AVAILABLE_OUNTSTANDING_REQ_COUNT - 1].nextReq = REQ_SLOT_TAG_NONE;
    freeReqQ.reqCnt = AVAILABLE_OUNTSTANDING_REQ_COUNT; // TODO: move this up ?

    notCompletedNandReqCnt = 0;
    blockedReqCnt          = 0;
}

/**
 * @brief Add the given request to the free request queue.
 *
 * Insert the given request into the tail of the free queue and update the corresponding
 * field of the given request.
 *
 * @note this function will not modify the `prevReq` of the next request in the original
 * request queue. That is, the prev request entry index of the original next request may
 * still be `reqSlotTag`.
 *
 * @note also, this function will not modify the `prevBlockingReq` and `nextBlockingReq`
 * of the original request.
 *
 * @param reqSlotTag the request pool entry index of the request to be added.
 */
void PutToFreeReqQ(unsigned int reqSlotTag)
{
    if (freeReqQ.tailReq != REQ_SLOT_TAG_NONE)
    {
        reqPoolPtr->reqPool[reqSlotTag].prevReq       = freeReqQ.tailReq;
        reqPoolPtr->reqPool[reqSlotTag].nextReq       = REQ_SLOT_TAG_NONE;
        reqPoolPtr->reqPool[freeReqQ.tailReq].nextReq = reqSlotTag;
        freeReqQ.tailReq                              = reqSlotTag;
    }
    else
    {
        reqPoolPtr->reqPool[reqSlotTag].prevReq = REQ_SLOT_TAG_NONE;
        reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
        freeReqQ.headReq                        = reqSlotTag;
        freeReqQ.tailReq                        = reqSlotTag;
    }

    reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_FREE;
    freeReqQ.reqCnt++;
}

/**
 * @brief Get a free request from the free request queue.
 *
 * Try to get the first entry (FIFO) from the free queue. If the `freeReqQ` is empty, use
 * the function `SyncAvailFreeReq()` to release some requests and recycle the request pool
 * entries occupied by them.
 *
 * @return unsigned int the request pool entry index of the free request queue entry
 */
unsigned int GetFromFreeReqQ()
{
    unsigned int reqSlotTag;

    reqSlotTag = freeReqQ.headReq;

    // try to release some request entries by doing scheduling
    if (reqSlotTag == REQ_SLOT_TAG_NONE)
    {
        SyncAvailFreeReq();
        reqSlotTag = freeReqQ.headReq;
    }

    // if the free queue becomes empty, don't forget to update its tail pointer
    if (reqPoolPtr->reqPool[reqSlotTag].nextReq != REQ_SLOT_TAG_NONE)
    {
        freeReqQ.headReq = reqPoolPtr->reqPool[reqSlotTag].nextReq;
        reqPoolPtr->reqPool[reqPoolPtr->reqPool[reqSlotTag].nextReq].prevReq = REQ_SLOT_TAG_NONE;
    }
    else
    {
        freeReqQ.headReq = REQ_SLOT_TAG_NONE;
        freeReqQ.tailReq = REQ_SLOT_TAG_NONE;
    }

    reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NONE;
    freeReqQ.reqCnt--;

    return reqSlotTag;
}

/**
 * @brief Add the given request to the slice request queue.
 *
 * Similar to `PutToFreeReqQ()`.
 *
 * @param reqSlotTag the request pool entry index of the request to be added
 */
void PutToSliceReqQ(unsigned int reqSlotTag)
{
    if (sliceReqQ.tailReq != REQ_SLOT_TAG_NONE)
    {
        reqPoolPtr->reqPool[reqSlotTag].prevReq        = sliceReqQ.tailReq;
        reqPoolPtr->reqPool[reqSlotTag].nextReq        = REQ_SLOT_TAG_NONE;
        reqPoolPtr->reqPool[sliceReqQ.tailReq].nextReq = reqSlotTag;
        sliceReqQ.tailReq                              = reqSlotTag;
    }
    else
    {
        reqPoolPtr->reqPool[reqSlotTag].prevReq = REQ_SLOT_TAG_NONE;
        reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
        sliceReqQ.headReq                       = reqSlotTag;
        sliceReqQ.tailReq                       = reqSlotTag;
    }

    reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_SLICE;
    sliceReqQ.reqCnt++;
}

/**
 * @brief Get a slice request from the slice request queue.
 *
 * Try to pop the first request of the slice request queue.
 *
 * @note fail if the request queue is empty.
 *
 * @return unsigned int the entry index of chosen slice request, or `REQ_SLOT_TAG_FAIL`
 * if the slice request queue is empty.
 */
unsigned int GetFromSliceReqQ()
{
    unsigned int reqSlotTag;

    reqSlotTag = sliceReqQ.headReq;

    if (reqSlotTag == REQ_SLOT_TAG_NONE)
        return REQ_SLOT_TAG_FAIL;

    if (reqPoolPtr->reqPool[reqSlotTag].nextReq != REQ_SLOT_TAG_NONE)
    {
        sliceReqQ.headReq = reqPoolPtr->reqPool[reqSlotTag].nextReq;
        reqPoolPtr->reqPool[reqPoolPtr->reqPool[reqSlotTag].nextReq].prevReq = REQ_SLOT_TAG_NONE;
    }
    else
    {
        sliceReqQ.headReq = REQ_SLOT_TAG_NONE;
        sliceReqQ.tailReq = REQ_SLOT_TAG_NONE;
    }

    reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NONE;
    sliceReqQ.reqCnt--;

    return reqSlotTag;
}

/**
 * @brief Add the given request to `blockedByBufDepReqQ`.
 *
 * Similar to `PutToFreeReqQ()`.
 *
 * @note the request blocked by buffer dependency is also a blocked request, therefore the
 * `blockedReqCnt` should also be increased.
 *
 * @param reqSlotTag the request pool entry index of the request to be added
 */
void PutToBlockedByBufDepReqQ(unsigned int reqSlotTag)
{
    if (blockedByBufDepReqQ.tailReq != REQ_SLOT_TAG_NONE)
    {
        reqPoolPtr->reqPool[reqSlotTag].prevReq                  = blockedByBufDepReqQ.tailReq;
        reqPoolPtr->reqPool[reqSlotTag].nextReq                  = REQ_SLOT_TAG_NONE;
        reqPoolPtr->reqPool[blockedByBufDepReqQ.tailReq].nextReq = reqSlotTag;
        blockedByBufDepReqQ.tailReq                              = reqSlotTag;
    }
    else
    {
        reqPoolPtr->reqPool[reqSlotTag].prevReq = REQ_SLOT_TAG_NONE;
        reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
        blockedByBufDepReqQ.headReq             = reqSlotTag;
        blockedByBufDepReqQ.tailReq             = reqSlotTag;
    }

    reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_BLOCKED_BY_BUF_DEP;
    blockedByBufDepReqQ.reqCnt++;
    blockedReqCnt++;
}

/**
 * @brief Remove the given request from the `blockedByBufDepReqQ`.
 *
 * Similar to `GetFromSliceReqQ()`, but the entry to be removed is specified by the param
 * `reqSlotTag`.
 *
 * @note don't forget to decrease `blockedReqCnt`
 *
 * @param reqSlotTag the request pool entry index of the request to be removed
 */
void SelectiveGetFromBlockedByBufDepReqQ(unsigned int reqSlotTag)
{
    unsigned int prevReq, nextReq;

    if (reqSlotTag == REQ_SLOT_TAG_NONE)
        assert(!"[WARNING] Wrong reqSlotTag [WARNING]");

    prevReq = reqPoolPtr->reqPool[reqSlotTag].prevReq;
    nextReq = reqPoolPtr->reqPool[reqSlotTag].nextReq;

    if ((nextReq != REQ_SLOT_TAG_NONE) && (prevReq != REQ_SLOT_TAG_NONE))
    {
        reqPoolPtr->reqPool[prevReq].nextReq = nextReq;
        reqPoolPtr->reqPool[nextReq].prevReq = prevReq;
    }
    else if ((nextReq == REQ_SLOT_TAG_NONE) && (prevReq != REQ_SLOT_TAG_NONE))
    {
        reqPoolPtr->reqPool[prevReq].nextReq = REQ_SLOT_TAG_NONE;
        blockedByBufDepReqQ.tailReq          = prevReq;
    }
    else if ((nextReq != REQ_SLOT_TAG_NONE) && (prevReq == REQ_SLOT_TAG_NONE))
    {
        reqPoolPtr->reqPool[nextReq].prevReq = REQ_SLOT_TAG_NONE;
        blockedByBufDepReqQ.headReq          = nextReq;
    }
    else
    {
        blockedByBufDepReqQ.headReq = REQ_SLOT_TAG_NONE;
        blockedByBufDepReqQ.tailReq = REQ_SLOT_TAG_NONE;
    }

    reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NONE;
    blockedByBufDepReqQ.reqCnt--;
    blockedReqCnt--;
}

/**
 * @brief Add the given request to `blockedByRowAddrDepReqQ`.
 *
 * Similar to `PutToBlockedByBufDepReqQ()`.
 *
 * @note the request blocked by row address dependency is also a blocked request, the
 * `blockedReqCnt` thus should also be increased.
 *
 * @param reqSlotTag the request pool entry index of the request to be added.
 * @param chNo the channel number of the specified queue.
 * @param wayNo the die number of the specified queue.
 */
void PutToBlockedByRowAddrDepReqQ(unsigned int reqSlotTag, unsigned int chNo, unsigned int wayNo)
{
    if (blockedByRowAddrDepReqQ[chNo][wayNo].tailReq != REQ_SLOT_TAG_NONE)
    {
        reqPoolPtr->reqPool[reqSlotTag].prevReq = blockedByRowAddrDepReqQ[chNo][wayNo].tailReq;
        reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
        reqPoolPtr->reqPool[blockedByRowAddrDepReqQ[chNo][wayNo].tailReq].nextReq = reqSlotTag;
        blockedByRowAddrDepReqQ[chNo][wayNo].tailReq                              = reqSlotTag;
    }
    else
    {
        reqPoolPtr->reqPool[reqSlotTag].prevReq      = REQ_SLOT_TAG_NONE;
        reqPoolPtr->reqPool[reqSlotTag].nextReq      = REQ_SLOT_TAG_NONE;
        blockedByRowAddrDepReqQ[chNo][wayNo].headReq = reqSlotTag;
        blockedByRowAddrDepReqQ[chNo][wayNo].tailReq = reqSlotTag;
    }

    reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_BLOCKED_BY_ROW_ADDR_DEP;
    blockedByRowAddrDepReqQ[chNo][wayNo].reqCnt++;
    blockedReqCnt++;
}

/**
 * @brief Remove the given request from the `blockedByRowAddrDepReqQ`.
 *
 * Similar to `SelectiveGetFromBlockedByBufDepReqQ()`.
 *
 * @param reqSlotTag the request pool entry index of the request to be removed.
 * @param chNo the channel number of the specified queue.
 * @param wayNo the die number of the specified queue.
 */
void SelectiveGetFromBlockedByRowAddrDepReqQ(unsigned int reqSlotTag, unsigned int chNo, unsigned int wayNo)
{
    unsigned int prevReq, nextReq;

    if (reqSlotTag == REQ_SLOT_TAG_NONE)
        assert(!"[WARNING] Wrong reqSlotTag [WARNING]");

    prevReq = reqPoolPtr->reqPool[reqSlotTag].prevReq;
    nextReq = reqPoolPtr->reqPool[reqSlotTag].nextReq;

    if ((nextReq != REQ_SLOT_TAG_NONE) && (prevReq != REQ_SLOT_TAG_NONE))
    {
        reqPoolPtr->reqPool[prevReq].nextReq = nextReq;
        reqPoolPtr->reqPool[nextReq].prevReq = prevReq;
    }
    else if ((nextReq == REQ_SLOT_TAG_NONE) && (prevReq != REQ_SLOT_TAG_NONE))
    {
        reqPoolPtr->reqPool[prevReq].nextReq         = REQ_SLOT_TAG_NONE;
        blockedByRowAddrDepReqQ[chNo][wayNo].tailReq = prevReq;
    }
    else if ((nextReq != REQ_SLOT_TAG_NONE) && (prevReq == REQ_SLOT_TAG_NONE))
    {
        reqPoolPtr->reqPool[nextReq].prevReq         = REQ_SLOT_TAG_NONE;
        blockedByRowAddrDepReqQ[chNo][wayNo].headReq = nextReq;
    }
    else
    {
        blockedByRowAddrDepReqQ[chNo][wayNo].headReq = REQ_SLOT_TAG_NONE;
        blockedByRowAddrDepReqQ[chNo][wayNo].tailReq = REQ_SLOT_TAG_NONE;
    }

    reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NONE;
    blockedByRowAddrDepReqQ[chNo][wayNo].reqCnt--;
    blockedReqCnt--;
}

/**
 * @brief Add the given request to the NVMe DMA request queue and update its status.
 *
 * Similar to `PutToFreeReqQ()`.
 *
 * @param reqSlotTag the request pool entry index of the request to be added
 */
void PutToNvmeDmaReqQ(unsigned int reqSlotTag)
{
    if (nvmeDmaReqQ.tailReq != REQ_SLOT_TAG_NONE)
    {
        reqPoolPtr->reqPool[reqSlotTag].prevReq          = nvmeDmaReqQ.tailReq;
        reqPoolPtr->reqPool[reqSlotTag].nextReq          = REQ_SLOT_TAG_NONE;
        reqPoolPtr->reqPool[nvmeDmaReqQ.tailReq].nextReq = reqSlotTag;
        nvmeDmaReqQ.tailReq                              = reqSlotTag;
    }
    else
    {
        reqPoolPtr->reqPool[reqSlotTag].prevReq = REQ_SLOT_TAG_NONE;
        reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
        nvmeDmaReqQ.headReq                     = reqSlotTag;
        nvmeDmaReqQ.tailReq                     = reqSlotTag;
    }

    reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NVME_DMA;
    nvmeDmaReqQ.reqCnt++;
}

/**
 * @brief Move the specified entry from the `nvmeDmaReqQ` to the `freeReqQ`.
 *
 * Remove the given request entry from the NVMe DMA request queue, and then insert into
 * the free request queue.
 *
 * @note the function `PutToFreeReqQ()` and its friends won't modify the `prevBlockingReq`
 * and the `nextBlockingReq` of the specified request entry, therefore after request being
 * added to free request queue, we should also try to remove the specified request from
 * the blocking request by calling the function `ReleaseBlockedByBufDepReq()`. // FIXME
 *
 * @param reqSlotTag the request pool entry index of the request to be removed
 */
void SelectiveGetFromNvmeDmaReqQ(unsigned int reqSlotTag)
{
    unsigned int prevReq, nextReq;

    prevReq = reqPoolPtr->reqPool[reqSlotTag].prevReq;
    nextReq = reqPoolPtr->reqPool[reqSlotTag].nextReq;

    if ((nextReq != REQ_SLOT_TAG_NONE) && (prevReq != REQ_SLOT_TAG_NONE))
    {
        reqPoolPtr->reqPool[prevReq].nextReq = nextReq;
        reqPoolPtr->reqPool[nextReq].prevReq = prevReq;
    }
    else if ((nextReq == REQ_SLOT_TAG_NONE) && (prevReq != REQ_SLOT_TAG_NONE))
    {
        reqPoolPtr->reqPool[prevReq].nextReq = REQ_SLOT_TAG_NONE;
        nvmeDmaReqQ.tailReq                  = prevReq;
    }
    else if ((nextReq != REQ_SLOT_TAG_NONE) && (prevReq == REQ_SLOT_TAG_NONE))
    {
        reqPoolPtr->reqPool[nextReq].prevReq = REQ_SLOT_TAG_NONE;
        nvmeDmaReqQ.headReq                  = nextReq;
    }
    else
    {
        nvmeDmaReqQ.headReq = REQ_SLOT_TAG_NONE;
        nvmeDmaReqQ.tailReq = REQ_SLOT_TAG_NONE;
    }

    reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NONE;
    nvmeDmaReqQ.reqCnt--;

    PutToFreeReqQ(reqSlotTag);
    ReleaseBlockedByBufDepReq(reqSlotTag); // release the request from blocking request if needed
}

/**
 * @brief Add the given request to `nandReqQ` of the specified die.
 *
 * Similar to `PutToFreeReqQ()`.
 *
 * @note we should not only increase the size of the specified request queue, but also
 * increase the number of uncompleted nand request.
 *
 * @param reqSlotTag the request pool entry index of the request to be added.
 * @param chNo the channel number of the specified queue.
 * @param wayNo the die number of the specified queue.
 */
void PutToNandReqQ(unsigned int reqSlotTag, unsigned chNo, unsigned wayNo)
{
    if (nandReqQ[chNo][wayNo].tailReq != REQ_SLOT_TAG_NONE)
    {
        reqPoolPtr->reqPool[reqSlotTag].prevReq                    = nandReqQ[chNo][wayNo].tailReq;
        reqPoolPtr->reqPool[reqSlotTag].nextReq                    = REQ_SLOT_TAG_NONE;
        reqPoolPtr->reqPool[nandReqQ[chNo][wayNo].tailReq].nextReq = reqSlotTag;
        nandReqQ[chNo][wayNo].tailReq                              = reqSlotTag;
    }
    else
    {
        reqPoolPtr->reqPool[reqSlotTag].prevReq = REQ_SLOT_TAG_NONE;
        reqPoolPtr->reqPool[reqSlotTag].nextReq = REQ_SLOT_TAG_NONE;
        nandReqQ[chNo][wayNo].headReq           = reqSlotTag;
        nandReqQ[chNo][wayNo].tailReq           = reqSlotTag;
    }

    reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NAND;
    nandReqQ[chNo][wayNo].reqCnt++;
    notCompletedNandReqCnt++;
}

/**
 * @brief Move the head request of the specified `nandReqQ` queue to `freeReqQ`.
 *
 * Similar to `GetFromSliceReqQ()` and `SelectiveGetFromNvmeDmaReqQ()`.
 *
 * @warning currently just simply choose and remove the head request
 *
 * @warning @p reqStatus and @p reqCode not used
 *
 * @param chNo the target channel
 * @param wayNo the target way
 * @param reqStatus the status of the previous request executed on the specified die.
 * @param reqCode the command code of the request attempt to execute on the specified die.
 */
void GetFromNandReqQ(unsigned int chNo, unsigned int wayNo, unsigned int reqStatus, unsigned int reqCode)
{
    unsigned int reqSlotTag;

    reqSlotTag = nandReqQ[chNo][wayNo].headReq;
    if (reqSlotTag == REQ_SLOT_TAG_NONE)
        assert(!"[WARNING] there is no request in Nand-req-queue[WARNING]");

    if (reqPoolPtr->reqPool[reqSlotTag].nextReq != REQ_SLOT_TAG_NONE)
    {
        nandReqQ[chNo][wayNo].headReq = reqPoolPtr->reqPool[reqSlotTag].nextReq;
        reqPoolPtr->reqPool[reqPoolPtr->reqPool[reqSlotTag].nextReq].prevReq = REQ_SLOT_TAG_NONE;
    }
    else
    {
        nandReqQ[chNo][wayNo].headReq = REQ_SLOT_TAG_NONE;
        nandReqQ[chNo][wayNo].tailReq = REQ_SLOT_TAG_NONE;
    }

    reqPoolPtr->reqPool[reqSlotTag].reqQueueType = REQ_QUEUE_TYPE_NONE;
    nandReqQ[chNo][wayNo].reqCnt--;
    notCompletedNandReqCnt--;

    PutToFreeReqQ(reqSlotTag);
    ReleaseBlockedByBufDepReq(reqSlotTag);
}
