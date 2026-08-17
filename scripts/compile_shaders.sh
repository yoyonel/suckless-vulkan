#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat <<'EOF'
Usage: scripts/compile_shaders.sh <mode>

Modes:
  raster       Compile all top-level .vert/.frag shaders
  raster-debug Compile all top-level .vert/.frag shaders with debug info
  ibl          Compile all top-level ibl_*.comp shaders
  ibl-debug    Compile all top-level ibl_*.comp shaders with debug info
  lint         Validate all top-level .vert/.frag/.comp shaders with glslangValidator
EOF
}

require_mode() {
	if [[ $# -ne 1 ]]; then
		usage
		exit 2
	fi
}

raster_output_name() {
	local base="$1"
	if [[ "$base" == "shader.vert" ]]; then
		echo "vert"
	elif [[ "$base" == "shader.frag" ]]; then
		echo "frag"
	else
		local stem="${base%.*}"
		local ext="${base##*.}"
		echo "${stem}_${ext}"
	fi
}

compile_raster() {
	local glslc_flags="$1"
	local glslang_flags="$2"
	local emit_asm="$3"

	mapfile -t files < <(find shaders -maxdepth 1 -type f \( -name '*.vert' -o -name '*.frag' \) | sort)

	if command -v glslc >/dev/null 2>&1; then
		for src in "${files[@]}"; do
			local base out
			base="$(basename "$src")"
			out="$(raster_output_name "$base")"
			# shellcheck disable=SC2086
			glslc ${glslc_flags} "$src" -o "shaders/${out}.spv"
			if [[ "$emit_asm" == "1" ]]; then
				# shellcheck disable=SC2086
				glslc ${glslc_flags} -S "$src" -o "shaders/${out}.spvasm"
			fi
		done
	else
		for src in "${files[@]}"; do
			local base out
			base="$(basename "$src")"
			out="$(raster_output_name "$base")"
			# shellcheck disable=SC2086
			glslangValidator ${glslang_flags} -V "$src" -o "shaders/${out}.spv"
		done
		if [[ "$emit_asm" == "1" ]]; then
			echo "Generation .spvasm ignoree (glslc requis)."
		fi
	fi
}

compile_compute() {
	local glslc_flags="$1"
	local glslang_flags="$2"

	mapfile -t files < <(find shaders -maxdepth 1 -type f -name '*.comp' | sort)

	if command -v glslc >/dev/null 2>&1; then
		for src in "${files[@]}"; do
			local base stem
			base="$(basename "$src")"
			stem="${base%.*}"
			# shellcheck disable=SC2086
			glslc --target-env=vulkan1.2 ${glslc_flags} "$src" -o "shaders/${stem}.spv"
		done
	else
		for src in "${files[@]}"; do
			local base stem
			base="$(basename "$src")"
			stem="${base%.*}"
			# shellcheck disable=SC2086
			glslangValidator ${glslang_flags} -V --target-env vulkan1.2 "$src" -o "shaders/${stem}.spv"
		done
	fi
}

lint_all() {
	mapfile -t files < <(find shaders -maxdepth 1 -type f \( -name '*.vert' -o -name '*.frag' -o -name '*.comp' \) | sort)
	for src in "${files[@]}"; do
		glslangValidator -V --target-env vulkan1.2 "$src" -o /dev/null
	done
}

main() {
	require_mode "$@"

	case "$1" in
	raster)
		compile_raster "" "" "0"
		;;
	raster-debug)
		compile_raster "-g -O0" "-g -Od" "1"
		;;
	compute | ibl)
		compile_compute "" ""
		;;
	compute-debug | ibl-debug)
		compile_compute "-g -O0" "-g -Od"
		;;
	lint)
		lint_all
		;;
	*)
		usage
		exit 2
		;;
	esac
}

main "$@"
