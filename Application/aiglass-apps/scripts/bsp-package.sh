#!/usr/bin/env bash

set -eo pipefail

AIGLASS_SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
AIGLASS_SDK_DIR="$(cd -- "${AIGLASS_SCRIPT_DIR}/.." && pwd -P)"
AIGLASS_BSP_ROOT="${AIGLASS_BSP_ROOT:-$(cd -- "${AIGLASS_SDK_DIR}/.." && pwd -P)}"
AIGLASS_CHIP="${AIGLASS_CHIP:-v821}"
AIGLASS_BOARD="${AIGLASS_BOARD:-aiglass}"
AIGLASS_JOBS="${AIGLASS_JOBS:-$(nproc 2>/dev/null || echo 4)}"
AIGLASS_MODE="${1:---current}"

[[ -f "${AIGLASS_BSP_ROOT}/build/envsetup.sh" ]] || {
	echo "错误：找不到 BSP envsetup.sh：${AIGLASS_BSP_ROOT}/build/envsetup.sh" >&2
	exit 1
}

cd -- "${AIGLASS_BSP_ROOT}"

# Tina 的 envsetup 包含可能返回非零的 unalias/探测命令；加载期间临时关闭
# errexit。这里不启用 nounset，因为后续 lunch/make 函数也会读取可选变量。
set +e
# shellcheck disable=SC1091
source "${AIGLASS_BSP_ROOT}/build/envsetup.sh"
AIGLASS_ENVSETUP_STATUS=$?
set -e
if (( AIGLASS_ENVSETUP_STATUS != 0 )); then
	echo "错误：加载 BSP envsetup.sh 失败（${AIGLASS_ENVSETUP_STATUS}）" >&2
	exit "${AIGLASS_ENVSETUP_STATUS}"
fi

case "${AIGLASS_MODE}" in
	--switch)
		echo "[aiglass] 切换 BSP 配置到 ${AIGLASS_CHIP}-${AIGLASS_BOARD}-tina"
		echo "[aiglass] 注意：lunch 会更新 BSP 根目录的 .buildconfig 和 OpenWrt .config。"
		lunch "${AIGLASS_CHIP}-${AIGLASS_BOARD}-tina"
		;;
	--current)
		AIGLASS_CURRENT_CHIP="$(sed -n 's/^export LICHEE_IC=//p' .buildconfig | tail -n 1)"
		AIGLASS_CURRENT_BOARD="$(sed -n 's/^export LICHEE_BOARD=//p' .buildconfig | tail -n 1)"
		if [[ "${AIGLASS_CURRENT_CHIP}" != "${AIGLASS_CHIP}" || "${AIGLASS_CURRENT_BOARD}" != "${AIGLASS_BOARD}" ]]; then
			echo "错误：当前 BSP 是 ${AIGLASS_CURRENT_CHIP}-${AIGLASS_CURRENT_BOARD}，不是 ${AIGLASS_CHIP}-${AIGLASS_BOARD}。" >&2
			echo "请先运行 make bsp-prepare，或手动 lunch ${AIGLASS_CHIP}-${AIGLASS_BOARD}-tina。" >&2
			exit 1
		fi
		;;
	*)
		echo "错误：未知参数 ${AIGLASS_MODE}" >&2
		exit 2
		;;
esac

echo "[aiglass] 仅使用 BSP 原生规则构建外部应用所需的工具链/开发库"
echo "[aiglass] 不构建 BSP 内的 demo_ai_glass；主程序以 ${AIGLASS_SDK_DIR}/apps 为准"
AIGLASS_DEPENDENCY_TARGETS=(
	package/subpackage/allwinner/wireless/common/compile
	package/subpackage/allwinner/wireless/wifimanager/compile
	package/subpackage/allwinner/common/libawion/compile
	package/subpackage/allwinner/multimedia/tina_multimedia/libcedarc/compile
	package/subpackage/allwinner/vision/person_detection/compile
	package/subpackage/allwinner/vision/motion_detection/compile
	package/subpackage/allwinner/multimedia/rt_media/compile
	package/subpackage/allwinner/eyesee-mpp/system/public/compile
	package/subpackage/allwinner/eyesee-mpp/external/compile
	package/subpackage/allwinner/eyesee-mpp/middleware/compile
	package/subpackage/allwinner/eyesee-mpp/private/rtsp/compile
)
make openwrt_rootfs "${AIGLASS_DEPENDENCY_TARGETS[@]}" "-j${AIGLASS_JOBS}" V=s

echo "[aiglass] BSP sysroot 已就绪：${AIGLASS_BSP_ROOT}/out/${AIGLASS_CHIP}/${AIGLASS_BOARD}/openwrt/staging_dir/target"
echo "[aiglass] 后续可在 ${AIGLASS_SDK_DIR} 中执行 make all；BSP 内应用源码不会参与编译。"
