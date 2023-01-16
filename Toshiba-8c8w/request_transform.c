//////////////////////////////////////////////////////////////////////////////////
// request_transform.c for Cosmos+ OpenSSD
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
// File Name: request_transform.c
//
// Version: v1.0.0
//
// Description:
//	 - transform request information
//   - check dependency between requests
//   - issue host DMA request to host DMA engine
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#include "xil_printf.h"
#include <assert.h>
#include "nvme/nvme.h"
#include "nvme/host_lld.h"
#include "memory_map.h"
#include "ftl_config.h"

P_ROW_ADDR_DEPENDENCY_TABLE rowAddrDependencyTablePtr;

void InitDependencyTable()
{
    unsigned int blockNo, wayNo, chNo;
    rowAddrDependencyTablePtr = (P_ROW_ADDR_DEPENDENCY_TABLE)ROW_ADDR_DEPENDENCY_TABLE_ADDR;

    for (blockNo = 0; blockNo < MAIN_BLOCKS_PER_DIE; blockNo++)
    {
        for (wayNo = 0; wayNo < USER_WAYS; wayNo++)
        {
            for (chNo = 0; chNo < USER_CHANNELS; chNo++)
            {
                rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage   = 0;
                rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt   = 0;
                rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag = 0;
            }
        }
    }
}

/**
 * @brief Split NVMe command into slice requests.
 *
 * @note The unit of the given `startLba` and `nlb` is NVMe block, not NAND block.
 *
 * To get the starting LSA of this NVMe command, we need to divide the given `startLba` by
 * `NVME_BLOCKS_PER_SLICE` which indicates that how many NVMe blocks can be merged into a
 * slice request.
 *
 * To get the number of NAND blocks needed by this NVMe command, we should first align the
 * starting NVMe block address `startLba` to slice 0, then convert the ending NVMe block
 * address (`startLba` % `NVME_BLOCKS_PER_SLICE` + `requestedNvmeBlock`) to LSA, then the
 * result indicates the number of slice requests needed by this NVMe command.
 *
 * @note Accroding to the NVMe spec, NLB is a 0's based value, so we should increase the
 * `requestedNvmeBlock` by 1 to get the real number of NVMe blocks to be read/written by
 * this NVMe command.
 *
 * Now the address translation part is finished and we can start to split the NVMe command
 * into slice requests. The splitting process can be separated into 3 steps:
 *
 * 1. Fill the remaining NVMe blocks in first slice request (head)
 *
 *  Since the `startLba` may not perfectly align to the first NVMe block of first slice
 *  command, we should access the tailing N NVMe blocks in the first slice request, where
 *  N is the number of misaligned NVMe blocks in the first slice requests.
 *
 * 2. Generate slice requests for the aligned NVMe blocks (body)
 *
 *  General case. The number of the NVMe blocks to be filled by these slice requests is
 *  exactly `NVME_BLOCKS_PER_SLICE`. So here just simply use a loop to generate same slice
 *  requests.
 *
 * 3. Generate slice request for the remaining NVMe blocks (tail)
 *
 *  Similar to the first step, but here we need to access the first K NVMe blocks in the
 *  last slice request, where K is the number of remaining NVMe blocks in this slice
 *  request.
 *
 * @todo generalize the three steps
 *
 * @param cmdSlotTag @todo //TODO
 * @param startLba address of the first logical NVMe block to read/write.
 * @param nlb number of logical NVMe blocks to read/write.
 * @param cmdCode opcode of the given NVMe command.
 */
void ReqTransNvmeToSlice(unsigned int cmdSlotTag, unsigned int startLba, unsigned int nlb, unsigned int cmdCode)
{
    unsigned int reqSlotTag, requestedNvmeBlock, tempNumOfNvmeBlock, transCounter, tempLsa, loop, nvmeBlockOffset,
        nvmeDmaStartIndex, reqCode;

    requestedNvmeBlock = nlb + 1;
    transCounter       = 0;
    nvmeDmaStartIndex  = 0;
    tempLsa            = startLba / NVME_BLOCKS_PER_SLICE;
    loop               = ((startLba % NVME_BLOCKS_PER_SLICE) + requestedNvmeBlock) / NVME_BLOCKS_PER_SLICE;

    // translate the opcode for NVMe command into that for slice requests.
    if (cmdCode == IO_NVM_WRITE)
        reqCode = REQ_CODE_WRITE;
    else if (cmdCode == IO_NVM_READ)
        reqCode = REQ_CODE_READ;
    else
        assert(!"[WARNING] Not supported command code [WARNING]");

    // first transform
    nvmeBlockOffset = (startLba % NVME_BLOCKS_PER_SLICE);
    if (loop)
        tempNumOfNvmeBlock = NVME_BLOCKS_PER_SLICE - nvmeBlockOffset;
    else
        tempNumOfNvmeBlock = requestedNvmeBlock;

    reqSlotTag = GetFromFreeReqQ();

    reqPoolPtr->reqPool[reqSlotTag].reqType                     = REQ_TYPE_SLICE;
    reqPoolPtr->reqPool[reqSlotTag].reqCode                     = reqCode;
    reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag              = cmdSlotTag;
    reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr            = tempLsa;
    reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.startIndex      = nvmeDmaStartIndex;
    reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.nvmeBlockOffset = nvmeBlockOffset;
    reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock  = tempNumOfNvmeBlock;

    PutToSliceReqQ(reqSlotTag);

    tempLsa++;
    transCounter++;
    nvmeDmaStartIndex += tempNumOfNvmeBlock;

    // transform continue
    while (transCounter < loop)
    {
        nvmeBlockOffset    = 0;
        tempNumOfNvmeBlock = NVME_BLOCKS_PER_SLICE;

        reqSlotTag = GetFromFreeReqQ();

        reqPoolPtr->reqPool[reqSlotTag].reqType                     = REQ_TYPE_SLICE;
        reqPoolPtr->reqPool[reqSlotTag].reqCode                     = reqCode;
        reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag              = cmdSlotTag;
        reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr            = tempLsa;
        reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.startIndex      = nvmeDmaStartIndex;
        reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.nvmeBlockOffset = nvmeBlockOffset;
        reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock  = tempNumOfNvmeBlock;

        PutToSliceReqQ(reqSlotTag);

        tempLsa++;
        transCounter++;
        nvmeDmaStartIndex += tempNumOfNvmeBlock;
    }

    // last transform
    nvmeBlockOffset    = 0;
    tempNumOfNvmeBlock = (startLba + requestedNvmeBlock) % NVME_BLOCKS_PER_SLICE;
    if ((tempNumOfNvmeBlock == 0) || (loop == 0))
        return;

    reqSlotTag = GetFromFreeReqQ();

    reqPoolPtr->reqPool[reqSlotTag].reqType                     = REQ_TYPE_SLICE;
    reqPoolPtr->reqPool[reqSlotTag].reqCode                     = reqCode;
    reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag              = cmdSlotTag;
    reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr            = tempLsa;
    reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.startIndex      = nvmeDmaStartIndex;
    reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.nvmeBlockOffset = nvmeBlockOffset;
    reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock  = tempNumOfNvmeBlock;

    PutToSliceReqQ(reqSlotTag);
}

/**
 * @brief Clear the specified data buffer entry and sync dirty data if needed.
 *
 * In current implementation, the data buffer entry used for a request is determined on
 * the `logicalSliceAddr` of that request, therefore the data buffer entry may be reused.
 *
 * If the data buffer entry need to be reused, we may need to sync the buffer content if
 * the content of the data buffer entry was marked as dirty.
 *
 * @param originReqSlotTag the request pool entry index of the given slice request.
 */
void EvictDataBufEntry(unsigned int originReqSlotTag)
{
    unsigned int reqSlotTag, virtualSliceAddr, dataBufEntry;

    dataBufEntry = reqPoolPtr->reqPool[originReqSlotTag].dataBufInfo.entry;
    if (dataBufMapPtr->dataBuf[dataBufEntry].dirty == DATA_BUF_DIRTY)
    {
        reqSlotTag       = GetFromFreeReqQ();
        virtualSliceAddr = AddrTransWrite(dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr);

        reqPoolPtr->reqPool[reqSlotTag].reqType          = REQ_TYPE_NAND;
        reqPoolPtr->reqPool[reqSlotTag].reqCode          = REQ_CODE_WRITE;
        reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag   = reqPoolPtr->reqPool[originReqSlotTag].nvmeCmdSlotTag;
        reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr;
        reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat          = REQ_OPT_DATA_BUF_ENTRY;
        reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr               = REQ_OPT_NAND_ADDR_VSA;
        reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc                = REQ_OPT_NAND_ECC_ON;
        reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning         = REQ_OPT_NAND_ECC_WARNING_ON;
        reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
        reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace             = REQ_OPT_BLOCK_SPACE_MAIN;
        reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry             = dataBufEntry;
        UpdateDataBufEntryInfoBlockingReq(dataBufEntry, reqSlotTag);
        reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = virtualSliceAddr;

        SelectLowLevelReqQ(reqSlotTag);

        dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_CLEAN;
    }
}

/**
 * @brief Generate and dispatch a flash read request for the given slice request.
 *
 * @param originReqSlotTag the request pool entry index of the given slice request.
 */
void DataReadFromNand(unsigned int originReqSlotTag)
{
    unsigned int reqSlotTag, virtualSliceAddr;

    virtualSliceAddr = AddrTransRead(reqPoolPtr->reqPool[originReqSlotTag].logicalSliceAddr);

    if (virtualSliceAddr != VSA_FAIL)
    {
        reqSlotTag = GetFromFreeReqQ();

        reqPoolPtr->reqPool[reqSlotTag].reqType          = REQ_TYPE_NAND;
        reqPoolPtr->reqPool[reqSlotTag].reqCode          = REQ_CODE_READ;
        reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag   = reqPoolPtr->reqPool[originReqSlotTag].nvmeCmdSlotTag;
        reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = reqPoolPtr->reqPool[originReqSlotTag].logicalSliceAddr;
        reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat          = REQ_OPT_DATA_BUF_ENTRY;
        reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr               = REQ_OPT_NAND_ADDR_VSA;
        reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc                = REQ_OPT_NAND_ECC_ON;
        reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning         = REQ_OPT_NAND_ECC_WARNING_ON;
        reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
        reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace             = REQ_OPT_BLOCK_SPACE_MAIN;

        reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry =
            reqPoolPtr->reqPool[originReqSlotTag].dataBufInfo.entry;
        UpdateDataBufEntryInfoBlockingReq(reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry, reqSlotTag);
        reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = virtualSliceAddr;

        SelectLowLevelReqQ(reqSlotTag);
    }
}

/**
 * @brief Data Buffer Manager. Handle all the pending slice requests.
 *
 * This function will repeat the following steps until all the pending slice requests are
 * consumed:
 *
 * 1. Select a slice request from the slice request queue `sliceReqQ`.
 *
 * 2. Allocate a data buffer entry for the request and generate flash requests if needed.
 *
 *  @warning Why no need to modify `logicalSliceAddr` and generate flash request when
 *  buffer hit? data cache hit??
 *
 * 3. Generate NVMe transfer/receive request for read/write request.
 *
 *  @warning Why mark the data buffer dirty for write request?
 *
 * 4. Dispatch the transfer/receive request by calling `SelectLowLevelReqQ()`.
 *
 *
 * @note This function is currently only called after `handle_nvme_io_cmd()` during the
 * process of handling NVMe I/O commands in `nvme_main.c`.
 */
void ReqTransSliceToLowLevel()
{
    unsigned int reqSlotTag, dataBufEntry;

    // consume all pending slice requests in slice request queue
    while (sliceReqQ.headReq != REQ_SLOT_TAG_NONE)
    {
        // get the request pool entry index of the slice request
        reqSlotTag = GetFromSliceReqQ();
        if (reqSlotTag == REQ_SLOT_TAG_FAIL)
            return;

        /*
         * In current implementation, the data buffer to be used is determined on the
         * `logicalSliceAddr` of this request, so the data buffer may already be allocated
         * before and so we can simply reuse that data buffer.
         *
         * If the data buffer not exists, we must allocate a data buffer entry by calling
         * `AllocateDataBuf()` and initialize the newly created data buffer.
         */
        dataBufEntry = CheckDataBufHit(reqSlotTag);
        if (dataBufEntry != DATA_BUF_FAIL)
        {
            // data buffer hit
            reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
        }
        else
        {
            // data buffer miss, allocate a new buffer entry
            dataBufEntry                                      = AllocateDataBuf();
            reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;

            // initialize the newly allocated data buffer entry for this request
            EvictDataBufEntry(reqSlotTag);
            dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr =
                reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;
            PutToDataBufHashList(dataBufEntry);

            /*
             * The allocated buffer will be used to store the data to be sent to host, or
             * received from the host. So before transfering the data to host, we need to
             * call the function `DataReadFromNand()` to read the desired data to buffer.
             */
            if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ)
                DataReadFromNand(reqSlotTag);
            else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE)
                if (reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock != NVME_BLOCKS_PER_SLICE)
                    // for read modify write
                    DataReadFromNand(reqSlotTag);
        }

        // generate NVMe request by replacing the slice request entry directly
        if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE)
        {
            dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_DIRTY;
            reqPoolPtr->reqPool[reqSlotTag].reqCode    = REQ_CODE_RxDMA;
        }
        else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ)
            reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_TxDMA;
        else
            assert(!"[WARNING] Not supported reqCode. [WARNING]");

        reqPoolPtr->reqPool[reqSlotTag].reqType              = REQ_TYPE_NVME_DMA;
        reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_ENTRY;

        UpdateDataBufEntryInfoBlockingReq(dataBufEntry, reqSlotTag);
        SelectLowLevelReqQ(reqSlotTag);
    }
}

/**
 * @brief Check if this request has the buffer dependency problem.
 *
 * If the specified request is the first (head) request in the blocking request queue,
 * which means this request should not be blocked by buffer dependency, return PASS.
 * Otherwise return BLOCKED without adding this request to the blocking request queue.
 *
 * @todo why check the block head
 *
 * @param reqSlotTag the request pool entry index of the request to be checked
 * @return unsigned int 1 for pass, 0 for blocked
 */
unsigned int CheckBufDep(unsigned int reqSlotTag)
{
    if (reqPoolPtr->reqPool[reqSlotTag].prevBlockingReq == REQ_SLOT_TAG_NONE)
        return BUF_DEPENDENCY_REPORT_PASS;
    else
        return BUF_DEPENDENCY_REPORT_BLOCKED;
}

/**
 * @brief Check if this request has the row address dependency problem.
 *
 * @param reqSlotTag the request pool entry index of the request to be checked
 * @param checkRowAddrDepOpt @todo meaning
 * @return unsigned int 1 for pass, 0 for blocked
 */
unsigned int CheckRowAddrDep(unsigned int reqSlotTag, unsigned int checkRowAddrDepOpt)
{
    unsigned int dieNo, chNo, wayNo, blockNo, pageNo;

    if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr == REQ_OPT_NAND_ADDR_VSA)
    {
        dieNo   = Vsa2VdieTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
        chNo    = Vdie2PchTranslation(dieNo);
        wayNo   = Vdie2PwayTranslation(dieNo);
        blockNo = Vsa2VblockTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
        pageNo  = Vsa2VpageTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
    }
    else
        assert(!"[WARNING] Not supported reqOpt-nandAddress [WARNING]");

    if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ)
    {
        if (checkRowAddrDepOpt == ROW_ADDR_DEPENDENCY_CHECK_OPT_SELECT)
        {
            if (rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag)
                SyncReleaseEraseReq(chNo, wayNo, blockNo);

            if (pageNo < rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage)
                return ROW_ADDR_DEPENDENCY_REPORT_PASS;

            rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt++;
        }
        else if (checkRowAddrDepOpt == ROW_ADDR_DEPENDENCY_CHECK_OPT_RELEASE)
        {
            if (pageNo < rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage)
            {
                rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt--;
                return ROW_ADDR_DEPENDENCY_REPORT_PASS;
            }
        }
        else
            assert(!"[WARNING] Not supported checkRowAddrDepOpt [WARNING]");
    }
    else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE)
    {
        if (pageNo == rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage)
        {
            rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage++;

            return ROW_ADDR_DEPENDENCY_REPORT_PASS;
        }
    }
    else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_ERASE)
    {
        if (rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage ==
            reqPoolPtr->reqPool[reqSlotTag].nandInfo.programmedPageCnt)
            if (rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt == 0)
            {
                rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage   = 0;
                rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag = 0;

                return ROW_ADDR_DEPENDENCY_REPORT_PASS;
            }

        if (checkRowAddrDepOpt == ROW_ADDR_DEPENDENCY_CHECK_OPT_SELECT)
            rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag = 1;
        else if (checkRowAddrDepOpt == ROW_ADDR_DEPENDENCY_CHECK_OPT_RELEASE)
        {
            // pass, go to return
        }
        else
            assert(!"[WARNING] Not supported checkRowAddrDepOpt [WARNING]");
    }
    else
        assert(!"[WARNING] Not supported reqCode [WARNING]");

    return ROW_ADDR_DEPENDENCY_REPORT_BLOCKED;
}

unsigned int UpdateRowAddrDepTableForBufBlockedReq(unsigned int reqSlotTag)
{
    unsigned int dieNo, chNo, wayNo, blockNo, pageNo, bufDepCheckReport;

    if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr == REQ_OPT_NAND_ADDR_VSA)
    {
        dieNo   = Vsa2VdieTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
        chNo    = Vdie2PchTranslation(dieNo);
        wayNo   = Vdie2PwayTranslation(dieNo);
        blockNo = Vsa2VblockTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
        pageNo  = Vsa2VpageTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
    }
    else
        assert(!"[WARNING] Not supported reqOpt-nandAddress [WARNING]");

    if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ)
    {
        if (rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag)
        {
            SyncReleaseEraseReq(chNo, wayNo, blockNo);

            bufDepCheckReport = CheckBufDep(reqSlotTag);
            if (bufDepCheckReport == BUF_DEPENDENCY_REPORT_PASS)
            {
                if (pageNo < rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage)
                    PutToNandReqQ(reqSlotTag, chNo, wayNo);
                else
                {
                    rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt++;
                    PutToBlockedByRowAddrDepReqQ(reqSlotTag, chNo, wayNo);
                }

                return ROW_ADDR_DEPENDENCY_TABLE_UPDATE_REPORT_SYNC;
            }
        }
        rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt++;
    }
    else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_ERASE)
        rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag = 1;

    return ROW_ADDR_DEPENDENCY_TABLE_UPDATE_REPORT_DONE;
}

/**
 * @brief Dispatch the NVMe/NAND request to corresponding request queue.
 *
 * @param reqSlotTag the request pool index of the given request.
 */
void SelectLowLevelReqQ(unsigned int reqSlotTag)
{
    unsigned int dieNo, chNo, wayNo, bufDepCheckReport, rowAddrDepCheckReport, rowAddrDepTableUpdateReport;

    bufDepCheckReport = CheckBufDep(reqSlotTag);

    /**
     * Check if this request can pass the buffer dependency check:
     *
     * If the request doesn't have buffer dependency problem, schedule the request and add
     * to the corresponding request queue based on the request type.
     *
     * If the request should be blocked, first check if it also have the row address dep
     * problem (NAND only), then block the request by inserting to `blockedByBufDep`.
     */
    if (bufDepCheckReport == BUF_DEPENDENCY_REPORT_PASS)
    {
        /**
         * For host requests, just issue the request and then push to the corrsponding
         * request queue for checking the status of that request later.
         */
        if (reqPoolPtr->reqPool[reqSlotTag].reqType == REQ_TYPE_NVME_DMA)
        {
            IssueNvmeDmaReq(reqSlotTag);
            PutToNvmeDmaReqQ(reqSlotTag);
        }

        /**
         * For NAND request, we may have to translate the address, since the address
         * format of a request may be VSA (Virtual Slice Address), that is to say the
         * Location Vector (chNo, wayNo, rowNo/blockNo) mentioned in the paper.
         *
         * Sometimes, like the RESET and SET_FEATURE requests sent by `InitNandArray()`,
         * the given request's address format may not be VSA, so we can easily get the
         * channel number and the way number by accessing `nandInfo` of the given request.
         */
        else if (reqPoolPtr->reqPool[reqSlotTag].reqType == REQ_TYPE_NAND)
        {
            // translate the location vector address to physical address
            if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr == REQ_OPT_NAND_ADDR_VSA)
            {
                dieNo = Vsa2VdieTranslation(reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
                chNo  = Vdie2PchTranslation(dieNo);
                wayNo = Vdie2PwayTranslation(dieNo);
            }
            else if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr == REQ_OPT_NAND_ADDR_PHY_ORG)
            {
                chNo  = reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalCh;
                wayNo = reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalWay;
            }
            else
                assert(!"[WARNING] Not supported reqOpt-nandAddress [WARNING]");

            /**
             * Flash requests like READ and WRITE may be blocked when the way is busy, so
             * we have to check the dep of row address and determine whether we have to
             * block the request.
             *
             * If the request can not pass the row dependency check, this request should
             * be added to the `BlockedByRowAddrDepReqQ` and wait for being released by
             * calling the function `ReleaseBlockedByBufDepReq()`.
             *
             * If the request passed the check or no need to be checked, we can just move
             * the request to the `NandReqQ`.
             *
             * @todo What kinds of requests are need to be checked?
             */
            if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck == REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK)
            {
                rowAddrDepCheckReport = CheckRowAddrDep(reqSlotTag, ROW_ADDR_DEPENDENCY_CHECK_OPT_SELECT);

                if (rowAddrDepCheckReport == ROW_ADDR_DEPENDENCY_REPORT_PASS)
                    PutToNandReqQ(reqSlotTag, chNo, wayNo);
                else if (rowAddrDepCheckReport == ROW_ADDR_DEPENDENCY_REPORT_BLOCKED)
                    PutToBlockedByRowAddrDepReqQ(reqSlotTag, chNo, wayNo);
                else
                    assert(!"[WARNING] Not supported report [WARNING]");
            }
            else if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck ==
                     REQ_OPT_ROW_ADDR_DEPENDENCY_NONE)
                PutToNandReqQ(reqSlotTag, chNo, wayNo);
            else
                assert(!"[WARNING] Not supported reqOpt [WARNING]");
        }
        else
            assert(!"[WARNING] Not supported reqType [WARNING]");
    }
    else if (bufDepCheckReport == BUF_DEPENDENCY_REPORT_BLOCKED)
    {
        if (reqPoolPtr->reqPool[reqSlotTag].reqType == REQ_TYPE_NAND)
            if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck == REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK)
            {
                // TODO why no need to put to queue
                rowAddrDepTableUpdateReport = UpdateRowAddrDepTableForBufBlockedReq(reqSlotTag);

                if (rowAddrDepTableUpdateReport == ROW_ADDR_DEPENDENCY_TABLE_UPDATE_REPORT_DONE)
                {
                    // pass, go to PutToBlockedByBufDepReqQ
                }
                else if (rowAddrDepTableUpdateReport == ROW_ADDR_DEPENDENCY_TABLE_UPDATE_REPORT_SYNC)
                    return;
                else
                    assert(!"[WARNING] Not supported report [WARNING]");
            }

        PutToBlockedByBufDepReqQ(reqSlotTag);
    }
    else
        assert(!"[WARNING] Not supported report [WARNING]");
}

/**
 * @brief // TODO
 *
 * In current implementation, this function only called by `SelectiveGetFromNvmeDmaReqQ()`
 * and `GetFromNandReqQ()` for removing the request from blocking request queue after the
 * request being added into the free request queue.
 *
 * Therefore, this function can be split into wo parts:
 *
 * 1. Remove the request from the blocking request queue
 * 2.
 *
 * @note the target request may fail to issue and be blocked again if it does't pass the
 * row address dependency check.
 *
 * @param reqSlotTag the request pool entry index of the given request
 */
void ReleaseBlockedByBufDepReq(unsigned int reqSlotTag)
{
    unsigned int targetReqSlotTag, dieNo, chNo, wayNo, rowAddrDepCheckReport;

    // split the blocking request queue into 2 parts at `reqSlotTag`
    targetReqSlotTag = REQ_SLOT_TAG_NONE;
    if (reqPoolPtr->reqPool[reqSlotTag].nextBlockingReq != REQ_SLOT_TAG_NONE)
    {
        targetReqSlotTag                                      = reqPoolPtr->reqPool[reqSlotTag].nextBlockingReq;
        reqPoolPtr->reqPool[targetReqSlotTag].prevBlockingReq = REQ_SLOT_TAG_NONE;
        reqPoolPtr->reqPool[reqSlotTag].nextBlockingReq       = REQ_SLOT_TAG_NONE;
    }

    // if this request is also the tail of blocking request, reset blocking request tail
    if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat == REQ_OPT_DATA_BUF_ENTRY)
    {
        if (dataBufMapPtr->dataBuf[reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry].blockingReqTail ==
            reqSlotTag)
            dataBufMapPtr->dataBuf[reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry].blockingReqTail =
                REQ_SLOT_TAG_NONE;
    }
    else if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat == REQ_OPT_DATA_BUF_TEMP_ENTRY)
    {
        if (tempDataBufMapPtr->tempDataBuf[reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry].blockingReqTail ==
            reqSlotTag)
            tempDataBufMapPtr->tempDataBuf[reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry].blockingReqTail =
                REQ_SLOT_TAG_NONE;
    }

    /**
     * if the next blocking request of `reqSlotTag` is blocked due to buffer dependency,
     * remove it from the blocked request queue and try to schedule the request.
     */
    if ((targetReqSlotTag != REQ_SLOT_TAG_NONE) &&
        (reqPoolPtr->reqPool[targetReqSlotTag].reqQueueType == REQ_QUEUE_TYPE_BLOCKED_BY_BUF_DEP))
    {
        SelectiveGetFromBlockedByBufDepReqQ(targetReqSlotTag);

        if (reqPoolPtr->reqPool[targetReqSlotTag].reqType == REQ_TYPE_NVME_DMA)
        {
            IssueNvmeDmaReq(targetReqSlotTag);
            PutToNvmeDmaReqQ(targetReqSlotTag);
        }
        else if (reqPoolPtr->reqPool[targetReqSlotTag].reqType == REQ_TYPE_NAND)
        {
            // TODO location vector translation ?
            if (reqPoolPtr->reqPool[targetReqSlotTag].reqOpt.nandAddr == REQ_OPT_NAND_ADDR_VSA)
            {
                dieNo = Vsa2VdieTranslation(reqPoolPtr->reqPool[targetReqSlotTag].nandInfo.virtualSliceAddr);
                chNo  = Vdie2PchTranslation(dieNo);
                wayNo = Vdie2PwayTranslation(dieNo);
            }
            else
                assert(!"[WARNING] Not supported reqOpt-nandAddress [WARNING]");

            // check the row address dependency if the flag was set
            if (reqPoolPtr->reqPool[targetReqSlotTag].reqOpt.rowAddrDependencyCheck ==
                REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK)
            {
                rowAddrDepCheckReport = CheckRowAddrDep(targetReqSlotTag, ROW_ADDR_DEPENDENCY_CHECK_OPT_RELEASE);

                if (rowAddrDepCheckReport == ROW_ADDR_DEPENDENCY_REPORT_PASS)
                    PutToNandReqQ(targetReqSlotTag, chNo, wayNo);
                else if (rowAddrDepCheckReport == ROW_ADDR_DEPENDENCY_REPORT_BLOCKED)
                    PutToBlockedByRowAddrDepReqQ(targetReqSlotTag, chNo, wayNo);
                else
                    assert(!"[WARNING] Not supported report [WARNING]");
            }
            else if (reqPoolPtr->reqPool[targetReqSlotTag].reqOpt.rowAddrDependencyCheck ==
                     REQ_OPT_ROW_ADDR_DEPENDENCY_NONE)
                PutToNandReqQ(targetReqSlotTag, chNo, wayNo);
            else
                assert(!"[WARNING] Not supported reqOpt [WARNING]");
        }
    }
}

/**
 * @brief Try to release some request in the `blockedByRowAddrDepReqQ` of specified die.
 *
 * Traverse the `blockedByRowAddrDepReqQ` of the specified die, and then release all the
 * requests that can pass the row address dependency check.
 *
 * @note the word `RELEASE` means the requests will be moved to the NAND request queue,
 * instead of executing it right away.
 *
 * @note if a request is no need to be checked, it should not in this queue!
 *
 * @param chNo the channel number of the specified queue.
 * @param wayNo the way number of the specified queue.
 */
void ReleaseBlockedByRowAddrDepReq(unsigned int chNo, unsigned int wayNo)
{
    unsigned int reqSlotTag, nextReq, rowAddrDepCheckReport;

    reqSlotTag = blockedByRowAddrDepReqQ[chNo][wayNo].headReq;

    while (reqSlotTag != REQ_SLOT_TAG_NONE)
    {
        nextReq = reqPoolPtr->reqPool[reqSlotTag].nextReq;

        if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck == REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK)
        {
            rowAddrDepCheckReport = CheckRowAddrDep(reqSlotTag, ROW_ADDR_DEPENDENCY_CHECK_OPT_RELEASE);

            if (rowAddrDepCheckReport == ROW_ADDR_DEPENDENCY_REPORT_PASS)
            {
                SelectiveGetFromBlockedByRowAddrDepReqQ(reqSlotTag, chNo, wayNo);
                PutToNandReqQ(reqSlotTag, chNo, wayNo);
            }
            else if (rowAddrDepCheckReport == ROW_ADDR_DEPENDENCY_REPORT_BLOCKED)
            {
                // pass, go to while loop
            }
            else
                assert(!"[WARNING] Not supported report [WARNING]");
        }
        else
            assert(!"[WARNING] Not supported reqOpt [WARNING]");

        reqSlotTag = nextReq;
    }
}

/**
 * @brief Allocate the DMA buffer for the given request.
 *
 * @param reqSlotTag the request pool index of the given request.
 */
void IssueNvmeDmaReq(unsigned int reqSlotTag)
{
    unsigned int devAddr, dmaIndex, numOfNvmeBlock;

    dmaIndex       = reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.startIndex;
    devAddr        = GenerateDataBufAddr(reqSlotTag);
    numOfNvmeBlock = 0;

    if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_RxDMA)
    {
        while (numOfNvmeBlock < reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock)
        {
            set_auto_rx_dma(reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag, dmaIndex, devAddr,
                            NVME_COMMAND_AUTO_COMPLETION_ON);

            numOfNvmeBlock++;
            dmaIndex++;
            devAddr += BYTES_PER_NVME_BLOCK;
        }
        reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.reqTail     = g_hostDmaStatus.fifoTail.autoDmaRx;
        reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.overFlowCnt = g_hostDmaAssistStatus.autoDmaRxOverFlowCnt;
    }
    else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_TxDMA)
    {
        while (numOfNvmeBlock < reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock)
        {
            set_auto_tx_dma(reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag, dmaIndex, devAddr,
                            NVME_COMMAND_AUTO_COMPLETION_ON);

            numOfNvmeBlock++;
            dmaIndex++;
            devAddr += BYTES_PER_NVME_BLOCK;
        }
        reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.reqTail     = g_hostDmaStatus.fifoTail.autoDmaTx;
        reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.overFlowCnt = g_hostDmaAssistStatus.autoDmaTxOverFlowCnt;
    }
    else
        assert(!"[WARNING] Not supported reqCode [WARNING]");
}

void CheckDoneNvmeDmaReq()
{
    unsigned int reqSlotTag, prevReq;
    unsigned int rxDone, txDone;

    reqSlotTag = nvmeDmaReqQ.tailReq;
    rxDone     = 0;
    txDone     = 0;

    while (reqSlotTag != REQ_SLOT_TAG_NONE)
    {
        prevReq = reqPoolPtr->reqPool[reqSlotTag].prevReq;

        if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_RxDMA)
        {
            if (!rxDone)
                rxDone = check_auto_rx_dma_partial_done(reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.reqTail,
                                                        reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.overFlowCnt);

            if (rxDone)
                SelectiveGetFromNvmeDmaReqQ(reqSlotTag);
        }
        else
        {
            if (!txDone)
                txDone = check_auto_tx_dma_partial_done(reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.reqTail,
                                                        reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.overFlowCnt);

            if (txDone)
                SelectiveGetFromNvmeDmaReqQ(reqSlotTag);
        }

        reqSlotTag = prevReq;
    }
}
