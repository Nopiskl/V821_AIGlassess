#!/usr/bin/env bash

set -euo pipefail

AIGLASS_SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
AIGLASS_SDK_DIR="$(cd -- "${AIGLASS_SCRIPT_DIR}/.." && pwd -P)"
AIGLASS_DIST_DIR="${AIGLASS_DIST_DIR:-${AIGLASS_SDK_DIR}/dist}"
AIGLASS_RELEASE_VERSION="${AIGLASS_RELEASE_VERSION:-$(sed -n '1p' "${AIGLASS_SDK_DIR}/VERSION")}"
AIGLASS_RELEASE_NAME="aiglass-apps-${AIGLASS_RELEASE_VERSION}"
AIGLASS_COMMAND="${1:-create}"
AIGLASS_DIST_MARKER="${AIGLASS_DIST_DIR}/.aiglass-apps-generated"

die() {
	printf '[aiglass-release] 错误：%s\n' "$*" >&2
	exit 1
}

check_dist_dir() {
	if [[ -d "${AIGLASS_DIST_DIR}" && ! -f "${AIGLASS_DIST_MARKER}" ]]; then
		if [[ -n "$(find "${AIGLASS_DIST_DIR}" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
			die "发布目录非空且不是本工具生成，拒绝接管：${AIGLASS_DIST_DIR}"
		fi
	fi
	mkdir -p -- "${AIGLASS_DIST_DIR}"
	: > "${AIGLASS_DIST_MARKER}"
}

clean_dist() {
	if [[ ! -e "${AIGLASS_DIST_DIR}" ]]; then
		return 0
	fi
	[[ -f "${AIGLASS_DIST_MARKER}" ]] || die "拒绝删除未带生成标记的目录：${AIGLASS_DIST_DIR}"
	case "${AIGLASS_DIST_DIR}" in
		/|"${AIGLASS_SDK_DIR}") die "拒绝删除危险路径：${AIGLASS_DIST_DIR}" ;;
	esac
	rm -rf -- "${AIGLASS_DIST_DIR}"
	printf '[aiglass-release] 已清理：%s\n' "${AIGLASS_DIST_DIR}"
}

create_release() {
	local aiglass_temp_dir
	local aiglass_stage_dir
	local aiglass_archive
	local aiglass_checksum
	local aiglass_link
	local aiglass_item
	local -a aiglass_release_items=(
		.gitignore
		Makefile
		README.md
		MANIFEST.md
		VERSION
		config.mk.example
		envsetup.sh
		apps
		integration
		scripts
	)

	[[ "${AIGLASS_RELEASE_VERSION}" =~ ^[A-Za-z0-9._-]+$ ]] || die "非法版本号：${AIGLASS_RELEASE_VERSION}"
	for aiglass_item in "${aiglass_release_items[@]}"; do
		[[ -e "${AIGLASS_SDK_DIR}/${aiglass_item}" ]] || die "缺少发布内容：${aiglass_item}"
	done
	aiglass_link="$(find "${aiglass_release_items[@]/#/${AIGLASS_SDK_DIR}/}" -type l -print -quit)"
	[[ -z "${aiglass_link}" ]] || die "发布内容中不允许软链接：${aiglass_link}"

	aiglass_temp_dir="$(mktemp -d)"
	trap 'rm -rf -- "${aiglass_temp_dir}"' EXIT
	aiglass_stage_dir="${aiglass_temp_dir}/${AIGLASS_RELEASE_NAME}"
	mkdir -p -- "${aiglass_stage_dir}"
	for aiglass_item in "${aiglass_release_items[@]}"; do
		cp -a -- "${AIGLASS_SDK_DIR}/${aiglass_item}" "${aiglass_stage_dir}/"
	done
	if aiglass_link="$(find "${aiglass_stage_dir}" -type l -print -quit)" && [[ -n "${aiglass_link}" ]]; then
		die "暂存内容中出现软链接：${aiglass_link}"
	fi

	aiglass_archive="${aiglass_temp_dir}/${AIGLASS_RELEASE_NAME}.tar.gz"
	tar --sort=name --mtime='@0' --owner=0 --group=0 --numeric-owner \
		-C "${aiglass_temp_dir}" -cf - "${AIGLASS_RELEASE_NAME}" | gzip -n > "${aiglass_archive}"
	check_dist_dir
	mv -f -- "${aiglass_archive}" "${AIGLASS_DIST_DIR}/${AIGLASS_RELEASE_NAME}.tar.gz"
	aiglass_checksum="${AIGLASS_DIST_DIR}/${AIGLASS_RELEASE_NAME}.tar.gz.sha256"
	(
		cd -- "${AIGLASS_DIST_DIR}"
		sha256sum "${AIGLASS_RELEASE_NAME}.tar.gz" > "$(basename -- "${aiglass_checksum}")"
	)
	rm -rf -- "${aiglass_temp_dir}"
	trap - EXIT
	printf '[aiglass-release] 已生成独立源码补丁包：%s\n' "${AIGLASS_DIST_DIR}/${AIGLASS_RELEASE_NAME}.tar.gz"
	printf '[aiglass-release] 校验文件：%s\n' "${aiglass_checksum}"
}

case "${AIGLASS_COMMAND}" in
	create) create_release ;;
	clean) clean_dist ;;
	*) die "未知命令：${AIGLASS_COMMAND}" ;;
esac
