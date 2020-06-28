
-include .config
-include version

ifneq ($(CONFIG_ARCH_NAME),) 
	ARCH=$(CONFIG_ARCH_NAME)
endif

ifneq ($(CROSS_COMPILE),) 
	CROSS_COMPILE_PREFIX=$(TOOLCHAIN)/$(CROSS_COMPILE)
endif

CC:=$(CROSS_COMPILE_PREFIX)gcc
RM:=rm
CP:=cp
LD:=$(CROSS_COMPILE_PREFIX)ld
CXX:=$(CROSS_COMPILE_PREFIX)g++
PROJECT_ROOT=$(CURDIR)
PYTHON:=python
PIP:=pip
AR:=$(CROSS_COMPILE_PREFIX)ar
MKDIR:=mkdir
TOOL_DIR=$(PROJECT_ROOT)/tools
CONFIG_DIR=$(PROJECT_ROOT)/config

DEF-y+=$(DEF) MAJOR=$(MAJOR) MINOR=$(MINOR) PATCH=$(PATCH)
DEFS=$(DEF-y:%=-D%)

DBG_CACHE_DIR:=Debug
REL_CACHE_DIR:=Release

DBG_TARGET_OUTPUT_DIR=$(DBG_CACHE_DIR)/out
REL_TARGET_OUTPUT_DIR=$(REL_CACHE_DIR)/out

ARM_OPTION= -pipe -march=armv7-a -mtune=cortex-a7 -mfpu=neon -fno-caller-saves -Wno-unused-result -mfloat-abi=hard 
# -D_GLIBCXX_USE_CXX11_ABI=0 is essential to generate .so whose symbol names (mangled name) are compatible to nugu_albert_app
ALBERT_OPTION = -pthread -D_GLIBCXX_USE_CXX11_ABI=0 -Wformat -Werror=format-security -D_FORTIFY_SOURCE=1 -fstack-protector -Wl,-z,now -Wl,-z,relro 

DBG_CFLAG = -O0 -g3 -fmessage-length=0  $(CFLAG) -D__DBG $(VERSION) $(DEFS) $(ALBERT_OPTION)
REL_CFLAG = -O2 -g0 -fmessage-length=0  $(CFLAG) $(VERSION) $(DEFS) $(ALBERT_OPTION)

ifneq ($(CROSS_COMPILE),) 
	DBG_CFLAG += $(ARM_OPTION)
	REL_CFLAG += $(ARM_OPTION)
endif

DYNAMIC_FLAG = -fPIC

TEST_TARGET:=gst_aplay_dbg_$(MAJOR).$(MINOR).$(PATCH)
REL_TARGET:=gst_aplay_$(MAJOR).$(MINOR).$(PATCH)

REL_STATIC_TARGET=$(REL_TARGET_OUTPUT_DIR)/libgstaply.a
REL_DYNAMIC_TARGET=$(REL_TARGET_OUTPUT_DIR)/libgstaply.so

DBG_STATIC_TARGET=$(DBG_TARGET_OUTPUT_DIR)/libgstaply.a
DBG_DYNAMIC_TARGET=$(DBG_TARGET_OUTPUT_DIR)/libgstaply.so


VPATH=$(SRC-y)
INCS=$(INC-y:%=-I%)
LIB_DIR=$(LDIR-y:%=-L%)
LIBS=$(LIB-y:%=-l%)

DBG_OBJS=$(OBJ-y:%=$(DBG_CACHE_DIR)/%.do)
REL_OBJS=$(OBJ-y:%=$(REL_CACHE_DIR)/%.o)
TOBJ:=main.o

DBG_SH_OBJS=$(OBJ-y:%=$(DBG_CACHE_DIR)/%.s.do)
REL_SH_OBJS=$(OBJ-y:%=$(REL_CACHE_DIR)/%.s.o)


SILENT+= $(REL_STATIC_TARGET) $(REL_DYNAMIC_TARGET) $(REL_OBJS)
SILENT+= $(DBG_STATIC_TARGET) $(DBG_DYNAMIC_TARGET) $(DBG_OBJS)
SILENT+= $(DBG_SH_OBJS) $(REL_SH_OBJS)
SILENT+= $(TEST_TARGET) $(TOBJ) $(REL_TARGET)

CONFIG_PY:=$(TOOL_DIR)/jconfigpy
export STAGING_DIR=./


.SILENT : $(SILENT) clean reset install 

PHONY+= all debug release clean test

test: $(TEST_TARGET)

dist: $(REL_TARGET)

install: release
	@echo 'install ... $(REL_DYNAMIC_TARGET) into /usr/lib/'
	$(CP) $(REL_DYNAMIC_TARGET) /usr/lib/


all: debug release

debug: $(DBG_CACHE_DIR) $(DBG_STATIC_TARGET) 

release: $(REL_CACHE_DIR) $(REL_STATIC_TARGET) 

reset : clean
	rm -f .config

clean : 
	rm -rf $(DBG_CACHE_DIR) $(DBG_STATIC_TARGET) $(DBG_DYNAMIC_TARGET)\
			$(REL_CACHE_DIR) $(REL_STATIC_TARGET) $(REL_DYNAMIC_TARGET)\
			$(TEST_TARGET) $(REL_SH_OBJS) $(DBG_SH_OBJS) $(TEST_TARGET) $(REL_TARGET) $(TOBJ) \
			gst_aplay_*


ifeq ($(DEFCONF),) 
config : $(CONFIG_PY) $(TOOL_DIR)
	$(PYTHON) $(CONFIG_PY) -c -i config.json
else
config : $(CONFIG_PY) $(TOOL_DIR) $(CONFIG_DIR)
	@echo 'config path $(CONFIG_DIR)'
	$(PYTHON) $(CONFIG_PY) -s -i $(CONFIG_DIR)/$(DEFCONF)_config -t ./config.json -o ./.config
endif

$(TEST_TARGET) : $(TOBJ) debug
	@echo 'Compile Test Executable ... $(CXX) $@'
	$(CXX)  -o $@ $(DBG_CFLAG) $< $(INCS) $(LIB_DIR) -L./Debug/out -l:libgstaply.a $(LIBS) 

$(REL_TARGET) : $(TOBJ) release
	@echo 'Compile Test Executable ... $(CXX) $@'
	$(CXX)  -o $@ $(REL_CFLAG) $< $(INCS) $(LIB_DIR) -L./Release/out -l:libgstaply.a $(LIBS)


%.o : %.cc
	$(CXX) -c -o $@ $(DBG_CFLAG) $< $(INCS) $(LIB_DIR) -L./Debug/out -l:libgstaply.a $(LIBS)


%.o : %.c
	$(CXX) -c -o $@ $(DBG_CFLAG) $< $(INCS) $(LIB_DIR) -L./Debug/out $(LIBS) -l:libgstaply.a

$(DBG_CACHE_DIR)/%.do : %.c
	@echo '$(ARCH) debug compile ... $@'
	$(CXX) -c -o $@ $(DBG_CFLAG) $< $(INCS)  $(LIB_DIR) $(LIBS)

$(REL_CACHE_DIR)/%.o : %.c
	@echo '$(ARCH) release compile .... $@'
	$(CXX) -c -o $@ $(REL_CFLAG) $< $(INCS)  $(LIB_DIR) $(LIBS)
 
$(DBG_CACHE_DIR)/%.s.do : %.c
	@echo '$(ARCH) debug compile (for .so).... $@'
	$(CXX) -c -o $@ $(DBG_CFLAG)  $< $(INCS) $(DYNAMIC_FLAG)  $(LIB_DIR) $(LIBS)

$(REL_CACHE_DIR)/%.o : %.c
	@echo '$(ARCH) release compile .... $@'
	$(CXX) -c -o $@ $(REL_CFLAG)  $< $(INCS)  $(LIB_DIR) $(LIBS)

$(REL_CACHE_DIR)/%.s.o : %.c
	@echo '$(ARCH) release compile (for .so)... $@'
	$(CXX) -c -o $@ $(REL_CFLAG)  $< $(INCS) $(DYNAMIC_FLAG) $(LIB_DIR) $(LIBS)

	
## --system option is used to workaround ubuntu / debian pip bug https://github.com/pypa/pip/issues/3826
$(CONFIG_PY):
	$(PIP) install jconfigpy -t  $(TOOL_DIR)

$(DBG_CACHE_DIR) $(REL_CACHE_DIR) $(TOOL_DIR) $(CONFIG_DIR) $(DBG_TARGET_OUTPUT_DIR) $(REL_TARGET_OUTPUT_DIR) :
	$(MKDIR) $@

$(DBG_DYNAMIC_TARGET) : $(DBG_SH_OBJS) $(DBG_TARGET_OUTPUT_DIR)
	@echo 'Generating shared library file for $(ARCH) .... $@'
	$(CXX) -o $@ -shared $(DBG_CFLAG) $(DYNAMIC_FLAG) $(DBG_SH_OBJS) $(LIB_DIR) $(LIBS)

$(DBG_STATIC_TARGET) : $(DBG_OBJS) $(DBG_TARGET_OUTPUT_DIR)
	@echo 'Generating Archive File for $(ARCH) ....$@'
	$(AR) rcs  $@  $(DBG_OBJS) 

$(REL_DYNAMIC_TARGET) : $(REL_SH_OBJS) $(REL_TARGET_OUTPUT_DIR)
	@echo 'Generating Share Library File for $(ARCH) .... $@'
	$(CXX) -o $@ -shared $(REL_CFLAG) $(DYNAMIC_FLAG) $(REL_SH_OBJS) $(LIB_DIR) $(LIBS)


$(REL_STATIC_TARGET) : $(REL_OBJS) $(REL_TARGET_OUTPUT_DIR)
	@echo 'Generating Archive File for $(ARCH) ....$@'
	$(AR) rcs  $@ $(REL_OBJS) 