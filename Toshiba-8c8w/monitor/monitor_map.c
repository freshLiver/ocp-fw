#include "monitor/monitor.h"
#include "debug.h"

#include "ftl_config.h"
#include "memory_map.h"

void monitor_dump_lsa(uint32_t lsa)
{
    if (lsa < SLICES_PER_SSD)
        pr_info("LSA[%u] -> VSA[%u]", lsa, LSA2VSA(lsa));
    else
        pr_error("Skipped, LSA(%u) out-of-range!!!", lsa);
}

void monitor_dump_vsa(uint32_t vsa)
{
    uint32_t iDie, iCh, iWay, iBlk, iPage;

    if (vsa < SLICES_PER_SSD)
    {
        iDie  = VSA2VDIE(vsa);
        iCh   = VDIE2PCH(iDie);
        iWay  = VDIE2PWAY(iDie);
        iBlk  = VBA2PBA_MBS(VSA2VBLK(vsa));
        iPage = VSA2VPAGE(vsa);

        pr_info("VSA[%u] = Ch %u Way %u PBlk %u Page %u", vsa, iCh, iWay, iBlk, iPage);
        if (PBLK_ENTRY(iDie, iBlk)->bad)
        {
            pr_info("\t PBlk[%u] marked as bad", iBlk);
            pr_info("\t\t remapped to: PBlk[%u]", PBLK_ENTRY(iDie, iBlk)->remappedPhyBlock);
        }
    }
    else
        pr_error("Skipped, VSA(%u) out-of-range!!!", vsa);
}

/**
 * @brief Dump some info about a specified physical page.
 *
 * @param iCh The channel number of the target page to be dumped.
 * @param iWay The way number of the target page to be dumped.
 * @param iPBlk The physical block number of the target page to be dumped.
 * @param iPage The page number of the target page to be dumped.
 */
void monitor_dump_phy_page_info(uint32_t iCh, uint32_t iWay, uint32_t iPBlk, uint32_t iPage)
{
    uint32_t iDie, iVBlk;

    // try to do P2V
    iDie  = PCH2VDIE(iCh, iWay);
    iVBlk = monitor_p2vblk(iDie, iPBlk);

    // print request info
    pr_info("Ch[%u].Way[%u].PBlk[%u].Page[%u]:", iCh, iWay, iPBlk, iPage);
    pr_info("\t VSA: %u", (iVBlk == BLOCK_FAIL) ? VSA_FAIL : VORG2VSA(iDie, iVBlk, iPage));
    pr_info("\t bad block: %u", PBLK_ENTRY(iDie, iPBlk)->bad);
    pr_info("\t remapped to PhyBlock[%u]", PBLK_ENTRY(iDie, iPBlk)->remappedPhyBlock);
}

void monitor_dump_mapping()
{
    // pr_info("L2V Mapping Table:");
    // for (uint32_t lsa = 0; lsa < SLICES_PER_SSD; ++lsa) // TOO MANY
    //     pr_info("\tLSA[%u] -> VSA[%u]", lsa, LSA2VSA(lsa));

    for (uint32_t iDie = 0; iDie < USER_DIES; ++iDie)
    {
        pr_info("Die[%u]: V2P Mapping Table (Remapped Only):", iDie);
        for (uint32_t vba = 0, pba; vba < USER_BLOCKS_PER_DIE; ++vba)
            if (vba != (pba = PBLK_ENTRY(iDie, VBA2PBA_MBS(vba))->remappedPhyBlock))
                pr_info("\tVBA[%u] -> PBA[%u]", vba, pba);
    }
}

void monitor_set_l2v(uint32_t lsa, uint32_t vsa)
{
    uint32_t iDie  = VSA2VDIE(vsa);
    uint32_t iBlk  = VSA2VBLK(vsa);
    uint32_t iPage = VSA2VPAGE(vsa);

    if (lsa < SLICES_PER_SSD && vsa < SLICES_PER_SSD)
    {
        LSA_ENTRY(lsa)->virtualSliceAddr = vsa;
        VSA_ENTRY(vsa)->logicalSliceAddr = lsa;
        pr_info("MONITOR: Updated LSA[%u] -> VSA[%u] (Die[%u].Blk[%u].Page[%u])", lsa, vsa, iDie, iBlk, iPage);
    }
    else
        pr_error("Skipped, LSA(%u) or VSA(%u) out-of-range!!!", lsa, vsa);
}

uint32_t monitor_p2vblk(uint32_t iDie, uint32_t iPBlk)
{
    uint32_t iVBlk, iLun, off;

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
    return iVBlk;
}