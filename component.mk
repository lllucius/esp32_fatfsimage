#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

COMPONENT_OWNBUILDTARGET := 1
COMPONENT_PRIV_INCLUDEDIRS := private

CC = gcc
CXX = g++

CPPFLAGS = -D_XOPEN_SOURCE=500 \
           -D_GNU_SOURCE \
           -DLOG_LOCAL_LEVEL=10 \
           -include stdlib.h

CFLAGS = -O0 -g

CXXFLAGS = $(CFLAGS)

INCLUDES = $(COMPONENT_PATH)/private \
           $(BUILD_DIR_BASE)/include \
           ${IDF_PATH}/components/esp32/include \
           ${IDF_PATH}/components/fatfs/src \
           ${IDF_PATH}/components/log/include \
           ${IDF_PATH}/components/nvs_flash/test_nvs_host \
           ${IDF_PATH}/components/soc/esp32/include \
           ${IDF_PATH}/components/spi_flash/include \
           ${IDF_PATH}/components/wear_levelling/private_include \
           ${IDF_PATH}/components/console/argtable3

OBJS = $(COMPONENT_BUILD_DIR)/fatfsimage.o \
       $(COMPONENT_BUILD_DIR)/WL_Flash.o \
       $(COMPONENT_BUILD_DIR)/crc32.o \
       $(COMPONENT_BUILD_DIR)/crc.o \
       $(COMPONENT_BUILD_DIR)/argtable3.o \
       $(COMPONENT_BUILD_DIR)/ff.o \
       $(COMPONENT_BUILD_DIR)/ffsystem.o \
       $(COMPONENT_BUILD_DIR)/ffunicode.o 

build: $(COMPONENT_BUILD_DIR)/fatfsimage

$(COMPONENT_BUILD_DIR)/fatfsimage.o: $(COMPONENT_PATH)/fatfsimage.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(addprefix -I ,$(INCLUDES)) -c $< -o $@

$(COMPONENT_BUILD_DIR)/WL_Flash.o: ${IDF_PATH}/components/wear_levelling/WL_Flash.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(addprefix -I ,$(INCLUDES)) -c $< -o $@

$(COMPONENT_BUILD_DIR)/crc32.o: ${IDF_PATH}/components/wear_levelling/crc32.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(addprefix -I ,$(INCLUDES)) -c $< -o $@

$(COMPONENT_BUILD_DIR)/crc.o: ${IDF_PATH}/components/nvs_flash/test_nvs_host/crc.cpp
	$(CC) $(CFLAGS) $(CPPFLAGS) $(addprefix -I ,$(INCLUDES)) -c $< -o $@

$(COMPONENT_BUILD_DIR)/argtable3.o: ${IDF_PATH}/components/console/argtable3/argtable3.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(addprefix -I ,$(INCLUDES)) -c $< -o $@

$(COMPONENT_BUILD_DIR)/ff.o: ${IDF_PATH}/components/fatfs/src/ff.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(addprefix -I ,$(INCLUDES)) -c $< -o $@

$(COMPONENT_BUILD_DIR)/ffsystem.o: ${IDF_PATH}/components/fatfs/src/ffsystem.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(addprefix -I ,$(INCLUDES)) -c $< -o $@

$(COMPONENT_BUILD_DIR)/ffunicode.o: ${IDF_PATH}/components/fatfs/src/ffunicode.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(addprefix -I ,$(INCLUDES)) -c $< -o $@

$(COMPONENT_BUILD_DIR)/fatfsimage: $(OBJS)
	$(CXX) $(CFLAGS) $(CPPFLAGS) $(addprefix -I ,$(INCLUDES)) $(OBJS) -o $@ -lc
	# Create dummy archive to satisfy main app build
	echo "!<arch>" >$(COMPONENT_BUILD_DIR)/libfatfsimage.a

.PHONY: build
