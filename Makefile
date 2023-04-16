FW_VERSION  := Toshiba-8c8w
BUILD_DIR   := build
DST         := OpenSSD

CC              = arm-none-eabi-gcc

C_DEFS          = DEBUG HOST_DEBUG
C_MODS          = . nvme
C_DIRS          = bsp $(addprefix $(FW_VERSION)/, $(C_MODS))
C_LIBS          =

C_SRCS          = $(foreach sdir, $(C_DIRS), $(wildcard $(sdir)/*.c))
C_SRCS_NOEXT    = $(patsubst %.c, %, $(C_SRCS))
C_OBJS          = $(foreach sp, $(C_SRCS_NOEXT), $(BUILD_DIR)/$(basename $(notdir $(sp))).o)

C_DEFINES       = $(addprefix -D, $(C_DEFS))
C_INCLUDES      = $(addprefix -I, $(C_DIRS))
C_FLAGS         = -g -std=c99 -march=armv7-a $(C_DEFINES) $(C_INCLUDES)


LD              = $(CC)
LD_FLAGS        = --specs=nosys.specs

all: $(C_SRCS_NOEXT)
	$(LD) $(LD_FLAGS) -o $(DST) $(C_OBJS) $(addprefix -l, $(C_LIBS))

$(C_SRCS_NOEXT): %: %.c
	$(shell mkdir -p $(BUILD_DIR))
	$(CC) $(C_FLAGS) -o $(BUILD_DIR)/$(basename $(notdir $@)).o -c $<

clean:
	rm -f $(C_OBJS) $(DST)