#!/bin/bash
# WSL SSH Setup - run on WSL to enable SSH access from LAN

set -e

# Check if running on WSL
if ! grep -qi microsoft /proc/version 2>/dev/null; then
    echo "x Not running on WSL"; exit 1
fi

echo "=== WSL SSH Setup ==="

# Get IPs
WSL_IP=$(hostname -I | awk '{print $1}')
WIN_IP=$(powershell.exe -c "(Get-NetIPAddress -AddressFamily IPv4 | Where-Object { \$_.InterfaceAlias -notmatch 'Loopback|vEthernet|WSL' -and \$_.IPAddress -match '^192\.' }).IPAddress" 2>/dev/null | tr -d '\r\n')

echo "WSL IP: $WSL_IP"
echo "Windows LAN IP: $WIN_IP"

[ -z "$WIN_IP" ] && { echo "x Could not detect Windows LAN IP"; exit 1; }

# Ensure sshd running
if ! pgrep -x sshd >/dev/null; then
    echo "Starting sshd..."
    sudo service ssh start || sudo /usr/sbin/sshd
fi

# Set up port forwarding (prompts for admin)
echo "Setting up port forward (will prompt for admin)..."
powershell.exe -c "Start-Process powershell -Verb RunAs -Wait -ArgumentList '-c','netsh interface portproxy delete v4tov4 listenport=2222 listenaddress=0.0.0.0 2>\$null; netsh interface portproxy add v4tov4 listenport=2222 listenaddress=0.0.0.0 connectport=22 connectaddress=$WSL_IP; netsh advfirewall firewall delete rule name=\"WSL SSH\" 2>\$null; netsh advfirewall firewall add rule name=\"WSL SSH\" dir=in action=allow protocol=tcp localport=2222; echo Done; Start-Sleep 2'"

# Update aio ssh entry
NAME="${1:-wsl-$(hostname)}"
USER=$(whoami)
HOST="$USER@$WIN_IP:2222"

echo "Registering: $NAME = $HOST"
python3 ~/.local/bin/aio ssh rm "$NAME" 2>/dev/null || true

# Add to aio (need to handle password)
read -sp "SSH password for $USER: " PW; echo
python3 -c "
import sys; sys.path.insert(0, '$HOME/aio')
from aio_cmd._common import init_db, db, emit_event, db_sync
from aio_cmd.ssh import _enc
init_db()
c = db()
c.execute('INSERT OR REPLACE INTO ssh(name,host,pw) VALUES(?,?,?)', ('$NAME','$HOST',_enc('$PW')))
c.commit()
emit_event('ssh','add',{'name':'$NAME','host':'$HOST','pw':_enc('$PW')}, sync=True)
print('✓ Added $NAME = $HOST')
"

echo "=== Done! Test with: aio ssh $NAME ==="
