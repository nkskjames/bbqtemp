#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := app-template
CFLAGS += -D LOG_LOCAL_LEVEL=ESP_LOG_DEBUG
include $(IDF_PATH)/make/project.mk

