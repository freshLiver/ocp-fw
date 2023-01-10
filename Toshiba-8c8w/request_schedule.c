//////////////////////////////////////////////////////////////////////////////////
// request_schedule.c for Cosmos+ OpenSSD
// Copyright (c) 2017 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//			      Sangjin Lee <sjlee@enc.hanyang.ac.kr>
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
// Module Name: Request Scheduler
// File Name: request_schedule.c
//
// Version: v1.0.0
//
// Description:
//	 - decide request execution sequence
//   - issue NAND request to NAND storage controller
//   - check request execution result
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#include <assert.h>
#include "xil_printf.h"
#include "memory_map.h"
#include "nvme/debug.h"

P_COMPLETE_FLAG_TABLE completeFlagTablePtr;
P_STATUS_REPORT_TABLE statusReportTablePtr;
P_ERROR_INFO_TABLE eccErrorInfoTablePtr;
P_RETRY_LIMIT_TABLE retryLimitTablePtr;

P_DIE_STATE_TABLE dieStateTablePtr;
P_WAY_PRIORITY_TABLE wayPriorityTablePtr;

/**
 * @brief Initialize scheduling related tables.
 *
 * The following tables may be used for scheduling:
 *
 * @todo
 *
 * - `dieStateTablePtr`: contains the metadata of all dies
 * - `wayPriorityTablePtr`: manages the way state table of each channel
 * - `completeFlagTablePtr`:
 * - `statusReportTablePtr`:
 * - `eccErrorInfoTablePtr`:
 * - `retryLimitTablePtr`: manages the remain retry count of each die
 *
 * In this function:
 *
 * - all the ways of same channel are in the idle list of their channel.
 * - all the dies are in idle state and connected in serial order
 * - the completion count and status report of all die are set to 0
 * - the retry limit number of each die is set to `RETRY_LIMIT`
 */
void InitReqScheduler()
{
    int chNo, wayNo;

    completeFlagTablePtr = (P_COMPLETE_FLAG_TABLE)COMPLETE_FLAG_TABLE_ADDR;
    statusReportTablePtr = (P_STATUS_REPORT_TABLE)STATUS_REPORT_TABLE_ADDR;
    eccErrorInfoTablePtr = (P_ERROR_INFO_TABLE)ERROR_INFO_TABLE_ADDR;
    retryLimitTablePtr   = (P_RETRY_LIMIT_TABLE)RETRY_LIMIT_TABLE_ADDR;

    dieStateTablePtr    = (P_DIE_STATE_TABLE)DIE_STATE_TABLE_ADDR;
    wayPriorityTablePtr = (P_WAY_PRIORITY_TABLE)WAY_PRIORITY_TABLE_ADDR;

    for (chNo = 0; chNo < USER_CHANNELS; ++chNo)
    {
        wayPriorityTablePtr->wayPriority[chNo].idleHead         = 0;
        wayPriorityTablePtr->wayPriority[chNo].idleTail         = USER_WAYS - 1;
        wayPriorityTablePtr->wayPriority[chNo].statusReportHead = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].statusReportTail = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].eraseHead        = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].eraseTail        = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].readTriggerHead  = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].readTriggerTail  = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].writeHead        = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].writeTail        = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].readTransferHead = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].readTransferTail = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].statusCheckHead  = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].statusCheckTail  = WAY_NONE;

        for (wayNo = 0; wayNo < USER_WAYS; ++wayNo)
        {
            dieStateTablePtr->dieState[chNo][wayNo].dieState          = DIE_STATE_IDLE;
            dieStateTablePtr->dieState[chNo][wayNo].reqStatusCheckOpt = REQ_STATUS_CHECK_OPT_NONE;
            dieStateTablePtr->dieState[chNo][wayNo].prevWay           = wayNo - 1;
            dieStateTablePtr->dieState[chNo][wayNo].nextWay           = wayNo + 1;

            completeFlagTablePtr->completeFlag[chNo][wayNo] = 0;
            statusReportTablePtr->statusReport[chNo][wayNo] = 0;
            retryLimitTablePtr->retryLimit[chNo][wayNo]     = RETRY_LIMIT;
        }
        dieStateTablePtr->dieState[chNo][0].prevWay             = WAY_NONE;
        dieStateTablePtr->dieState[chNo][USER_WAYS - 1].nextWay = WAY_NONE;
    }
}

/**
 * @brief Do schedule until all the requests are done.
 *
 * As mentioned in the paper, the NVMe requests have higher priority than NAND requests.
 * Therefore the function that handles NVMe request `CheckDoneNvmeDmaReq()` should be
 * called before the function for scheduling NAND requests `SchedulingNandReq()`.
 */
void SyncAllLowLevelReqDone()
{
    while ((nvmeDmaReqQ.headReq != REQ_SLOT_TAG_NONE) || notCompletedNandReqCnt || blockedReqCnt)
    {
        CheckDoneNvmeDmaReq();
        SchedulingNandReq();
    }
}

/**
 * @brief Try release request entries by doing scheduling (both NVMe and NAND).
 *
 * Similar to `SyncAllLowLevelReqDone()`, but this function will stop at there is at least
 * one free request entry exists.
 */
void SyncAvailFreeReq()
{
    while (freeReqQ.headReq == REQ_SLOT_TAG_NONE)
    {
        CheckDoneNvmeDmaReq();
        SchedulingNandReq();
    }
}

/**
 * @brief
 *
 * @todo
 *
 * Similar to `SyncAllLowLevelReqDone()`, but
 *
 * @param chNo
 * @param wayNo
 * @param blockNo
 */
void SyncReleaseEraseReq(unsigned int chNo, unsigned int wayNo, unsigned int blockNo)
{
    while (rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag)
    {
        CheckDoneNvmeDmaReq();
        SchedulingNandReq();
    }
}

/**
 * @brief Iteratively do schedule on each channel by calling `SchedulingNandReqPerCh`.
 */
void SchedulingNandReq()
{
    int chNo;

    for (chNo = 0; chNo < USER_CHANNELS; chNo++)
        SchedulingNandReqPerCh(chNo);
}

/**
 * @brief The main function to schedule NAND requests on the specified channel.
 *
 * This function can be separated into 3 parts:
 *
 * 1. select idle ways which has requests want to execute
 *
 * 2. update the request status of each way (second stage status check) and move the die
 * to the idle list if the request is done.
 *
 * 3. update some requests' status (first step status check) and issue requests based on
 * the predefined flash operation priority.
 *
 * Check the inline comments for more detailed explanation.
 *
 * @param chNo the channel number for scheduling
 */
void SchedulingNandReqPerCh(unsigned int chNo)
{
    unsigned int readyBusy, wayNo, reqStatus, nextWay, waitWayCnt;

    waitWayCnt = 0;

    /**
     * Before doing scheduling on this channel, we should check if there is any idle way
     * on this channel.
     *
     * If there is any idle way in the idle list, we have to traverse the idle list, and
     * try to schedule requests on the idle ways.
     */
    if (wayPriorityTablePtr->wayPriority[chNo].idleHead != WAY_NONE)
    {
        wayNo = wayPriorityTablePtr->wayPriority[chNo].idleHead;
        while (wayNo != WAY_NONE)
        {
            /**
             * Currently no available requests should be executed on this way, but there
             * may be some requests in the `blockedByRowAddrDepReqQ` instead. Try to
             * release the `blockedByRowAddrDepReqQ` and check again if there are any
             * requests to do later.
             */
            if (nandReqQ[chNo][wayNo].headReq == REQ_SLOT_TAG_NONE)
                ReleaseBlockedByRowAddrDepReq(chNo, wayNo);

            /**
             * If any blocked request is released in `ReleaseBlockedByRowAddrDepReq()`,
             * they will be added to the `PutToNandReqQ`.
             *
             * If there is any request should be scheduled on this die, move this die
             * from idle state list to the state list corresponding to the request type.
             * Otherwise, there is really no request want to use this die, just skip this
             * die.
             */
            if (nandReqQ[chNo][wayNo].headReq != REQ_SLOT_TAG_NONE)
            {
                nextWay = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
                SelectivGetFromNandIdleList(chNo, wayNo);
                PutToNandWayPriorityTable(nandReqQ[chNo][wayNo].headReq, chNo, wayNo);
                wayNo = nextWay;
            }
            else
            {
                wayNo = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
                waitWayCnt++;
            }
        }
    }

    /**
     * The most important task of this part is to check the request status (second stage
     * status check). Base on the request status, move to the die to the idle state list
     * if its request is done, or back to `statusCheck` list (first stage status check,
     * check next part for details) if its request not complete yet.
     *
     * But beside this, we also move the dies that had completed their requests to the
     * corresponding state list if there is any request should be scheduled on this die,
     * just like what we did in the previous part, before issuing requests in next part.
     */
    if (wayPriorityTablePtr->wayPriority[chNo].statusReportHead != WAY_NONE)
    {
        readyBusy = V2FReadyBusyAsync(&chCtlReg[chNo]);
        wayNo     = wayPriorityTablePtr->wayPriority[chNo].statusReportHead;

        while (wayNo != WAY_NONE)
        {
            if (V2FWayReady(readyBusy, wayNo))
            {
                reqStatus = CheckReqStatus(chNo, wayNo);
                if (reqStatus != REQ_STATUS_RUNNING)
                {
                    /**
                     * Since the previous part had moved all idle ways to the state list
                     * corresponding to the head request of this die, this die must not
                     * in `DIE_STATE_IDLE` state.
                     *
                     * Also, we had check the request state is not `REQ_STATUS_RUNNING`,
                     * thus the `ExecuteNandReq()` must bring the die to `DIE_STATE_IDLE`.
                     */
                    ExecuteNandReq(chNo, wayNo, reqStatus);
                    nextWay = dieStateTablePtr->dieState[chNo][wayNo].nextWay;

                    /**
                     * Since the die become idle, now we can try to schedule request on
                     * this die like what we just did in the previous part, if there is
                     * any request should be executed on this die.
                     *
                     * @note In current implementation, here is the only place to change
                     * the state a die to IDLE and put it into the idle state list.
                     */
                    SelectivGetFromNandStatusReportList(chNo, wayNo);
                    if (nandReqQ[chNo][wayNo].headReq == REQ_SLOT_TAG_NONE)
                        ReleaseBlockedByRowAddrDepReq(chNo, wayNo);

                    if (nandReqQ[chNo][wayNo].headReq != REQ_SLOT_TAG_NONE)
                        PutToNandWayPriorityTable(nandReqQ[chNo][wayNo].headReq, chNo, wayNo);
                    else
                    {
                        PutToNandIdleList(chNo, wayNo);
                        waitWayCnt++;
                    }

                    wayNo = nextWay;
                }
                else if (dieStateTablePtr->dieState[chNo][wayNo].reqStatusCheckOpt == REQ_STATUS_CHECK_OPT_CHECK)
                {
                    /**
                     * If the status shows that this request not finish yet, and the flag
                     * `reqStatusCheckOpt` is set to `REQ_STATUS_CHECK_OPT_CHECK`, which
                     * means this request is not READ_TRANSFER since READ_TRANSFER use
                     * another flag `REQ_STATUS_CHECK_OPT_COMPLETION_FLAG` for checking,
                     * thus we have to move it back to the `statusCheck` list (first stage
                     * status check) for the next part to check it again.
                     *
                     * Similar to next part, because the `CheckReqStatus()` will turn the
                     * flag `reqStatusCheckOpt` from `REQ_STATUS_CHECK_OPT_REPORT` to
                     * `REQ_STATUS_CHECK_OPT_CHECK`, we don't have to manually change the
                     * flag after/before the die was moved to the `statusCheck` list.
                     */
                    nextWay = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
                    SelectivGetFromNandStatusReportList(chNo, wayNo);
                    PutToNandStatusCheckList(chNo, wayNo);
                    wayNo = nextWay;
                }
                else
                {
                    wayNo = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
                    waitWayCnt++;
                }
            }
            else
            {
                wayNo = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
                waitWayCnt++;
            }
        }
    }

    /**
     * In the last part, we now can try to issue the requests based on the priority of
     * flash operations. (check the paper)
     *
     * @note This part reflect the priority of flash operations mentioned in the paper,
     * therefore DO NOT break the order of these 'if' conditions as long as there is no
     * need to adjust the priority of flash operations.
     *
     * If there is at least one request should be scheduled and their ways and the channel
     * is not busy, we can issue the requests by calling `ExecuteNandReq()`.
     *
     * @note Since some request may make the channel controller busy, we should skip the
     * remaining requests right away as the channel become busy.
     *
     * After the command is issued, we should move the die to the state list `statusCheck`
     * or `statusReport` based on the request type:
     *
     * - For READ_TRIGGER, WRITE, ERASE requests:
     *
     *      These types of requests will be moved to the `statusCheck` list as they were
     *      issued. And next time we entered this part, we will check the status of those
     *      requests first, since they are in the `statusCheck` list and status check has
     *      the highest priority.
     *
     *      To check the request status, we use the function `CheckReqStatus()` to get the
     *      requests status of the ways and then move the die to the `statusReport` list.
     *
     *      @note the function `CheckReqStatus()` will change the flag `reqStatusCheckOpt`
     *      to `REQ_STATUS_CHECK_OPT_REPORT`, therefore there is no need to modified the
     *      flag before/after we move the die to the `statusReport` list.
     *
     *      @note Not all the requests' status can be retrieve since the die may be busy.
     *      Therefore, we only move the dies that successfully retrieved the status to the
     *      `statusReport` for second stage status checking.
     *
     * - For READ_TRANSFER requests:
     *
     *      This type of requests will be directly added to the `statusReport` list. in
     *      other words, the status of READ_TRANSFER requests will not be checked in this
     *      part.
     *
     *      GUESS: The reason that we don't check the status of READ_TRANSFER requests may
     *      be this type of requests are supposed to be time consuming task, thus we have
     *      no need to check the status right after they just being issued.
     *
     *      @warning here should second stage status check
     */
    if (waitWayCnt != USER_WAYS)
        if (!V2FIsControllerBusy(&chCtlReg[chNo]))
        {
            if (wayPriorityTablePtr->wayPriority[chNo].statusCheckHead != WAY_NONE)
            {
                readyBusy = V2FReadyBusyAsync(&chCtlReg[chNo]);
                wayNo     = wayPriorityTablePtr->wayPriority[chNo].statusCheckHead;

                while (wayNo != WAY_NONE)
                {
                    if (V2FWayReady(readyBusy, wayNo)) // TODO why this need?
                    {
                        // FIXME: called again in second stage status check?? redundant?
                        reqStatus = CheckReqStatus(chNo, wayNo);

                        SelectiveGetFromNandStatusCheckList(chNo, wayNo);
                        PutToNandStatusReportList(chNo, wayNo);

                        if (V2FIsControllerBusy(&chCtlReg[chNo]))
                            return;
                    }

                    wayNo = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
                }
            }
            if (wayPriorityTablePtr->wayPriority[chNo].readTriggerHead != WAY_NONE)
            {
                wayNo = wayPriorityTablePtr->wayPriority[chNo].readTriggerHead;

                while (wayNo != WAY_NONE)
                {
                    ExecuteNandReq(chNo, wayNo, REQ_STATUS_RUNNING);

                    SelectiveGetFromNandReadTriggerList(chNo, wayNo);
                    PutToNandStatusCheckList(chNo, wayNo);

                    if (V2FIsControllerBusy(&chCtlReg[chNo]))
                        return;

                    wayNo = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
                }
            }

            if (wayPriorityTablePtr->wayPriority[chNo].eraseHead != WAY_NONE)
            {
                wayNo = wayPriorityTablePtr->wayPriority[chNo].eraseHead;

                while (wayNo != WAY_NONE)
                {
                    ExecuteNandReq(chNo, wayNo, REQ_STATUS_RUNNING);

                    SelectiveGetFromNandEraseList(chNo, wayNo);
                    PutToNandStatusCheckList(chNo, wayNo);

                    if (V2FIsControllerBusy(&chCtlReg[chNo]))
                        return;

                    wayNo = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
                }
            }
            if (wayPriorityTablePtr->wayPriority[chNo].writeHead != WAY_NONE)
            {
                wayNo = wayPriorityTablePtr->wayPriority[chNo].writeHead;

                while (wayNo != WAY_NONE)
                {
                    ExecuteNandReq(chNo, wayNo, REQ_STATUS_RUNNING);

                    SelectiveGetFromNandWriteList(chNo, wayNo);
                    PutToNandStatusCheckList(chNo, wayNo);

                    if (V2FIsControllerBusy(&chCtlReg[chNo]))
                        return;

                    wayNo = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
                }
            }
            if (wayPriorityTablePtr->wayPriority[chNo].readTransferHead != WAY_NONE)
            {
                wayNo = wayPriorityTablePtr->wayPriority[chNo].readTransferHead;

                while (wayNo != WAY_NONE)
                {
                    ExecuteNandReq(chNo, wayNo, REQ_STATUS_RUNNING);

                    SelectiveGetFromNandReadTransferList(chNo, wayNo);
                    PutToNandStatusReportList(chNo, wayNo);

                    if (V2FIsControllerBusy(&chCtlReg[chNo]))
                        return;

                    wayNo = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
                }
            }
        }
}

/* -------------------------------------------------------------------------- */
/*                 functions for adjusting the die state list                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Append the specified die to a NAND state list based on the request type of the
 * given request.
 *
 * @param reqSlotTag the request pool entry index of a request.
 * @param chNo the channel number of the specified die.
 * @param wayNo the way number of the specified die.
 */
void PutToNandWayPriorityTable(unsigned int reqSlotTag, unsigned int chNo, unsigned int wayNo)
{
    if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ)
        PutToNandReadTriggerList(chNo, wayNo);
    else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ_TRANSFER)
        PutToNandReadTransferList(chNo, wayNo);
    else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE)
        PutToNandWriteList(chNo, wayNo);
    else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_ERASE)
        PutToNandEraseList(chNo, wayNo);
    else if ((reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_RESET) ||
             (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_SET_FEATURE))
        PutToNandWriteList(chNo, wayNo);
    else
        assert(!"[WARNING] wrong reqCode [WARNING]");
}

/**
 * @brief Append the specified die to the tail of the idle list of its channel.
 *
 * @note This function won't change the `reqStatusCheckOpt` and `dieState` flags.
 *
 * @param chNo the channel number of target die
 * @param wayNo the way number of target die
 */
void PutToNandIdleList(unsigned int chNo, unsigned int wayNo)
{
    if (wayPriorityTablePtr->wayPriority[chNo].idleTail != WAY_NONE)
    {
        dieStateTablePtr->dieState[chNo][wayNo].prevWay = wayPriorityTablePtr->wayPriority[chNo].idleTail;
        dieStateTablePtr->dieState[chNo][wayNo].nextWay = WAY_NONE;
        dieStateTablePtr->dieState[chNo][wayPriorityTablePtr->wayPriority[chNo].idleTail].nextWay = wayNo;
        wayPriorityTablePtr->wayPriority[chNo].idleTail                                           = wayNo;
    }
    else
    {
        dieStateTablePtr->dieState[chNo][wayNo].prevWay = WAY_NONE;
        dieStateTablePtr->dieState[chNo][wayNo].nextWay = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].idleHead = wayNo;
        wayPriorityTablePtr->wayPriority[chNo].idleTail = wayNo;
    }
}

/**
 * @brief Remove the specified die from the idle list of its channel.
 *
 * Since the state lists only records the index of head/tail way in their list, there is
 * no need to modify the `wayPriority` if the target way is at the body of its idle list.
 *
 * @note Like `PutToNandIdleList()`, this function won't change `reqStatusCheckOpt` and
 * `dieState` too.
 *
 * @warning function name typo
 *
 * @param chNo the channel number of target die
 * @param wayNo the way number of target die
 */
void SelectivGetFromNandIdleList(unsigned int chNo, unsigned int wayNo)
{
    if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay != WAY_NONE) &&
        (dieStateTablePtr->dieState[chNo][wayNo].prevWay != WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].prevWay].nextWay =
            dieStateTablePtr->dieState[chNo][wayNo].nextWay;
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].nextWay].prevWay =
            dieStateTablePtr->dieState[chNo][wayNo].prevWay;
    }
    else if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay == WAY_NONE) &&
             (dieStateTablePtr->dieState[chNo][wayNo].prevWay != WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].prevWay].nextWay = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].idleTail = dieStateTablePtr->dieState[chNo][wayNo].prevWay;
    }
    else if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay != WAY_NONE) &&
             (dieStateTablePtr->dieState[chNo][wayNo].prevWay == WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].nextWay].prevWay = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].idleHead = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
    }
    else
    {
        wayPriorityTablePtr->wayPriority[chNo].idleHead = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].idleTail = WAY_NONE;
    }
}

/**
 * @brief Append the specified die to the tail of the status report list of its channel.
 *
 * Similar to `PutToNandIdleList()` but with different target list.
 *
 * @param chNo the channel number of target die
 * @param wayNo the way number of target die
 */
void PutToNandStatusReportList(unsigned int chNo, unsigned int wayNo)
{
    if (wayPriorityTablePtr->wayPriority[chNo].statusReportTail != WAY_NONE)
    {
        dieStateTablePtr->dieState[chNo][wayNo].prevWay = wayPriorityTablePtr->wayPriority[chNo].statusReportTail;
        dieStateTablePtr->dieState[chNo][wayNo].nextWay = WAY_NONE;
        dieStateTablePtr->dieState[chNo][wayPriorityTablePtr->wayPriority[chNo].statusReportTail].nextWay = wayNo;
        wayPriorityTablePtr->wayPriority[chNo].statusReportTail                                           = wayNo;
    }
    else
    {
        dieStateTablePtr->dieState[chNo][wayNo].prevWay         = WAY_NONE;
        dieStateTablePtr->dieState[chNo][wayNo].nextWay         = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].statusReportHead = wayNo;
        wayPriorityTablePtr->wayPriority[chNo].statusReportTail = wayNo;
    }
}

/**
 * @brief Remove the specified die from the status report list of its channel.
 *
 * Similar to `SelectivGetFromNandIdleList()`, but with different target list.
 *
 * @warning function name typo
 *
 * @param chNo the channel number of target die
 * @param wayNo the way number of target die
 */
void SelectivGetFromNandStatusReportList(unsigned int chNo, unsigned int wayNo)
{
    if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay != WAY_NONE) &&
        (dieStateTablePtr->dieState[chNo][wayNo].prevWay != WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].prevWay].nextWay =
            dieStateTablePtr->dieState[chNo][wayNo].nextWay;
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].nextWay].prevWay =
            dieStateTablePtr->dieState[chNo][wayNo].prevWay;
    }
    else if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay == WAY_NONE) &&
             (dieStateTablePtr->dieState[chNo][wayNo].prevWay != WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].prevWay].nextWay = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].statusReportTail = dieStateTablePtr->dieState[chNo][wayNo].prevWay;
    }
    else if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay != WAY_NONE) &&
             (dieStateTablePtr->dieState[chNo][wayNo].prevWay == WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].nextWay].prevWay = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].statusReportHead = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
    }
    else
    {
        wayPriorityTablePtr->wayPriority[chNo].statusReportHead = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].statusReportTail = WAY_NONE;
    }
}

/**
 * @brief Append the specified die to the tail of the read trigger list of its channel.
 *
 * Similar to `PutToNandIdleList()` but with different target list.
 *
 * @param chNo the channel number of target die
 * @param wayNo the way number of target die
 */
void PutToNandReadTriggerList(unsigned int chNo, unsigned int wayNo)
{
    if (wayPriorityTablePtr->wayPriority[chNo].readTriggerTail != WAY_NONE)
    {
        dieStateTablePtr->dieState[chNo][wayNo].prevWay = wayPriorityTablePtr->wayPriority[chNo].readTriggerTail;
        dieStateTablePtr->dieState[chNo][wayNo].nextWay = WAY_NONE;
        dieStateTablePtr->dieState[chNo][wayPriorityTablePtr->wayPriority[chNo].readTriggerTail].nextWay = wayNo;
        wayPriorityTablePtr->wayPriority[chNo].readTriggerTail                                           = wayNo;
    }
    else
    {
        dieStateTablePtr->dieState[chNo][wayNo].prevWay        = WAY_NONE;
        dieStateTablePtr->dieState[chNo][wayNo].nextWay        = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].readTriggerHead = wayNo;
        wayPriorityTablePtr->wayPriority[chNo].readTriggerTail = wayNo;
    }
}

/**
 * @brief Remove the specified die from the read trigger list of its channel.
 *
 * Similar to `SelectivGetFromNandIdleList()`, but with different target list.
 *
 * @param chNo the channel number of target die
 * @param wayNo the way number of target die
 */
void SelectiveGetFromNandReadTriggerList(unsigned int chNo, unsigned int wayNo)
{
    if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay != WAY_NONE) &&
        (dieStateTablePtr->dieState[chNo][wayNo].prevWay != WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].prevWay].nextWay =
            dieStateTablePtr->dieState[chNo][wayNo].nextWay;
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].nextWay].prevWay =
            dieStateTablePtr->dieState[chNo][wayNo].prevWay;
    }
    else if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay == WAY_NONE) &&
             (dieStateTablePtr->dieState[chNo][wayNo].prevWay != WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].prevWay].nextWay = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].readTriggerTail = dieStateTablePtr->dieState[chNo][wayNo].prevWay;
    }
    else if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay != WAY_NONE) &&
             (dieStateTablePtr->dieState[chNo][wayNo].prevWay == WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].nextWay].prevWay = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].readTriggerHead = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
    }
    else
    {
        wayPriorityTablePtr->wayPriority[chNo].readTriggerHead = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].readTriggerTail = WAY_NONE;
    }
}

/**
 * @brief Append the specified die to the tail of the write list of its channel.
 *
 * Similar to `PutToNandIdleList()` but with different target list.
 *
 * @param chNo the channel number of target die
 * @param wayNo the way number of target die
 */
void PutToNandWriteList(unsigned int chNo, unsigned int wayNo)
{
    if (wayPriorityTablePtr->wayPriority[chNo].writeTail != WAY_NONE)
    {
        dieStateTablePtr->dieState[chNo][wayNo].prevWay = wayPriorityTablePtr->wayPriority[chNo].writeTail;
        dieStateTablePtr->dieState[chNo][wayNo].nextWay = WAY_NONE;
        dieStateTablePtr->dieState[chNo][wayPriorityTablePtr->wayPriority[chNo].writeTail].nextWay = wayNo;
        wayPriorityTablePtr->wayPriority[chNo].writeTail                                           = wayNo;
    }
    else
    {
        dieStateTablePtr->dieState[chNo][wayNo].prevWay  = WAY_NONE;
        dieStateTablePtr->dieState[chNo][wayNo].nextWay  = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].writeHead = wayNo;
        wayPriorityTablePtr->wayPriority[chNo].writeTail = wayNo;
    }
}

/**
 * @brief Remove the specified die from the write list of its channel.
 *
 * Similar to `SelectivGetFromNandIdleList()`, but with different target list.
 *
 * @param chNo the channel number of target die
 * @param wayNo the way number of target die
 */
void SelectiveGetFromNandWriteList(unsigned int chNo, unsigned int wayNo)
{
    if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay != WAY_NONE) &&
        (dieStateTablePtr->dieState[chNo][wayNo].prevWay != WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].prevWay].nextWay =
            dieStateTablePtr->dieState[chNo][wayNo].nextWay;
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].nextWay].prevWay =
            dieStateTablePtr->dieState[chNo][wayNo].prevWay;
    }
    else if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay == WAY_NONE) &&
             (dieStateTablePtr->dieState[chNo][wayNo].prevWay != WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].prevWay].nextWay = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].writeTail = dieStateTablePtr->dieState[chNo][wayNo].prevWay;
    }
    else if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay != WAY_NONE) &&
             (dieStateTablePtr->dieState[chNo][wayNo].prevWay == WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].nextWay].prevWay = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].writeHead = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
    }
    else
    {
        wayPriorityTablePtr->wayPriority[chNo].writeHead = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].writeTail = WAY_NONE;
    }
}

/**
 * @brief Append the specified die to the tail of the read transfer list of its channel.
 *
 * Similar to `PutToNandIdleList()` but with different target list.
 *
 * @param chNo the channel number of target die
 * @param wayNo the way number of target die
 */
void PutToNandReadTransferList(unsigned int chNo, unsigned int wayNo)
{
    if (wayPriorityTablePtr->wayPriority[chNo].readTransferTail != WAY_NONE)
    {
        dieStateTablePtr->dieState[chNo][wayNo].prevWay = wayPriorityTablePtr->wayPriority[chNo].readTransferTail;
        dieStateTablePtr->dieState[chNo][wayNo].nextWay = WAY_NONE;
        dieStateTablePtr->dieState[chNo][wayPriorityTablePtr->wayPriority[chNo].readTransferTail].nextWay = wayNo;
        wayPriorityTablePtr->wayPriority[chNo].readTransferTail                                           = wayNo;
    }
    else
    {
        dieStateTablePtr->dieState[chNo][wayNo].prevWay         = WAY_NONE;
        dieStateTablePtr->dieState[chNo][wayNo].nextWay         = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].readTransferHead = wayNo;
        wayPriorityTablePtr->wayPriority[chNo].readTransferTail = wayNo;
    }
}

/**
 * @brief Remove the specified die from the read transfer list of its channel.
 *
 * Similar to `SelectivGetFromNandIdleList()`, but with different target list.
 *
 * @param chNo the channel number of target die
 * @param wayNo the way number of target die
 */
void SelectiveGetFromNandReadTransferList(unsigned int chNo, unsigned int wayNo)
{
    if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay != WAY_NONE) &&
        (dieStateTablePtr->dieState[chNo][wayNo].prevWay != WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].prevWay].nextWay =
            dieStateTablePtr->dieState[chNo][wayNo].nextWay;
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].nextWay].prevWay =
            dieStateTablePtr->dieState[chNo][wayNo].prevWay;
    }
    else if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay == WAY_NONE) &&
             (dieStateTablePtr->dieState[chNo][wayNo].prevWay != WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].prevWay].nextWay = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].readTransferTail = dieStateTablePtr->dieState[chNo][wayNo].prevWay;
    }
    else if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay != WAY_NONE) &&
             (dieStateTablePtr->dieState[chNo][wayNo].prevWay == WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].nextWay].prevWay = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].readTransferHead = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
    }
    else
    {
        wayPriorityTablePtr->wayPriority[chNo].readTransferHead = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].readTransferTail = WAY_NONE;
    }
}

/**
 * @brief Append the specified die to the tail of the erase list of its channel.
 *
 * Similar to `PutToNandIdleList()` but with different target list.
 *
 * @param chNo the channel number of target die
 * @param wayNo the way number of target die
 */
void PutToNandEraseList(unsigned int chNo, unsigned int wayNo)
{
    if (wayPriorityTablePtr->wayPriority[chNo].eraseTail != WAY_NONE)
    {
        dieStateTablePtr->dieState[chNo][wayNo].prevWay = wayPriorityTablePtr->wayPriority[chNo].eraseTail;
        dieStateTablePtr->dieState[chNo][wayNo].nextWay = WAY_NONE;
        dieStateTablePtr->dieState[chNo][wayPriorityTablePtr->wayPriority[chNo].eraseTail].nextWay = wayNo;
        wayPriorityTablePtr->wayPriority[chNo].eraseTail                                           = wayNo;
    }
    else
    {
        dieStateTablePtr->dieState[chNo][wayNo].prevWay  = WAY_NONE;
        dieStateTablePtr->dieState[chNo][wayNo].nextWay  = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].eraseHead = wayNo;
        wayPriorityTablePtr->wayPriority[chNo].eraseTail = wayNo;
    }
}

/**
 * @brief Remove the specified die from the erase list of its channel.
 *
 * Similar to `SelectivGetFromNandIdleList()`, but with different target list.
 *
 * @param chNo the channel number of target die
 * @param wayNo the way number of target die
 */
void SelectiveGetFromNandEraseList(unsigned int chNo, unsigned int wayNo)
{
    if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay != WAY_NONE) &&
        (dieStateTablePtr->dieState[chNo][wayNo].prevWay != WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].prevWay].nextWay =
            dieStateTablePtr->dieState[chNo][wayNo].nextWay;
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].nextWay].prevWay =
            dieStateTablePtr->dieState[chNo][wayNo].prevWay;
    }
    else if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay == WAY_NONE) &&
             (dieStateTablePtr->dieState[chNo][wayNo].prevWay != WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].prevWay].nextWay = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].eraseTail = dieStateTablePtr->dieState[chNo][wayNo].prevWay;
    }
    else if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay != WAY_NONE) &&
             (dieStateTablePtr->dieState[chNo][wayNo].prevWay == WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].nextWay].prevWay = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].eraseHead = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
    }
    else
    {
        wayPriorityTablePtr->wayPriority[chNo].eraseHead = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].eraseTail = WAY_NONE;
    }
}

/**
 * @brief Append the specified die to the tail of the status check list of its channel.
 *
 * Similar to `PutToNandIdleList()` but with different target list.
 *
 * @param chNo the channel number of target die
 * @param wayNo the way number of target die
 */
void PutToNandStatusCheckList(unsigned int chNo, unsigned int wayNo)
{
    if (wayPriorityTablePtr->wayPriority[chNo].statusCheckTail != WAY_NONE)
    {
        dieStateTablePtr->dieState[chNo][wayNo].prevWay = wayPriorityTablePtr->wayPriority[chNo].statusCheckTail;
        dieStateTablePtr->dieState[chNo][wayNo].nextWay = WAY_NONE;
        dieStateTablePtr->dieState[chNo][wayPriorityTablePtr->wayPriority[chNo].statusCheckTail].nextWay = wayNo;
        wayPriorityTablePtr->wayPriority[chNo].statusCheckTail                                           = wayNo;
    }
    else
    {
        dieStateTablePtr->dieState[chNo][wayNo].prevWay        = WAY_NONE;
        dieStateTablePtr->dieState[chNo][wayNo].nextWay        = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].statusCheckHead = wayNo;
        wayPriorityTablePtr->wayPriority[chNo].statusCheckTail = wayNo;
    }
}

/**
 * @brief Remove the specified die from the status check list of its channel.
 *
 * Similar to `SelectivGetFromNandIdleList()`, but with different target list.
 *
 * @param chNo the channel number of target die
 * @param wayNo the way number of target die
 */
void SelectiveGetFromNandStatusCheckList(unsigned int chNo, unsigned int wayNo)
{
    if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay != WAY_NONE) &&
        (dieStateTablePtr->dieState[chNo][wayNo].prevWay != WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].prevWay].nextWay =
            dieStateTablePtr->dieState[chNo][wayNo].nextWay;
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].nextWay].prevWay =
            dieStateTablePtr->dieState[chNo][wayNo].prevWay;
    }
    else if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay == WAY_NONE) &&
             (dieStateTablePtr->dieState[chNo][wayNo].prevWay != WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].prevWay].nextWay = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].statusCheckTail = dieStateTablePtr->dieState[chNo][wayNo].prevWay;
    }
    else if ((dieStateTablePtr->dieState[chNo][wayNo].nextWay != WAY_NONE) &&
             (dieStateTablePtr->dieState[chNo][wayNo].prevWay == WAY_NONE))
    {
        dieStateTablePtr->dieState[chNo][dieStateTablePtr->dieState[chNo][wayNo].nextWay].prevWay = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].statusCheckHead = dieStateTablePtr->dieState[chNo][wayNo].nextWay;
    }
    else
    {
        wayPriorityTablePtr->wayPriority[chNo].statusCheckHead = WAY_NONE;
        wayPriorityTablePtr->wayPriority[chNo].statusCheckTail = WAY_NONE;
    }
}

/* -------------------------------------------------------------------------- */
/*                end of functions for managing die state lists               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Issue a flash operations to the storage controller.
 *
 * To issue a NAND request, we should first allocate a data buffer entry for the request,
 * though some request may not use the buffer, and then call the corresponding function
 * defined in the `nsc_driver.c` based on the request type.
 *
 * Before we issue the request, we should set the `reqStatusCheckOpt` flag of the
 * specified die based on the request type:
 *
 * - for READ_TRANSFER request, set to `REQ_STATUS_CHECK_OPT_COMPLETION_FLAG`, which means
 * the we should use the complete flag table to determine if this request is done. Beside
 * checking the status, this flag also used for distinguishing between READ_TRANSFER and
 * other flash operations, since the ECC result may need to be checked, check the function
 * `CheckReqStatus()` for details.
 *
 * - for RESET, SET_FEATURE request, set to `REQ_STATUS_CHECK_OPT_NONE`, which means the
 * request won't update the status table, and we can just use the busy/ready status to
 * determine whether this request is done.
 *
 * - for READ_TRIGGER, WRITE, ERASE request, set to `REQ_STATUS_CHECK_OPT_CHECK`, but this
 * flag is a little bit complicated; this flag actually represent the first stage status
 * check, and this flag may be updated to the `REQ_STATUS_CHECK_OPT_REPORT`, which is used
 * to represent the second stage status check, if the function `CheckReqStatus()` called
 * during the scheduling process. Check `SchedulingNandReqPerCh()` and `CheckReqStatus()`
 * for details.
 *
 * @warning replace the condition statements with switch statement
 *
 * @param chNo the channel number of the targe die to issue the NAND request.
 * @param wayNo the way number of the targe die to issue the NAND request.
 */
void IssueNandReq(unsigned int chNo, unsigned int wayNo)
{
    unsigned int reqSlotTag, rowAddr;
    void *dataBufAddr;
    void *spareDataBufAddr;
    unsigned int *errorInfo;
    unsigned int *completion;

    reqSlotTag       = nandReqQ[chNo][wayNo].headReq;
    rowAddr          = GenerateNandRowAddr(reqSlotTag);
    dataBufAddr      = (void *)GenerateDataBufAddr(reqSlotTag);
    spareDataBufAddr = (void *)GenerateSpareDataBufAddr(reqSlotTag);

    if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ)
    {
        dieStateTablePtr->dieState[chNo][wayNo].reqStatusCheckOpt = REQ_STATUS_CHECK_OPT_CHECK;

        V2FReadPageTriggerAsync(&chCtlReg[chNo], wayNo, rowAddr);
    }
    else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ_TRANSFER)
    {
        dieStateTablePtr->dieState[chNo][wayNo].reqStatusCheckOpt = REQ_STATUS_CHECK_OPT_COMPLETION_FLAG;

        errorInfo  = (unsigned int *)(&eccErrorInfoTablePtr->errorInfo[chNo][wayNo]);
        completion = (unsigned int *)(&completeFlagTablePtr->completeFlag[chNo][wayNo]);

        if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc == REQ_OPT_NAND_ECC_ON)
            V2FReadPageTransferAsync(&chCtlReg[chNo], wayNo, dataBufAddr, spareDataBufAddr, errorInfo, completion,
                                     rowAddr);
        else
            V2FReadPageTransferRawAsync(&chCtlReg[chNo], wayNo, dataBufAddr, completion);
    }
    else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE)
    {
        dieStateTablePtr->dieState[chNo][wayNo].reqStatusCheckOpt = REQ_STATUS_CHECK_OPT_CHECK;

        V2FProgramPageAsync(&chCtlReg[chNo], wayNo, rowAddr, dataBufAddr, spareDataBufAddr);
    }
    else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_ERASE)
    {
        dieStateTablePtr->dieState[chNo][wayNo].reqStatusCheckOpt = REQ_STATUS_CHECK_OPT_CHECK;

        V2FEraseBlockAsync(&chCtlReg[chNo], wayNo, rowAddr);
    }
    else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_RESET)
    {
        dieStateTablePtr->dieState[chNo][wayNo].reqStatusCheckOpt = REQ_STATUS_CHECK_OPT_NONE;

        V2FResetSync(&chCtlReg[chNo], wayNo);
    }
    else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_SET_FEATURE)
    {
        dieStateTablePtr->dieState[chNo][wayNo].reqStatusCheckOpt = REQ_STATUS_CHECK_OPT_NONE;

        V2FEnterToggleMode(&chCtlReg[chNo], wayNo, TEMPORARY_PAY_LOAD_ADDR);
    }
    else
        assert(!"[WARNING] not defined nand req [WARNING]");
}

/**
 * @brief Get the nand row (block) address of the given request.
 *
 * To get the row address, we have first check if the nand address format is VSA or not.
 * If the address is VSA, we should convert it to the physical address by doing address
 * translation.
 *
 * @param reqSlotTag the request pool index of the target request.
 * @return unsigned int the nand row address for the target request.
 */
unsigned int GenerateNandRowAddr(unsigned int reqSlotTag)
{
    unsigned int rowAddr, lun, virtualBlockNo, tempBlockNo, phyBlockNo, tempPageNo, dieNo;

    if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr == REQ_OPT_NAND_ADDR_VSA)
    {
        dieNo          = Vsa2VdieTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
        virtualBlockNo = Vsa2VblockTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
        phyBlockNo     = Vblock2PblockOfTbsTranslation(virtualBlockNo);
        lun            = phyBlockNo / TOTAL_BLOCKS_PER_LUN;
        tempBlockNo    = phyBlockMapPtr->phyBlock[dieNo][phyBlockNo].remappedPhyBlock % TOTAL_BLOCKS_PER_LUN;
        tempPageNo     = Vsa2VpageTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);

        // FIXME for SLC mode, use the LSB page of MLC page?
        // if(BITS_PER_FLASH_CELL == SLC_MODE)
        //	tempPageNo = Vpage2PlsbPageTranslation(tempPageNo);
    }
    else if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr == REQ_OPT_NAND_ADDR_PHY_ORG)
    {
        dieNo = Pcw2VdieTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalCh,
                                    reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalWay);
        if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace == REQ_OPT_BLOCK_SPACE_TOTAL)
        {
            lun         = reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalBlock / TOTAL_BLOCKS_PER_LUN;
            tempBlockNo = reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalBlock % TOTAL_BLOCKS_PER_LUN;
            tempPageNo  = reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalPage;
        }
        else if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace == REQ_OPT_BLOCK_SPACE_MAIN)
        {
            // FIXME: why not use `USER_BLOCKS_PER_LUN`
            lun         = reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalBlock / MAIN_BLOCKS_PER_LUN;
            tempBlockNo = reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalBlock % MAIN_BLOCKS_PER_LUN +
                          lun * TOTAL_BLOCKS_PER_LUN;

            // phyBlock remap
            tempBlockNo = phyBlockMapPtr->phyBlock[dieNo][tempBlockNo].remappedPhyBlock % TOTAL_BLOCKS_PER_LUN;
            tempPageNo  = reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalPage;
        }
    }
    else
        assert(!"[WARNING] wrong nand addr option [WARNING]");

    if (lun == 0)
        rowAddr = LUN_0_BASE_ADDR + tempBlockNo * PAGES_PER_MLC_BLOCK + tempPageNo;
    else
        rowAddr = LUN_1_BASE_ADDR + tempBlockNo * PAGES_PER_MLC_BLOCK + tempPageNo;

    return rowAddr;
}

/**
 * @brief Get the corresponding data buffer entry address of the given request.
 *
 * Before issuing the DMA operations, we have to set the buffer address where the request
 * should get/put their data during the READ/WRITE request.
 *
 * To do this, we may have to specify the entry index of real data buffer and convert to
 * the real address if the data buffer format not `REQ_OPT_DATA_BUF_ADDR`:
 *
 * - find the index of data buffer entry to be used:
 *
 *      Similar to the two request data buffer `dataBuf` and `tempDataBuf`, there are also
 *      two types of real data buffer specified by the two address `DATA_BUFFER_BASE_ADDR`
 *      and `TEMPORARY_DATA_BUFFER_BASE_ADDR`.
 *
 *      Since the two real data buffer have same number of entries with its corresponding
 *      request data buffer, `AVAILABLE_TEMPORARY_DATA_BUFFER_ENTRY_COUNT` for temp buffer
 *      and `AVAILABLE_DATA_BUFFER_ENTRY_COUNT` for normal buffer, we can just simply use
 *      the index of the request data buffer entry to find the corresponding index of real
 *      data buffer entry.
 *
 * - get the address of real data buffer entry:
 *
 *      Since the real data buffer entry have the same size with NAND page size, and we
 *      have already found the entry index, we can just multiply the index by the page
 *      size defined by `BYTES_PER_DATA_REGION_OF_SLICE`.
 *
 *      However, if the request is a NVMe request, we should do some extra works. Since
 *      the unit size of NVMe block is 4K, which is one-fourth of the page size, if the
 *      LBA didn't align by 4, we have to move N*4K bytes within the real data buffer
 *      entry by using the offset to find the address corresponding to the NVMe block.
 *
 * @param reqSlotTag the request data buffer entry index
 * @return unsigned int the address of real data buffer entry to be used
 */
unsigned int GenerateDataBufAddr(unsigned int reqSlotTag)
{
    if (reqPoolPtr->reqPool[reqSlotTag].reqType == REQ_TYPE_NAND)
    {
        if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat == REQ_OPT_DATA_BUF_ENTRY)
            return (DATA_BUFFER_BASE_ADDR +
                    reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry * BYTES_PER_DATA_REGION_OF_SLICE);
        else if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat == REQ_OPT_DATA_BUF_TEMP_ENTRY)
            return (TEMPORARY_DATA_BUFFER_BASE_ADDR +
                    reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry * BYTES_PER_DATA_REGION_OF_SLICE);
        else if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat == REQ_OPT_DATA_BUF_ADDR)
            return reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.addr;

        /**
         * For some requests like RESET, SET_FEATURE and ERASE, the
         * they won't use the data
         * buffer. Therefore we just assign a reserved buffer for them.
         */
        return RESERVED_DATA_BUFFER_BASE_ADDR;
    }
    else if (reqPoolPtr->reqPool[reqSlotTag].reqType == REQ_TYPE_NVME_DMA)
    {
        if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat == REQ_OPT_DATA_BUF_ENTRY)
            return (DATA_BUFFER_BASE_ADDR +
                    reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry * BYTES_PER_DATA_REGION_OF_SLICE +
                    reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.nvmeBlockOffset * BYTES_PER_NVME_BLOCK);
        else
            assert(!"[WARNING] wrong reqOpt-dataBufFormat [WARNING]");
    }
    else
        assert(!"[WARNING] wrong reqType [WARNING]");
}

/**
 * @brief Get the corresponding sparse data buffer entry address of the given request.
 *
 * Similar to `GenerateDataBufAddr()`, but with different buffer base address, different
 * entry size (256B for metadata/sparse data) and no need to deal with the offset of NVMe
 * requests.
 *
 * @note the `RESERVED_DATA_BUFFER_BASE_ADDR` didn't explicitly split into real data area
 * and sparse data area, hence we should manually add 16K (page size) to get the area for
 * sparse data.
 *
 * @param reqSlotTag the request data buffer entry index
 * @return unsigned int the address of sparse data buffer entry to be used
 */
unsigned int GenerateSpareDataBufAddr(unsigned int reqSlotTag)
{
    if (reqPoolPtr->reqPool[reqSlotTag].reqType == REQ_TYPE_NAND)
    {
        if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat == REQ_OPT_DATA_BUF_ENTRY)
            return (SPARE_DATA_BUFFER_BASE_ADDR +
                    reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry * BYTES_PER_SPARE_REGION_OF_SLICE);
        else if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat == REQ_OPT_DATA_BUF_TEMP_ENTRY)
            return (TEMPORARY_SPARE_DATA_BUFFER_BASE_ADDR +
                    reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry * BYTES_PER_SPARE_REGION_OF_SLICE);
        else if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat == REQ_OPT_DATA_BUF_ADDR)
            return (reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.addr +
                    BYTES_PER_DATA_REGION_OF_SLICE); // modify PAGE_SIZE to other

        return (RESERVED_DATA_BUFFER_BASE_ADDR + BYTES_PER_DATA_REGION_OF_SLICE);
    }
    else if (reqPoolPtr->reqPool[reqSlotTag].reqType == REQ_TYPE_NVME_DMA)
    {
        if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat == REQ_OPT_DATA_BUF_ENTRY)
            return (SPARE_DATA_BUFFER_BASE_ADDR +
                    reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry * BYTES_PER_SPARE_REGION_OF_SLICE);
        else
            assert(!"[WARNING] wrong reqOpt-dataBufFormat [WARNING]");
    }
    else
        assert(!"[WARNING] wrong reqType [WARNING]");
}

/**
 * @brief Update the die status and return the request status.
 *
 * @note This function has a side effect: the flag `reqStatusCheckOpt` of this die may be
 * updated from `REQ_STATUS_CHECK_OPT_CHECK`, which means the first stage status check, to
 * `REQ_STATUS_CHECK_OPT_REPORT`, which means the second stage status check, or changed
 * from `REQ_STATUS_CHECK_OPT_REPORT` to `REQ_STATUS_CHECK_OPT_CHECK`, so this function
 * should be used carefully.
 *
 * @param chNo the target channel number to check the status.
 * @param wayNo the target die number to check the status.
 * @return unsigned int the status of the request being executed on this die.
 */
unsigned int CheckReqStatus(unsigned int chNo, unsigned int wayNo)
{
    unsigned int reqSlotTag, completeFlag, statusReport, errorInfo, readyBusy, status;
    unsigned int *statusReportPtr;

    reqSlotTag = nandReqQ[chNo][wayNo].headReq;
    if (dieStateTablePtr->dieState[chNo][wayNo].reqStatusCheckOpt == REQ_STATUS_CHECK_OPT_COMPLETION_FLAG)
    {
        completeFlag = completeFlagTablePtr->completeFlag[chNo][wayNo];

        if (V2FTransferComplete(completeFlag))
        {
            if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc == REQ_OPT_NAND_ECC_ON)
            {
                errorInfo = CheckEccErrorInfo(chNo, wayNo);

                if (errorInfo == ERROR_INFO_WARNING)
                    return REQ_STATUS_WARNING;
                else if (errorInfo == ERROR_INFO_FAIL)
                    return REQ_STATUS_FAIL;
            }
            return REQ_STATUS_DONE;
        }
    }
    else if (dieStateTablePtr->dieState[chNo][wayNo].reqStatusCheckOpt == REQ_STATUS_CHECK_OPT_CHECK)
    {
        statusReportPtr = (unsigned int *)(&statusReportTablePtr->statusReport[chNo][wayNo]);

        V2FStatusCheckAsync(&chCtlReg[chNo], wayNo, statusReportPtr);

        dieStateTablePtr->dieState[chNo][wayNo].reqStatusCheckOpt = REQ_STATUS_CHECK_OPT_REPORT;
    }
    else if (dieStateTablePtr->dieState[chNo][wayNo].reqStatusCheckOpt == REQ_STATUS_CHECK_OPT_REPORT)
    {
        statusReport = statusReportTablePtr->statusReport[chNo][wayNo];

        if (V2FRequestReportDone(statusReport))
        {
            status = V2FEliminateReportDoneFlag(statusReport);
            if (V2FRequestComplete(status))
            {
                if (V2FRequestFail(status))
                    return REQ_STATUS_FAIL;

                dieStateTablePtr->dieState[chNo][wayNo].reqStatusCheckOpt = REQ_STATUS_CHECK_OPT_NONE;
                return REQ_STATUS_DONE;
            }
            else
                dieStateTablePtr->dieState[chNo][wayNo].reqStatusCheckOpt = REQ_STATUS_CHECK_OPT_CHECK;
        }
    }
    else if (dieStateTablePtr->dieState[chNo][wayNo].reqStatusCheckOpt == REQ_STATUS_CHECK_OPT_NONE)
    {
        readyBusy = V2FReadyBusyAsync(&chCtlReg[chNo]);

        if (V2FWayReady(readyBusy, wayNo))
            return REQ_STATUS_DONE;
    }
    else
        assert(!"[WARNING] wrong request status check option [WARNING]");

    return REQ_STATUS_RUNNING;
}

unsigned int CheckEccErrorInfo(unsigned int chNo, unsigned int wayNo)
{
    unsigned int errorInfo0, errorInfo1, reqSlotTag;

    reqSlotTag = nandReqQ[chNo][wayNo].headReq;

    errorInfo0 = eccErrorInfoTablePtr->errorInfo[chNo][wayNo][0];
    errorInfo1 = eccErrorInfoTablePtr->errorInfo[chNo][wayNo][1];

    if (V2FCrcValid(eccErrorInfoTablePtr->errorInfo[chNo][wayNo]))
    // if (V2FPageDecodeSuccess(&eccErrorInfoTablePtr->errorInfo[chNo][wayNo][1]))
    {
        if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning == REQ_OPT_NAND_ECC_WARNING_ON)
            if (V2FWorstChunkErrorCount(&errorInfo0) > BIT_ERROR_THRESHOLD_PER_CHUNK)
                return ERROR_INFO_WARNING;

        return ERROR_INFO_PASS;
    }

    return ERROR_INFO_FAIL;
}

/**
 * @brief Update die state and issue new NAND requests if the die is in IDLE state.
 *
 * If the die state is IDLE, we can just issue new NAND request on that die. But if the
 * state is EXE, we should do different thing based on the request status:
 *
 * - previous request is RUNNING
 *
 *      Nothing to do, just wait for it.
 *
 * - previous request is DONE
 *
 *      Before we set the die state to IDLE, we need to do something based on its request
 *      type:
 *
 *      - if previous request is READ_TRIGGER, just convert to READ_TRANSFER
 *      - otherwise, removed from the request queue and reset the retry count
 *
 *      @note if a command should be split into some continuous requests, we should modify
 *      this to ensure other requests will not be executed before these requests finished.
 *      Take READ for example, if a READ_TRIGGER finished, the data is still in its die
 *      register, thus we should make sure the READ_TRANSFER to be the next request that
 *      will be executed on this die to prevent the die register from being overwritten.
 *
 * - previous request is FAIL
 *
 *      If the request failed, there are different things to do based on the request type.
 *      For READ_TRIGGER and READ_TRANSFER, retry the request (from READ_TRIGGER state)
 *      until reaching the retry limitation. For other requests, just mark as bad block.
 *
 *
 * - previous request is WARNING
 *
 *      This means ECC failed, report bad block then make die IDLE and reset retry count
 *
 * @todo bad block and ECC related handling
 *
 * @note in current implementation, this is the only function that changes the dieState.
 *
 * @param chNo the channel number which should exec this request
 * @param wayNo the way number which should exec this request
 * @param reqStatus the status of the previous request executed on the specified die
 */
void ExecuteNandReq(unsigned int chNo, unsigned int wayNo, unsigned int reqStatus)
{
    unsigned int reqSlotTag, rowAddr, phyBlockNo;
    unsigned char *badCheck;

    reqSlotTag = nandReqQ[chNo][wayNo].headReq;

    switch (dieStateTablePtr->dieState[chNo][wayNo].dieState)
    {
    case DIE_STATE_IDLE:
        IssueNandReq(chNo, wayNo);
        dieStateTablePtr->dieState[chNo][wayNo].dieState = DIE_STATE_EXE;
        break;
    case DIE_STATE_EXE:
        if (reqStatus == REQ_STATUS_DONE)
        {
            if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ)
                reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_READ_TRANSFER;
            else
            {
                retryLimitTablePtr->retryLimit[chNo][wayNo] = RETRY_LIMIT;
                GetFromNandReqQ(chNo, wayNo, reqStatus, reqPoolPtr->reqPool[reqSlotTag].reqCode);
            }

            dieStateTablePtr->dieState[chNo][wayNo].dieState = DIE_STATE_IDLE;
        }
        else if (reqStatus == REQ_STATUS_FAIL)
        {
            if ((reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ) ||
                (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ_TRANSFER))
                if (retryLimitTablePtr->retryLimit[chNo][wayNo] > 0)
                {
                    retryLimitTablePtr->retryLimit[chNo][wayNo]--;
                    // FIXME: why not just use READ_TRANSFER
                    if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ_TRANSFER)
                        reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_READ;

                    dieStateTablePtr->dieState[chNo][wayNo].dieState = DIE_STATE_IDLE;
                    return;
                }

            if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ)
                xil_printf("Read Trigger FAIL on      ");
            else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ_TRANSFER)
                xil_printf("Read Transfer FAIL on     ");
            else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE)
                xil_printf("Write FAIL on             ");
            else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_ERASE)
                xil_printf("Erase FAIL on             ");

            rowAddr = GenerateNandRowAddr(reqSlotTag);
            xil_printf("ch %x way %x rowAddr %x / completion %x statusReport %x \r\n", chNo, wayNo, rowAddr,
                       completeFlagTablePtr->completeFlag[chNo][wayNo],
                       statusReportTablePtr->statusReport[chNo][wayNo]);

            if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc == REQ_OPT_NAND_ECC_OFF)
                if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat == REQ_OPT_DATA_BUF_ADDR)
                {
                    // Request fail in the bad block detection process
                    badCheck  = (unsigned char *)reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.addr;
                    *badCheck = PSEUDO_BAD_BLOCK_MARK; // FIXME: why not two step assign ?
                }

            // grown bad block information update
            phyBlockNo = ((rowAddr % LUN_1_BASE_ADDR) / PAGES_PER_MLC_BLOCK) +
                         ((rowAddr / LUN_1_BASE_ADDR) * TOTAL_BLOCKS_PER_LUN);
            UpdatePhyBlockMapForGrownBadBlock(Pcw2VdieTranslation(chNo, wayNo), phyBlockNo);

            retryLimitTablePtr->retryLimit[chNo][wayNo] = RETRY_LIMIT;
            GetFromNandReqQ(chNo, wayNo, reqStatus, reqPoolPtr->reqPool[reqSlotTag].reqCode);
            dieStateTablePtr->dieState[chNo][wayNo].dieState = DIE_STATE_IDLE;
        }
        else if (reqStatus == REQ_STATUS_WARNING)
        {
            rowAddr = GenerateNandRowAddr(reqSlotTag);
            xil_printf("ECC Uncorrectable Soon on ch %x way %x rowAddr %x / completion %x statusReport %x \r\n",
                       chNo, wayNo, rowAddr, completeFlagTablePtr->completeFlag[chNo][wayNo],
                       statusReportTablePtr->statusReport[chNo][wayNo]);

            // grown bad block information update
            phyBlockNo = ((rowAddr % LUN_1_BASE_ADDR) / PAGES_PER_MLC_BLOCK) +
                         ((rowAddr / LUN_1_BASE_ADDR) * TOTAL_BLOCKS_PER_LUN);
            UpdatePhyBlockMapForGrownBadBlock(Pcw2VdieTranslation(chNo, wayNo), phyBlockNo);

            retryLimitTablePtr->retryLimit[chNo][wayNo] = RETRY_LIMIT;
            GetFromNandReqQ(chNo, wayNo, reqStatus, reqPoolPtr->reqPool[reqSlotTag].reqCode);
            dieStateTablePtr->dieState[chNo][wayNo].dieState = DIE_STATE_IDLE;
        }
        else if (reqStatus == REQ_STATUS_RUNNING)
            break;
        else
            assert(!"[WARNING] wrong req status [WARNING]");
        break;
    }
}
