#!/usr/bin/env bash
set -euo pipefail

die() {
    echo "ERROR: $*" >&2
    exit 1
}

usage() {
    echo "Usage: build.sh [all|sync|build|package] [--config release|debug|both] [--jobs N] [--profile release|official]"
}

release_gn_defines='use_sysroot=true is_component_build=false symbol_level=1 is_cfi=false use_thin_lto=false use_remoteexec=false enable_remoting=false proprietary_codecs=true ffmpeg_branding="Chrome"'
# ThinLTO and PGO follow Chromium's official defaults. Split DWARF is disabled
# so libcef.so carries complete DWARF that package can move into a .debug file
# instead of leaving it in .dwo files inside the out volume.
official_gn_defines='use_sysroot=true is_component_build=false is_official_build=true symbol_level=1 use_debug_fission=false is_cfi=false use_remoteexec=false enable_remoting=false proprietary_codecs=true ffmpeg_branding="Chrome"'
pgo_gs_url_base=chromium-optimization-profiles/pgo_profiles

parse_args() {
    command=all
    config=release
    profile=release
    jobs=
    if [[ $# -gt 0 && $1 != -* ]]; then
        command=$1
        shift
    fi
    case "$command" in
        all|sync|build|package) ;;
        *) die "Unknown command: $command" ;;
    esac
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --config)
                [[ $# -ge 2 ]] || die "--config requires a value"
                config=$2
                shift 2
                ;;
            --jobs)
                [[ $# -ge 2 && $2 =~ ^[1-9][0-9]*$ ]] ||
                    die "--jobs requires a positive integer"
                jobs=$2
                shift 2
                ;;
            --profile)
                [[ $# -ge 2 ]] || die "--profile requires a value"
                profile=$2
                shift 2
                ;;
            -h|--help) usage; exit 0 ;;
            *) die "Unknown option: $1" ;;
        esac
    done
    case "$config" in
        release|debug|both) ;;
        *) die "Invalid configuration: $config" ;;
    esac
    case "$profile" in
        release) ;;
        official)
            [[ "$config" == release ]] ||
                die "--profile official supports only --config release"
            ;;
        *) die "Invalid profile: $profile" ;;
    esac
}

# Each Compose service owns exactly one profile. The official service sets
# CEF_BUILD_SERVICE_PROFILE=official and mounts a separate out/ volume, so the
# release out/Release_GN_x64 tree can never be reconfigured as official.
check_service_profile() {
    local service=${CEF_BUILD_SERVICE_PROFILE:-release}
    if [[ "$profile" != "$service" ]]; then
        if [[ "$service" == official ]]; then
            die "The builder-official service only runs --profile official."
        fi
        die "--profile $profile must run in the builder-$profile service" \
            "(found the $service service)."
    fi
    if [[ "$profile" == official ]]; then
        mountpoint -q -- "$src/out" ||
            die "$src/out is not a separate mount; refusing to touch the shared release output."
        [[ -d "$src/.git" ]] ||
            die "Initialize the shared Chromium checkout with the builder service (sync) first."
    fi
}

gn_define_value() {
    local word value=
    local -a words
    read -r -a words <<< "$GN_DEFINES"
    for word in "${words[@]}"; do
        [[ "$word" == "$1="* ]] && value=${word#*=}
    done
    printf '%s' "$value"
}

# Chromium defaults official Linux builds to chrome_pgo_phase=2 (optimize with
# a downloaded profile) unless dcheck_always_on is set.
pgo_profile_required() {
    local phase pgo_path
    phase=$(gn_define_value chrome_pgo_phase)
    pgo_path=$(gn_define_value pgo_data_path)
    [[ -n "$pgo_path" && "$pgo_path" != '""' ]] && return 1
    if [[ -z "$phase" ]]; then
        [[ "$(gn_define_value dcheck_always_on)" != true ]]
        return
    fi
    [[ "$phase" == 2 ]]
}

# Equivalent to Chromium's DEPS hook gated on checkout_pgo_profiles; the shared
# .gclient is left untouched so release syncs do not change.
fetch_pgo_profile() {
    pgo_profile_required || return 0
    (cd "$src" && python3 tools/update_pgo_profiles.py --target=linux update \
        "--gs-url-base=$pgo_gs_url_base") ||
        die "Downloading the Chromium PGO profile failed."
    v8_builtins_profile_required || return 0
    # Equivalent to the DEPS hook gated on checkout_v8_builtins_pgo_profiles.
    (cd "$src" && python3 v8/tools/builtins-pgo/download_profiles.py download \
        --depot-tools "$work/depot_tools" --force --quiet --check-v8-revision) ||
        die "Downloading the V8 builtins PGO profile failed."
}

# Optimized V8 builds compile embedded builtins with
# v8/tools/builtins-pgo/profiles/<cpu>.profile unless the log file is overridden.
v8_builtins_profile_required() {
    [[ -z "$(gn_define_value v8_builtins_profiling_log_file)" ]]
}

check_pgo_profile() {
    pgo_profile_required || return 0
    (cd "$src" && python3 tools/update_pgo_profiles.py --target=linux \
        get_profile_path >/dev/null) ||
        die "The Chromium PGO profile is missing. Run 'all' or 'sync' with" \
            "--profile official to download it, or opt out by setting" \
            "OFFICIAL_GN_DEFINES with chrome_pgo_phase=0."
    v8_builtins_profile_required || return 0
    [[ -s "$src/v8/tools/builtins-pgo/profiles/x64.profile" ]] ||
        die "The V8 builtins PGO profile is missing. Run 'all' or 'sync' with" \
            "--profile official to download it, or opt out by setting" \
            "OFFICIAL_GN_DEFINES with v8_builtins_profiling_log_file=\"\"."
}

copy_source() {
    local path
    local -a generated=()
    # The translator's inventory distinguishes generated outputs from sources.
    if [[ -f "$2/.gitignore" ]] &&
        grep -q '^# This file was generated by the CEF translator tool' "$2/.gitignore"; then
        while IFS= read -r path || [[ -n "$path" ]]; do
            if [[ "$path" == /* ]]; then
                generated+=(--filter="protect $path")
            fi
        done < "$2/.gitignore"
    fi
    rsync -rlt --checksum --delete \
        --exclude=/artifacts --exclude=/binary_distrib --exclude=/cppdocs \
        --exclude=/tmp --exclude=/docker --exclude=/.env --exclude=/.artifacts \
        --exclude=__pycache__ --exclude=/cef_api_untracked.json \
        "${generated[@]}" "${@:3}" "$1/" "$2/"
}

mirror() {
    [[ -e "$source_dir/.git" ]] ||
        die "$source_dir must be a CEF clone or initialized submodule"
    local git_dir common_dir
    git_dir=$(git -c "safe.directory=$source_dir" -C "$source_dir" \
        rev-parse --absolute-git-dir) || return $?
    common_dir=$(git -c "safe.directory=$source_dir" -C "$source_dir" \
        rev-parse --path-format=absolute --git-common-dir) || return $?
    # Local clone/fetch validates .git separately from the worktree.
    local -a source_trust=(-c "safe.directory=$source_dir"
                           -c "safe.directory=$git_dir"
                           -c "safe.directory=$common_dir")
    if [[ ! -d "$cef/.git" ]]; then
        git "${source_trust[@]}" clone --depth=1 --no-local --no-tags "$source_dir" "$cef"
    else
        local head current
        head=$(git "${source_trust[@]}" -C "$source_dir" rev-parse HEAD)
        current=$(git -C "$cef" rev-parse HEAD)
        if [[ "$head" != "$current" ]]; then
            git "${source_trust[@]}" -C "$cef" fetch --depth=1 --no-tags "$source_dir" HEAD
            # Move only the disposable mirror's index, never the host checkout.
            git -C "$cef" reset --mixed --quiet FETCH_HEAD
        fi
    fi
    copy_source "$source_dir" "$cef" --exclude=/.git
}

ready() {
    [[ -f "$stamp" && -d "$src/.git" ]] &&
        cmp -s "$stamp" "$cef/CHROMIUM_BUILD_COMPATIBILITY.txt"
}

sync_dependencies() {
    rm -f -- "$stamp"
    if [[ ! -d "$work/depot_tools" ]]; then
        git clone --depth=1 --no-tags \
            https://chromium.googlesource.com/chromium/tools/depot_tools.git \
            "$work/depot_tools" || return $?
    fi
    "$work/depot_tools/ensure_bootstrap" || return $?
    python3 "$cef/tools/automate/automate-git.py" \
        "--download-dir=$work" --no-cef-update --no-build --no-distrib \
        --x64-build --no-chromium-history --no-depot-tools-update || return $?
    cp "$cef/CHROMIUM_BUILD_COMPATIBILITY.txt" "$stamp"
}

configurations() {
    case "$config" in
        release) configs=(Release) ;;
        debug) configs=(Debug) ;;
        both) configs=(Debug Release) ;;
    esac
}

compile() {
    ready || die "Run sync (or all) first: Chromium dependencies are missing or changed."
    configurations
    local default_gn=$release_gn_defines
    if [[ "${profile:-release}" == official ]]; then
        default_gn=$official_gn_defines
    fi
    export GN_DEFINES="${GN_DEFINES:-$default_gn}"
    if [[ "${profile:-release}" == official ]]; then
        [[ "$(gn_define_value is_official_build)" == true ]] ||
            die "--profile official requires is_official_build=true in GN_DEFINES."
        check_pgo_profile
    fi
    export CEF_INSTALL_SYSROOT=amd64
    local name
    local -a output_configs=() ninja_args=()
    for name in "${configs[@]}"; do
        output_configs+=("${name}_GN_x64")
    done
    GN_OUT_CONFIGS=$(IFS=,; echo "${output_configs[*]}")
    export GN_OUT_CONFIGS
    # Re-running the patcher over an already patched tree needs upstream files;
    # unchanged results keep their timestamps so the build stays incremental.
    python3 "$(dirname "${BASH_SOURCE[0]}")/reset_patched_files.py" reset "$src"
    (cd "$src/cef" && python3 tools/gclient_hook.py)
    python3 "$(dirname "${BASH_SOURCE[0]}")/reset_patched_files.py" restore "$src"
    ninja_args=(-j "${jobs:-$(nproc)}")
    for name in "${configs[@]}"; do
        (cd "$src" &&
            autoninja -C "out/${name}_GN_x64" "${ninja_args[@]}" cefsimple chrome_sandbox)
    done
}

package() {
    if [[ "${profile:-release}" == official ]]; then
        package_official
        return
    fi
    configurations
    local name binary
    for name in "${configs[@]}"; do
        binary=$src/out/${name}_GN_x64/libcef.so
        [[ -f "$binary" ]] || die "Build first: missing $binary"
    done
    mkdir -p "$artifacts"
    (cd "$src/cef/tools" &&
        python3 make_distrib.py "--output-dir=$artifacts" --ninja-build \
            --x64-build --allow-partial --no-docs --no-archive)
}

json_string() {
    local value=$1
    value=${value//\\/\\\\}
    value=${value//\"/\\\"}
    value=${value//$'\n'/\\n}
    value=${value//$'\r'/\\r}
    value=${value//$'\t'/\\t}
    printf '"%s"' "$value"
}

sha256_of() {
    local output
    output=$(sha256sum -- "$1") || return $?
    printf '%s' "${output%% *}"
}

build_id_of() {
    local notes
    notes=$("$readelf" -n "$1") || die "llvm-readelf failed for $1"
    [[ "$notes" =~ Build\ ID:\ *([0-9a-fA-F]+) ]] ||
        die "No GNU build-id in $1"
    printf '%s' "${BASH_REMATCH[1],,}"
}

# Moves DWARF and the static symbol table of $1 into $2, keeps .dynsym, and
# links the stripped library to its debug file with .gnu_debuglink.
split_debug_symbols() {
    local lib=$1 debug=$2 stripped=$1.stripped lib_sections debug_sections
    mkdir -p -- "$(dirname -- "$debug")"
    "$objcopy" --only-keep-debug "$lib" "$debug" ||
        die "llvm-objcopy --only-keep-debug failed for $lib"
    "$objcopy" --strip-unneeded "--add-gnu-debuglink=$debug" \
        "$lib" "$stripped" || die "llvm-objcopy strip failed for $lib"
    chmod --reference="$lib" -- "$stripped" ||
        die "Cannot copy permissions to $stripped"
    mv -f -- "$stripped" "$lib" || die "Cannot replace $lib"

    lib_sections=$("$readelf" -S --wide "$lib") ||
        die "llvm-readelf failed for $lib"
    debug_sections=$("$readelf" -S --wide "$debug") ||
        die "llvm-readelf failed for $debug"
    if grep -q '\.debug_' <<< "$lib_sections"; then
        die "$lib still contains .debug_* sections after stripping"
    fi
    grep -q '\.gnu_debuglink' <<< "$lib_sections" ||
        die "$lib has no .gnu_debuglink section"
    grep -q '\.dynsym' <<< "$lib_sections" ||
        die "$lib lost its dynamic symbol table"
    grep -q '\.debug_' <<< "$debug_sections" ||
        die "$debug contains no DWARF; was the build made with symbol_level=0?"
    lib_build_id=$(build_id_of "$lib") || exit 1
    debug_build_id=$(build_id_of "$debug") || exit 1
    [[ "$lib_build_id" == "$debug_build_id" ]] ||
        die "Build-id mismatch: $lib_build_id (lib) != $debug_build_id (debug)"
}

gn_effective_value() {
    local line
    while IFS= read -r line; do
        if [[ "$line" =~ ^$1\ =\ (.*)$ ]]; then
            printf '%s' "${BASH_REMATCH[1]}"
            return
        fi
    done <<< "$gn_effective"
}

write_official_manifest() {
    local manifest=$1 dist_name=$2 lib=$3 debug=$4
    local phase=$5 profile_name=$6 args_gn=$7 key first=1 lib_sha debug_sha
    lib_sha=$(sha256_of "$lib") || return 1
    debug_sha=$(sha256_of "$debug") || return 1
    {
        printf '{\n'
        printf '  "format": "cef-official-symbols/v1",\n'
        printf '  "profile": "official",\n'
        printf '  "distribution": %s,\n' "$(json_string "$dist_name")"
        printf '  "files": [\n'
        printf '    {\n'
        printf '      "path": "Release/libcef.so",\n'
        printf '      "sha256": "%s",\n' "$lib_sha"
        printf '      "build_id": "%s",\n' "$lib_build_id"
        printf '      "debug_file": "libcef.so.debug",\n'
        printf '      "debug_sha256": "%s"\n' "$debug_sha"
        printf '    }\n'
        printf '  ],\n'
        printf '  "pgo": {\n'
        printf '    "phase": %s,\n' "$(json_string "$phase")"
        if [[ -n "$profile_name" ]]; then
            printf '    "profile": %s\n' "$(json_string "$profile_name")"
        else
            printf '    "profile": null\n'
        fi
        printf '  },\n'
        printf '  "gn_effective": {'
        for key in is_official_build is_debug use_thin_lto chrome_pgo_phase \
                   pgo_data_path symbol_level use_debug_fission is_cfi \
                   dcheck_always_on; do
            [[ $first -eq 1 ]] || printf ','
            first=0
            printf '\n    "%s": %s' "$key" \
                "$(json_string "$(gn_effective_value "$key")")"
        done
        printf '\n  },\n'
        printf '  "args_gn": %s\n' "$(json_string "$args_gn")"
        printf '}\n'
    } > "$manifest"
}

# Packages into a staging directory beside the published outputs, splits and
# verifies symbols there, and only then renames the results into place.
package_official() {
    local out=$src/out/Release_GN_x64
    local llvm_bin=$src/third_party/llvm-build/Release+Asserts/bin
    [[ -f "$out/libcef.so" ]] || die "Build first: missing $out/libcef.so"
    objcopy=$llvm_bin/llvm-objcopy
    readelf=$llvm_bin/llvm-readelf
    [[ -x "$objcopy" && -x "$readelf" ]] ||
        die "Chromium's llvm-objcopy/llvm-readelf are missing from $llvm_bin"
    [[ -f "$out/args.gn" ]] || die "Missing $out/args.gn"
    gn_effective=$(cd "$src" && gn args out/Release_GN_x64 --list --short) ||
        die "gn args failed for $out"
    [[ "$(gn_effective_value is_official_build)" == true ]] ||
        die "$out is not an is_official_build=true build"
    local phase profile_name=
    phase=$(gn_effective_value chrome_pgo_phase)
    if [[ "$phase" == 2 && "$(gn_effective_value pgo_data_path)" == '""' ]]; then
        profile_name=$(< "$src/chrome/build/linux.pgo.txt") ||
            die "Cannot read the PGO profile name"
    fi
    mkdir -p -- "$artifacts"
    local staging
    staging=$(mktemp -d -- "$artifacts/.official-staging.XXXXXX")
    (
        trap 'rm -rf -- "$staging"' EXIT
        (cd "$src/cef/tools" &&
            python3 make_distrib.py "--output-dir=$staging" --ninja-build \
                --x64-build --allow-partial --no-docs --no-archive) ||
            die "make_distrib.py failed"
        shopt -s nullglob
        local -a dists=("$staging"/cef_binary_*_linux64)
        [[ ${#dists[@]} -eq 1 && -d "${dists[0]}" ]] ||
            die "Expected one cef_binary_*_linux64 distribution in $staging"
        local dist=${dists[0]} name symbols lib
        name=${dist##*/}
        symbols=$staging/${name}_release_symbols
        lib=$dist/Release/libcef.so
        [[ -f "$lib" && ! -L "$lib" ]] || die "Missing $lib"
        split_debug_symbols "$lib" "$symbols/libcef.so.debug"
        mkdir -p -- "$symbols/.build-id/${lib_build_id:0:2}" &&
            ln -s -- ../../libcef.so.debug \
                "$symbols/.build-id/${lib_build_id:0:2}/${lib_build_id:2}.debug" ||
            die "Cannot create the build-id symbol link"
        write_official_manifest "$symbols/manifest.json" "$name" "$lib" \
            "$symbols/libcef.so.debug" "$phase" "$profile_name" \
            "$(< "$out/args.gn")" || die "Cannot write the symbol manifest"
        local item
        # Symbols first: a published distribution always has its symbols.
        for item in "$symbols" "$dist"; do
            if [[ -e "$artifacts/${item##*/}" ]]; then
                mv -- "$artifacts/${item##*/}" \
                    "$staging/.replaced-${item##*/}" ||
                    die "Cannot replace $artifacts/${item##*/}"
            fi
            mv -- "$item" "$artifacts/${item##*/}" ||
                die "Cannot publish $artifacts/${item##*/}"
        done
        echo "Published $artifacts/$name and $artifacts/${name}_release_symbols"
    )
}

main() {
    parse_args "$@"
    trap 'echo "ERROR: build command failed at line $LINENO" >&2' ERR
    source_dir=${CEF_SOURCE_DIRECTORY:-/source/cef}
    work=/work
    artifacts=/artifacts
    cef=$work/cef
    src=$work/chromium/src
    stamp=$work/.cef-synced-compatibility
    exec 9>"$work/.cef-build.lock"
    flock -n 9 || die "Another build is using this workspace volume."
    check_service_profile
    if [[ "$command" != package ]]; then
        mirror
        if [[ "$command" == sync ]] || { [[ "$command" == all ]] && ! ready; }; then
            sync_dependencies
        fi
        if [[ "$profile" == official && "$command" != build ]]; then
            export GN_DEFINES="${GN_DEFINES:-$official_gn_defines}"
            fetch_pgo_profile
        fi
        if [[ "$command" == all || "$command" == build ]]; then
            ready || die "Run sync (or all) first: Chromium dependencies are missing or changed."
            copy_source "$cef" "$src/cef"
            compile
        fi
    fi
    if [[ "$command" == all || "$command" == package ]]; then
        package
    fi
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    main "$@"
fi
