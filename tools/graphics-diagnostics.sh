#!/bin/sh
# Read-only graphics inventory and optional process measurements for Linux targets.
set -f
export LC_ALL=C

usage() {
    cat <<'EOF'
Usage: sh tools/graphics-diagnostics.sh [options]

  --probe-egl        Run eglinfo -B -p gbm with a 10-second timeout, if available
  --probe-drm        Summarize DRM connectors, planes and rotation (no modeset)
  --drm-driver NAME  Probe this DRM module instead of autodetecting (implies --probe-drm)
  --full-drm         Show full modetest output (implies --probe-drm)
  --pid PID          Measure this process's CPU and RSS after the inventory
  --seconds N        Measurement duration in seconds, 1..300 (default: 10)
  --interval N       Measurement interval in seconds, 1..60 (default: 1)
  -h, --help         Show this help

Run as the same user and in the same session as the UI. Optional probes need
the timeout utility. This tool does not install software or change display state.
EOF
}

fail() {
    printf 'Error: %s\n' "$*" >&2
    exit 2
}

integer_in_range() {
    case "$1" in ''|*[!0-9]*) return 1 ;; esac
    [ "${#1}" -le 9 ] && [ "$1" -ge "$2" ] && [ "$1" -le "$3" ]
}

probe_egl=0
probe_drm=0
drm_driver=
full_drm=0
pid=
seconds=10
interval=1
while [ "$#" -gt 0 ]; do
    case "$1" in
        --probe-egl) probe_egl=1; shift ;;
        --probe-drm) probe_drm=1; shift ;;
        --full-drm) full_drm=1; probe_drm=1; shift ;;
        --pid|--seconds|--interval|--drm-driver)
            option=$1
            [ "$#" -ge 2 ] || fail "$option needs a value"
            case "$option" in
                --drm-driver)
                    case "$2" in ''|*[!a-zA-Z0-9_-]*) fail "DRM driver must be a module name, for example vc4" ;; esac
                    [ "${#2}" -le 64 ] || fail "DRM driver name is too long"
                    drm_driver=$2
                    probe_drm=1 ;;
                --pid)
                    integer_in_range "$2" 1 999999999 || fail "PID must be a positive integer"
                    pid=$2 ;;
                --seconds)
                    integer_in_range "$2" 1 300 || fail "seconds must be an integer in 1..300"
                    seconds=$2 ;;
                --interval)
                    integer_in_range "$2" 1 60 || fail "interval must be an integer in 1..60"
                    interval=$2 ;;
            esac
            shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) fail "unknown option: $1" ;;
    esac
done

[ "$(uname -s)" = Linux ] || fail "this diagnostic tool requires a Linux target with /proc and /sys"
command -v awk >/dev/null 2>&1 || fail "awk is required"
# Normalize decimal options before shell arithmetic (which interprets leading 0 as octal).
seconds=$(awk -v value="$seconds" 'BEGIN { printf "%d", value }')
interval=$(awk -v value="$interval" 'BEGIN { printf "%d", value }')
if [ -n "$pid" ]; then
    pid=$(awk -v value="$pid" 'BEGIN { printf "%d", value }')
fi

printf '== Target and session ==\n'
uname -a
id
for model in /sys/firmware/devicetree/base/model /proc/device-tree/model; do
    if [ -r "$model" ]; then
        printf 'Board: '
        tr '\000' '\n' < "$model"
        printf '\n'
        break
    fi
done
printf 'DISPLAY=%s\nWAYLAND_DISPLAY=%s\nXDG_SESSION_TYPE=%s\n' \
    "${DISPLAY:-<unset>}" "${WAYLAND_DISPLAY:-<unset>}" "${XDG_SESSION_TYPE:-<unset>}"
printf 'EGL_PLATFORM=%s\nLIBGL_ALWAYS_SOFTWARE=%s\nMESA_LOADER_DRIVER_OVERRIDE=%s\n' \
    "${EGL_PLATFORM:-<unset>}" "${LIBGL_ALWAYS_SOFTWARE:-<unset>}" "${MESA_LOADER_DRIVER_OVERRIDE:-<unset>}"

printf '\n== DRM devices and kernel drivers ==\n'
# Enable pathname expansion only for the device inventory.
set +f
found_device=0
for device in /dev/dri/card* /dev/dri/renderD*; do
    [ -e "$device" ] || continue
    found_device=1
    ls -l "$device"
    readable=no
    writable=no
    [ ! -r "$device" ] || readable=yes
    [ ! -w "$device" ] || writable=yes
    printf '  Current user access: read=%s write=%s\n' "$readable" "$writable"
    driver=/sys/class/drm/${device##*/}/device/driver
    if [ -L "$driver" ]; then
        printf '  Driver: '
        readlink "$driver"
    else
        printf '  Driver link unavailable: %s\n' "$driver"
    fi
done
[ "$found_device" -eq 1 ] || printf 'No DRM device nodes found.\n'
set -f

printf '\n== Userspace libraries ==\n'
if command -v pkg-config >/dev/null 2>&1; then
    for package in egl glesv2 gbm libdrm; do
        if version=$(pkg-config --modversion "$package" 2>/dev/null); then
            printf 'pkg-config %s: %s\n' "$package" "$version"
        else
            printf 'pkg-config %s: unavailable (runtime library may still be installed)\n' "$package"
        fi
    done
else
    printf 'pkg-config unavailable.\n'
fi
ldconfig_tool=$(command -v ldconfig 2>/dev/null || true)
if [ -z "$ldconfig_tool" ]; then
    for candidate in /sbin/ldconfig /usr/sbin/ldconfig; do
        if [ -x "$candidate" ]; then
            ldconfig_tool=$candidate
            break
        fi
    done
fi
if [ -n "$ldconfig_tool" ]; then
    if cache=$("$ldconfig_tool" -p 2>/dev/null); then
        printf '%s\n' "$cache" | awk '
            /lib(EGL|GLESv2|gbm|drm)\.so/ { print; found=1 }
            END { if (!found) print "No EGL/GLESv2/GBM/DRM entries in the loader cache." }
        '
    else
        printf 'Loader cache query unavailable.\n'
    fi
else
    printf 'ldconfig unavailable.\n'
fi
printf 'Library availability does not establish hardware rendering or application usage.\n'

bounded_probe() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf '%s unavailable; probe skipped.\n' "$1"
        return
    fi
    if ! command -v timeout >/dev/null 2>&1; then
        printf 'timeout unavailable; %s probe skipped to keep execution bounded.\n' "$1"
        return
    fi
    timeout -k 2 10 "$@"
    result=$?
    if [ "$result" -ne 0 ]; then
        printf 'Probe exited with status %s; check permissions, display ownership, or utility options.\n' "$result"
    fi
}

drm_module() {
    sysfs_driver=/sys/class/drm/${1##*/}/device/driver
    if module_link=$(readlink "$sysfs_driver/module" 2>/dev/null); then
        printf '%s\n' "${module_link##*/}"
    elif driver_link=$(readlink "$sysfs_driver" 2>/dev/null); then
        # The built-in VC4 platform driver and libdrm module use different names.
        case "${driver_link##*/}" in
            vc4-drm) printf 'vc4\n' ;;
            *) printf '%s\n' "${driver_link##*/}" ;;
        esac
    else
        return 1
    fi
}

summarize_drm() {
    awk '
        /^(Encoders|Connectors|CRTCs|Planes|Frame buffers):/ {
            print; rotation=0; found=1; next
        }
        /^id[[:space:]]/ || /^[0-9]+[[:space:]]/ ||
            /^[[:space:]]*#[0-9]+[[:space:]]/ || /^[[:space:]]*formats:/ {
            print; found=1; next
        }
        /^[[:space:]]*[0-9]+[[:space:]]+[^:]+:/ {
            rotation=($0 ~ /^[[:space:]]*[0-9]+[[:space:]]+rotation:/)
            if (rotation) { print; found=1 }
            next
        }
        rotation && /^[[:space:]]*(flags|values|value):/ { print; next }
        /unavailable|skipped|failed|Failed|Probe exited|[Pp]ermission|[Ee]rror/ { print }
        END {
            if (!found) print "No recognized DRM tables; use --full-drm to inspect complete output."
        }
    '
}

probe_drm_module() {
    printf '\nDRM module: %s\n' "$1"
    if [ "$full_drm" -eq 1 ]; then
        bounded_probe modetest -M "$1" -c -p
    else
        bounded_probe modetest -M "$1" -c -p | summarize_drm
    fi
}

if [ "$probe_egl" -eq 1 ]; then
    printf '\n== EGL and renderer probe ==\n'
    bounded_probe eglinfo -B -p gbm
    printf 'Inspect the renderer for the application display platform; llvmpipe/softpipe indicate software.\n'
fi
if [ "$probe_drm" -eq 1 ]; then
    printf '\n== DRM connector and plane properties ==\n'
    if [ -n "$drm_driver" ]; then
        probe_drm_module "$drm_driver"
    else
        probed_modules=' '
        set +f
        for device in /dev/dri/card*; do
            [ -e "$device" ] || continue
            if module=$(drm_module "$device"); then
                printf 'Device: %s, DRM module: %s\n' "$device" "$module"
                case "$probed_modules" in *" $module "*) continue ;; esac
                probed_modules="$probed_modules$module "
                probe_drm_module "$module"
            else
                printf 'Cannot resolve driver for %s; use --drm-driver NAME.\n' "$device"
            fi
        done
        set -f
    fi
    [ "$full_drm" -eq 1 ] || printf 'Use --full-drm for all properties, blobs and format modifiers.\n'
    printf 'A rotation property alone does not prove a plane supports the required format and rotation together.\n'
fi

# Strip through the final ") " because the process name can contain spaces,
# parentheses, and newlines. The remaining stat fields have fixed positions.
process_sample() {
    process_stat=$(cat "/proc/$pid/stat" 2>/dev/null) || return 1
    fields=${process_stat##*) }
    [ "$fields" != "$process_stat" ] || return 1
    printf '%s\n' "$fields" | awk '
        NF >= 22 && $1 != "Z" && $1 != "X" &&
            $12 ~ /^[0-9]+$/ && $13 ~ /^[0-9]+$/ &&
            $20 ~ /^[0-9]+$/ && $22 ~ /^[0-9]+$/ {
            printf "%.0f %s %s\n", $12 + $13, $20, $22
            valid=1
        }
        END { if (!valid) exit 1 }
    '
}

sample_process() {
    command -v getconf >/dev/null 2>&1 || { printf 'getconf is required for CPU/RSS measurements.\n' >&2; return 1; }
    ticks_per_second=$(getconf CLK_TCK 2>/dev/null) || return 1
    page_size=$(getconf PAGESIZE 2>/dev/null) || return 1
    integer_in_range "$ticks_per_second" 1 1000000 || return 1
    integer_in_range "$page_size" 1 999999999 || return 1
    initial=$(process_sample) || { printf 'Cannot sample PID %s: absent, exited, or stat unreadable.\n' "$pid" >&2; return 1; }
    set -- $initial
    previous_ticks=$1
    initial_start=$2
    read -r start_uptime unused < /proc/uptime || return 1
    previous_uptime=$start_uptime
    remaining=$seconds
    printf '\n== PID %s: %s-second sample, %s-second intervals ==\n' "$pid" "$seconds" "$interval"
    printf 'CPU uses one logical CPU = 100%%; multithreaded processes may exceed 100%%.\n'
    printf '%12s %12s %12s\n' 'elapsed_s' 'CPU_percent' 'RSS_MiB'
    while [ "$remaining" -gt 0 ]; do
        step=$interval
        [ "$step" -le "$remaining" ] || step=$remaining
        sleep "$step" || return 1
        current=$(process_sample) || { printf 'PID %s exited or became unreadable; sampling stopped.\n' "$pid" >&2; return 1; }
        set -- $current
        current_ticks=$1
        [ "$2" = "$initial_start" ] || { printf 'PID %s was reused; sampling stopped.\n' "$pid" >&2; return 1; }
        current_rss=$3
        read -r current_uptime unused < /proc/uptime || return 1
        awk -v now="$current_uptime" -v previous="$previous_uptime" -v start="$start_uptime" \
            -v ticks="$current_ticks" -v previous_ticks="$previous_ticks" \
            -v hz="$ticks_per_second" -v pages="$current_rss" -v page_size="$page_size" '
            BEGIN {
                if (now <= previous || ticks < previous_ticks) exit 1
                printf "%12.2f %12.2f %12.2f\n", now - start,
                    (ticks - previous_ticks) / hz / (now - previous) * 100,
                    pages * page_size / 1048576
            }
        ' || { printf 'Invalid process counters or sample timing; sampling stopped.\n' >&2; return 1; }
        previous_ticks=$current_ticks
        previous_uptime=$current_uptime
        remaining=$((remaining - step))
    done
}

if [ -n "$pid" ]; then
    sample_process || exit 1
fi
