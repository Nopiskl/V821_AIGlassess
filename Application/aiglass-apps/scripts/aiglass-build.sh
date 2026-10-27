#!/usr/bin/env bash

set -euo pipefail

AIGLASS_SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
AIGLASS_SDK_DIR="$(cd -- "${AIGLASS_SCRIPT_DIR}/.." && pwd -P)"
AIGLASS_BSP_ROOT="${AIGLASS_BSP_ROOT:-$(cd -- "${AIGLASS_SDK_DIR}/.." && pwd -P)}"
AIGLASS_CHIP="${AIGLASS_CHIP:-v821}"
AIGLASS_BOARD="${AIGLASS_BOARD:-aiglass}"
AIGLASS_JOBS="${AIGLASS_JOBS:-$(nproc 2>/dev/null || echo 4)}"
AIGLASS_BUILD_DIR="${AIGLASS_BUILD_DIR:-${AIGLASS_SDK_DIR}/build}"
AIGLASS_OUTPUT_DIR="${AIGLASS_OUTPUT_DIR:-${AIGLASS_SDK_DIR}/output}"
AIGLASS_SOURCE_DIR="${AIGLASS_SOURCE_DIR:-${AIGLASS_SDK_DIR}/apps}"

AIGLASS_BSP_OUT="${AIGLASS_BSP_OUT:-${AIGLASS_BSP_ROOT}/out/${AIGLASS_CHIP}/${AIGLASS_BOARD}/openwrt}"
AIGLASS_BSP_STAGING="${AIGLASS_BSP_STAGING:-${AIGLASS_BSP_OUT}/staging_dir/target}"
AIGLASS_TOOLCHAIN_DIR="${AIGLASS_TOOLCHAIN_DIR:-${AIGLASS_BSP_OUT}/staging_dir/toolchain}"
AIGLASS_KERNEL_HEADERS="${AIGLASS_KERNEL_HEADERS:-${AIGLASS_BSP_ROOT}/out/${AIGLASS_CHIP}/kernel/build/user_headers}"
AIGLASS_WORK_SOURCE="${AIGLASS_BUILD_DIR}/source"
AIGLASS_PACKAGE_PREFIX="${AIGLASS_BUILD_DIR}/pkg-install"
AIGLASS_ROOTFS="${AIGLASS_OUTPUT_DIR}/rootfs"
AIGLASS_CC="${AIGLASS_TOOLCHAIN_DIR}/bin/riscv32-linux-musl-gcc"
AIGLASS_CXX="${AIGLASS_TOOLCHAIN_DIR}/bin/riscv32-linux-musl-g++"
AIGLASS_AR="${AIGLASS_TOOLCHAIN_DIR}/bin/riscv32-linux-musl-ar"
AIGLASS_STRIP="${AIGLASS_TOOLCHAIN_DIR}/bin/riscv32-linux-musl-strip"

AIGLASS_CFLAGS_DEFAULT="-Os -pipe -fno-caller-saves -fno-plt -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -funwind-tables -Wno-unused-but-set-variable -Wno-unused-variable -Wno-unused-function -Wno-unused-label -Wno-unused-const-variable -Wno-comment -Wno-unused-value -DTINA_5_0 -DOS_NET_LINUX_OS"
AIGLASS_CXXFLAGS_DEFAULT="${AIGLASS_CFLAGS_DEFAULT} -std=c++11 -fno-rtti"
AIGLASS_LDFLAGS_DEFAULT="-Wl,-rpath=/usr/lib"
AIGLASS_CFLAGS="${AIGLASS_CFLAGS:-${AIGLASS_CFLAGS_DEFAULT}}"
AIGLASS_CXXFLAGS="${AIGLASS_CXXFLAGS:-${AIGLASS_CXXFLAGS_DEFAULT}}"
AIGLASS_LDFLAGS="${AIGLASS_LDFLAGS:-${AIGLASS_LDFLAGS_DEFAULT}}"

AIGLASS_ALL_COMPONENTS=(normal photo audio download ota video livestream)
AIGLASS_FAILURES=0

info() {
	printf '[aiglass] %s\n' "$*"
}

warn() {
	printf '[aiglass] 警告：%s\n' "$*" >&2
}

die() {
	printf '[aiglass] 错误：%s\n' "$*" >&2
	exit 1
}

mark_generated_dir() {
	local aiglass_dir="$1"
	if [[ -d "${aiglass_dir}" && ! -f "${aiglass_dir}/.aiglass-apps-generated" ]]; then
		if [[ -n "$(find "${aiglass_dir}" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
			die "目录非空且不是本工具生成，拒绝接管：${aiglass_dir}"
		fi
	fi
	mkdir -p -- "${aiglass_dir}"
	: > "${aiglass_dir}/.aiglass-apps-generated"
}

ensure_target_identity() {
	local aiglass_dir="$1"
	local aiglass_identity_file="${aiglass_dir}/.aiglass-target"
	local aiglass_expected_identity="${AIGLASS_CHIP}-${AIGLASS_BOARD}"
	local aiglass_existing_identity=""
	if [[ -f "${aiglass_identity_file}" ]]; then
		aiglass_existing_identity="$(<"${aiglass_identity_file}")"
	fi
	if [[ -n "${aiglass_existing_identity}" && "${aiglass_existing_identity}" != "${aiglass_expected_identity}" ]]; then
		die "目录属于 ${aiglass_existing_identity}，不能用于 ${aiglass_expected_identity}：${aiglass_dir}；请先 clean/distclean 或改用独立目录"
	fi
	printf '%s\n' "${aiglass_expected_identity}" > "${aiglass_identity_file}"
}

remove_generated_dir() {
	local aiglass_dir="$1"
	local aiglass_label="$2"
	if [[ ! -e "${aiglass_dir}" ]]; then
		return 0
	fi
	if [[ ! -f "${aiglass_dir}/.aiglass-apps-generated" ]]; then
		die "拒绝删除未带生成标记的${aiglass_label}：${aiglass_dir}"
	fi
	case "${aiglass_dir}" in
		/|"${AIGLASS_BSP_ROOT}"|"${AIGLASS_SDK_DIR}")
			die "拒绝删除危险路径：${aiglass_dir}"
			;;
	esac
	rm -rf -- "${aiglass_dir}"
}

require_path() {
	local aiglass_path="$1"
	local aiglass_description="$2"
	if [[ -e "${aiglass_path}" ]]; then
		printf '  [OK]   %-24s %s\n' "${aiglass_description}" "${aiglass_path}"
	else
		printf '  [缺失] %-24s %s\n' "${aiglass_description}" "${aiglass_path}" >&2
		AIGLASS_FAILURES=$((AIGLASS_FAILURES + 1))
	fi
}

require_glob() {
	local aiglass_pattern="$1"
	local aiglass_description="$2"
	if compgen -G "${aiglass_pattern}" >/dev/null; then
		printf '  [OK]   %-24s %s\n' "${aiglass_description}" "${aiglass_pattern}"
	else
		printf '  [缺失] %-24s %s\n' "${aiglass_description}" "${aiglass_pattern}" >&2
		AIGLASS_FAILURES=$((AIGLASS_FAILURES + 1))
	fi
}

doctor_base() {
	AIGLASS_FAILURES=0
	info "检查 ${AIGLASS_CHIP}-${AIGLASS_BOARD} 应用编译基础环境"
	if [[ ! -d "${AIGLASS_SOURCE_DIR}" ]]; then
		printf '  [缺失] %-24s %s\n' "独立应用源码目录" "${AIGLASS_SOURCE_DIR}" >&2
		AIGLASS_FAILURES=$((AIGLASS_FAILURES + 1))
	elif [[ -L "${AIGLASS_SOURCE_DIR}" ]] || [[ -n "$(find "${AIGLASS_SOURCE_DIR}" -type l -print -quit 2>/dev/null)" ]]; then
		printf '  [链接] %-24s %s\n' "应用源码必须是实体文件" "${AIGLASS_SOURCE_DIR}" >&2
		AIGLASS_FAILURES=$((AIGLASS_FAILURES + 1))
	else
		printf '  [OK]   %-24s %s\n' "应用源码为独立副本" "${AIGLASS_SOURCE_DIR}"
	fi
	require_path "${AIGLASS_SOURCE_DIR}/src/Makefile" "AIGlass 应用源码"
	require_path "${AIGLASS_SOURCE_DIR}/src/aglink/libaglink.a" "AGLink 预编译静态库"
	require_path "${AIGLASS_BSP_STAGING}/usr/include" "BSP sysroot 头文件"
	require_path "${AIGLASS_BSP_STAGING}/usr/lib" "BSP sysroot 库"
	require_path "${AIGLASS_CC}" "RISC-V musl C 编译器"
	require_path "${AIGLASS_CXX}" "RISC-V musl C++ 编译器"
	require_path "${AIGLASS_KERNEL_HEADERS}/include/bsp/aglink/ag_proto.h" "BSP AGLink UAPI"
	require_path "${AIGLASS_BSP_STAGING}/usr/lib/libwirelesscom.so" "wirelesscommon"
}

doctor_component() {
	local aiglass_component="$1"
	case "${aiglass_component}" in
		lib|normal|photo|audio)
			;;
		download|ota)
			require_path "${AIGLASS_BSP_STAGING}/usr/include/wifimg.h" "Wi-Fi Manager 头文件"
			require_glob "${AIGLASS_BSP_STAGING}/lib/libwifimg-v2.0.so*" "Wi-Fi Manager 库"
			;;
		video|media)
			require_path "${AIGLASS_BSP_STAGING}/usr/include/rt_media/AW_VideoInput_API.h" "RT Media 头文件"
			require_glob "${AIGLASS_BSP_STAGING}/usr/lib/rt_media/librt_media.*" "RT Media 库"
			require_path "${AIGLASS_BSP_STAGING}/usr/include/eyesee-mpp/middleware/config/mpp_config.mk" "MPP 配置"
			require_glob "${AIGLASS_BSP_STAGING}/usr/lib/eyesee-mpp/libaw_mpp.*" "MPP 库"
			;;
		livestream)
			require_path "${AIGLASS_BSP_STAGING}/usr/include/eyesee-mpp/middleware/config/mpp_config.mk" "MPP 配置"
			require_path "${AIGLASS_BSP_STAGING}/usr/include/wifimg.h" "Wi-Fi Manager 头文件"
			require_glob "${AIGLASS_BSP_STAGING}/lib/libwifimg-v2.0.so*" "Wi-Fi Manager 库"
			require_path "${AIGLASS_BSP_STAGING}/usr/include/rt_media/AW_VideoInput_API.h" "RT Media 头文件"
			require_glob "${AIGLASS_BSP_STAGING}/usr/lib/rt_media/librt_media.*" "RT Media 库"
			require_path "${AIGLASS_BSP_STAGING}/usr/include/eyesee-mpp/system/private/rtsp/IPCProgram/interface/TinyServer.h" "RTSP 头文件"
			require_glob "${AIGLASS_BSP_STAGING}/usr/lib/eyesee-mpp/libTinyServer.*" "RTSP 库"
			require_glob "${AIGLASS_BSP_STAGING}/usr/lib/pdet/librt_aiservice_pdet.*" "人员检测库"
			;;
		all)
			doctor_component video
			doctor_component livestream
			;;
		*)
			die "未知组件：${aiglass_component}"
			;;
	esac
}

run_doctor() {
	local aiglass_component="${1:-lib}"
	doctor_base
	doctor_component "${aiglass_component}"
	if (( AIGLASS_FAILURES > 0 )); then
		printf '\n缺少 %d 项依赖。请将 AIGLASS_BSP_ROOT 指向完整 Tina BSP；首次使用可运行 make bsp-prepare。\n' "${AIGLASS_FAILURES}" >&2
		return 1
	fi
	printf '\n基础环境可用。\n'
}

sync_source() {
	mark_generated_dir "${AIGLASS_BUILD_DIR}"
	ensure_target_identity "${AIGLASS_BUILD_DIR}"
	mkdir -p -- "${AIGLASS_WORK_SOURCE}"
	if command -v rsync >/dev/null 2>&1; then
		rsync -a --delete \
			--exclude='*.o' --exclude='*.o.d' --exclude='*.d' \
			--exclude='libaglink.so' --exclude='ai_glass_*' \
			--exclude='libaiglass_media.a' --exclude='aiglass-media.ldflags' \
			"${AIGLASS_SOURCE_DIR}/" "${AIGLASS_WORK_SOURCE}/"
	else
		warn "未找到 rsync，使用 cp 增量同步；源码中已经删除的文件需 make clean 后再编译"
		cp -a -- "${AIGLASS_SOURCE_DIR}/." "${AIGLASS_WORK_SOURCE}/"
	fi
}

prepare_workspace() {
	run_doctor "${1:-lib}"
	sync_source
	mkdir -p -- "${AIGLASS_PACKAGE_PREFIX}/usr/include" "${AIGLASS_PACKAGE_PREFIX}/usr/lib"
}

prepare_output() {
	mark_generated_dir "${AIGLASS_OUTPUT_DIR}"
	ensure_target_identity "${AIGLASS_OUTPUT_DIR}"
	mkdir -p -- "${AIGLASS_ROOTFS}/bin" "${AIGLASS_ROOTFS}/lib" "${AIGLASS_ROOTFS}/etc/media"
	cp -a -- "${AIGLASS_WORK_SOURCE}/files/log_handle.sh" "${AIGLASS_ROOTFS}/etc/media/"
	cp -a -- "${AIGLASS_WORK_SOURCE}/files/rtc_init.sh" "${AIGLASS_ROOTFS}/etc/media/"
	install -m 0755 "${AIGLASS_WORK_SOURCE}/files/v821-mode.sh" "${AIGLASS_ROOTFS}/etc/media/"
}

run_component_make() {
	local aiglass_directory="$1"
	local aiglass_make_target="${2:-all}"
	STAGING_DIR="${AIGLASS_BSP_STAGING}" command make \
		-C "${aiglass_directory}" -j"${AIGLASS_JOBS}" \
		ARCH="riscv32" \
		AR="${AIGLASS_AR}" \
		CC="${AIGLASS_CC}" \
		CXX="${AIGLASS_CXX}" \
		CFLAGS="${AIGLASS_CFLAGS}" \
		CXXFLAGS="${AIGLASS_CXXFLAGS}" \
		CPPFLAGS="" \
		LDFLAGS="${AIGLASS_LDFLAGS}" \
		CONFIG_PREFIX="${AIGLASS_PACKAGE_PREFIX}" \
		STAGING_DIR="${AIGLASS_BSP_STAGING}" \
		LINUX_USER_HEADERS="${AIGLASS_KERNEL_HEADERS}" \
		"${aiglass_make_target}"
}

build_library() {
	info "编译 libaglink.so"
	# 原始 src/Makefile 的 all 同时依赖 install 和共享库；并行 make 时，
	# 对象编译可能先于 install 复制公共头文件。先串行执行 install 可消除竞态。
	run_component_make "${AIGLASS_WORK_SOURCE}/src" install
	run_component_make "${AIGLASS_WORK_SOURCE}/src"
	install -m 0755 "${AIGLASS_WORK_SOURCE}/src/libaglink.so" "${AIGLASS_ROOTFS}/lib/libaglink.so"
}

build_application() {
	local aiglass_component="$1"
	local aiglass_binary="ai_glass_${aiglass_component}"
	info "编译 ${aiglass_binary}"
	run_component_make "${AIGLASS_WORK_SOURCE}/app/${aiglass_component}"
	install -m 0755 "${AIGLASS_WORK_SOURCE}/app/${aiglass_component}/${aiglass_binary}" "${AIGLASS_ROOTFS}/bin/${aiglass_binary}"
}

build_media_library() {
	info "生成受控模式媒体开发库"
	run_component_make "${AIGLASS_WORK_SOURCE}/app/video" media-library
	mkdir -p -- "${AIGLASS_OUTPUT_DIR}/dev/include" "${AIGLASS_OUTPUT_DIR}/dev/lib"
	install -m 0644 "${AIGLASS_WORK_SOURCE}/src/media/ag_media.h" "${AIGLASS_OUTPUT_DIR}/dev/include/"
	install -m 0644 "${AIGLASS_WORK_SOURCE}/app/video/libaiglass_media.a" "${AIGLASS_OUTPUT_DIR}/dev/lib/"
	install -m 0644 "${AIGLASS_WORK_SOURCE}/app/video/aiglass-media.ldflags" "${AIGLASS_OUTPUT_DIR}/dev/lib/"
}

strip_outputs() {
	local aiglass_file
	if [[ "${AIGLASS_STRIP_OUTPUT:-1}" != "1" ]]; then
		return 0
	fi
	for aiglass_file in "${AIGLASS_ROOTFS}/lib/libaglink.so" "${AIGLASS_ROOTFS}/bin"/ai_glass_*; do
		if [[ -f "${aiglass_file}" ]]; then
			"${AIGLASS_STRIP}" --strip-unneeded "${aiglass_file}"
		fi
	done
}

build_target() {
	local aiglass_target="$1"
	prepare_workspace "${aiglass_target}"
	prepare_output
	build_library
	if [[ "${aiglass_target}" == "lib" ]]; then
		strip_outputs
		return 0
	fi
	if [[ "${aiglass_target}" == "all" ]]; then
		for aiglass_component in "${AIGLASS_ALL_COMPONENTS[@]}"; do
			build_application "${aiglass_component}"
		done
		build_media_library
	elif [[ "${aiglass_target}" == "media" ]]; then
		build_media_library
	else
		build_application "${aiglass_target}"
	fi
	strip_outputs
	info "产物目录：${AIGLASS_ROOTFS}"
}

package_outputs() {
	local aiglass_archive="${AIGLASS_OUTPUT_DIR}/aiglass-apps-${AIGLASS_CHIP}-${AIGLASS_BOARD}.tar.gz"
	build_target all
	tar --sort=name --mtime='@0' --owner=0 --group=0 --numeric-owner \
		-C "${AIGLASS_ROOTFS}" -czf "${aiglass_archive}" .
	info "已生成 rootfs overlay：${aiglass_archive}"
}

install_outputs() {
	local aiglass_destination="$1"
	local aiglass_destination_absolute
	[[ -n "${aiglass_destination}" ]] || die "install 需要目标 rootfs 路径"
	mkdir -p -- "${aiglass_destination}"
	aiglass_destination_absolute="$(cd -- "${aiglass_destination}" && pwd -P)"
	case "${aiglass_destination_absolute}" in
		/|"${AIGLASS_BSP_ROOT}"|"${AIGLASS_SDK_DIR}")
			die "拒绝安装到危险路径：${aiglass_destination_absolute}"
			;;
	esac
	if [[ ! -d "${AIGLASS_ROOTFS}" ]]; then
		warn "尚无输出，先执行完整编译"
		build_target all
	fi
	cp -a -- "${AIGLASS_ROOTFS}/." "${aiglass_destination_absolute}/"
	info "已安装到：${aiglass_destination_absolute}"
}

print_paths() {
	cat <<EOF
AIGLASS_SDK_DIR=${AIGLASS_SDK_DIR}
AIGLASS_BSP_ROOT=${AIGLASS_BSP_ROOT}
AIGLASS_SOURCE_DIR=${AIGLASS_SOURCE_DIR}
AIGLASS_BSP_OUT=${AIGLASS_BSP_OUT}
AIGLASS_BSP_STAGING=${AIGLASS_BSP_STAGING}
AIGLASS_TOOLCHAIN_DIR=${AIGLASS_TOOLCHAIN_DIR}
AIGLASS_KERNEL_HEADERS=${AIGLASS_KERNEL_HEADERS}
AIGLASS_BUILD_DIR=${AIGLASS_BUILD_DIR}
AIGLASS_OUTPUT_DIR=${AIGLASS_OUTPUT_DIR}
AIGLASS_APP_PREFIX=${AIGLASS_PACKAGE_PREFIX}
EOF
}

print_list() {
	cat <<'EOF'
lib         libaglink.so（公共通信、日志、网络封装）
media       libaiglass_media.a（受控模式可选开发接口，依赖 RT Media/MPP/AGLink）
normal      ai_glass_normal
photo       ai_glass_photo
audio       ai_glass_audio
download    ai_glass_download
ota         ai_glass_ota
video       ai_glass_video（依赖 RT Media/MPP）
livestream  ai_glass_livestream（依赖 RT Media/MPP/RTSP/PDET）
EOF
}

clean_build() {
	remove_generated_dir "${AIGLASS_BUILD_DIR}" "编译目录"
	info "已清理：${AIGLASS_BUILD_DIR}"
}

distclean_all() {
	clean_build
	remove_generated_dir "${AIGLASS_OUTPUT_DIR}" "输出目录"
	info "已清理：${AIGLASS_OUTPUT_DIR}"
}

AIGLASS_COMMAND="${1:-help}"
case "${AIGLASS_COMMAND}" in
	help)
		command make -C "${AIGLASS_SDK_DIR}" help
		;;
	list)
		print_list
		;;
	paths)
		print_paths
		;;
	doctor)
		run_doctor "${2:-lib}"
		;;
	prepare)
		prepare_workspace "${2:-lib}"
		info "外部应用工作区已准备：${AIGLASS_WORK_SOURCE}"
		info "只读使用 BSP sysroot：${AIGLASS_BSP_STAGING}"
		;;
	build)
		build_target "${2:-all}"
		;;
	package)
		package_outputs
		;;
	install)
		install_outputs "${2:-}"
		;;
	clean)
		clean_build
		;;
	distclean)
		distclean_all
		;;
	*)
		die "未知命令：${AIGLASS_COMMAND}"
		;;
esac
