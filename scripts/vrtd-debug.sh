#!/usr/bin/env bash
# Enable debug logging for the vrtd service via a systemd drop-in override.

set -euo pipefail

OVERRIDE_DIR="/etc/systemd/system/vrtd.service.d"

mkdir -p "$OVERRIDE_DIR"
cat > "$OVERRIDE_DIR/debug.conf" <<'EOF'
[Service]
LogLevelMax=debug
EOF

systemctl daemon-reload
systemctl restart vrtd

echo "vrtd debug logging enabled. View with: journalctl -u vrtd -f"
