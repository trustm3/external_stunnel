LOCAL_PATH := $(call my-dir)/stunnel-5.07/src

include $(CLEAR_VARS)

common_headers = common.h prototypes.h version.h
common_sources = str.c file.c client.c log.c options.c protocol.c network.c
common_sources += resolver.c ssl.c ctx.c verify.c sthreads.c fd.c stunnel.c
unix_sources = pty.c libwrap.c ui_unix.c
shared_sources = env.c

LOCAL_SRC_FILES := $(common_sources) $(unix_sources)

LOCAL_MODULE := stunnel
LOCAL_MODULE_TAGS := optional

LOCAL_C_INCLUDES := \
	external/stunnel \
	external/sqlite/dist \
	external/openssl/include \
	external/zlib

LOCAL_STATIC_LIBRARIES := \
	libssl_static \
        libcrypto_static \
        libcutils \
        liblog \
        libdl-static \
        libc


LOCAL_CFLAGS += -DHAVE_CONFIG_H
#LOCAL_LDFLAGS := -ldl

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT_SBIN)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_SBIN_UNSTRIPPED)

include $(BUILD_EXECUTABLE)
