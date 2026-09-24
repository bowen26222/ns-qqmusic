#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
APP_TITLE	:=	QQMusic

TARGET		:=	qqmusic-overlay
BUILD		:=	build
SOURCES		:=	source source/gui source/elm source/core ../common/ipc ../common/config ../common/minIni ../common/sdmc ../common/pm ../third_party/stb ../common/qr
DATA		:=
INCLUDES	:=	../common/ipc ../common source/gui source/elm source/core ../third_party/stb ../common/qr
APP_VERSION	:=	0.1.0
API_VERSION	:=	3
APP_AUTHOR	:=	ns-qqmusic
NO_ICON		:= 1

# libultrahand（libtesla + libultra）— 追加自身 SOURCES/INCLUDES
include $(TOPDIR)/lib/ultrahand.mk

ARCH	:=	-march=armv8-a+simd+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -Wno-format-truncation -O2 -ffunction-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -DVERSION=\"v$(APP_VERSION)\" -DQQMUSIC_API_VERSION=$(API_VERSION) -DNDEBUG=1

# libultrahand：per-overlay 配置目录
# 音频格式（browser 白名单 + player 解码）
CFLAGS	+=	-DWANT_MP3 -DWANT_FLAC -DWANT_WAV -DIS_STATUS_MONITOR_DIRECTIVE=1

CXXFLAGS	:= $(CFLAGS) -fno-exceptions -fno-rtti -std=c++20

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lmbedtls -lmbedx509 -lmbedcrypto -lnx

#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -fr $(BUILD) $(TARGET).ovl $(TARGET).nro $(TARGET).nacp $(TARGET).elf

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

all	:	$(OUTPUT).ovl
# Ultrahand 菜单要求 ovl 内嵌 NACP（getOverlayInfo 解析失败会被静默跳过）；
# elf2nro 之后追加 "ULTR" 识别标记（WerWolv ovlloader 用 "WLOV"）。
$(OUTPUT).ovl	:	$(OUTPUT).elf $(OUTPUT).nacp
	@elf2nro $< $@ $(NROFLAGS) --nacp=$(OUTPUT).nacp
	@printf 'ULTR' >> $@
	@echo "built ... $(notdir $(OUTPUT).ovl)"

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	:	$(HFILES_BIN)

%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif