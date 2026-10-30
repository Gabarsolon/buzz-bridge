#!/usr/bin/env bash
# Put your original netemu back (Linux, macOS, Git Bash). Needs: bash, curl, sha256sum or shasum.
#   ./restore.sh [PS3_IP]
#   ./restore.sh [PS3_IP] path/to/a/backup.self      (restore one specific saved copy)
# Without a file it uses your oldest saved copy that is not this project's modified netemu:
# first in ./backups, then the copies this installer left on the console in /dev_hdd0/buzzbridge/netemu_backup/.
set -euo pipefail

HOST="${1:-${PS3_HOST:-192.168.0.169}}"
CHOSEN="${2:-}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OURS_HASH='74e890bf7f77edfd92d4c0a57f45e73cf7fe39aa3c21cd7587948fb3c81ade40'
BACKUPS="$HERE/backups"
mkdir -p "$BACKUPS"

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
die() { echo "ERROR: $*" >&2; exit 1; }
command -v curl >/dev/null 2>&1 || die "curl is required."

SRC=""
if [ -n "$CHOSEN" ]; then
  [ -f "$CHOSEN" ] || die "File not found: $CHOSEN"
  SRC="$CHOSEN"
else
  # oldest local backup that is not ours (file names sort by time)
  for f in $(ls -1 "$BACKUPS"/ps2_netemu_*.self 2>/dev/null | sort); do
    if [ "$(sha256 "$f")" != "$OURS_HASH" ]; then SRC="$f"; break; fi
  done
  if [ -z "$SRC" ]; then
    echo "No local backup of your original found; looking on the console disk..."
    NAMES="$(curl --silent --max-time 30 --list-only "ftp://$HOST/dev_hdd0/buzzbridge/netemu_backup/" 2>/dev/null | tr -d '\r' | grep -E '^ps2_netemu_.*\.self$' | sort || true)"
    for n in $NAMES; do
      curl --fail --silent --show-error --max-time 120 -o "$BACKUPS/_console_$n" "ftp://$HOST/dev_hdd0/buzzbridge/netemu_backup/$n" || continue
      if [ "$(sha256 "$BACKUPS/_console_$n")" != "$OURS_HASH" ]; then SRC="$BACKUPS/_console_$n"; break; fi
    done
  fi
  [ -n "$SRC" ] || die "No saved copy of your original netemu was found (this installer saves one every time it runs). Give a file explicitly: ./restore.sh $HOST path/to/ps2_netemu.self"
fi
WANT="$(sha256 "$SRC")"
echo "Restoring: $SRC"
echo "  sha256 $WANT"

# Keep what is on the console now, then write.
TMP="$BACKUPS/_reading.self"
curl --fail --silent --show-error --max-time 120 -o "$TMP" "ftp://$HOST/dev_flash/ps2emu/ps2_netemu.self" \
  || die "Could not read the current netemu from ftp://$HOST/. Is the PS3 on, at the XMB, with webMAN MOD running? Nothing was changed."
CURRENT="$(sha256 "$TMP")"
KEPT="$BACKUPS/ps2_netemu_${CURRENT:0:8}_$(date +%Y%m%d_%H%M%S).self"
mv "$TMP" "$KEPT"
echo "Current netemu saved to: $KEPT"
if [ "$CURRENT" = "$WANT" ]; then echo "That file is already installed. Nothing to do."; exit 0; fi

VERIFY="$BACKUPS/_uploaded_check.self"
unmount() {
  curl --fail --silent --show-error -o /dev/null --max-time 20 "http://$HOST/dev_blind?0" || true
  rm -f "$VERIFY"
}
trap unmount EXIT
curl --fail --silent --show-error -o /dev/null --max-time 20 "http://$HOST/dev_blind?1" \
  || die "Could not make dev_flash writable (CFW with webMAN MOD needed). Nothing was changed."
curl --fail --silent --show-error --max-time 120 -T "$SRC" "ftp://$HOST/dev_blind/ps2emu/ps2_netemu.self" \
  || die "Upload failed. Run this script again before starting any PS2 game."
curl --fail --silent --show-error --max-time 120 -o "$VERIFY" "ftp://$HOST/dev_blind/ps2emu/ps2_netemu.self" \
  || die "Read-back failed. Run this script again before starting any PS2 game."
[ "$(sha256 "$VERIFY")" = "$WANT" ] || die "Read-back check failed. Run this script again before starting any PS2 game."
echo "OK restored; file read back and verified."
echo "Done. The original netemu is back; no restart is needed."
