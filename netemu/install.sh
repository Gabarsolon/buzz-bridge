#!/usr/bin/env bash
# Install the modified ps2_netemu.self on a CFW PS3 (Linux, macOS, Git Bash). Needs: bash, curl, sha256sum or shasum.
#   ./install.sh [PS3_IP]          (default 192.168.0.169)
#   FORCE=1 ./install.sh [PS3_IP]  (skip the question for an unknown current file)
# The PS3 must be at the XMB with webMAN MOD running (its FTP and web server).
set -euo pipefail

HOST="${1:-${PS3_HOST:-192.168.0.169}}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/ps2_netemu.self"
TARGET_HASH='74e890bf7f77edfd92d4c0a57f45e73cf7fe39aa3c21cd7587948fb3c81ade40'
STOCK_HASH='bad3e45c9a81f25726e414b146ea5a112f28216ab4bdd2fd9f5c81b33d04c80a'   # Evilnat 4.93 netemu

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
die() { echo "ERROR: $*" >&2; exit 1; }
command -v curl >/dev/null 2>&1 || die "curl is required."

[ -f "$SRC" ] || die "ps2_netemu.self is missing next to this script."
[ "$(sha256 "$SRC")" = "$TARGET_HASH" ] || die "ps2_netemu.self is not the file I validated; nothing was changed."

BACKUPS="$HERE/backups"
mkdir -p "$BACKUPS"

# 1. Read what is on the console now and keep it. Nothing is written yet.
TMP="$BACKUPS/_reading.self"
curl --fail --silent --show-error --max-time 120 -o "$TMP" "ftp://$HOST/dev_flash/ps2emu/ps2_netemu.self" \
  || die "Could not read the current netemu from ftp://$HOST/. Is the PS3 on, at the XMB, with webMAN MOD running? Nothing was changed."
CURRENT="$(sha256 "$TMP")"
BACKUP="$BACKUPS/ps2_netemu_${CURRENT:0:8}_$(date +%Y%m%d_%H%M%S).self"
mv "$TMP" "$BACKUP"
echo "Current netemu saved to: $BACKUP"
echo "  sha256 $CURRENT"

if [ "$CURRENT" = "$TARGET_HASH" ]; then echo "This netemu is already installed. Nothing to do."; exit 0; fi

if [ "$CURRENT" != "$STOCK_HASH" ] && [ "${FORCE:-0}" != "1" ]; then
  echo
  echo "The netemu on your console is not the Evilnat 4.93 one this file was made from."
  echo "The backup above lets you put it back. Overwriting is usually fine but is not tested on other firmware."
  read -r -p "Type OVERWRITE to continue: " ANSWER
  [ "$ANSWER" = "OVERWRITE" ] || { echo "Cancelled. Nothing was changed."; exit 0; }
fi

# 2. Also keep a copy on the console's own disk.
curl --fail --silent --show-error --max-time 120 --ftp-create-dirs -T "$BACKUP" \
  "ftp://$HOST/dev_hdd0/buzzbridge/netemu_backup/$(basename "$BACKUP")" \
  || echo "WARNING: could not copy the backup to the console disk. The copy on this computer is still there." >&2

# 3. Overwrite, verify by reading it back, always unmount.
VERIFY="$BACKUPS/_uploaded_check.self"
unmount() {
  curl --fail --silent --show-error -o /dev/null --max-time 20 "http://$HOST/dev_blind?0" || true
  rm -f "$VERIFY"
}
trap unmount EXIT
curl --fail --silent --show-error -o /dev/null --max-time 20 "http://$HOST/dev_blind?1" \
  || die "Could not make dev_flash writable (CFW with webMAN MOD needed). Nothing was changed."
curl --fail --silent --show-error --max-time 120 -T "$SRC" "ftp://$HOST/dev_blind/ps2emu/ps2_netemu.self" \
  || die "Upload failed. Run ./restore.sh before starting any PS2 game."
curl --fail --silent --show-error --max-time 120 -o "$VERIFY" "ftp://$HOST/dev_blind/ps2emu/ps2_netemu.self" \
  || die "Read-back failed. Run ./restore.sh before starting any PS2 game."
[ "$(sha256 "$VERIFY")" = "$TARGET_HASH" ] || die "Read-back check failed. Run ./restore.sh before starting any PS2 game."
echo "OK installed; file read back and verified."
echo "Done. Plug the dongle in and start your PS2 Buzz! game; no restart is needed."
