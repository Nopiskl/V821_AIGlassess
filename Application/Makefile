SHELL := /bin/bash

.DEFAULT_GOAL := help

AIGLASS_APPLICATION_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
AIGLASS_APPS_DIR := $(AIGLASS_APPLICATION_DIR)/aiglass-apps
# The repository contains SDK patches. Point this at a complete Tina BSP to build.
AIGLASS_BSP_ROOT ?= $(shell cd "$(AIGLASS_APPLICATION_DIR)/../SDK/TinaLinux 5.0" && pwd -P)
AIGLASS_BOARD ?= aiglass_fastboot

export AIGLASS_BSP_ROOT
export AIGLASS_BOARD

AIGLASS_TARGETS := help list paths doctor check prepare all lib media normal photo audio \
	download ota video livestream package install release source-package \
	release-clean clean distclean bsp-prepare bsp-deps bsp-package

.PHONY: $(AIGLASS_TARGETS)

$(AIGLASS_TARGETS):
	+@$(MAKE) -C "$(AIGLASS_APPS_DIR)" $@

.PHONY: local-media
local-media:
	+@$(MAKE) -C "$(AIGLASS_APPLICATION_DIR)/mpp-media" all
