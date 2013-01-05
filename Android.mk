LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := screenrec

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    screenrec.cpp \

LOCAL_LDLIBS := \

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libEGL \
    libmedia \
    libutils \
    libgui \
    libGLESv2 \

include $(BUILD_EXECUTABLE)

