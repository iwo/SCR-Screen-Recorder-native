LOCAL_PATH := $(call my-dir)

MY_SHARED_LIBRARIES := \
    libcutils \
    libEGL \
    libmedia \
    libutils \
    libbinder \
    libgui \
    libui \
    libGLESv2 \

include $(CLEAR_VARS)

LOCAL_MODULE := screenrec-ffmpeg
LOCAL_CFLAGS := -D__STDC_CONSTANT_MACROS -DSCR_SDK_VERSION=$(PLATFORM_SDK_VERSION) -DSCR_FREE

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    ffmpeg_output.cpp \
    capture.cpp \
    main.cpp \

LOCAL_SHARED_LIBRARIES := $(MY_SHARED_LIBRARIES) \
    libz \

LOCAL_STATIC_LIBRARIES := \
    libavdevice-1.2                        \
    libavformat-1.2                        \
    libavfilter-1.2                        \
    libavcodec-1.2                         \
    libswresample-1.2                      \
    libswscale-1.2                         \
    libavutil-1.2                          \


#TODO use vars
LOCAL_C_INCLUDES := \
    external/ffmpeg-1.2.android \
    external/ffmpeg-1.2.android/android/full-userdebug \

LOCAL_LDLIBS += -lm

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_MODULE := screenrec-free
LOCAL_CFLAGS := -DSCR_SDK_VERSION=$(PLATFORM_SDK_VERSION) -DSCR_FREE

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    mediarecorder_output.cpp \
    capture.cpp \
    main.cpp \

LOCAL_SHARED_LIBRARIES := $(MY_SHARED_LIBRARIES)

include $(BUILD_EXECUTABLE)


include $(CLEAR_VARS)

LOCAL_MODULE := screenrec-fb-free
LOCAL_CFLAGS := -DSCR_SDK_VERSION=$(PLATFORM_SDK_VERSION) -DSCR_FREE -DSCR_FB

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    mediarecorder_output.cpp \
    capture.cpp \
    main.cpp \

LOCAL_SHARED_LIBRARIES := $(MY_SHARED_LIBRARIES)

include $(BUILD_EXECUTABLE)


include $(CLEAR_VARS)

LOCAL_MODULE := screenrec-pro
LOCAL_CFLAGS := -DSCR_SDK_VERSION=$(PLATFORM_SDK_VERSION)

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    mediarecorder_output.cpp \
    capture.cpp \
    main.cpp \

LOCAL_SHARED_LIBRARIES := $(MY_SHARED_LIBRARIES)

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_MODULE := screenrec-fb-pro
LOCAL_CFLAGS := -DSCR_SDK_VERSION=$(PLATFORM_SDK_VERSION) -DSCR_FB

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    mediarecorder_output.cpp \
    capture.cpp \
    main.cpp \

LOCAL_SHARED_LIBRARIES := $(MY_SHARED_LIBRARIES)

include $(BUILD_EXECUTABLE)
