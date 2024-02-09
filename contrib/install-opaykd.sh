#!/bin/sh

set -e

if ! systemd-notify --booted 2>/dev/null; then
  echo "Error: systemd is not the active init system." >&2
  exit 1
fi

if [ ! "$(uname -m)" = "x86_64" ]; then
  echo "Error: Only x86_64 architecture is supported for now." >&2
  exit 1
fi

if systemctl list-unit-files opaykd.service >/dev/null; then
  echo "Error: opaykd service is already installed. Not reinstalling." >&2
  exit 1
fi

if [ $(id -u) -ne 0 ]; then
  echo "Error: This script must be run as root." >&2
  exit 1
fi

OPAYKD_PATH=/usr/bin/opaykd
OPAYKD_UNIT_PATH=/lib/systemd/system/opaykd.service
# TODO: change to https://github.com/opaykproject/opayk/releases/latest/download/opaykd-x86_64-linux when the first stable release is published
OPAYKD_URL=https://github.com/opaykproject/opayk/releases/download/v0.1.0/opaykd-x86_64-linux
OPAYKD_PID_PATH=/run/opaykd/opaykd.pid
OPAYKD_DATA_PATH=/var/lib/opaykd
OPAYKD_USER=opayk-daemon
OPAYKD_GROUP=opayk-daemon
DIR_MODE=0710

if [ -f "$OPAYKD_PATH" ]; then
  echo "Warning: $OPAYKD_PATH already exists. Not overwriting." >&2
else
  download_wget() {
    wget -O "$1" "$OPAYKD_URL"
  }

  download_curl() {
    curl -fLo "$1" "$OPAYKD_URL"
  }

  if command -v wget >/dev/null 2>&1; then
    dl_cmd=download_wget
  elif command -v curl >/dev/null 2>&1; then
    dl_cmd=download_curl
  else
    echo "Error: wget or curl is required." >&2
    exit 1
  fi

  tmp=$(mktemp)
  echo "Downloading opaykd binary..."
  $dl_cmd "$tmp"
  chmod 0755 "$tmp"
  mv "$tmp" "$OPAYKD_PATH"
fi

if id "$OPAYKD_USER" >/dev/null 2>&1; then
  echo "Warning: user $OPAYKD_USER already exists. Not creating." >&2
else
  if [ -f /usr/bin/nologin ]; then
    shell=/usr/bin/nologin 
  elif [ -f /usr/sbin/nologin ]; then
    shell=/usr/sbin/nologin 
  else
    shell=/bin/false
  fi

  useradd -rMUd "$OPAYKD_DATA_PATH" -s "$shell" "$OPAYKD_USER"
  usermod -L "$OPAYKD_USER"
fi

echo "Installing opaykd systemd service..."
cat >"$OPAYKD_UNIT_PATH" <<EOF
[Unit]
Description=Opayk Daemon
After=network.target

[Service]
ExecStart="$OPAYKD_PATH" -daemon -pid="$OPAYKD_PID_PATH" -conf=/etc/opaykd/opaykd.conf -datadir="$OPAYKD_DATA_PATH"
Type=forking
PIDFile=$OPAYKD_PID_PATH
Restart=on-failure
TimeoutStopSec=600
User=$OPAYKD_USER
Group=$OPAYKD_GROUP
RuntimeDirectory=opaykd
RuntimeDirectoryMode=$DIR_MODE
ConfigurationDirectory=opaykd
ConfigurationDirectoryMode=$DIR_MODE
StateDirectory=opaykd
StateDirectoryMode=$DIR_MODE
PrivateTmp=true
ProtectSystem=full
ProtectHome=true
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable opaykd.service
echo "Starting opaykd service..."
systemctl start opaykd.service
echo "Opayk daemon launched!"
