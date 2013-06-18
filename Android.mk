LOCAL_PATH := $(call my-dir)

MY_SHARED_LIBRARIES := \
    libcutils \
    libEGL \
    libmedia \
    libutils \
    libgui \
    libGLESv2 \

include $(CLEAR_VARS)

LOCAL_MODULE := screenrec-free
LOCAL_CXXFLAGS := -DFREE

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    screenrec.cpp \

LOCAL_SHARED_LIBRARIES := $(MY_SHARED_LIBRARIES)

include $(BUILD_EXECUTABLE)


include $(CLEAR_VARS)

LOCAL_MODULE := screenrec-fb-free
LOCAL_CXXFLAGS := -DFREE -DFB

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    screenrec.cpp \

LOCAL_SHARED_LIBRARIES := $(MY_SHARED_LIBRARIES)

include $(BUILD_EXECUTABLE)


include $(CLEAR_VARS)

LOCAL_MODULE := screenrec-pro
LOCAL_CXXFLAGS :=

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    screenrec.cpp \

LOCAL_SHARED_LIBRARIES := $(MY_SHARED_LIBRARIES)

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_MODULE := screenrec-fb-pro
LOCAL_CXXFLAGS := -DFB

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
    screenrec.cpp \

LOCAL_SHARED_LIBRARIES := $(MY_SHARED_LIBRARIES)

include $(BUILD_EXECUTABLE)
