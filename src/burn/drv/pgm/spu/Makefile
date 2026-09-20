ifeq ($(strip $(PS3DEV)),)
$(error PS3DEV is not set)
endif

ifeq ($(strip $(PSL1GHT)),)
$(error PSL1GHT is not set)
endif

SPU_CC := $(PS3DEV)/spu/bin/spu-gcc

TARGET := ps3_sprite_worker_spu.elf
SOURCE := source/main.c
HEADER := generated/ps3_sprite_worker_spu_bin.h

SPU_CFLAGS := \
	-O3 \
	-Wall \
	-mdual-nops \
	-fmodulo-sched \
	-ffunction-sections \
	-fdata-sections \
	-I$(PSL1GHT)/spu/include

SPU_LDFLAGS := \
	-L$(PSL1GHT)/spu/lib \
	-lsputhread

.PHONY: all clean

all: $(HEADER)

$(TARGET): $(SOURCE) ps3_sprite_worker_protocol.h ps3_sprite_color_protocol.h
	$(SPU_CC) \
		$(SPU_CFLAGS) \
		$< \
		$(SPU_LDFLAGS) \
		-Wl,-Map,$@.map \
		-o $@

$(HEADER): $(TARGET) bin2c.py
	python3 bin2c.py $(TARGET) $(HEADER)

clean:
	rm -f \
		$(TARGET) \
		$(TARGET).map \
		$(HEADER)
