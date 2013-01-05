LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := screenrec

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    screenrec.cpp \

LOCAL_LDLIBS := \
    -lGLESv2 \

LOCAL_SHARED_LIBRARIES := \
    libcutils \
	libEGL \

include $(BUILD_EXECUTABLE)

