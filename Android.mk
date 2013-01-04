LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := screenrec

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    screenrec.cpp \

include $(BUILD_EXECUTABLE)

