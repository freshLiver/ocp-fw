# Introduce to Cosmos+ OpenSSD Platform Firmware

## Abbreviations

### NVMe

- `CPL`: Complete or Completion

### Scheduling

- `Fb`: Free Block
- `Llr`: Low-level Scheduler
- `LSA`: Logical Slice Address
- `VSA`: Virtual Slice Address
- `Q`: queue

### NAND Flash

- `bbt`: Bad Block Table
- `ROW`: Flash Page
- `LUN`: Flash Plane
- `NSC`: NAND Storage Controller
- `Mbs`: Main Block Space
- `Tbs`: Total Block Space
- ??`V2F`: ==Virtual/Vector== to Flash
- ??`V2FMCRegisters`: ==Virtual/Vector== to Flash Memory Controller Registers

## DRAM Structure

```
+-------------------------------------------------+ << 0
~                                                 ~
~                                                 ~
+-------------------------------------------------+ << 
+-------------------------------------------------+ << 
+-------------------------------------------------+ << 
+-------------------------------------------------+ << 
+-------------------------------------------------+
~                                                 ~
~                                                 ~
+-------------------------------------------------+ << COMPLETE_FLAG_TABLE_ADDR
+-------------------------------------------------+ << STATUS_REPORT_TABLE_ADDR
+-------------------------------------------------+ << ERROR_INFO_TABLE_ADDR
+-------------------------------------------------+ << TEMPORARY_PAY_LOAD_ADDR
+-------------------------------------------------+
|                                                 |
~                                                 ~
~                                                 ~
|                                                 |
+-------------------------------------------------+ << DATA_BUFFER_MAP_ADDR
|                                                 |
| dataBuf[NUM_OF_DATA_BUFFER_ENTRY]               |
|                                                 |
+-------------------------------------------------+ << DATA_BUFFFER_HASH_TABLE_ADDR
|                                                 |
| dataBufHash[NUM_OF_DATA_BUFFER_ENTRY]           |
|                                                 |
+-------------------------------------------------+ << TEMPORARY_DATA_BUFFER_MAP_ADDR
|                                                 |
| tempDataBuf[NUM_OF_TEMPORARY_DATA_BUFFER_ENTRY] |
|                                                 |
+-------------------------------------------------+ << LOGICAL_SLICE_MAP_ADDR
+-------------------------------------------------+ << VIRTUAL_SLICE_MAP_ADDR
+-------------------------------------------------+ << VIRTUAL_BLOCK_MAP_ADDR
+-------------------------------------------------+ << PHY_BLOCK_MAP_ADDR
+-------------------------------------------------+ << BAD_BLOCK_TABLE_INFO_MAP_ADDR
+-------------------------------------------------+ << VIRTUAL_DIE_MAP_ADDR
+-------------------------------------------------+ << GC_VICTIM_MAP_ADDR
+-------------------------------------------------+ << REQ_POOL_ADDR
+-------------------------------------------------+ << ROW_ADDR_DEPENDENCY_TABLE_ADDR
+-------------------------------------------------+ << DIE_STATE_TABLE_ADDR
+-------------------------------------------------+ << RETRY_LIMIT_TABLE_ADDR
+-------------------------------------------------+ << WAY_PRIORITY_TABLE_ADDR
+-------------------------------------------------+ << FTL_MANAGEMENT_END_ADDR
+-------------------------------------------------+ << RESERVED1_START_ADDR
|                                                 |
~                                                 ~
~                                                 ~
|                                                 |
+-------------------------------------------------+ << 1 GB
```

## Data Structures

### Mapping Table

- Physical Block Map:
- Virtual Block Map:
- Bad Block Table Info Map:
- Logical Slice Map:
- Virtual Slice Map:

    ![VSA Strcture](../imgs/fw-vsa-strcture.png)


### Data Buffer

- `DATA_BUF_MAP` / `dataBuf`: An 1D data buffer array with `AVAILABLE_DATA_BUFFER_ENTRY_COUNT` entries.

    There are N(default 16) x `USER_DIES` entries in this array. Each of the entry is represented by the data structure `DATA_BUF_ENTRY` which is consist of several members:

    - `logicalSliceAddr`: 32 bits unsigned integer, the logical address of the slice command
    - `dirty`: 1 bit flag, whether this entry is dirty or not. 0 (`DATA_BUF_CLEAN`) for clean, 1 for dirty.
    - `prevEntry`: 16 bits unsigned integer, the prev data buffer entry index in the LRU list.
    - `nextEntry`: 16 bits unsigned integer, the next data buffer entry index in the LRU list.

        The LRU list only maintain the index of the head and tail data buffer entry, thus we have to use the above two members to manage the connection between the entries in the list.

        At the begining, the `prevEntry` and `nextEntry` just simply initialized to the index of prev/next element in this array. But as the time goes by, some entries may be chosen to store some data, //TODO

    - `hashPrevEntry`: 16 bits unsigned integer, the prev data buffer entry index in the current `dataBufHash` bucket
    - `hashNextEntry`: 16 bits unsigned integer, the next data buffer entry index in the current `dataBufHash` bucket

        Similar to LRU list, the buckets of hash table only records the index of head and tail data buffer entry.

    - `blockingReqTail`: 16 bits unsigned integer, the data buffer entry index of the last blocking request

        Similar to LRU and hash table bucket but the request queue is more complicated, detailed in later section.

        FIXME


- `DATA_BUF_LRU_LIST` / `dataBufLruList`: A structure that manages the MRU and LRU data buffer entry.

    This structure used for choosing the victim data buffer entry.

    There are only two members in this structure:

    - `headEntry`: 16 bits unsigned integer, the index of MRU data buffer entry. (first data buffer entry by default).
    - `tailEntry`: 16 bits unsigned integer, the index of LRU data buffer entry. (last data buffer entry by default).

    Note that because this structure only records the index of MRU/LRU data buffer entry, the relation between data buffer entries is managed by the `prevEntry` and `nextEntry` of `dataBuf` described above. 
    
    When a LRU entry is chosen to be the victim entry, the previous entry indicated by `prevEntry` and the next entry indicated by the `nextEntry` should be modified as well, concretely: 

    - evicted.prev: should be `DATA_BUF_NONE`, because this node is the new head of LRU list
    - evicted.next: should be the index of original head of LRU list 
    - evicted.prev.next: if evicted.prev exists, its next should be `DATA_BUF_NONE`, because it's now the new LRU entry
    - evicted.next.prev: should be the index of evicted entry

    The function for choosing the victim entry is `AllocateDataBuf()` defined in `data_buffer.c`.

- `DATA_BUF_HASH_TABLE` / `dataBufHash`:

    The structure `DATA_BUF_HASH_ENTRY` is consist of the following two members:

    - `headEntry`: 16 bits unsigned integer that indicates the first data buffer entry in this bucket
    - `tailEntry`: 16 bits unsigned integer that indicates the last data buffer entry in this bucket

    Because this structure only record the head and tail index, we should keep the relation between the data buffer entries in corresponding data buffer bucket by maintain the `hashPrevEntry` and `hashNextEntry` of those data buffer entries.

    // TODO: how to determine the bucket to put

    just simply the `logicalSliceAddr` % the number of hash buckets:

    ```c
    #define FindDataBufHashTableEntry(logicalSliceAddr) ((logicalSliceAddr) % AVAILABLE_DATA_BUFFER_ENTRY_COUNT)
    ```

    Therefore, if we want to find the data buffer of a given request, we can just traverse the correspoding bucket, instead of traverse the whole data buffer or LRU list. (check the implementation of the function  `CheckDataBufHit` defined in `data_buffer.c`)

- `TEMPORARY_DATA_BUF_MAP` / `tempDataBuf`:

    There are `USER_DIES` entries in this array, namely one temp buffer entry per die, therefore just simply use the `dieNo` to choose the entry of temp data buffer, in current implementation.

    Each temp buffer entry is a `TEMPORARY_DATA_BUF_ENTRY`, and this structure have only one member:

    - `blockingReqTail`:

    TODO: when to use this temp buffer?

- `DATA_BUFFER_BASE_ADDR`

### Requests

- `REQ_POOL` / `reqPool`: The request pool, a fixed-sized 1D array of requests.

    There are `AVAILABLE_OUNTSTANDING_REQ_COUNT` (128 * `USER_DIES` by default. typo, to be fixed) entries in this array. Every entry in the request pool is a `SSD_REQ_FORMAT`, and the structure `SSD_REQ_FORMAT` is consist of:

    - `logicalSliceAddr`: the logical address of this slice command

    - `reqType`: 4 bits flag, the type of this request, should be one of the macros `REQ_TYPE_*` defined in `request_format.h`
    - `reqQueueType`: 4 bits flag, the type of queue where this request current stored
    - `reqCode`: 8 bits flag, the operation code of this request
    - `nvmeCmdSlotTag`: 16 bits flag, <!-- TODO -->

    - `reqOpt`:
    - `dataBufInfo`:
    - `nvmeDmaInfo`:
    - `nandInfo`:

    - `prevReq`: 16 bits unsigned integer, the request pool entry index of the prev request in the same queue
    - `nextReq`: 16 bits unsigned integer, the request pool entry index of the next request in the same queue

        Similar to the two members `prevEntry` and `nextEntry` of the data buffer entry, this two members are used to managing the connection between the entries in the same request queues.

    - `prevBlockingReq` : 16 bits unsigned integer, the request pool entry index of the prev blocking request
    - `nextBlockingReq` : 16 bits unsigned integer, the request pool entry index of the next blocking request

        Similar to the member `blockingReqTail` of data buffer entry, detailed in later section.

        FIXME

    Within this firmware project, the variable `reqSlotTag` is widely used to represent a request pool index, check the `InitReqPool` function implemented in the `request_allocation.c` for example.

- `REQ_QUEUE_TYPE_*` / `*_REQUEST_QUEUE`

    There are 7 macros `REQ_QUEUE_TYPE_*` and corresponding 6 queue structures `*_REQUEST_QUEUE` (no queue for `REQ_QUEUE_TYPE_NONE`). Each request in the request pool belongs one of the 6 queues.

    These 6 queues has exactly same structure and the structure has only 3 members:

    - `headReq`: 16 bits unsigned integer, the request pool index of the head request in this queue
    - `tailReq`: 16 bits unsigned integer, the request pool index of the tail request in this queue
    - `reqCnt`: 16 bits unsigned integer, the number of requests in this requests queue

    But with different meaning and usage:

    - `REQ_QUEUE_TYPE_FREE` / `FREE_REQUEST_QUEUE` / `freeReqQ`:

        a

    - `REQ_QUEUE_TYPE_SLICE` / `SLICE_REQUEST_QUEUE` / `sliceReqQ`:

        a

    - `REQ_QUEUE_TYPE_BLOCKED_BY_BUF_DEP` / `BLOCKED_BY_BUFFER_DEPENDENCY_REQUEST_QUEUE` / `blockedByBufDepReqQ`:
    - `REQ_QUEUE_TYPE_BLOCKED_BY_ROW_ADDR_DEP` / `BLOCKED_BY_ROW_ADDR_DEPENDENCY_REQUEST_QUEUE` / `blockedByRowAddrDepReqQ`:
        
        The data structure of the `blockedByBufDepReqQ` request queue only records the request pool entry index of the first (head) and last (tail) request in the queue, so we need to use `prevBlockingReq` and `nextBlockingReq` of the request pool entry and the `blockingReqTail` of data buffer entry to maintain the blocking queue.

        The member `blockingReqTail` of each data buffer entry used to check the tail blocking request in the queue,
        by calling `UpdateDataBufEntryInfoBlockingReq()` or `UpdateTempDataBufEntryInfoBlockingReq`.

        FIXME

    - `REQ_QUEUE_TYPE_NVME_DMA` / `NVME_DMA_REQUEST_QUEUE` / `nvmeDmaReqQ`:

        a

    - `REQ_QUEUE_TYPE_NAND` / `NAND_REQUEST_QUEUE` / `nandReqQ`:

        The `nandReqQ` is a 2D array with `USER_CHANNELS` rows and `USER_WAYS` cols. Each element of it is a `NAND_REQUEST_QUEUE` that records the following

    FIXME: simplify these queue structures

- `sliceReqQ`
- `LOGICAL_SLICE_MAP` `LOGICAL_SLICE_ENTRY`
- `VIRTUAL_SLICE_MAP` `VIRTUAL_SLICE_ENTRY`
- `NandXXXXList`:
    - `Erase`:
    - `Idle`:
    - `StatusReport`:
    - `Write`:
    - `ReadTrigger`:
    - `ReadTransfer`:

### Flash Memory

- `DIE_STATE_TABLE` / `dieState`:

    A fixed-sized 2D array, each element in this array represents a physical die and contains some infomation of that die.

    Each entry (die) in this table is a instance of `DIE_STATE_ENTRY` which is comprised of the following members:

    - `dieState`: 8 bits unsigned integer, 
    - `reqStatusCheckOpt`: 4 bits unsigned integer, 
    - `prevWay`: 4 bits unsigned integer, the die state entry index of the prev die in the same priority list of its channel
    - `nextWay`: 4 bits unsigned integer, the die state entry index of the next die in the same priority list of its channel

- `WAY_PRIORITY_TABLE` / `wayPriority`:

    A fixed-sized 1D array, each entry in this array represent a status table of each channel. And the status table is managed in the form of the structure `WAY_PRIORITY_ENTRY`.

    Each instance of `WAY_PRIORITY_ENTRY`, namely each channel, manages 7 lists. And each list only records the die state entry index of head and tail way. Therefore, similar to the request queues, the connection between the ways in the same list should be maintained by using the `prevWay` and `nextWay` of `DIE_STATE_ENTRY`.

    - `idleHead` / `idleTail`:

        At the beginning, all the ways on the channel are in this list.

    - `writeHead` / `writeTail`:
    - `eraseHead` / `eraseTail`:
    - `readTriggerHead` / `readTriggerTail`:
    - `readTransferHead` / `readTransferTail`:
    - `statusCheckHead` / `statusCheckTail`:
    - `statusReportHead` / `statusReportTail`:


### GC

- `FbList`

### NVMe

## Configurations

- `XPAR_TIGER4NSC_[0-7]_BASEADDR` defined in `ftl_config.h`
- `#define V2FCommand_NOP                 0`
- `#define V2FCommand_Reset               1`
- `#define V2FCommand_SetFeatures         6`
- `#define V2FCommand_GetFeatures         46`
- `#define V2FCommand_ReadPageTrigger     13` ///< Read data from nand to die register
- `#define V2FCommand_ReadPageTransfer    18` ///< Read data from die register to buffer
- `#define V2FCommand_ProgramPage         28` ///< Recv data from buffer and program to page
- `#define V2FCommand_BlockErase          37` ///< Erase a flash block
- `#define V2FCommand_StatusCheck         41` ///< Check the exec result of previous command
- `#define V2FCommand_ReadPageTransferRaw 55`
