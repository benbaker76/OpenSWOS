LOCAL_PATH := $(call my-dir)
SWOS_ROOT := $(LOCAL_PATH)/../../..
SDL_PATH := $(SWOS_ROOT)/build/android-deps/SDL2-2.32.10
include $(SDL_PATH)/Android.mk
LOCAL_PATH := $(SWOS_ROOT)/android/app/jni
include $(CLEAR_VARS)
LOCAL_MODULE := main
LOCAL_SHORT_COMMANDS := true
LOCAL_SRC_FILES := $(wildcard $(SWOS_ROOT)/src/*.c) $(wildcard $(SWOS_ROOT)/src/generated/*.c)
LOCAL_C_INCLUDES := $(SWOS_ROOT)/include $(SWOS_ROOT)/src $(SDL_PATH)/include
LOCAL_CFLAGS := -std=gnu11 -O2 -fno-strict-aliasing -fno-builtin \
    -include $(SWOS_ROOT)/include/swos_platform.h -D_FILE_OFFSET_BITS=64 \
    -DCOMPETITION_DATA_C=1 -DTACTICS_DATA_C=1 -DFIXTURE_DATA_C=1 \
    -DSIMULATION_DATA_C=1 -DPLAYER_DATA_C=1 -DRENDER_DATA_C=1 \
    -DPALETTE_DATA_C=1 -DSTRINGS_DATA_C=1 -DGENERAL_DATA_C=1 \
    -DSWOS_SAVE_FORMAT_JSON=1 -DSWOS_TEST_RULES=1 \
    -Wno-incompatible-pointer-types -Wno-int-conversion -Wno-pointer-sign \
    -Wno-address-of-packed-member -Wno-initializer-overrides -Wno-unknown-attributes
LOCAL_SHARED_LIBRARIES := SDL2
LOCAL_LDLIBS := -llog -landroid -lm
include $(BUILD_SHARED_LIBRARY)
