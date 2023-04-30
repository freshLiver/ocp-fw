#include "debug.h"

#include "monitor/monitor.h"

void monitor_handle_admin_cmds(uint32_t cmdSlotTag, NVME_ADMIN_COMMAND *nvmeAdminCmd)
{
    uint32_t mode = nvmeAdminCmd->dword10; // dword10 is used to specify the monitor mode

    if (nvmeAdminCmd->OPC == ADMIN_MONITOR_BUFFER)
    {
        uint32_t iDie = nvmeAdminCmd->dword11;
        uint32_t lba1 = nvmeAdminCmd->dword11;
        uint32_t lba2 = nvmeAdminCmd->dword12;

        switch (mode)
        {
        case 1:
            monitor_dump_data_buffer_info(MONITOR_MODE_DUMP_DIRTY, 0, 0);
            break;
        case 2:
            monitor_dump_data_buffer_info(MONITOR_MODE_DUMP_SPECIFY, lba1, 0);
            break;
        case 3:
            monitor_dump_data_buffer_info(MONITOR_MODE_DUMP_RANGE, lba1, lba2);
            break;
        case 4:
            monitor_dump_slice_buffer(iDie);
            break;
        case 5:
            monitor_clear_slice_buffer(iDie);
            break;
        default:
            monitor_dump_data_buffer_info(MONITOR_MODE_DUMP_FULL, 0, 0);
            break;
        }
    }
    else if (nvmeAdminCmd->OPC == ADMIN_MONITOR_FLASH)
    {
        uint32_t iDie      = nvmeAdminCmd->dword11;
        uint32_t iBlk      = nvmeAdminCmd->dword12;
        uint32_t iPage     = nvmeAdminCmd->dword13;

        uint32_t iCh = VDIE2PCH(iDie), iWay = VDIE2PWAY(iDie);

        switch (mode)
        {
        case 1:
            monitor_dump_free_blocks(iDie);
            break;
        case 2:
            monitor_dump_phy_page(iCh, iWay, iBlk, iPage);
            break;
        case 4:
            monitor_erase_phy_blk(iCh, iWay, iBlk);
            break;

        default:
            for (iDie = 0; iDie < USER_DIES; ++iDie)
                monitor_dump_free_blocks(iDie);
            break;
        }
    }
    else if (nvmeAdminCmd->OPC == ADMIN_MONITOR_MAPPING)
    {
        uint32_t src = nvmeAdminCmd->dword11;
        uint32_t dst = nvmeAdminCmd->dword12;

        switch (mode)
        {
        case 1:
            monitor_dump_lsa(src);
            break;
        case 2:
            monitor_dump_vsa(src);
            break;
        case 3:
            monitor_set_l2v(src, dst);
            break;

        default:
            monitor_dump_mapping();
            break;
        }
    }
    else
        pr_error("Monitor: Unexpected monitor opcode: %u", nvmeAdminCmd->OPC);
}

void handle_nvme_io_monitor(uint32_t cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{
    uint32_t mode = nvmeIOCmd->dword10; // dword10 is used to specify the monitor mode
    uint32_t iDie;

    if (nvmeIOCmd->OPC == IO_NVM_WRITE_SLICE)
    {
        switch (mode)
        {
        case 1:
            iDie = nvmeIOCmd->dword11;
            monitor_nvme_write_slice_buffer(cmdSlotTag, iDie);
            break;

        default:
            pr_error("Monitor: Unexpected WRITE_SLICE mode: %u", mode);
            break;
        }
    }
    else
        pr_error("Monitor: Unexpected monitor opcode: %u", nvmeIOCmd->OPC);
}