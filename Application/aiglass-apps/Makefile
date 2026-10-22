SHELL := /bin/bash

.DEFAULT_GOAL := help

AIGLASS_SDK_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
AIGLASS_BSP_ROOT ?= $(abspath $(AIGLASS_SDK_DIR)/..)
AIGLASS_CHIP ?= v821
AIGLASS_BOARD ?= aiglass
AIGLASS_JOBS ?= $(shell nproc 2>/dev/null || echo 4)
AIGLASS_BUILD_DIR ?= $(AIGLASS_SDK_DIR)/build
AIGLASS_OUTPUT_DIR ?= $(AIGLASS_SDK_DIR)/output
AIGLASS_SOURCE_DIR ?= $(AIGLASS_SDK_DIR)/apps
AIGLASS_DIST_DIR ?= $(AIGLASS_SDK_DIR)/dist
AIGLASS_RELEASE_VERSION ?= $(strip $(shell sed -n '1p' $(AIGLASS_SDK_DIR)/VERSION))

-include $(AIGLASS_SDK_DIR)/local.mk

export AIGLASS_BSP_ROOT
export AIGLASS_CHIP
export AIGLASS_BOARD
export AIGLASS_JOBS
export AIGLASS_BUILD_DIR
export AIGLASS_OUTPUT_DIR
export AIGLASS_SOURCE_DIR
export AIGLASS_DIST_DIR
export AIGLASS_RELEASE_VERSION
export AIGLASS_BSP_OUT
export AIGLASS_BSP_STAGING
export AIGLASS_TOOLCHAIN_DIR
export AIGLASS_KERNEL_HEADERS
export AIGLASS_CFLAGS
export AIGLASS_CXXFLAGS
export AIGLASS_LDFLAGS
export AIGLASS_STRIP_OUTPUT

AIGLASS_BUILD_TOOL := $(AIGLASS_SDK_DIR)/scripts/aiglass-build.sh
AIGLASS_BSP_TOOL := $(AIGLASS_SDK_DIR)/scripts/bsp-package.sh
AIGLASS_RELEASE_TOOL := $(AIGLASS_SDK_DIR)/scripts/release.sh
AIGLASS_COMPONENTS := lib normal photo audio download ota video livestream

.PHONY: help list paths doctor check prepare all package install release source-package \
	release-clean clean distclean bsp-prepare bsp-deps bsp-package $(AIGLASS_COMPONENTS)

help:
	@echo "AIGlass 应用开发环境（v821）"
	@echo
	@echo "首次准备 AIGlass BSP 依赖："
	@echo "  make bsp-prepare       切换到 $(AIGLASS_CHIP)-$(AIGLASS_BOARD) 并只构建所需 BSP 依赖"
	@echo
	@echo "日常应用编译："
	@echo "  make doctor           检查基础工具链和 sysroot"
	@echo "  make all              编译 libaglink 和全部 7 个应用"
	@echo "  make video            只编译 libaglink + ai_glass_video"
	@echo "  make normal           只编译 libaglink + ai_glass_normal"
	@echo "  make package          编译全部并生成二进制 rootfs overlay"
	@echo "  make install DESTDIR=/path/to/rootfs"
	@echo "  make release          生成可单独发布的源码补丁包（不包含 BSP 链接）"
	@echo
	@echo "其他："
	@echo "  make list             列出组件"
	@echo "  make paths            显示当前使用的源码、sysroot 和输出路径"
	@echo "  make bsp-deps         当前已是 $(AIGLASS_BOARD) 配置时构建 BSP 依赖"
	@echo "  make bsp-package      bsp-deps 的兼容别名，不编译 BSP 内 demo_ai_glass"
	@echo "  make release-clean    清理源码发布归档"
	@echo "  make clean            清理私有编译目录，保留 output"
	@echo "  make distclean        清理私有编译目录和 output"
	@echo
	@echo "可覆盖变量：AIGLASS_BSP_ROOT、AIGLASS_BOARD、AIGLASS_JOBS、"
	@echo "            AIGLASS_BUILD_DIR、AIGLASS_OUTPUT_DIR、AIGLASS_DIST_DIR"

list:
	@$(AIGLASS_BUILD_TOOL) list

paths:
	@$(AIGLASS_BUILD_TOOL) paths

doctor:
	@$(AIGLASS_BUILD_TOOL) doctor

check:
	@$(AIGLASS_BUILD_TOOL) doctor all

prepare:
	@$(AIGLASS_BUILD_TOOL) prepare

all:
	@$(AIGLASS_BUILD_TOOL) build all

$(AIGLASS_COMPONENTS):
	@$(AIGLASS_BUILD_TOOL) build $@

package:
	@$(AIGLASS_BUILD_TOOL) package

release source-package:
	@$(AIGLASS_RELEASE_TOOL) create

install:
	@if [[ -z "$(DESTDIR)" ]]; then \
		echo "错误：请指定 DESTDIR，例如 make install DESTDIR=/tmp/aiglass-rootfs" >&2; \
		exit 2; \
	fi
	@$(AIGLASS_BUILD_TOOL) install "$(DESTDIR)"

bsp-prepare:
	@$(AIGLASS_BSP_TOOL) --switch

bsp-deps bsp-package:
	@$(AIGLASS_BSP_TOOL) --current

release-clean:
	@$(AIGLASS_RELEASE_TOOL) clean

clean:
	@$(AIGLASS_BUILD_TOOL) clean

distclean:
	@$(AIGLASS_BUILD_TOOL) distclean
