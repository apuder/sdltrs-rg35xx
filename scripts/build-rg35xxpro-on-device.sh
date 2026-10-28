#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 <rg35xxpro-ip-address>"
  echo "Example: $0 192.168.1.42"
}

if [[ ${1:-} == "" ]] || [[ ${1:-} == "-h" ]] || [[ ${1:-} == "--help" ]]; then
  usage
  exit 1
fi

DEVICE_IP="$1"
DEVICE_USER="root"
DEVICE_PASSWORD="root"
REPO_URL="https://github.com/apuder/sdltrs-rg35xx.git"
REPO_DIR="sdltrs-rg35xx"
ROM_URL="https://github.com/lkesteloot/trs80/raw/refs/heads/master/packages/trs80-emulator/roms/model3.rom"
APPS_ROOT="/mnt/mmc/Roms/APPS"
APP_NAME="TRS-80"
MAX_CLOCK_SKEW_SECONDS=3600
HOST_EPOCH="$(date +%s)"
HOST_UTC_ISO="$(date -u '+%Y-%m-%d %H:%M:%S')"
HOST_UTC_BUSYBOX="$(date -u '+%Y.%m.%d-%H:%M:%S')"

if ! command -v sshpass >/dev/null 2>&1; then
  echo "Error: sshpass is required but not installed."
  echo "Install it on Debian/Ubuntu with: sudo apt install sshpass"
  exit 1
fi

SSH_OPTS=(
  -o StrictHostKeyChecking=no
  -o UserKnownHostsFile=/dev/null
  -o LogLevel=ERROR
  -o ConnectTimeout=10
)

echo "Connecting to ${DEVICE_USER}@${DEVICE_IP}..."

sshpass -p "$DEVICE_PASSWORD" ssh "${SSH_OPTS[@]}" "${DEVICE_USER}@${DEVICE_IP}" \
  "HOST_EPOCH='${HOST_EPOCH}' HOST_UTC_ISO='${HOST_UTC_ISO}' HOST_UTC_BUSYBOX='${HOST_UTC_BUSYBOX}' MAX_CLOCK_SKEW_SECONDS='${MAX_CLOCK_SKEW_SECONDS}' ROM_URL='${ROM_URL}' APPS_ROOT='${APPS_ROOT}' APP_NAME='${APP_NAME}' bash -s" <<'REMOTE_EOF'
set -euo pipefail

REPO_URL="https://github.com/apuder/sdltrs-rg35xx.git"
REPO_DIR="sdltrs-rg35xx"

read_remote_epoch() {
  date +%s 2>/dev/null || true
}

clock_skew_seconds() {
  local remote_epoch="$1"
  if (( remote_epoch >= HOST_EPOCH )); then
    echo $((remote_epoch - HOST_EPOCH))
  else
    echo $((HOST_EPOCH - remote_epoch))
  fi
}

sync_clock_with_ntp() {
  if command -v ntpd >/dev/null 2>&1; then
    ntpd -q -n -p pool.ntp.org >/dev/null 2>&1 && return 0
    ntpd -q -p pool.ntp.org >/dev/null 2>&1 && return 0
  fi

  if command -v busybox >/dev/null 2>&1; then
    busybox ntpd -q -n -p pool.ntp.org >/dev/null 2>&1 && return 0
  fi

  if command -v sntp >/dev/null 2>&1; then
    sntp -sS pool.ntp.org >/dev/null 2>&1 && return 0
  fi

  if command -v ntpdate >/dev/null 2>&1; then
    ntpdate -u pool.ntp.org >/dev/null 2>&1 && return 0
  fi

  return 1
}

sync_clock_from_host() {
  date -u -s "@${HOST_EPOCH}" >/dev/null 2>&1 && return 0
  date -u -s "${HOST_UTC_ISO}" >/dev/null 2>&1 && return 0
  date -u -s "${HOST_UTC_BUSYBOX}" >/dev/null 2>&1 && return 0
  return 1
}

ensure_pasmo() {
  if command -v pasmo >/dev/null 2>&1; then
    echo "pasmo is already installed."
    return 0
  fi

  if ! command -v apt-get >/dev/null 2>&1; then
    echo "Error: pasmo is not installed and apt-get is unavailable."
    exit 1
  fi

  echo "pasmo not found. Installing with apt-get..."
  export DEBIAN_FRONTEND=noninteractive
  apt-get update
  apt-get install -y pasmo

  if ! command -v pasmo >/dev/null 2>&1; then
    echo "Error: pasmo installation completed but executable is still unavailable."
    exit 1
  fi

  echo "pasmo installed successfully."
}

download_model3_rom() {
  local destination="$1"
  echo "Downloading Model 3 ROM to ${destination}..."

  if command -v curl >/dev/null 2>&1; then
    curl -fL "$ROM_URL" -o "$destination"
    return
  fi

  if command -v wget >/dev/null 2>&1; then
    wget -O "$destination" "$ROM_URL"
    return
  fi

  echo "Error: neither curl nor wget is available to download model3.rom."
  exit 1
}

if ! command -v git >/dev/null 2>&1; then
  echo "Error: git is not installed on the RG35XXPRO."
  exit 1
fi

if ! command -v make >/dev/null 2>&1; then
  echo "Error: make is not installed on the RG35XXPRO."
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "Error: python3 is not installed on the RG35XXPRO."
  exit 1
fi

if ! command -v aclocal >/dev/null 2>&1; then
  echo "Error: aclocal is not installed on the RG35XXPRO."
  exit 1
fi

if ! command -v autoconf >/dev/null 2>&1; then
  echo "Error: autoconf is not installed on the RG35XXPRO."
  exit 1
fi

if ! command -v automake >/dev/null 2>&1; then
  echo "Error: automake is not installed on the RG35XXPRO."
  exit 1
fi

remote_epoch="$(read_remote_epoch)"
if [[ ! "$remote_epoch" =~ ^[0-9]+$ ]]; then
  echo "Error: unable to read RG35XXPRO system time with 'date +%s'."
  exit 1
fi

echo "Device UTC time before sync check: $(date -u '+%Y-%m-%d %H:%M:%S')"
clock_skew="$(clock_skew_seconds "$remote_epoch")"

if (( clock_skew > MAX_CLOCK_SKEW_SECONDS )); then
  echo "RG35XXPRO clock skew (${clock_skew}s) exceeds ${MAX_CLOCK_SKEW_SECONDS}s."
  echo "Attempting automatic clock synchronization..."

  if sync_clock_with_ntp; then
    echo "Clock synchronized using NTP."
  elif sync_clock_from_host; then
    echo "Clock synchronized from host time over SSH."
  else
    echo "Error: unable to set a current date/time on RG35XXPRO automatically."
    echo "Please set the correct date/time on the device and rerun this script."
    exit 1
  fi

  remote_epoch="$(read_remote_epoch)"
  if [[ ! "$remote_epoch" =~ ^[0-9]+$ ]]; then
    echo "Error: unable to re-check RG35XXPRO system time after sync attempt."
    exit 1
  fi

  clock_skew="$(clock_skew_seconds "$remote_epoch")"
  echo "Device UTC time after sync check: $(date -u '+%Y-%m-%d %H:%M:%S')"

  if (( clock_skew > MAX_CLOCK_SKEW_SECONDS )); then
    echo "Error: RG35XXPRO clock is still not current (skew ${clock_skew}s)."
    echo "Please set the correct date/time on the device and rerun this script."
    exit 1
  fi
else
  echo "RG35XXPRO clock is current (skew ${clock_skew}s)."
fi

echo "Removing any prior TRS-80 installation artifacts..."
rm -rf "$REPO_DIR"
rm -rf "${APPS_ROOT}/${APP_NAME}"
rm -f "${APPS_ROOT}/TRS-80.sh"
rm -f "${APPS_ROOT}/Imgs/TRS-80.png"

echo "Cloning repository (including submodules)..."
git clone --recursive "$REPO_URL"

cd "$REPO_DIR"

if [[ ! -d "tpk" ]]; then
  echo "Error: tpk submodule directory is missing after clone."
  exit 1
fi

ensure_pasmo

echo "Bootstrapping configure script (autogen.sh)..."
./autogen.sh

echo "Configuring..."
./configure

echo "Building sdl2trs..."
make -j"$(getconf _NPROCESSORS_ONLN || echo 1)"

echo "Building launcher and disk images with build.py..."
python3 rg35xx/build.py

INSTALL_ROOT="${APPS_ROOT}/${APP_NAME}"
INSTALL_DISKS_DIR="${INSTALL_ROOT}/disks"
INSTALL_ROMS_DIR="${INSTALL_ROOT}/roms"
APPS_IMGS_DIR="${APPS_ROOT}/Imgs"

echo "Installing files under ${APPS_ROOT}..."
mkdir -p "$INSTALL_DISKS_DIR" "$INSTALL_ROMS_DIR" "$APPS_IMGS_DIR"

install -m 0755 "sdl2trs" "${INSTALL_ROOT}/sdl2trs"
install -m 0755 "rg35xx/TRS-80.sh" "${APPS_ROOT}/TRS-80.sh"
install -m 0644 "rg35xx/TRS-80.png" "${APPS_IMGS_DIR}/TRS-80.png"
install -m 0644 "rg35xx/XTRSHARD-LICENSE.txt" "${INSTALL_ROOT}/XTRSHARD-LICENSE.txt"

install -m 0644 "out/hard-disk/games.hdv" "${INSTALL_DISKS_DIR}/games.hdv"
install -m 0644 "out/hard-disk/empty.hdv" "${INSTALL_DISKS_DIR}/empty.hdv"
install -m 0644 "out/hard-disk/ldos531-model3-boot.dsk" "${INSTALL_DISKS_DIR}/ldos531-model3-boot.dsk"
install -m 0644 "out/hard-disk/LIST.TXT" "${INSTALL_ROOT}/LIST.TXT"
install -m 0644 "out/hard-disk/LAUNCH.CMD" "${INSTALL_ROOT}/LAUNCH.CMD"
install -m 0644 "out/hard-disk/manifest.csv" "${INSTALL_ROOT}/manifest.csv"

download_model3_rom "${INSTALL_ROMS_DIR}/model3.rom"

echo "Installation completed successfully."
echo "Installed launcher script: ${APPS_ROOT}/TRS-80.sh"
echo "Installed app directory: ${INSTALL_ROOT}"
REMOTE_EOF

echo "Disconnected from ${DEVICE_IP}."
echo "If successful, TRS-80 is installed under: ${APPS_ROOT}/TRS-80 on the RG35XXPRO"
