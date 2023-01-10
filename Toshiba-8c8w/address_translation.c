//////////////////////////////////////////////////////////////////////////////////
// address_translation.c for Cosmos+ OpenSSD
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
// Module Name: Address Translator
// File Name: address translation.c
//
// Version: v1.0.0
//
// Description:
//   - translate address between address space of host system and address space of NAND device
//   - manage bad blocks in NAND device
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#include <assert.h>
#include "memory_map.h"
#include "xil_printf.h"

P_LOGICAL_SLICE_MAP logicalSliceMapPtr;
P_VIRTUAL_SLICE_MAP virtualSliceMapPtr;
P_VIRTUAL_BLOCK_MAP virtualBlockMapPtr;
P_VIRTUAL_DIE_MAP virtualDieMapPtr;
P_PHY_BLOCK_MAP phyBlockMapPtr;
P_BAD_BLOCK_TABLE_INFO_MAP bbtInfoMapPtr;

unsigned char sliceAllocationTargetDie; // the destination die of next slice command
unsigned int mbPerbadBlockSpace;

/**
 * @brief Initialize the translation related maps.
 *
 * There are 5 translation related maps:
 *
 * @todo
 * - Logical Slice Map:
 * - Virtual Slice Map:
 * - Virtual Block Map:
 * - Physical Block Map:
 * - Bad Block Table (bbt) Info Map:
 *
 *
 * This function, only initialize the base addresses of these maps, the physical block map
 * and some bad blocks info. The other maps will be initialized in `InitBlockDieMap()` and
 * `InitSliceMap()`, check the two functions for further initialization.
 *
 * @todo To initialize the physical block map,
 *
 */
void InitAddressMap()
{
    unsigned int blockNo, dieNo;

    logicalSliceMapPtr = (P_LOGICAL_SLICE_MAP)LOGICAL_SLICE_MAP_ADDR;
    virtualSliceMapPtr = (P_VIRTUAL_SLICE_MAP)VIRTUAL_SLICE_MAP_ADDR;
    virtualBlockMapPtr = (P_VIRTUAL_BLOCK_MAP)VIRTUAL_BLOCK_MAP_ADDR;
    virtualDieMapPtr   = (P_VIRTUAL_DIE_MAP)VIRTUAL_DIE_MAP_ADDR;
    phyBlockMapPtr     = (P_PHY_BLOCK_MAP)PHY_BLOCK_MAP_ADDR;
    bbtInfoMapPtr      = (P_BAD_BLOCK_TABLE_INFO_MAP)BAD_BLOCK_TABLE_INFO_MAP_ADDR;

    // init phyblockMap
    for (dieNo = 0; dieNo < USER_DIES; dieNo++)
    {
        // the blocks should not be remapped to any other blocks before remmaping
        for (blockNo = 0; blockNo < TOTAL_BLOCKS_PER_DIE; blockNo++)
            phyBlockMapPtr->phyBlock[dieNo][blockNo].remappedPhyBlock = blockNo;

        bbtInfoMapPtr->bbtInfo[dieNo].phyBlock       = 0;
        bbtInfoMapPtr->bbtInfo[dieNo].grownBadUpdate = BBT_INFO_GROWN_BAD_UPDATE_NONE;
    }

    // by default, the request start from the first die
    sliceAllocationTargetDie = FindDieForFreeSliceAllocation();

    InitSliceMap();
    InitBlockDieMap();
}

/**
 * @brief Initialize Logical and Virtual Slick Map.
 *
 * This function simply initialize all the slice addresses in the both map to NONE.
 */
void InitSliceMap()
{
    int sliceAddr;
    for (sliceAddr = 0; sliceAddr < SLICES_PER_SSD; sliceAddr++)
    {
        logicalSliceMapPtr->logicalSlice[sliceAddr].virtualSliceAddr = VSA_NONE;
        virtualSliceMapPtr->virtualSlice[sliceAddr].logicalSliceAddr = LSA_NONE;
    }
}

/**
 * @brief Try to remap the bad blocks in the main block space.
 *
 * If the number of user blocks is configured as less than the number of total blocks on
 * each die, there may be some redundant blocks can be used for replacing the bad blocks
 * in the user blocks.
 *
 * Therefore, this function will sequentially search the redundant (reserved) blocks on
 * each die and try to replace the bad block in the user blocks on that die with the
 * reserved blocks.
 *
 * @bug use array to simplify lun0 and lun1
 */
void RemapBadBlock()
{
    unsigned int blockNo, dieNo, remapFlag, maxBadBlockCount;
    unsigned int reservedBlockOfLun0[USER_DIES]; // PBA of first reserved block on lun 0
    unsigned int reservedBlockOfLun1[USER_DIES]; // PBA of first reserved block on lun 1
    unsigned int badBlockCount[USER_DIES];       // non-remmappable blocks on this die

    xil_printf("Bad block remapping start...\r\n");

    // view the blocks after user blocks as reserved blocks for remapping
    for (dieNo = 0; dieNo < USER_DIES; dieNo++)
    {
        reservedBlockOfLun0[dieNo] = USER_BLOCKS_PER_LUN;
        reservedBlockOfLun1[dieNo] = TOTAL_BLOCKS_PER_LUN + USER_BLOCKS_PER_LUN;
        badBlockCount[dieNo]       = 0;
    }

    for (blockNo = 0; blockNo < USER_BLOCKS_PER_LUN; blockNo++)
    {
        for (dieNo = 0; dieNo < USER_DIES; dieNo++)
        {
            // lun0
            if (phyBlockMapPtr->phyBlock[dieNo][blockNo].bad)
            {
                if (reservedBlockOfLun0[dieNo] < TOTAL_BLOCKS_PER_LUN)
                {
                    // sequentially find a reserved non-bad block to replace the bad block
                    remapFlag = 1;
                    while (phyBlockMapPtr->phyBlock[dieNo][reservedBlockOfLun0[dieNo]].bad)
                    {
                        reservedBlockOfLun0[dieNo]++;
                        if (reservedBlockOfLun0[dieNo] >= TOTAL_BLOCKS_PER_LUN)
                        {
                            remapFlag = 0;
                            break;
                        }
                    }

                    // we found a block for replacing the bad block
                    if (remapFlag)
                    {
                        phyBlockMapPtr->phyBlock[dieNo][blockNo].remappedPhyBlock = reservedBlockOfLun0[dieNo];
                        reservedBlockOfLun0[dieNo]++;
                    }
                    else
                    {
                        xil_printf("No reserved block - Ch %d Way %d virtualBlock %d is bad block \r\n",
                                   Vdie2PchTranslation(dieNo), Vdie2PwayTranslation(dieNo), blockNo);
                        badBlockCount[dieNo]++;
                    }
                }
                else
                {
                    xil_printf("No reserved block - Ch %d Way %d virtualBlock %d is bad block \r\n",
                               Vdie2PchTranslation(dieNo), Vdie2PwayTranslation(dieNo), blockNo);
                    badBlockCount[dieNo]++;
                }
            }

            if (LUNS_PER_DIE > 1)
            {
                // lun1
                if (phyBlockMapPtr->phyBlock[dieNo][blockNo + TOTAL_BLOCKS_PER_LUN].bad)
                {
                    if (reservedBlockOfLun1[dieNo] < TOTAL_BLOCKS_PER_DIE)
                    {
                        remapFlag = 1;
                        while (phyBlockMapPtr->phyBlock[dieNo][reservedBlockOfLun1[dieNo]].bad)
                        {
                            reservedBlockOfLun1[dieNo]++;
                            if (reservedBlockOfLun1[dieNo] >= TOTAL_BLOCKS_PER_DIE)
                            {
                                remapFlag = 0;
                                break;
                            }
                        }

                        if (remapFlag)
                        {
                            phyBlockMapPtr->phyBlock[dieNo][blockNo + TOTAL_BLOCKS_PER_LUN].remappedPhyBlock =
                                reservedBlockOfLun1[dieNo];
                            reservedBlockOfLun1[dieNo]++;
                        }
                        else
                        {
                            xil_printf("No reserved block - Ch %x Way %x virtualBlock %d is bad block \r\n",
                                       Vdie2PchTranslation(dieNo), Vdie2PwayTranslation(dieNo),
                                       blockNo + USER_BLOCKS_PER_LUN);
                            badBlockCount[dieNo]++;
                        }
                    }
                    else
                    {
                        xil_printf("No reserved block - Ch %x Way %x virtualBlock %d is bad block \r\n",
                                   Vdie2PchTranslation(dieNo), Vdie2PwayTranslation(dieNo),
                                   blockNo + USER_BLOCKS_PER_LUN);
                        badBlockCount[dieNo]++;
                    }
                }
            }
        }
    }

    xil_printf("Bad block remapping end\r\n");

    maxBadBlockCount = 0;
    for (dieNo = 0; dieNo < USER_DIES; dieNo++)
    {
        xil_printf("[WARNING!!!] There are %d bad blocks on Ch %d Way %d.\r\n", badBlockCount[dieNo],
                   Vdie2PchTranslation(dieNo), Vdie2PwayTranslation(dieNo));
        if (maxBadBlockCount < badBlockCount[dieNo])
            maxBadBlockCount = badBlockCount[dieNo];
    }

    mbPerbadBlockSpace = maxBadBlockCount * USER_DIES * MB_PER_BLOCK;
}

/**
 * @brief Empty the free block list and counter of each die.
 */
void InitDieMap()
{
    unsigned int dieNo;

    for (dieNo = 0; dieNo < USER_DIES; dieNo++)
    {
        virtualDieMapPtr->die[dieNo].headFreeBlock = BLOCK_NONE;
        virtualDieMapPtr->die[dieNo].tailFreeBlock = BLOCK_NONE;
        virtualDieMapPtr->die[dieNo].freeBlockCnt  = 0;
    }
}

/**
 * @brief Map the virtual blocks to physical blocks and add non-bad blocks to free list.
 *
 * Instead of creating a V2P table and doing translation at runtime, we directly map the
 * virtual blocks to a physical blocks and add non-bad virtual blocks to the free block
 * list.
 *
 * @note The V2P mapping rule is defined by the two macros `Vblock2PblockOfTbsTranslation`
 * and `Vblock2PblockOfMbsTranslation`, check `address_translation.h` for details.
 */
void InitBlockMap()
{
    unsigned int dieNo, phyBlockNo, virtualBlockNo, remappedPhyBlock;

    for (dieNo = 0; dieNo < USER_DIES; dieNo++)
    {
        for (virtualBlockNo = 0; virtualBlockNo < USER_BLOCKS_PER_DIE; virtualBlockNo++)
        {
            phyBlockNo       = Vblock2PblockOfTbsTranslation(virtualBlockNo);
            remappedPhyBlock = phyBlockMapPtr->phyBlock[dieNo][phyBlockNo].remappedPhyBlock;
            virtualBlockMapPtr->block[dieNo][virtualBlockNo].bad =
                phyBlockMapPtr->phyBlock[dieNo][remappedPhyBlock].bad;

            virtualBlockMapPtr->block[dieNo][virtualBlockNo].free            = 1;
            virtualBlockMapPtr->block[dieNo][virtualBlockNo].invalidSliceCnt = 0;
            virtualBlockMapPtr->block[dieNo][virtualBlockNo].currentPage     = 0;
            virtualBlockMapPtr->block[dieNo][virtualBlockNo].eraseCnt        = 0;

            // bad block should not be added to free block list
            if (virtualBlockMapPtr->block[dieNo][virtualBlockNo].bad)
            {
                virtualBlockMapPtr->block[dieNo][virtualBlockNo].prevBlock = BLOCK_NONE;
                virtualBlockMapPtr->block[dieNo][virtualBlockNo].nextBlock = BLOCK_NONE;
            }
            else
                PutToFbList(dieNo, virtualBlockNo);
        }
    }
}

/**
 * @brief Get a default free block for each die.
 */
void InitCurrentBlockOfDieMap()
{
    unsigned int dieNo, chNo, wayNo;

    for (dieNo = 0; dieNo < USER_DIES; dieNo++)
    {
        virtualDieMapPtr->die[dieNo].currentBlock = GetFromFbList(dieNo, GET_FREE_BLOCK_NORMAL);
        if (virtualDieMapPtr->die[dieNo].currentBlock == BLOCK_FAIL)
        {
            // assert(!"[WARNING] There is no free block [WARNING]");
            chNo  = Vdie2PchTranslation(dieNo);
            wayNo = Vdie2PwayTranslation(dieNo);
            xil_printf("[WARNING] There is no free block on Ch %d Way %d (Die %d)!\r\n", chNo, wayNo, dieNo);
        }
    }
}

/**
 * @brief Read the pages that should contain the bad block table.
 *
 * The bbt of that die is stored in the LSB page (or pages) of the block specified by the
 * `BAD_BLOCK_TABLE_INFO_ENTRY::phyBlock`, which is default to 0 in `InitAddressMap()`.
 *
 * Each block on this die (including the extended blocks), use 1 Byte to store the bad
 * block info, so we have to read `TOTAL_BLOCKS_PER_DIE / PAGE_SIZE_IN_BYTES` pages from
 * the flash.
 *
 * @todo why ECC and row addr dependency can be turned off ??
 *
 * @param tempBbtBufAddr the addresses of the bbt of each die.
 * @param tempBbtBufEntrySize the size of the data and metadata region of a page.
 */
void ReadBadBlockTable(unsigned int tempBbtBufAddr[], unsigned int tempBbtBufEntrySize)
{
    unsigned int tempPage, reqSlotTag, dieNo;
    int loop, dataSize;

    loop     = 0;
    dataSize = DATA_SIZE_OF_BAD_BLOCK_TABLE_PER_DIE;
    tempPage = PlsbPage2VpageTranslation(START_PAGE_NO_OF_BAD_BLOCK_TABLE_BLOCK);

    while (dataSize > 0)
    {
        for (dieNo = 0; dieNo < USER_DIES; dieNo++)
        {
            reqSlotTag = GetFromFreeReqQ();

            reqPoolPtr->reqPool[reqSlotTag].reqType                       = REQ_TYPE_NAND;
            reqPoolPtr->reqPool[reqSlotTag].reqCode                       = REQ_CODE_READ;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat          = REQ_OPT_DATA_BUF_ADDR;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr               = REQ_OPT_NAND_ADDR_PHY_ORG;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc                = REQ_OPT_NAND_ECC_ON;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning         = REQ_OPT_NAND_ECC_WARNING_OFF;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_NONE;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace             = REQ_OPT_BLOCK_SPACE_TOTAL;

            reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.addr = tempBbtBufAddr[dieNo] + loop * tempBbtBufEntrySize;

            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalCh    = Vdie2PchTranslation(dieNo);
            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalWay   = Vdie2PwayTranslation(dieNo);
            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalBlock = bbtInfoMapPtr->bbtInfo[dieNo].phyBlock;
            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalPage  = Vpage2PlsbPageTranslation(tempPage);

            SelectLowLevelReqQ(reqSlotTag);
        }

        tempPage++;
        loop++;
        dataSize -= BYTES_PER_DATA_REGION_OF_PAGE;
    }

    xil_printf("[INFO] %s: bbt size: %d pages per die.\r\n", __FUNCTION__, loop);
    SyncAllLowLevelReqDone();
}

/**
 * @brief Build the bbt for those dies whose bbt doesn't exist.
 *
 * To determine whether a block is bad block, we should check 4 bytes in that block:
 *
 * - The first byte of the data region of the first page in this block
 * - The first byte of the data region of the last page in this block
 * - The first byte of the metadata region of the first page in this block
 * - The first byte of the metadata region of the last page in this block
 *
 * If only of the 4 bytes are not 0xFF, we can view this block as bad block.
 *
 * @todo why check these 4 bytes, why ECC and row addr dep off?
 * @todo the update in the second for is redundant, it can be merged to next part
 *
 * @warning `tempBbtBufEntrySize` and `tempReadBufEntrySize` not used
 *
 * @bug `blockChecker` not initialized
 *
 * @param dieState the flags that indicate whether the bbt of that die should be rebuilt.
 * @param tempBbtBufAddr the addresses of the bbt of each die.
 * @param tempBbtBufEntrySize the size of the data and metadata region of a page.
 * @param tempReadBufAddr the buffer addresses for storing the pages to be read.
 * @param tempReadBufEntrySize the size of a whole page (data + metadata + ECC).
 */
void FindBadBlock(unsigned char dieState[], unsigned int tempBbtBufAddr[], unsigned int tempBbtBufEntrySize,
                  unsigned int tempReadBufAddr[], unsigned int tempReadBufEntrySize)
{
    unsigned int phyBlockNo, dieNo, reqSlotTag;
    unsigned char blockChecker[USER_DIES];
    unsigned char *markPointer0;
    unsigned char *markPointer1;
    unsigned char *bbtUpdater;

    /**
     * Check all the blocks on the SSD
     *
     * @note Check multiple dies at the same time to speed up and reduce the buffer size.
     */
    for (phyBlockNo = 0; phyBlockNo < TOTAL_BLOCKS_PER_DIE; phyBlockNo++)
    {
        // Read the first page of the specified block of each die
        for (dieNo = 0; dieNo < USER_DIES; dieNo++)
            if (!dieState[dieNo])
            {
                blockChecker[dieNo] = BLOCK_STATE_NORMAL;

                reqSlotTag = GetFromFreeReqQ();

                reqPoolPtr->reqPool[reqSlotTag].reqType                       = REQ_TYPE_NAND;
                reqPoolPtr->reqPool[reqSlotTag].reqCode                       = REQ_CODE_READ;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat          = REQ_OPT_DATA_BUF_ADDR;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr               = REQ_OPT_NAND_ADDR_PHY_ORG;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc                = REQ_OPT_NAND_ECC_OFF;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning         = REQ_OPT_NAND_ECC_WARNING_OFF;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_NONE;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace             = REQ_OPT_BLOCK_SPACE_TOTAL;

                reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.addr = tempReadBufAddr[dieNo];

                reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalCh    = Vdie2PchTranslation(dieNo);
                reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalWay   = Vdie2PwayTranslation(dieNo);
                reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalBlock = phyBlockNo;
                reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalPage  = BAD_BLOCK_MARK_PAGE0;

                SelectLowLevelReqQ(reqSlotTag);
            }
        SyncAllLowLevelReqDone();

        // Read the last page of the specified block of each die if needed
        for (dieNo = 0; dieNo < USER_DIES; dieNo++)
            if (!dieState[dieNo])
            {
                markPointer0 = (unsigned char *)(tempReadBufAddr[dieNo] + BAD_BLOCK_MARK_BYTE0);
                markPointer1 = (unsigned char *)(tempReadBufAddr[dieNo] + BAD_BLOCK_MARK_BYTE1);

                if ((*markPointer0 == CLEAN_DATA_IN_BYTE) && (*markPointer1 == CLEAN_DATA_IN_BYTE))
                {
                    reqSlotTag = GetFromFreeReqQ();

                    reqPoolPtr->reqPool[reqSlotTag].reqType               = REQ_TYPE_NAND;
                    reqPoolPtr->reqPool[reqSlotTag].reqCode               = REQ_CODE_READ;
                    reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat  = REQ_OPT_DATA_BUF_ADDR;
                    reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr       = REQ_OPT_NAND_ADDR_PHY_ORG;
                    reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc        = REQ_OPT_NAND_ECC_OFF;
                    reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning = REQ_OPT_NAND_ECC_WARNING_OFF;
                    reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck =
                        REQ_OPT_ROW_ADDR_DEPENDENCY_NONE;
                    reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_TOTAL;

                    reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.addr = tempReadBufAddr[dieNo];

                    reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalCh    = Vdie2PchTranslation(dieNo);
                    reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalWay   = Vdie2PwayTranslation(dieNo);
                    reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalBlock = phyBlockNo;
                    reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalPage  = BAD_BLOCK_MARK_PAGE1;

                    SelectLowLevelReqQ(reqSlotTag);
                }
                else
                {
                    xil_printf("	bad block is detected: Ch %d Way %d phyBlock %d \r\n",
                               Vdie2PchTranslation(dieNo), Vdie2PwayTranslation(dieNo), phyBlockNo);

                    blockChecker[dieNo] = BLOCK_STATE_BAD;
                }
            }

        SyncAllLowLevelReqDone();

        // determine whether these blocks are bad blocks and update the bbt
        for (dieNo = 0; dieNo < USER_DIES; dieNo++)
            if (!dieState[dieNo])
            {
                markPointer0 = (unsigned char *)(tempReadBufAddr[dieNo] + BAD_BLOCK_MARK_BYTE0);
                markPointer1 = (unsigned char *)(tempReadBufAddr[dieNo] + BAD_BLOCK_MARK_BYTE1);

                if (!((*markPointer0 == CLEAN_DATA_IN_BYTE) && (*markPointer1 == CLEAN_DATA_IN_BYTE)))
                    if (blockChecker[dieNo] == BLOCK_STATE_NORMAL)
                    {
                        xil_printf("	bad block is detected: Ch %d Way %d phyBlock %d \r\n",
                                   Vdie2PchTranslation(dieNo), Vdie2PwayTranslation(dieNo), phyBlockNo);

                        blockChecker[dieNo] = BLOCK_STATE_BAD;
                    }

                // update the bbt this block
                bbtUpdater  = (unsigned char *)(tempBbtBufAddr[dieNo] + phyBlockNo);
                *bbtUpdater = blockChecker[dieNo];
                phyBlockMapPtr->phyBlock[dieNo][phyBlockNo].bad = blockChecker[dieNo];
            }
    }
}

/**
 * @brief Persist the newly created bbt for those dies whose bbt not exists.
 *
 * Similar to ReadBadBlockTable, but now we have to write the bbt to the pages.
 *
 * @note The bbt should be saved at lsb pages.
 *
 * @bug why dataSize++ ?? tempPage++ ?
 *
 * @param dieState the flags that indicate whether the bbt of that die should be rebuilt.
 * @param tempBbtBufAddr the addresses of the bbt of each die.
 * @param tempBbtBufEntrySize the size of the data and metadata region of a page.
 */
void SaveBadBlockTable(unsigned char dieState[], unsigned int tempBbtBufAddr[], unsigned int tempBbtBufEntrySize)
{
    unsigned int dieNo, reqSlotTag;
    int loop, dataSize, tempPage;

    loop     = 0;
    dataSize = DATA_SIZE_OF_BAD_BLOCK_TABLE_PER_DIE;
    tempPage =
        PlsbPage2VpageTranslation(START_PAGE_NO_OF_BAD_BLOCK_TABLE_BLOCK); // bad block table is saved at lsb pages

    while (dataSize > 0)
    {
        for (dieNo = 0; dieNo < USER_DIES; dieNo++)
            if ((dieState[dieNo] == DIE_STATE_BAD_BLOCK_TABLE_NOT_EXIST) ||
                (dieState[dieNo] == DIE_STATE_BAD_BLOCK_TABLE_UPDATE))
            {
                /* before writing the bbt to flash, we should do erase first. */
                if (loop == 0)
                {
                    reqSlotTag = GetFromFreeReqQ();

                    reqPoolPtr->reqPool[reqSlotTag].reqType              = REQ_TYPE_NAND;
                    reqPoolPtr->reqPool[reqSlotTag].reqCode              = REQ_CODE_ERASE;
                    reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr      = REQ_OPT_NAND_ADDR_PHY_ORG;
                    reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat = REQ_OPT_DATA_BUF_NONE;
                    reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck =
                        REQ_OPT_ROW_ADDR_DEPENDENCY_NONE;
                    reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace = REQ_OPT_BLOCK_SPACE_TOTAL;

                    reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalCh  = Vdie2PchTranslation(dieNo);
                    reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalWay = Vdie2PwayTranslation(dieNo);
                    reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalBlock =
                        bbtInfoMapPtr->bbtInfo[dieNo].phyBlock;
                    reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalPage = 0; // dummy

                    SelectLowLevelReqQ(reqSlotTag);
                }

                reqSlotTag = GetFromFreeReqQ();

                reqPoolPtr->reqPool[reqSlotTag].reqType                       = REQ_TYPE_NAND;
                reqPoolPtr->reqPool[reqSlotTag].reqCode                       = REQ_CODE_WRITE;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat          = REQ_OPT_DATA_BUF_ADDR;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr               = REQ_OPT_NAND_ADDR_PHY_ORG;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc                = REQ_OPT_NAND_ECC_ON;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning         = REQ_OPT_NAND_ECC_WARNING_OFF;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_NONE;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace             = REQ_OPT_BLOCK_SPACE_TOTAL;

                reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.addr =
                    tempBbtBufAddr[dieNo] + loop * tempBbtBufEntrySize;

                reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalCh    = Vdie2PchTranslation(dieNo);
                reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalWay   = Vdie2PwayTranslation(dieNo);
                reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalBlock = bbtInfoMapPtr->bbtInfo[dieNo].phyBlock;
                reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalPage  = Vpage2PlsbPageTranslation(tempPage);

                SelectLowLevelReqQ(reqSlotTag);
            }

        loop++;
        dataSize++;
        dataSize -= BYTES_PER_DATA_REGION_OF_PAGE;
    }

    SyncAllLowLevelReqDone();

    for (dieNo = 0; dieNo < USER_DIES; dieNo++)
        if (dieState[dieNo] == DIE_STATE_BAD_BLOCK_TABLE_NOT_EXIST)
            xil_printf("[ bad block table of Ch %d Way %d is saved. ]\r\n", dieNo % USER_CHANNELS,
                       dieNo / USER_CHANNELS);
}

/**
 * @brief Read the bbt from flash and re-create if not exists.
 *
 * This function is used for checking and recover the bad block table from the flash.
 *
 * To do this, we have several things to do:
 *
 * 1. Read the flash pages that is used for storing the bbt info
 *
 * 2. Check whether the specific pages contains the bbt info
 *
 *      a. if bbt info exists, check the bad blocks in this die.
 *      b. otherwise, mark the bbt this die should be rebuilt.
 *
 * 3. Rebuilt bbt if needed
 *
 *      1. read all the blocks in the target dies
 *      2. determine whether those blocks are bad blocks
 *      3. update the temp bbt
 *
 * 4. Persist the newly created bbt to the flash
 *
 * @param tempBufAddr the base address for buffering the pages that contain the bbt.
 */
void RecoverBadBlockTable(unsigned int tempBufAddr)
{
    unsigned int dieNo, phyBlockNo, bbtMaker, tempBbtBufBaseAddr, tempBbtBufEntrySize, tempReadBufBaseAddr,
        tempReadBufEntrySize;
    unsigned int tempBbtBufAddr[USER_DIES];  // buffer addresses for storing the bbt pages
    unsigned int tempReadBufAddr[USER_DIES]; // buffer addresses for finding bad blocks
    unsigned char dieState[USER_DIES];       // whether the bbt of this die should be rebuilt
    unsigned char *bbtTableChecker;

    // data buffer allocation
    tempBbtBufBaseAddr  = tempBufAddr;
    tempBbtBufEntrySize = BYTES_PER_DATA_REGION_OF_PAGE + BYTES_PER_SPARE_REGION_OF_PAGE;
    tempReadBufBaseAddr =
        tempBbtBufBaseAddr + USER_DIES * USED_PAGES_FOR_BAD_BLOCK_TABLE_PER_DIE * tempBbtBufEntrySize;
    tempReadBufEntrySize = BYTES_PER_NAND_ROW;
    for (dieNo = 0; dieNo < USER_DIES; dieNo++)
    {
        tempBbtBufAddr[dieNo] =
            tempBbtBufBaseAddr + dieNo * USED_PAGES_FOR_BAD_BLOCK_TABLE_PER_DIE * tempBbtBufEntrySize;
        tempReadBufAddr[dieNo] = tempReadBufBaseAddr + dieNo * tempReadBufEntrySize;
    }

    // read the bbt of each die into bbt buffer
    ReadBadBlockTable(tempBbtBufAddr, tempBbtBufEntrySize);

    // check bad block tables
    bbtMaker = BAD_BLOCK_TABLE_MAKER_IDLE;
    for (dieNo = 0; dieNo < USER_DIES; dieNo++)
    {
        bbtTableChecker = (unsigned char *)(tempBbtBufAddr[dieNo]);

        /**
         * Each block on this die use 1 byte to store the bad block info, but only use 1
         * bit to indicate whether that block is a bad block. So here just determine if
         * the bbt exists by checking the first byte.
         *
         * @todo use bitmap to record bad blocks
         *
         * @warning why not use magic number to make sure the data is valid ??
         */
        if ((*bbtTableChecker == BLOCK_STATE_NORMAL) || (*bbtTableChecker == BLOCK_STATE_BAD))
        {
            xil_printf("[ bad block table of ch %d way %d exists.]\r\n", Vdie2PchTranslation(dieNo),
                       Vdie2PwayTranslation(dieNo));

            dieState[dieNo] = DIE_STATE_BAD_BLOCK_TABLE_EXIST;
            for (phyBlockNo = 0; phyBlockNo < TOTAL_BLOCKS_PER_DIE; phyBlockNo++)
            {
                bbtTableChecker = (unsigned char *)(tempBbtBufAddr[dieNo] + phyBlockNo);

                phyBlockMapPtr->phyBlock[dieNo][phyBlockNo].bad = *bbtTableChecker;
                if (phyBlockMapPtr->phyBlock[dieNo][phyBlockNo].bad == BLOCK_STATE_BAD)
                    xil_printf("	bad block: ch %d way %d phyBlock %d  \r\n", Vdie2PchTranslation(dieNo),
                               Vdie2PwayTranslation(dieNo), phyBlockNo);
            }

            xil_printf("[ bad blocks of ch %d way %d are checked. ]\r\n", Vdie2PchTranslation(dieNo),
                       Vdie2PwayTranslation(dieNo));
        }
        else
        {
            xil_printf("[ bad block table of ch %d way %d does not exist.]\r\n", Vdie2PchTranslation(dieNo),
                       Vdie2PwayTranslation(dieNo));
            dieState[dieNo] = DIE_STATE_BAD_BLOCK_TABLE_NOT_EXIST;
            bbtMaker        = BAD_BLOCK_TABLE_MAKER_TRIGGER;
        }
    }

    // Create bbt for those dies whose bbt not found.
    if (bbtMaker == BAD_BLOCK_TABLE_MAKER_TRIGGER)
    {
        FindBadBlock(dieState, tempBbtBufAddr, tempBbtBufEntrySize, tempReadBufAddr, tempReadBufEntrySize);
        SaveBadBlockTable(dieState, tempBbtBufAddr, tempBbtBufEntrySize);
    }

    // bbt initialization done, no need to update the bbt until new bad block found
    for (dieNo = 0; dieNo < USER_DIES; dieNo++)
        bbtInfoMapPtr->bbtInfo[dieNo].grownBadUpdate = BBT_INFO_GROWN_BAD_UPDATE_NONE;
}

/**
 * @brief Erase all the blocks on user dies and wait until done.
 */
void EraseTotalBlockSpace()
{
    unsigned int blockNo, dieNo, reqSlotTag;

    xil_printf("Erase total block space...wait for a minute...\r\n");

    for (blockNo = 0; blockNo < TOTAL_BLOCKS_PER_DIE; blockNo++)
        for (dieNo = 0; dieNo < USER_DIES; dieNo++)
        {
            reqSlotTag = GetFromFreeReqQ();

            reqPoolPtr->reqPool[reqSlotTag].reqType                       = REQ_TYPE_NAND;
            reqPoolPtr->reqPool[reqSlotTag].reqCode                       = REQ_CODE_ERASE;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr               = REQ_OPT_NAND_ADDR_PHY_ORG;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat          = REQ_OPT_DATA_BUF_NONE;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_NONE;
            reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace             = REQ_OPT_BLOCK_SPACE_TOTAL;

            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalCh    = Vdie2PchTranslation(dieNo);
            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalWay   = Vdie2PwayTranslation(dieNo);
            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalBlock = blockNo;
            reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalPage  = 0;

            SelectLowLevelReqQ(reqSlotTag);
        }

    SyncAllLowLevelReqDone();
    xil_printf("Done.\r\n");
}

/**
 * @brief Erase all the non-bad main blocks on user dies and wait until done.
 */
void EraseUserBlockSpace()
{
    unsigned int blockNo, dieNo, reqSlotTag;

    xil_printf("Erase User block space...wait for a minute...\r\n");

    for (blockNo = 0; blockNo < USER_BLOCKS_PER_DIE; blockNo++)
        for (dieNo = 0; dieNo < USER_DIES; dieNo++)
            if (!virtualBlockMapPtr->block[dieNo][blockNo].bad)
            {
                reqSlotTag = GetFromFreeReqQ();

                reqPoolPtr->reqPool[reqSlotTag].reqType                       = REQ_TYPE_NAND;
                reqPoolPtr->reqPool[reqSlotTag].reqCode                       = REQ_CODE_ERASE;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr               = REQ_OPT_NAND_ADDR_VSA;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat          = REQ_OPT_DATA_BUF_NONE;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_NONE;
                reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace             = REQ_OPT_BLOCK_SPACE_MAIN;

                reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = Vorg2VsaTranslation(dieNo, blockNo, 0);

                SelectLowLevelReqQ(reqSlotTag);
            }

    SyncAllLowLevelReqDone();
    xil_printf("Done.\r\n");
}

/**
 * @brief Create the bad block table and V2P block mapping of each user die.
 *
 * To create the V2P mapping, we have to:
 *
 * 1. create the bad block table
 * 2. remap bad blocks
 * 3. Erase non-bad blocks in the main block space
 * 4. map virtual blocks to physical blocks
 */
void InitBlockDieMap()
{
    unsigned int dieNo;
    unsigned char eraseFlag = 1;

    xil_printf("Press 'X' to re-make the bad block table.\r\n");

    char input = inbyte();
    if (input == 'X')
    {
        xil_printf("[WARNING!!!] Start re-making bad block table\r\n");
        EraseTotalBlockSpace();
        eraseFlag = 0;
    }
    else
    {
        xil_printf("[WARNING!!!] Skip re-making bad block table\r\n");
    }

    // empty the free block list of each die
    InitDieMap();

    // read bbt from flash [, create bbt, persist new bbt to flash]
    RecoverBadBlockTable(RESERVED_DATA_BUFFER_BASE_ADDR);

    /**
     * Since the block specified by `BAD_BLOCK_TABLE_INFO_ENTRY::phyBlock` is used for
     * storing the bbt of that die, so we have to mark that block bad and let it to be
     * remapped to another block for using.
     *
     * And because we have at least one bad block on each die, the remapping process thus
     * should not be skipped.
     */
    for (dieNo = 0; dieNo < USER_DIES; dieNo++)
        phyBlockMapPtr->phyBlock[dieNo][bbtInfoMapPtr->bbtInfo[dieNo].phyBlock].bad = 1;
    RemapBadBlock();

    // create V2P block level mapping
    InitBlockMap();

    if (eraseFlag)
        EraseUserBlockSpace();

    InitCurrentBlockOfDieMap();
}

/* -------------------------------------------------------------------------- */
/*                      Utility Functions for Translation                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Get the virtual slice address of the given logical slice.
 *
 * @param logicalSliceAddr the logical address of the target slice.
 * @return unsigned int the virtual address of the target slice.
 */
unsigned int AddrTransRead(unsigned int logicalSliceAddr)
{
    unsigned int virtualSliceAddr;

    if (logicalSliceAddr < SLICES_PER_SSD)
    {
        virtualSliceAddr = logicalSliceMapPtr->logicalSlice[logicalSliceAddr].virtualSliceAddr;

        if (virtualSliceAddr != VSA_NONE)
            return virtualSliceAddr;
        else
            return VSA_FAIL;
    }
    else
        assert(!"[WARNING] Logical address is larger than maximum logical address served by SSD [WARNING]");
}

/**
 * @brief Assign a new virtual slice to the given slice and get its virtual address.
 *
 * @param logicalSliceAddr the logical address of the target slice.
 * @return unsigned int the renewed virtual address of the target slice.
 */
unsigned int AddrTransWrite(unsigned int logicalSliceAddr)
{
    unsigned int virtualSliceAddr;

    if (logicalSliceAddr < SLICES_PER_SSD)
    {
        InvalidateOldVsa(logicalSliceAddr);

        virtualSliceAddr = FindFreeVirtualSlice();

        logicalSliceMapPtr->logicalSlice[logicalSliceAddr].virtualSliceAddr = virtualSliceAddr;
        virtualSliceMapPtr->virtualSlice[virtualSliceAddr].logicalSliceAddr = logicalSliceAddr;

        return virtualSliceAddr;
    }
    else
        assert(!"[WARNING] Logical address is larger than maximum logical address served by SSD [WARNING]");
}

/**
 * @brief Assign a virtual slice to the current working page.
 *
 * If the current working block of the target die is full, try to allocate a new block.
 *
 * @todo
 *
 * @warning why assign dieNo before return? redundant?
 *
 * @return unsigned int the VSA for the request.
 */
unsigned int FindFreeVirtualSlice()
{
    unsigned int currentBlock, virtualSliceAddr, dieNo;

    dieNo        = sliceAllocationTargetDie;
    currentBlock = virtualDieMapPtr->die[dieNo].currentBlock;

    // if the currently used block is full, assign a free block as new current block
    if (virtualBlockMapPtr->block[dieNo][currentBlock].currentPage == USER_PAGES_PER_BLOCK)
    {
        currentBlock = GetFromFbList(dieNo, GET_FREE_BLOCK_NORMAL);

        if (currentBlock != BLOCK_FAIL)
            virtualDieMapPtr->die[dieNo].currentBlock = currentBlock;
        else
        {
            GarbageCollection(dieNo);
            currentBlock = virtualDieMapPtr->die[dieNo].currentBlock;

            if (virtualBlockMapPtr->block[dieNo][currentBlock].currentPage == USER_PAGES_PER_BLOCK)
            {
                currentBlock = GetFromFbList(dieNo, GET_FREE_BLOCK_NORMAL);
                if (currentBlock != BLOCK_FAIL)
                    virtualDieMapPtr->die[dieNo].currentBlock = currentBlock;
                else
                    assert(!"[WARNING] There is no available block [WARNING]");
            }
            else if (virtualBlockMapPtr->block[dieNo][currentBlock].currentPage > USER_PAGES_PER_BLOCK)
                assert(!"[WARNING] Current page management fail [WARNING]");
        }
    }
    else if (virtualBlockMapPtr->block[dieNo][currentBlock].currentPage > USER_PAGES_PER_BLOCK)
        assert(!"[WARNING] Current page management fail [WARNING]");

    virtualSliceAddr =
        Vorg2VsaTranslation(dieNo, currentBlock, virtualBlockMapPtr->block[dieNo][currentBlock].currentPage);
    virtualBlockMapPtr->block[dieNo][currentBlock].currentPage++;
    sliceAllocationTargetDie = FindDieForFreeSliceAllocation();
    dieNo                    = sliceAllocationTargetDie;
    return virtualSliceAddr;
}

unsigned int FindFreeVirtualSliceForGc(unsigned int copyTargetDieNo, unsigned int victimBlockNo)
{
    unsigned int currentBlock, virtualSliceAddr, dieNo;

    dieNo = copyTargetDieNo;
    if (victimBlockNo == virtualDieMapPtr->die[dieNo].currentBlock)
    {
        virtualDieMapPtr->die[dieNo].currentBlock = GetFromFbList(dieNo, GET_FREE_BLOCK_GC);
        if (virtualDieMapPtr->die[dieNo].currentBlock == BLOCK_FAIL)
            assert(!"[WARNING] There is no available block [WARNING]");
    }
    currentBlock = virtualDieMapPtr->die[dieNo].currentBlock;

    if (virtualBlockMapPtr->block[dieNo][currentBlock].currentPage == USER_PAGES_PER_BLOCK)
    {

        currentBlock = GetFromFbList(dieNo, GET_FREE_BLOCK_GC);

        if (currentBlock != BLOCK_FAIL)
            virtualDieMapPtr->die[dieNo].currentBlock = currentBlock;
        else
            assert(!"[WARNING] There is no available block [WARNING]");
    }
    else if (virtualBlockMapPtr->block[dieNo][currentBlock].currentPage > USER_PAGES_PER_BLOCK)
        assert(!"[WARNING] Current page management fail [WARNING]");

    virtualSliceAddr =
        Vorg2VsaTranslation(dieNo, currentBlock, virtualBlockMapPtr->block[dieNo][currentBlock].currentPage);
    virtualBlockMapPtr->block[dieNo][currentBlock].currentPage++;
    return virtualSliceAddr;
}

/**
 * @brief Choose a die for
 *
 * The rule to choose a die is:
 *
 * C0W0 -> C1W0 -> ... -> C7W0 ->
 * C0W1 -> C1W1 -> ... -> C7W1 ->
 * ...
 * C0W7 -> C1W7 -> ... -> C7W7 ->
 * C0W0 -> C1W0 -> ... -> C7W0 ->
 * ...
 *
 * So the allocation will be interleaved on channel first.
 *
 * @warning As paper the mentioned, may not perform well if latency largely varied.
 *
 * @return unsigned int the die number of the chosen die.
 */
unsigned int FindDieForFreeSliceAllocation()
{
    static unsigned char targetCh  = 0;
    static unsigned char targetWay = 0;
    unsigned int targetDie;

    targetDie = Pcw2VdieTranslation(targetCh, targetWay);

    if (targetCh != (USER_CHANNELS - 1))
        targetCh = targetCh + 1;
    else
    {
        targetCh  = 0;
        targetWay = (targetWay + 1) % USER_WAYS;
    }

    return targetDie;
}

void InvalidateOldVsa(unsigned int logicalSliceAddr)
{
    unsigned int virtualSliceAddr, dieNo, blockNo;

    virtualSliceAddr = logicalSliceMapPtr->logicalSlice[logicalSliceAddr].virtualSliceAddr;

    if (virtualSliceAddr != VSA_NONE)
    {
        if (virtualSliceMapPtr->virtualSlice[virtualSliceAddr].logicalSliceAddr != logicalSliceAddr)
            return;

        dieNo   = Vsa2VdieTranslation(virtualSliceAddr);
        blockNo = Vsa2VblockTranslation(virtualSliceAddr);

        // unlink
        SelectiveGetFromGcVictimList(dieNo, blockNo);
        virtualBlockMapPtr->block[dieNo][blockNo].invalidSliceCnt++;
        logicalSliceMapPtr->logicalSlice[logicalSliceAddr].virtualSliceAddr = VSA_NONE;

        PutToGcVictimList(dieNo, blockNo, virtualBlockMapPtr->block[dieNo][blockNo].invalidSliceCnt);
    }
}

/**
 * @brief Erase the specified block of the specified die and discard its LSAs.
 *
 * This function will:
 *
 * - Send a ERASE request to erase the specified block
 * - Move the specified block to free block list
 * - Discard all the logical slice addresses of the origin block
 *
 * @todo programmedPageCnt
 *
 * @note The specified block may not be invalidated immediately.
 *
 * @param dieNo the die number of the specified block.
 * @param blockNo the block number on the specified die.
 */
void EraseBlock(unsigned int dieNo, unsigned int blockNo)
{
    unsigned int pageNo, virtualSliceAddr, reqSlotTag;

    reqSlotTag = GetFromFreeReqQ();

    reqPoolPtr->reqPool[reqSlotTag].reqType                       = REQ_TYPE_NAND;
    reqPoolPtr->reqPool[reqSlotTag].reqCode                       = REQ_CODE_ERASE;
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr               = REQ_OPT_NAND_ADDR_VSA;
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat          = REQ_OPT_DATA_BUF_NONE;
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
    reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace             = REQ_OPT_BLOCK_SPACE_MAIN;
    reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr     = Vorg2VsaTranslation(dieNo, blockNo, 0);
    reqPoolPtr->reqPool[reqSlotTag].nandInfo.programmedPageCnt =
        virtualBlockMapPtr->block[dieNo][blockNo].currentPage;

    SelectLowLevelReqQ(reqSlotTag);

    // block map indicated blockNo initialization
    virtualBlockMapPtr->block[dieNo][blockNo].free = 1;
    virtualBlockMapPtr->block[dieNo][blockNo].eraseCnt++;
    virtualBlockMapPtr->block[dieNo][blockNo].invalidSliceCnt = 0;
    virtualBlockMapPtr->block[dieNo][blockNo].currentPage     = 0;

    PutToFbList(dieNo, blockNo);

    for (pageNo = 0; pageNo < USER_PAGES_PER_BLOCK; pageNo++)
    {
        virtualSliceAddr = Vorg2VsaTranslation(dieNo, blockNo, pageNo);
        virtualSliceMapPtr->virtualSlice[virtualSliceAddr].logicalSliceAddr = LSA_NONE;
    }
}

/**
 * @brief Append the given block to the free block list on its die.
 *
 * @param dieNo the die number of the given block.
 * @param blockNo a free block number.
 */
void PutToFbList(unsigned int dieNo, unsigned int blockNo)
{
    if (virtualDieMapPtr->die[dieNo].tailFreeBlock != BLOCK_NONE)
    {
        virtualBlockMapPtr->block[dieNo][blockNo].prevBlock = virtualDieMapPtr->die[dieNo].tailFreeBlock;
        virtualBlockMapPtr->block[dieNo][blockNo].nextBlock = BLOCK_NONE;
        virtualBlockMapPtr->block[dieNo][virtualDieMapPtr->die[dieNo].tailFreeBlock].nextBlock = blockNo;
        virtualDieMapPtr->die[dieNo].tailFreeBlock                                             = blockNo;
    }
    else
    {
        virtualBlockMapPtr->block[dieNo][blockNo].prevBlock = BLOCK_NONE;
        virtualBlockMapPtr->block[dieNo][blockNo].nextBlock = BLOCK_NONE;
        virtualDieMapPtr->die[dieNo].headFreeBlock          = blockNo;
        virtualDieMapPtr->die[dieNo].tailFreeBlock          = blockNo;
    }

    virtualDieMapPtr->die[dieNo].freeBlockCnt++;
}

/**
 * @brief Pop the first block in the free block list of the specified die.
 *
 * The chosen block may be used for normal request (`GET_FREE_BLOCK_NORMAL`) or GC request
 * (`GET_FREE_BLOCK_GC`),
 *
 * @todo @note If the
 *
 * @param dieNo the target die number.
 * @param getFreeBlockOption
 * @return unsigned int the virtual block map index of the evicted block.
 */
unsigned int GetFromFbList(unsigned int dieNo, unsigned int getFreeBlockOption)
{
    unsigned int evictedBlockNo;

    evictedBlockNo = virtualDieMapPtr->die[dieNo].headFreeBlock;

    if (getFreeBlockOption == GET_FREE_BLOCK_NORMAL)
    {
        if (virtualDieMapPtr->die[dieNo].freeBlockCnt <= RESERVED_FREE_BLOCK_COUNT)
            return BLOCK_FAIL;
    }
    else if (getFreeBlockOption == GET_FREE_BLOCK_GC)
    {
        if (evictedBlockNo == BLOCK_NONE)
            return BLOCK_FAIL;
    }
    else
        assert(!"[WARNING] Wrong getFreeBlockOption [WARNING]");

    if (virtualBlockMapPtr->block[dieNo][evictedBlockNo].nextBlock != BLOCK_NONE)
    {
        virtualDieMapPtr->die[dieNo].headFreeBlock = virtualBlockMapPtr->block[dieNo][evictedBlockNo].nextBlock;
        virtualBlockMapPtr->block[dieNo][virtualBlockMapPtr->block[dieNo][evictedBlockNo].nextBlock].prevBlock =
            BLOCK_NONE;
    }
    else
    {
        virtualDieMapPtr->die[dieNo].headFreeBlock = BLOCK_NONE;
        virtualDieMapPtr->die[dieNo].tailFreeBlock = BLOCK_NONE;
    }

    virtualBlockMapPtr->block[dieNo][evictedBlockNo].free = 0;
    virtualDieMapPtr->die[dieNo].freeBlockCnt--;

    virtualBlockMapPtr->block[dieNo][evictedBlockNo].nextBlock = BLOCK_NONE;
    virtualBlockMapPtr->block[dieNo][evictedBlockNo].prevBlock = BLOCK_NONE;

    return evictedBlockNo;
}

/**
 * @brief Mark the given physical block bad block and update the bbt later.
 *
 * @param dieNo the die number of the given block.
 * @param phyBlockNo the physical address of the block to be marked as bad block.
 */
void UpdatePhyBlockMapForGrownBadBlock(unsigned int dieNo, unsigned int phyBlockNo)
{
    phyBlockMapPtr->phyBlock[dieNo][phyBlockNo].bad = BLOCK_STATE_BAD;
    bbtInfoMapPtr->bbtInfo[dieNo].grownBadUpdate    = BBT_INFO_GROWN_BAD_UPDATE_BOOKED;
}

/**
 * @brief Update the bad block table and persist to the specified block.
 *
 * A little bit similar to `FindBadBlock()`, but this function use the bad block flag to
 * mark bad blocks, instead of reading the bad block marks.
 *
 * @param tempBufAddr the base address of the bad block tables.
 */
void UpdateBadBlockTableForGrownBadBlock(unsigned int tempBufAddr)
{
    unsigned int dieNo, phyBlockNo, tempBbtBufBaseAddr, tempBbtBufEntrySize;
    unsigned int tempBbtBufAddr[USER_DIES];
    unsigned char dieState[USER_DIES];
    unsigned char *bbtUpdater;

    // data buffer allocation
    tempBbtBufBaseAddr  = tempBufAddr;
    tempBbtBufEntrySize = BYTES_PER_DATA_REGION_OF_PAGE + BYTES_PER_SPARE_REGION_OF_PAGE;
    for (dieNo = 0; dieNo < USER_DIES; dieNo++)
        tempBbtBufAddr[dieNo] =
            tempBbtBufBaseAddr + dieNo * USED_PAGES_FOR_BAD_BLOCK_TABLE_PER_DIE * tempBbtBufEntrySize;

    // create new bad block table
    for (dieNo = 0; dieNo < USER_DIES; dieNo++)
    {
        if (bbtInfoMapPtr->bbtInfo[dieNo].grownBadUpdate == BBT_INFO_GROWN_BAD_UPDATE_BOOKED)
        {
            for (phyBlockNo = 0; phyBlockNo < TOTAL_BLOCKS_PER_DIE; phyBlockNo++)
            {
                bbtUpdater = (unsigned char *)(tempBbtBufAddr[dieNo] + phyBlockNo);

                if (phyBlockNo != bbtInfoMapPtr->bbtInfo[dieNo].phyBlock)
                    *bbtUpdater = phyBlockMapPtr->phyBlock[dieNo][phyBlockNo].bad;
                else
                    *bbtUpdater = BLOCK_STATE_NORMAL;
            }

            dieState[dieNo] = DIE_STATE_BAD_BLOCK_TABLE_UPDATE;
        }
        else
            dieState[dieNo] = DIE_STATE_BAD_BLOCK_TABLE_HOLD;
    }

    // update bad block tables in flash
    SaveBadBlockTable(dieState, tempBbtBufAddr, tempBbtBufEntrySize);
}
