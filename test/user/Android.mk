LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES += $(LOCAL_PATH)/../include/
LOCAL_SRC_FILES:= \
	ion_compound_page_test.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libion \
    liblog

LOCAL_MODULE:= test_cpa_user

LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS += -Wall -Werror -Wunused -Wunreachable-code

include $(BUILD_EXECUTABLE)
