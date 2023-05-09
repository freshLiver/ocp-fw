#include "monitor/monitor.h"
#include "debug.h"

#include "data_buffer.h"
#include "request_format.h"

void monitor_dump_data_buffer_info(MONITOR_MODE mode, uint32_t slsa, uint32_t elsa)
{
    P_DATA_BUF_ENTRY entry;

    if (mode == MONITOR_MODE_DUMP_FULL)
        pr_info("Dump all data buffer entries");
    else if (mode == MONITOR_MODE_DUMP_DIRTY)
        pr_info("Dump dirty data buffer entries");
    else if (mode == MONITOR_MODE_DUMP_RANGE)
        pr_info("Dump the data buffer entries in range LSA[%u] ~ LSA[%u]", slsa, elsa);
    else if (mode == MONITOR_MODE_DUMP_SPECIFY)
        pr_info("Dump the data buffer entry of LSA[%u]", slsa);
    else
    {
        pr_error("Unsupported MONITOR_MODE: %d", mode);
        return;
    }

    // traverse data buffer map
    for (uint32_t iEntry = BUF_HEAD_IDX(), lsa; iEntry != DATA_BUF_NONE; iEntry = BUF_NEXT_IDX(iEntry))
    {
        // get corresponding buffer entry
        entry = BUF_ENTRY(iEntry);
        lsa   = entry->logicalSliceAddr;

        if ((mode == MONITOR_MODE_DUMP_DIRTY) && !entry->dirty)
            continue;

        if ((mode == MONITOR_MODE_DUMP_RANGE) && ((slsa > lsa) || (lsa > elsa)))
            continue;

        if ((mode == MONITOR_MODE_DUMP_SPECIFY) && (lsa != slsa))
            continue;

        // dump buffer content
        pr_info("buffer entry [%04u]", iEntry);
        pr_info("   .logicalSliceAddr   = %u", entry->logicalSliceAddr);
        pr_info("   .dirty              = %u", entry->dirty);
        pr_info("   .prevEntry          = %u", entry->prevEntry);
        pr_info("   .nextEntry          = %u", entry->nextEntry);
        pr_info("   .hashPrevEntry      = %u", entry->hashPrevEntry);
        pr_info("   .hashNextEntry      = %u", entry->hashNextEntry);
        pr_info("   .blockingReqTail    = %u", entry->blockingReqTail);
        pr_info("   .phyReq             = %u", entry->phyReq);
        pr_info("   .dontCache          = %u", entry->dontCache);
    }
}

void monitor_dump_data_buffer_content(uint32_t iBufEntry)
{
    const uint32_t *data  = (uint32_t *)BUF_DATA_ENTRY2ADDR(iBufEntry);
    const uint32_t *spare = (uint32_t *)BUF_SPARE_ENTRY2ADDR(iBufEntry);

    pr_info(SPLIT_LINE);

#ifdef __ARM_BIG_ENDIAN
    pr_debug("Big-Endian Mode");
#else
    pr_debug("Little-Endian Mode");
#endif /* __ARM_BIG_ENDIAN */

    pr_info("Data Buffer[%u] (at 0x%p) Data:", iBufEntry, data);
    for (uint32_t iWord = 0; iWord < (BYTES_PER_DATA_REGION_OF_SLICE >> 2); iWord += DUMP_WORDS_PER_ROW)
    {
        pr_raw("Byte %08u:\t", iWord << 2);
        for (uint32_t off = 0; off < DUMP_WORDS_PER_ROW; ++off)
        {
#ifdef __ARM_BIG_ENDIAN
            pr_raw("%08x ", data[iWord + off]);
#else
            pr_raw("%08x ", __builtin_bswap32(data[iWord + off]));
#endif /* __ARM_BIG_ENDIAN */
        }
        pr("");
    }

    pr_info(SPLIT_LINE);

    pr_info("Data Buffer[%u] (at 0x%p) Spare:", iBufEntry, spare);

    for (uint32_t iWord = 0; iWord < (BYTES_PER_SPARE_REGION_OF_SLICE >> 2); iWord += DUMP_WORDS_PER_ROW)
    {
        pr_raw("Byte %08u:\t", iWord << 2);
        for (uint32_t off = 0; off < DUMP_WORDS_PER_ROW; ++off)
        {
#ifdef __ARM_BIG_ENDIAN
            pr_raw("%08x ", spare[iWord + off]);
#else
            pr_raw("%08x ", __builtin_bswap32(spare[iWord + off]));
#endif /* __ARM_BIG_ENDIAN */
        }
        pr("");
    }

    pr_info("\n" SPLIT_LINE);
}