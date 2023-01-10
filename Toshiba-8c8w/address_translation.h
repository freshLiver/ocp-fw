//////////////////////////////////////////////////////////////////////////////////
// address_translation.h for Cosmos+ OpenSSD
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
// Module Name: Address Translator
// File Name: address translation.h
//
// Version: v1.0.0
//
// Description:
//   - define parameters, data structure and functions of address translator
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#ifndef ADDRESS_TRANSLATION_H_
#define ADDRESS_TRANSLATION_H_

#include "ftl_config.h"
#include "nvme/nvme.h"

/* LSA for Logical Slice Address */

#define LSA_NONE 0xffffffff
#define LSA_FAIL 0xffffffff

#define VSA_NONE 0xffffffff
#define VSA_FAIL 0xffffffff

#define PAGE_NONE 0xffff

#define BLOCK_NONE 0xffff
#define BLOCK_FAIL 0xffff

#define DIE_NONE 0xff
#define DIE_FAIL 0xff

#define RESERVED_FREE_BLOCK_COUNT 0x1

#define GET_FREE_BLOCK_NORMAL 0x0
#define GET_FREE_BLOCK_GC     0x1

#define BLOCK_STATE_NORMAL 0 // this block is not bad block
#define BLOCK_STATE_BAD    1 // this block is bad block

#define DIE_STATE_BAD_BLOCK_TABLE_NOT_EXIST 0 // no bbt, do build and persist the bbt (initialize)
#define DIE_STATE_BAD_BLOCK_TABLE_EXIST     1 // bbt exists, don't build and persist the bbt (initialize)
#define DIE_STATE_BAD_BLOCK_TABLE_HOLD      2 // no unpersisted bad block, don't update the bbt (runtime)
#define DIE_STATE_BAD_BLOCK_TABLE_UPDATE    3 // unpersisted bad blocks exist, do update the bbt (runtime)

#define BAD_BLOCK_TABLE_MAKER_IDLE    0 // no need to rebuilt to bbt
#define BAD_BLOCK_TABLE_MAKER_TRIGGER 1 // rebuild the bbt, only used in initialization

#define CLEAN_DATA_IN_BYTE 0xff // the bit pattern for determining bad block

/* how many pages are used for storing the bbt of that die */
#define USED_PAGES_FOR_BAD_BLOCK_TABLE_PER_DIE (TOTAL_BLOCKS_PER_DIE / BYTES_PER_DATA_REGION_OF_PAGE + 1)
#define DATA_SIZE_OF_BAD_BLOCK_TABLE_PER_DIE   (TOTAL_BLOCKS_PER_DIE)

/**
 * @brief The start physical page number that stored the bbt of this die.
 *
 * The first page will be used for checking the bad block mark, so start putting the bbt
 * from second page.
 *
 * @note the bad block table should be saved at lsb pages
 */
#define START_PAGE_NO_OF_BAD_BLOCK_TABLE_BLOCK (1)

#define BBT_INFO_GROWN_BAD_UPDATE_NONE   0 // the bbt no need to be updated
#define BBT_INFO_GROWN_BAD_UPDATE_BOOKED 1 // the bbt should be updated

/* -------------------------------------------------------------------------- */
/*                NAND Address Translation (Virtual -> Virtual)               */
/* -------------------------------------------------------------------------- */

#define Vsa2VdieTranslation(virtualSliceAddr)   ((virtualSliceAddr) % (USER_DIES))
#define Vsa2VblockTranslation(virtualSliceAddr) (((virtualSliceAddr) / (USER_DIES)) / (SLICES_PER_BLOCK))
#define Vsa2VpageTranslation(virtualSliceAddr)  (((virtualSliceAddr) / (USER_DIES)) % (SLICES_PER_BLOCK))

// virtual organization to virtual slice address translation
#define Vorg2VsaTranslation(dieNo, blockNo, pageNo)                                                               \
    ((dieNo) + (USER_DIES) * ((blockNo) * (SLICES_PER_BLOCK) + (pageNo)))

/* -------------------------------------------------------------------------- */
/*               NAND Address Translation (Virtual -> Physical)               */
/* -------------------------------------------------------------------------- */

#define Vdie2PchTranslation(dieNo)  ((dieNo) % (USER_CHANNELS))
#define Vdie2PwayTranslation(dieNo) ((dieNo) / (USER_CHANNELS))

/**
 * @brief The V2P block mapping rule (total block space).
 *
 * This translation basically just map the virtual block to the physical
 *
 * To perform a V2P translation, we must consider two problems:
 *
 * - The number of user blocks may be less than total blocks on that die
 * - The blocks on LUN 0 and LUN 1 are flatten as 1D block array
 *
 * @todo Therefore, the
 *
 * @note Tbs = Total block space
 * @warning what if very small `USER_BLOCKS_PER_LUN`
 */
#define Vblock2PblockOfTbsTranslation(blockNo)                                                                    \
    (((blockNo) / (USER_BLOCKS_PER_LUN)) * (TOTAL_BLOCKS_PER_LUN) + ((blockNo) % (USER_BLOCKS_PER_LUN)))

/**
 * @brief The V2P block mapping rule (main block space).
 *
 * @note Mbs = Main block space
 * @warning what if very small `USER_BLOCKS_PER_LUN`
 */
#define Vblock2PblockOfMbsTranslation(blockNo)                                                                    \
    (((blockNo) / (USER_BLOCKS_PER_LUN)) * (MAIN_BLOCKS_PER_LUN) + ((blockNo) % (USER_BLOCKS_PER_LUN)))

/**
 * @brief Map the virtual page to the physical LSB page in SLC mode.
 *
 * @warning This may reduce the flash lifetime since this is not true SLC mode.
 * @warning Why the LSB page is at odd page ??
 */
#define Vpage2PlsbPageTranslation(pageNo) ((pageNo) > (0) ? (2 * (pageNo)-1) : (0))

/* -------------------------------------------------------------------------- */
/*               NAND Address Translation (Physical -> Virtual)               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Get die number from physical channel number and way number
 *
 * @warning: Why the LSB page is at odd page ??
 */
#define Pcw2VdieTranslation(chNo, wayNo) ((chNo) + (wayNo) * (USER_CHANNELS))

/**
 * @brief Get the corresponding virtual page number of the given LSB page number.
 *
 * @warning: No need to check if `pageNo` > 0 or not.
 */
#define PlsbPage2VpageTranslation(pageNo) ((pageNo) > (0) ? (((pageNo) + 1) / 2) : (0))

/* -------------------------------------------------------------------------- */
/*               Logical <-> Virtual Slice Address Mapping Table              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Exactly the Virtual Slice Address.
 */
typedef struct _LOGICAL_SLICE_ENTRY
{
    /* The corresponding virtual slice address (index) of the logical slice. */
    unsigned int virtualSliceAddr;
} LOGICAL_SLICE_ENTRY, *P_LOGICAL_SLICE_ENTRY;

/**
 * @brief The Logical -> Virtual Slice Address mapping table.
 */
typedef struct _LOGICAL_SLICE_MAP
{
    LOGICAL_SLICE_ENTRY logicalSlice[SLICES_PER_SSD];
} LOGICAL_SLICE_MAP, *P_LOGICAL_SLICE_MAP;

/**
 * @brief Exactly the Logical Slice Address
 */
typedef struct _VIRTUAL_SLICE_ENTRY
{
    /* The corresponding logical slice address (index) of the virtual slice. */
    unsigned int logicalSliceAddr;
} VIRTUAL_SLICE_ENTRY, *P_VIRTUAL_SLICE_ENTRY;

/**
 * @brief The Virtual -> Logical Slice mapping table.
 */
typedef struct _VIRTUAL_SLICE_MAP
{
    VIRTUAL_SLICE_ENTRY virtualSlice[SLICES_PER_SSD];
} VIRTUAL_SLICE_MAP, *P_VIRTUAL_SLICE_MAP;

/* -------------------------------------------------------------------------- */
/*               Structures for managing Virtual Block Metadata               */
/* -------------------------------------------------------------------------- */

/**
 * @brief The metadata for this block.
 */
typedef struct _VIRTUAL_BLOCK_ENTRY
{
    unsigned int bad : 1;              // 1 indicates that this block is bad block
    unsigned int free : 1;             // 1 indicates that this block is free block
    unsigned int invalidSliceCnt : 16; // how many invalid slices in this block
    unsigned int reserved0 : 10;       //
    unsigned int currentPage : 16;     // the current working page number of this block
    unsigned int eraseCnt : 16;        // how many times this block have been erased
    unsigned int prevBlock : 16;       // TODO
    unsigned int nextBlock : 16;       // TODO
} VIRTUAL_BLOCK_ENTRY, *P_VIRTUAL_BLOCK_ENTRY;

/**
 * @brief The block metadata table for all the blocks.
 */
typedef struct _VIRTUAL_BLOCK_MAP
{
    VIRTUAL_BLOCK_ENTRY block[USER_DIES][USER_BLOCKS_PER_DIE];
} VIRTUAL_BLOCK_MAP, *P_VIRTUAL_BLOCK_MAP;

/**
 * @brief The metadata for this die.
 *
 * @todo There are
 */
typedef struct _VIRTUAL_DIE_ENTRY
{
    unsigned int currentBlock : 16;  // the current working block number of this die
    unsigned int headFreeBlock : 16; // virtual block map index of the first free block of this die
    unsigned int tailFreeBlock : 16; // virtual block map index of the last free block of this die
    unsigned int freeBlockCnt : 16;  // how many free blocks on this die
    unsigned int prevDie : 8;
    unsigned int nextDie : 8;
    unsigned int reserved0 : 16;
} VIRTUAL_DIE_ENTRY, *P_VIRTUAL_DIE_ENTRY;

/**
 * @brief The metadata table for all user dies.
 */
typedef struct _VIRTUAL_DIE_MAP
{
    VIRTUAL_DIE_ENTRY die[USER_DIES];
} VIRTUAL_DIE_MAP, *P_VIRTUAL_DIE_MAP;

/**
 * @warning typo (FRRE -> FREE)
 * @warning not used
 */
typedef struct _FRRE_BLOCK_ALLOCATION_LIST
{ // free block allocation die sequence list
    unsigned int headDie : 8;
    unsigned int tailDie : 8;
    unsigned int reserved0 : 16;
} FRRE_BLOCK_ALLOCATION_LIST, *P_FRRE_BLOCK_ALLOCATION_LIST;

/**
 * @brief The bad block table for this die.
 */
typedef struct _BAD_BLOCK_TABLE_INFO_ENTRY
{
    unsigned int phyBlock : 16;      // which block in this die contains the bbt
    unsigned int grownBadUpdate : 1; // whether the bbt of this die should be updated
    unsigned int reserved0 : 15;
} BAD_BLOCK_TABLE_INFO_ENTRY, *P_BAD_BLOCK_TABLE_ENTRY;

/**
 * @brief The bad block tables.
 */
typedef struct _BAD_BLOCK_TABLE_INFO_MAP
{
    BAD_BLOCK_TABLE_INFO_ENTRY bbtInfo[USER_DIES];
} BAD_BLOCK_TABLE_INFO_MAP, *P_BAD_BLOCK_TABLE_INFO_MAP;

/**
 * @brief The metadata of the physical block.
 */
typedef struct _PHY_BLOCK_ENTRY
{
    /* the origin block was remapped to this physical block */
    unsigned int remappedPhyBlock : 16;
    /* the origin block is a bad block, check the remapped block */
    unsigned int bad : 1;
    unsigned int reserved0 : 15;
} PHY_BLOCK_ENTRY, *P_PHY_BLOCK_ENTRY;

/**
 * @brief The metadata table for all the physical blocks.
 */
typedef struct _PHY_BLOCK_MAP
{
    PHY_BLOCK_ENTRY phyBlock[USER_DIES][TOTAL_BLOCKS_PER_DIE];
} PHY_BLOCK_MAP, *P_PHY_BLOCK_MAP;

void InitAddressMap();
void InitSliceMap();
void InitBlockDieMap();

unsigned int AddrTransRead(unsigned int logicalSliceAddr);
unsigned int AddrTransWrite(unsigned int logicalSliceAddr);
unsigned int FindFreeVirtualSlice();
unsigned int FindFreeVirtualSliceForGc(unsigned int copyTargetDieNo, unsigned int victimBlockNo);
unsigned int FindDieForFreeSliceAllocation();

void InvalidateOldVsa(unsigned int logicalSliceAddr);
void EraseBlock(unsigned int dieNo, unsigned int blockNo);

void PutToFbList(unsigned int dieNo, unsigned int blockNo);
unsigned int GetFromFbList(unsigned int dieNo, unsigned int getFreeBlockOption);

void UpdatePhyBlockMapForGrownBadBlock(unsigned int dieNo, unsigned int phyBlockNo);
void UpdateBadBlockTableForGrownBadBlock(unsigned int tempBufAddr);

extern P_LOGICAL_SLICE_MAP logicalSliceMapPtr;
extern P_VIRTUAL_SLICE_MAP virtualSliceMapPtr;
extern P_VIRTUAL_BLOCK_MAP virtualBlockMapPtr;
extern P_VIRTUAL_DIE_MAP virtualDieMapPtr;
extern P_PHY_BLOCK_MAP phyBlockMapPtr;
extern P_BAD_BLOCK_TABLE_INFO_MAP bbtInfoMapPtr;

extern unsigned char sliceAllocationTargetDie;
extern unsigned int mbPerbadBlockSpace;

#endif /* ADDRESS_TRANSLATION_H_ */
