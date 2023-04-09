
#ifndef __OPENSSD_FW_MONITOR_H__
#define __OPENSSD_FW_MONITOR_H__

#include "stdint.h"
#include "stdbool.h"

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

/* -------------------------------------------------------------------------- */
/*                              public interfaces                             */
/* -------------------------------------------------------------------------- */

void monitorInit();
void monitor_clear_slice_buffer(uint32_t iDie);
void monitor_dump_slice_buffer(uint32_t iDie);

void monitor_dump_buffer(MONITOR_MODE mode, uint32_t slsa, uint32_t elsa);

void monitor_dump_lsa(uint32_t lsa);
void monitor_dump_vsa(uint32_t vsa);
void monitor_dump_mapping();
void monitor_set_l2v(uint32_t lsa, uint32_t vsa);
uint32_t monitor_p2vblk(uint32_t iDie, uint32_t iPBlk);

void monitor_dump_free_blocks(uint32_t iDie);
void monitor_dump_phy_page(uint32_t iCh, uint32_t iWay, uint32_t iPBlk, uint32_t iPage);
void monitor_erase_phy_blk(uint32_t iCh, uint32_t iWay, uint32_t iPBlk);

#endif /* __OPENSSD_FW_MONITOR_H__ */