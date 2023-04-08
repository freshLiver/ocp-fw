#include "monitor/monitor.h"
#include "debug.h"

#include "memory_map.h"
#include "data_buffer.h"
#include "request_allocation.h"
#include "request_transform.h"

void monitor_dump_free_blocks(uint32_t iDie)
{
    // each die has their own free block list
    pr_info("Die[%u]: Free Block List (%u blocks):", iDie, VDIE_ENTRY(iDie)->freeBlockCnt);
    for (uint32_t vba = VDIE_ENTRY(iDie)->headFreeBlock; vba != BLOCK_NONE; vba = VBLK_NEXT_IDX(iDie, vba))
        pr_raw("%u, ", vba);
    pr("");
}

void monitor_dump_phy_page(uint32_t iCh, uint32_t iWay, uint32_t iPBlk, uint32_t iPage)
{
    uint32_t iReqEntry, iDie, iLun, iVBlk, off;

    // try to do P2V
    iDie = Pcw2VdieTranslation(iCh, iWay);
    iLun = iPBlk / TOTAL_BLOCKS_PER_LUN;
    off  = iPBlk % TOTAL_BLOCKS_PER_LUN;

    if (off < USER_BLOCKS_PER_LUN)
    {
        iVBlk = iLun * USER_BLOCKS_PER_LUN + off;
    }
    else
    {
        /*
         * Even if the given pblk not directly belongs to a vblk, there may be a bad pblk
         * that was remapped to the given pblk.
         */
        for (off = 0; off < USER_BLOCKS_PER_LUN; ++off)
        {
            iVBlk = iLun * TOTAL_BLOCKS_PER_LUN + off;
            if (PBLK_ENTRY(iDie, iVBlk)->remappedPhyBlock == iPBlk)
                break;
            else
                iVBlk = BLOCK_FAIL;
        }
        // if vblk not found, iVBlk should be BLOCK_FAIL
    }

    // prepare request
    iReqEntry = GetFromFreeReqQ();

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

    // print request info
    pr_info("REQ[%u]: Read Ch[%u].Way[%u].PBlk[%u].Page[%u]", iReqEntry, iCh, iWay, iPBlk, iPage);
    pr_info("\t\t VSA: %u", (iVBlk == BLOCK_FAIL) ? VSA_FAIL : Vorg2VsaTranslation(iDie, iVBlk, iPage));
    pr_info("\t\t bad block: %u", PBLK_ENTRY(iDie, iPBlk)->bad);
    pr_info("\t\t remapped block: PhyBlock[%u]", PBLK_ENTRY(iDie, iPBlk)->remappedPhyBlock);

    // issue and wait until finished
    SelectLowLevelReqQ(iReqEntry);
    SyncAllLowLevelReqDone();

    // dump page content
    monitor_dump_slice_buffer(iDie);
}