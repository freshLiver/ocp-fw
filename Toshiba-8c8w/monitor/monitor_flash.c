#include "monitor/monitor.h"
#include "debug.h"

#include "memory_map.h"
#include "data_buffer.h"
#include "request_allocation.h"
#include "request_transform.h"

/**
 * @brief Dump all free blocks on the specified die by traversing the free block table.
 *
 * @param iDie The target die number.
 */
void monitor_dump_free_blocks(uint32_t iDie)
{
    // each die has their own free block list
    pr_info("Die[%u]: Free Block List (%u blocks):", iDie, VDIE_ENTRY(iDie)->freeBlockCnt);
    for (uint32_t vba = VDIE_ENTRY(iDie)->headFreeBlock; vba != BLOCK_NONE; vba = VBLK_NEXT_IDX(iDie, vba))
        pr_raw("%u, ", vba);
    pr("");
}

/**
 * @brief Read the data of the specified page into the slice buffer of the same die.
 *
 * @note The data read from the flash memory will be buffered to the slice buffer of the
 * same die where the data is originally stored.
 *
 * @param iCh The target channel number of the data to be read.
 * @param iWay The target way number of the data to be read.
 * @param iPBlk The target physical block number of the data to be read.
 * @param iPage The target page number of the data to be read.
 */
void monitor_dump_phy_page(uint32_t iCh, uint32_t iWay, uint32_t iPBlk, uint32_t iPage)
{
    // prepare request
    uint32_t iDie      = PCH2VDIE(iCh, iWay);
    uint32_t iReqEntry = GetFromFreeReqQ();

    REQ_ENTRY(iReqEntry)->reqType                       = REQ_TYPE_NAND;
    REQ_ENTRY(iReqEntry)->reqCode                       = REQ_CODE_READ;
    REQ_ENTRY(iReqEntry)->reqOpt.dataBufFormat          = REQ_OPT_DATA_BUF_ADDR;
    REQ_ENTRY(iReqEntry)->reqOpt.nandAddr               = REQ_OPT_NAND_ADDR_PHY_ORG;
    REQ_ENTRY(iReqEntry)->reqOpt.nandEcc                = REQ_OPT_NAND_ECC_ON;
    REQ_ENTRY(iReqEntry)->reqOpt.nandEccWarning         = REQ_OPT_NAND_ECC_WARNING_ON;
    REQ_ENTRY(iReqEntry)->reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_NONE;
    REQ_ENTRY(iReqEntry)->reqOpt.blockSpace             = REQ_OPT_BLOCK_SPACE_TOTAL;
    REQ_ENTRY(iReqEntry)->dataBufInfo.addr              = (uintptr_t)MONITOR_DIE_DATA_BUF(iDie).byte;
    REQ_ENTRY(iReqEntry)->nandInfo.physicalCh           = iCh;
    REQ_ENTRY(iReqEntry)->nandInfo.physicalWay          = iWay;
    REQ_ENTRY(iReqEntry)->nandInfo.physicalBlock        = iPBlk;
    REQ_ENTRY(iReqEntry)->nandInfo.physicalPage         = iPage;

    pr_debug("Req[%u]: MONITOR READ (buffer 0x%x)", iReqEntry, REQ_ENTRY(iReqEntry)->dataBufInfo.addr);
    monitor_dump_phy_page_info(iCh, iWay, iPBlk, iPage);

    // clear buffer before reading
    monitor_clear_slice_buffer(iDie);

    // issue and wait until finished
    SelectLowLevelReqQ(iReqEntry);
    SyncAllLowLevelReqDone();

    // dump page content
    monitor_dump_slice_buffer(iDie);
}

/**
 * @brief Write slice buffer data into the specific physical page.
 *
 * @note The data to be written to the target block is taken from the slice buffer of the
 * target die, the caller should make sure the slice buffer of target die contains correct
 * data before calling this function.
 *
 * @param iCh The target channel number of the data to be written to.
 * @param iWay The target way number of the data to be written to.
 * @param iPBlk The physical block number of the data to be written to.
 * @param iPage The target page number of the data to be written to.
 */
void monitor_write_phy_page(uint32_t iCh, uint32_t iWay, uint32_t iPBlk, uint32_t iPage)
{
    uint32_t iReqEntry = GetFromFreeReqQ();
    uint32_t iDie      = PCH2VDIE(iCh, iWay);

    REQ_ENTRY(iReqEntry)->reqType                       = REQ_TYPE_NAND;
    REQ_ENTRY(iReqEntry)->reqCode                       = REQ_CODE_WRITE;
    REQ_ENTRY(iReqEntry)->reqOpt.nandEcc                = REQ_OPT_NAND_ECC_ON;
    REQ_ENTRY(iReqEntry)->reqOpt.nandEccWarning         = REQ_OPT_NAND_ECC_WARNING_ON;
    REQ_ENTRY(iReqEntry)->reqOpt.dataBufFormat          = REQ_OPT_DATA_BUF_ADDR;
    REQ_ENTRY(iReqEntry)->reqOpt.nandAddr               = REQ_OPT_NAND_ADDR_PHY_ORG;
    REQ_ENTRY(iReqEntry)->reqOpt.blockSpace             = REQ_OPT_BLOCK_SPACE_TOTAL;
    REQ_ENTRY(iReqEntry)->reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_NONE;
    REQ_ENTRY(iReqEntry)->dataBufInfo.addr              = (uintptr_t)MONITOR_DIE_DATA_BUF(iDie).byte;
    REQ_ENTRY(iReqEntry)->nandInfo.physicalCh           = iCh;
    REQ_ENTRY(iReqEntry)->nandInfo.physicalWay          = iWay;
    REQ_ENTRY(iReqEntry)->nandInfo.physicalBlock        = iPBlk;
    REQ_ENTRY(iReqEntry)->nandInfo.physicalPage         = iPage;

    // dump buffer content before issuing write request
    monitor_dump_slice_buffer(iDie);

    pr_debug("Req[%u]: WRITE", iReqEntry);
    monitor_dump_phy_page_info(iCh, iWay, iPBlk, iPage);

    // issue and wait
    SelectLowLevelReqQ(iReqEntry);
    SyncAllLowLevelReqDone();

#if defined(DEBUG) && (DEBUG > 2)
    // read target page after writing for debugging
    monitor_dump_phy_page(iCh, iWay, iPBlk, iPage);
#endif /* DEBUG */
}

/**
 * @brief Erase the specified physical block.
 *
 * @param iCh The channel number of the physical block to be erase.
 * @param iWay The way number of the physical block to be erase.
 * @param iPBlk The physical block number of the physical block to be erase.
 */
void monitor_erase_phy_blk(uint32_t iCh, uint32_t iWay, uint32_t iPBlk)
{
    // prepare request
    uint32_t iReqEntry = GetFromFreeReqQ();

    REQ_ENTRY(iReqEntry)->reqType                       = REQ_TYPE_NAND;
    REQ_ENTRY(iReqEntry)->reqCode                       = REQ_CODE_ERASE;
    REQ_ENTRY(iReqEntry)->reqOpt.nandAddr               = REQ_OPT_NAND_ADDR_PHY_ORG;
    REQ_ENTRY(iReqEntry)->reqOpt.blockSpace             = REQ_OPT_BLOCK_SPACE_TOTAL;
    REQ_ENTRY(iReqEntry)->reqOpt.dataBufFormat          = REQ_OPT_DATA_BUF_NONE; /* no buffer needed */
    REQ_ENTRY(iReqEntry)->reqOpt.rowAddrDependencyCheck = REQ_OPT_ROW_ADDR_DEPENDENCY_NONE;
    REQ_ENTRY(iReqEntry)->nandInfo.physicalCh           = iCh;
    REQ_ENTRY(iReqEntry)->nandInfo.physicalWay          = iWay;
    REQ_ENTRY(iReqEntry)->nandInfo.physicalBlock        = iPBlk;
    REQ_ENTRY(iReqEntry)->nandInfo.physicalPage         = 0; /* dummy */

    // print request info
    pr_debug("Req[%u]: ERASE", iReqEntry);
    monitor_dump_phy_page_info(iCh, iWay, iPBlk, 0);

    // issue and wait until finished
    SelectLowLevelReqQ(iReqEntry);
    SyncAllLowLevelReqDone();
}
