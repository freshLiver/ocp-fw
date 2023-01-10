//////////////////////////////////////////////////////////////////////////////////
// ftl_config.c for Cosmos+ OpenSSD
// Copyright (c) 2017 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//				  Sangjin Lee <sjlee@enc.hanyang.ac.kr>
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
// Module Name: Flash Translation Layer Configuration Manager
// File Name: ftl_config.c
//
// Version: v1.0.0
//
// Description:
//   - initialize flash translation layer
//	 - check configuration options
//	 - initialize NAND device
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

unsigned int storageCapacity_L;
V2FMCRegisters *chCtlReg[USER_CHANNELS];

/**
 * @brief The entry function for FTL initialization.
 *
 * In the FTL initialization stage, we have several things to do:
 *
 * - check the memory map and the geometry of flash memory
 * - //TODO
 *
 */
void InitFTL()
{
    CheckConfigRestriction();

    InitChCtlReg();        // assigned the predefined addresses of channel controllers
    InitReqPool();         //
    InitDependencyTable(); //
    InitReqScheduler();    //
    InitNandArray();       // "[ NAND device reset complete. ]"
    InitAddressMap();      // "Press 'X' to re-make the bad block table."
    InitDataBuf();         //
    InitGcVictimMap();     //

    storageCapacity_L =
        (MB_PER_SSD - (MB_PER_MIN_FREE_BLOCK_SPACE + mbPerbadBlockSpace + MB_PER_OVER_PROVISION_BLOCK_SPACE)) *
        ((1024 * 1024) / BYTES_PER_NVME_BLOCK);

    xil_printf("[ storage capacity %d MB ]\r\n", storageCapacity_L / ((1024 * 1024) / BYTES_PER_NVME_BLOCK));
    xil_printf("[ ftl configuration complete. ]\r\n");
}

/**
 * @brief Initialize the base addresses of all channel controllers.
 */
void InitChCtlReg()
{
    if (USER_CHANNELS < 1)
        assert(!"[WARNING] Configuration Error: Channel [WARNING]");

    chCtlReg[0] = (V2FMCRegisters *)NSC_0_BASEADDR; // mmap reg addr

    if (USER_CHANNELS > 1)
        chCtlReg[1] = (V2FMCRegisters *)NSC_1_BASEADDR;

    if (USER_CHANNELS > 2)
        chCtlReg[2] = (V2FMCRegisters *)NSC_2_BASEADDR;

    if (USER_CHANNELS > 3)
        chCtlReg[3] = (V2FMCRegisters *)NSC_3_BASEADDR;

    if (USER_CHANNELS > 4)
        chCtlReg[4] = (V2FMCRegisters *)NSC_4_BASEADDR;

    if (USER_CHANNELS > 5)
        chCtlReg[5] = (V2FMCRegisters *)NSC_5_BASEADDR;

    if (USER_CHANNELS > 6)
        chCtlReg[6] = (V2FMCRegisters *)NSC_6_BASEADDR;

    if (USER_CHANNELS > 7)
        chCtlReg[7] = (V2FMCRegisters *)NSC_7_BASEADDR;
}

/**
 * @brief Send RESET and SET_FEATURE to all the flash dies.
 *
 * This function send two requests to each die:
 *
 * 1. `REQ_CODE_RESET`:
 *
 *      //TODO
 *
 * 2. `REQ_CODE_SET_FEATURE`:
 *
 *      This request is used to make the flash enter the toggle mode, check the function
 *      `IssueNandReq()` for details.
 *
 * The two types of request have some characteristics:
 *
 * - no need to do address translation -> use physical address directly:
 *
 *      set `nandAddr` to `REQ_OPT_NAND_ADDR_PHY_ORG`
 *      set `physicalCh` to the channel number of target die
 *      set `physicalWay` to the way number of target die
 *      set `physicalBlock` to any value (no effect)
 *      set `physicalPage` to any value (no effect)
 *
 * - no dependency problem:
 *
 *      set `rowAddrDependencyCheck` to `REQ_OPT_ROW_ADDR_DEPENDENCY_NONE`
 *      set `prevBlockingReq` to `REQ_SLOT_TAG_NONE`
 *
 * - no need data buffer needed:
 *
 *      set `dataBufFormat` to `REQ_OPT_DATA_BUF_NONE`
 *
 * - apply to all the blocks (main space + extended space)
 *
 *      set `blockSpace` to `REQ_OPT_BLOCK_SPACE_TOTAL`
 */
void InitNandArray()
{
    unsigned int chNo, wayNo, reqSlotTag;

    for (chNo = 0; chNo < USER_CHANNELS; ++chNo)
        for (wayNo = 0; wayNo < USER_WAYS; ++wayNo)
        {
            reqSlotTag                                                    = GetFromFreeReqQ();
            reqPoolPtr->reqPool[reqSlotTag].reqType                       = REQ_TYPE_NAND;
            reqPoolPtr->reqPool[reqSlotTag].reqCode                       = REQ_CODE_RESET;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr               = REQ_OPT_NAND_ADDR_PHY_ORG;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat          = REQ_OPT_DATA_BUF_NONE;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_NONE;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace             = REQ_OPT_BLOCK_SPACE_TOTAL;
            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalCh           = chNo;
            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalWay          = wayNo;
            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalBlock        = 0; // dummy
            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalPage         = 0; // dummy
            reqPoolPtr->reqPool[reqSlotTag].prevBlockingReq               = REQ_SLOT_TAG_NONE;
            SelectLowLevelReqQ(reqSlotTag);

            reqSlotTag                                                    = GetFromFreeReqQ();
            reqPoolPtr->reqPool[reqSlotTag].reqType                       = REQ_TYPE_NAND;
            reqPoolPtr->reqPool[reqSlotTag].reqCode                       = REQ_CODE_SET_FEATURE;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr               = REQ_OPT_NAND_ADDR_PHY_ORG;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat          = REQ_OPT_DATA_BUF_NONE;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_NONE;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace             = REQ_OPT_BLOCK_SPACE_TOTAL;
            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalCh           = chNo;
            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalWay          = wayNo;
            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalBlock        = 0; // dummy
            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalPage         = 0; // dummy
            reqPoolPtr->reqPool[reqSlotTag].prevBlockingReq               = REQ_SLOT_TAG_NONE;
            SelectLowLevelReqQ(reqSlotTag);
        }

    /**
     * These requests must be done in the initialization stage, make sure they are all
     * done before going to next initialization step.
     */
    SyncAllLowLevelReqDone();

    xil_printf("[ NAND device reset complete. ]\r\n");
}

/**
 * @brief Check the configurations are legal before the initializations start.
 *
 * This function will check two things:
 *
 * 1. the geometry of flash memory doesn't exceed the limit of flash controller
 * 2. there is no overlap in the memory map
 */
void CheckConfigRestriction()
{
    // geometry configutations of flash memory
    if (USER_CHANNELS > NSC_MAX_CHANNELS)
        assert(!"[WARNING] Configuration Error: Channel [WARNING]");
    if (USER_WAYS > NSC_MAX_WAYS)
        assert(!"[WARNING] Configuration Error: WAY [WARNING]");
    if (USER_BLOCKS_PER_LUN > MAIN_BLOCKS_PER_LUN)
        assert(!"[WARNING] Configuration Error: BLOCK [WARNING]");
    if ((BITS_PER_FLASH_CELL != SLC_MODE) && (BITS_PER_FLASH_CELL != MLC_MODE))
        assert(!"[WARNING] Configuration Error: BIT_PER_FLASH_CELL [WARNING]");

    // the size of some area are variable, make sure there is no overlap
    if (RESERVED_DATA_BUFFER_BASE_ADDR + 0x00200000 > COMPLETE_FLAG_TABLE_ADDR)
        assert(!"[WARNING] Configuration Error: Data buffer size is too large to be allocated to predefined range "
                "[WARNING]");
    if (TEMPORARY_PAY_LOAD_ADDR + 0x00001000 > DATA_BUFFER_MAP_ADDR)
        assert(!"[WARNING] Configuration Error: Metadata for NAND request completion process is too large to be "
                "allocated to predefined range [WARNING]");
    if (FTL_MANAGEMENT_END_ADDR > DRAM_END_ADDR)
        assert(!"[WARNING] Configuration Error: Metadata of FTL is too large to be allocated to DRAM [WARNING]");
}
