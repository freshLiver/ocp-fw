#include "debug.h"
#include "monitor/monitor.h"

MONITOR_DATA_BUFFER *monitorBuffers;

#define DUMP_WORDS_PER_ROW 8

/* -------------------------------------------------------------------------- */
/*                              public interfaces                             */
/* -------------------------------------------------------------------------- */

void monitorInit()
{
    // check memory size and range
    STATIC_ASSERT(MONITOR_END_ADDR < RESERVED1_END_ADDR);
    STATIC_ASSERT(sizeof(MONITOR_SLICE_BUFFER) == BYTES_PER_SLICE);
    STATIC_ASSERT(sizeof(MONITOR_DATA_BUFFER) == (USER_DIES * BYTES_PER_SLICE));

    // initialize
    monitorBuffers = (MONITOR_DATA_BUFFER *)MONITOR_DATA_BUFFER_ADDR;

    pr_info("MONITOR: Initializing Monitor Data Buffers...");
    for (uint8_t iDie = 0; iDie < USER_DIES; ++iDie)
        pr_debug("Die[%u]: Allocate data buffer at 0x%p", iDie, MONITOR_DIE_DATA_BUF(iDie).byte);
}

void monitor_clear_slice_buffer(uint32_t iDie) { memset(&MONITOR_DIE_DATA_BUF(iDie).byte, 0, BYTES_PER_SLICE); }

void monitor_dump_slice_buffer(uint32_t iDie)
{
    const uint32_t *data  = (uint32_t *)MONITOR_DIE_DATA_BUF(iDie).data;
    const uint32_t *spare = (uint32_t *)MONITOR_DIE_DATA_BUF(iDie).spare;

    pr_info(SPLIT_LINE);

#ifdef __ARM_BIG_ENDIAN
    pr_debug("Big-Endian Mode");
#else
    pr_debug("Little-Endian Mode");
#endif /* __ARM_BIG_ENDIAN */

    pr_info("Slice Buffer[%u] Data:", iDie);
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

    pr_info("Slice Buffer[%u] Spare:", iDie);

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