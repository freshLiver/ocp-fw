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
        iDie  = Vsa2VdieTranslation(vsa);
        iCh   = Vdie2PchTranslation(iDie);
        iWay  = Vdie2PwayTranslation(iDie);
        iBlk  = VBA2PBA_MBS(Vsa2VblockTranslation(vsa));
        iPage = Vsa2VpageTranslation(vsa);
        pr_info("VSA[%u] = Ch %u Way %u Blk %u Page %u", vsa, iCh, iWay, iBlk, iPage);
    }
    else
        pr_error("Skipped, VSA(%u) out-of-range!!!", vsa);
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
    if (lsa < SLICES_PER_SSD && vsa < SLICES_PER_SSD)
    {
        LSA_ENTRY(lsa)->virtualSliceAddr = vsa;
        VSA_ENTRY(vsa)->logicalSliceAddr = lsa;
        pr_info("Update V2P Mapping : LSA[%u] -> VSA[%u]", lsa, vsa);
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