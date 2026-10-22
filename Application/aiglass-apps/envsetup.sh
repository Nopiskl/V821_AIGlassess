#!/usr/bin/env bash

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
	echo "请使用 source envsetup.sh（不能直接执行）。" >&2
	exit 2
fi

AIGLASS_ENV_SDK_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
export AIGLASS_BSP_ROOT="${AIGLASS_BSP_ROOT:-$(cd -- "${AIGLASS_ENV_SDK_DIR}/.." && pwd -P)}"
export AIGLASS_CHIP="${AIGLASS_CHIP:-v821}"
export AIGLASS_BOARD="${AIGLASS_BOARD:-aiglass}"
export AIGLASS_BUILD_DIR="${AIGLASS_BUILD_DIR:-${AIGLASS_ENV_SDK_DIR}/build}"
export AIGLASS_OUTPUT_DIR="${AIGLASS_OUTPUT_DIR:-${AIGLASS_ENV_SDK_DIR}/output}"
export AIGLASS_SOURCE_DIR="${AIGLASS_SOURCE_DIR:-${AIGLASS_ENV_SDK_DIR}/apps}"

if ! "${AIGLASS_ENV_SDK_DIR}/scripts/aiglass-build.sh" prepare; then
	unset AIGLASS_ENV_SDK_DIR
	return 1
fi

export AIGLASS_SYSROOT="${AIGLASS_BSP_STAGING:-${AIGLASS_BSP_ROOT}/out/${AIGLASS_CHIP}/${AIGLASS_BOARD}/openwrt/staging_dir/target}"
export AIGLASS_APP_PREFIX="${AIGLASS_BUILD_DIR}/pkg-install"
export AIGLASS_KERNEL_HEADERS="${AIGLASS_KERNEL_HEADERS:-${AIGLASS_BSP_ROOT}/out/${AIGLASS_CHIP}/kernel/build/user_headers}"
export AIGLASS_TOOLCHAIN_DIR="${AIGLASS_TOOLCHAIN_DIR:-${AIGLASS_BSP_ROOT}/out/${AIGLASS_CHIP}/${AIGLASS_BOARD}/openwrt/staging_dir/toolchain}"
export STAGING_DIR="${AIGLASS_SYSROOT}"
export CROSS_COMPILE="${AIGLASS_TOOLCHAIN_DIR}/bin/riscv32-linux-musl-"
export CC="${CROSS_COMPILE}gcc"
export CXX="${CROSS_COMPILE}g++"
export AR="${CROSS_COMPILE}ar"
export STRIP="${CROSS_COMPILE}strip"
export PATH="${AIGLASS_TOOLCHAIN_DIR}/bin:${PATH}"

echo "AIGlass 应用环境已加载：${AIGLASS_CHIP}-${AIGLASS_BOARD}"
echo "源码：${AIGLASS_SOURCE_DIR}"
echo "BSP sysroot（只读依赖）：${AIGLASS_SYSROOT}"
echo "外部应用前缀：${AIGLASS_APP_PREFIX}"
echo "交叉编译器：${CC}"

unset AIGLASS_ENV_SDK_DIR
