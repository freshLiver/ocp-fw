#include "debug.h"
#include "monitor/monitor.h"
#include "nvme/host_lld.h"

MONITOR_DATA_BUFFER *monitorBuffers;

extern void EvictDataBufEntry(unsigned int originReqSlotTag);

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

void monitor_clear_slice_buffer(uint32_t iDie)
{
    pr_debug("Slice Buffer[%u]: Set all bytes 0", iDie);
    memset(MONITOR_DIE_DATA_BUF(iDie).byte, 0, BYTES_PER_SLICE);
}

/**
 * @brief Dump the slice buffer content of the specified die.
 *
 * @note Since the data will be parsed in 4-byte unit, if the data is stored in the format
 * of Little-Endian, it will be converted to Big-Endian during the parsing process.
 *
 * @param iDie The target die number of the slice buffer to be dumped.
 */
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

    pr_info("Slice Buffer[%u] Data (at 0x%p):", iDie, data);
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

    pr_info("Slice Buffer[%u] Spare (at 0x%p):", iDie, spare);

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

/**
 * @brief Read data from host buffer to the slice buffer of the specified die.
 *
 * @note Since the slice buffer size is limited, the should not
 *
 * @param cmdSlotTag The ??? of the source NVMe command.
 * @param iDie The target die number of the slice buffer to store the host data.
 */
void monitor_nvme_write_slice_buffer(uint32_t cmdSlotTag, uint32_t iDie)
{
    uint32_t iReqEntry = GetFromFreeReqQ();
    uint32_t iBufEntry = AllocateDataBuf();
    void *pBufEntry    = (void *)BUF_DATA_ENTRY2ADDR(iBufEntry);

    // clear data buffer
    EvictDataBufEntry(iReqEntry);                      // data buffer should be clean after eviction
    BUF_ENTRY(iBufEntry)->logicalSliceAddr = LSA_NONE; // should not be an useful cache entry
    PutToDataBufHashList(iBufEntry);

    // create RxDMA request
    REQ_ENTRY(iReqEntry)->reqType                     = REQ_TYPE_NVME_DMA;
    REQ_ENTRY(iReqEntry)->reqCode                     = REQ_CODE_RxDMA;
    REQ_ENTRY(iReqEntry)->nvmeCmdSlotTag              = cmdSlotTag;
    REQ_ENTRY(iReqEntry)->logicalSliceAddr            = LSA_NONE;               // dummy data, not useful cache
    REQ_ENTRY(iReqEntry)->reqOpt.dataBufFormat        = REQ_OPT_DATA_BUF_ENTRY; // must use data buffer entry
    REQ_ENTRY(iReqEntry)->dataBufInfo.entry           = iBufEntry;
    REQ_ENTRY(iReqEntry)->nvmeDmaInfo.startIndex      = 0;
    REQ_ENTRY(iReqEntry)->nvmeDmaInfo.nvmeBlockOffset = 0;
    REQ_ENTRY(iReqEntry)->nvmeDmaInfo.numOfNvmeBlock  = NVME_BLOCKS_PER_SLICE;
    REQ_ENTRY(iReqEntry)->nandInfo.virtualSliceAddr   = VSA_NONE; // dummy data, don't map to phy page

    // do and wait DMA
    IssueNvmeDmaReq(iReqEntry); // setup auto dma before being put to req queue
    PutToNvmeDmaReqQ(iReqEntry);
    CheckDoneNvmeDmaReq(); // wait all dma

    // copy data to slice buffer
    memcpy(MONITOR_DIE_DATA_BUF(iDie).data, pBufEntry, BYTES_PER_DATA_REGION_OF_SLICE);
    memset(MONITOR_DIE_DATA_BUF(iDie).spare, 0, BYTES_PER_SPARE_REGION_OF_SLICE);

#if defined(DEBUG) && (DEBUG > 2)
    // dump data buffer
    monitor_dump_data_buffer_content(iBufEntry);
#endif /* DEBUG */

    monitor_dump_slice_buffer(iDie);
}