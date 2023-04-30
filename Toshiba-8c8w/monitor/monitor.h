
#ifndef __OPENSSD_FW_MONITOR_H__
#define __OPENSSD_FW_MONITOR_H__

#include "stdint.h"
#include "stdbool.h"

#include "nvme/nvme.h"
#include "memory_map.h"

typedef enum
{
    MONITOR_MODE_DUMP_FULL,
    MONITOR_MODE_DUMP_DIRTY,
    MONITOR_MODE_DUMP_SPECIFY,
    MONITOR_MODE_DUMP_RANGE,
    MONITOR_MODE_SET_PAIR,
} MONITOR_MODE;

typedef union
{
    uint8_t byte[BYTES_PER_SLICE];
    struct
    {
        uint8_t data[BYTES_PER_DATA_REGION_OF_SLICE];
        uint8_t spare[BYTES_PER_SPARE_REGION_OF_SLICE];
    };
} MONITOR_SLICE_BUFFER;

typedef struct
{
    MONITOR_SLICE_BUFFER dieBuf[USER_DIES];
} MONITOR_DATA_BUFFER;

extern MONITOR_DATA_BUFFER *monitorBuffers;
#define MONITOR_DIE_DATA_BUF(iDie) (monitorBuffers->dieBuf[(iDie)])

#define DUMP_WORDS_PER_ROW 8

/* -------------------------------------------------------------------------- */
/*                              public interfaces                             */
/* -------------------------------------------------------------------------- */

void monitorInit();
void monitor_clear_slice_buffer(uint32_t iDie);
void monitor_dump_slice_buffer(uint32_t iDie);
void monitor_nvme_write_slice_buffer(uint32_t cmdSlotTag, uint32_t iDie);

void monitor_handle_admin_cmds(uint32_t cmdSlotTag, NVME_ADMIN_COMMAND *nvmeAdminCmd);
void handle_nvme_io_monitor(uint32_t cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd);

void monitor_dump_data_buffer_info(MONITOR_MODE mode, uint32_t slsa, uint32_t elsa);
void monitor_dump_data_buffer_content(uint32_t iBufEntry);

void monitor_dump_lsa(uint32_t lsa);
void monitor_dump_vsa(uint32_t vsa);
void monitor_dump_phy_page_info(uint32_t iCh, uint32_t iWay, uint32_t iPBlk, uint32_t iPage);
void monitor_dump_mapping();
void monitor_set_l2v(uint32_t lsa, uint32_t vsa);
uint32_t monitor_p2vblk(uint32_t iDie, uint32_t iPBlk);

void monitor_dump_free_blocks(uint32_t iDie);
void monitor_dump_phy_page(uint32_t iCh, uint32_t iWay, uint32_t iPBlk, uint32_t iPage);
void monitor_write_phy_page(uint32_t iCh, uint32_t iWay, uint32_t iPBlk, uint32_t iPage);
void monitor_erase_phy_blk(uint32_t iCh, uint32_t iWay, uint32_t iPBlk);

#endif /* __OPENSSD_FW_MONITOR_H__ */