#!/usr/bin/env python3
"""WSL SSH Setup - run on WSL to enable SSH access from LAN"""
import sys, os, subprocess as sp, getpass

if not os.path.exists('/proc/version') or 'microsoft' not in open('/proc/version').read().lower():
    sys.exit("x Not WSL")

WSL_IP = sp.run("hostname -I", shell=True, capture_output=True, text=True).stdout.split()[0]
WIN_IP = sp.run(["powershell.exe", "-c", "(Get-NetIPAddress -AddressFamily IPv4|Where-Object{$_.InterfaceAlias -notmatch 'Loopback|vEthernet|WSL' -and $_.IPAddress -match '^192\\.'}).IPAddress"], capture_output=True, text=True).stdout.strip()
print(f"WSL: {WSL_IP}  Windows: {WIN_IP}")
if not WIN_IP: sys.exit("x No Windows LAN IP found")

sp.run("pgrep -x sshd || sudo service ssh start || sudo /usr/sbin/sshd", shell=True)
sp.run(["powershell.exe", "-c", f"Start-Process powershell -Verb RunAs -Wait -ArgumentList '-c',\"netsh interface portproxy delete v4tov4 listenport=2222 listenaddress=0.0.0.0 2>\\$null; netsh interface portproxy add v4tov4 listenport=2222 listenaddress=0.0.0.0 connectport=22 connectaddress={WSL_IP}; netsh advfirewall firewall delete rule name=`\"WSL SSH`\" 2>\\$null; netsh advfirewall firewall add rule name=`\"WSL SSH`\" dir=in action=allow protocol=tcp localport=2222\""])

name, user, host = sys.argv[1] if len(sys.argv) > 1 else f"wsl-{os.uname().nodename}", getpass.getuser(), f"{getpass.getuser()}@{WIN_IP}:2222"
print(f"Registering: {name} = {host}")
pw = getpass.getpass("SSH password: ")

sys.path.insert(0, os.path.expanduser("~/aio"))
from aio_cmd._common import init_db, db, emit_event
from aio_cmd.ssh import _enc
init_db(); c = db(); c.execute("INSERT OR REPLACE INTO ssh(name,host,pw)VALUES(?,?,?)", (name, host, _enc(pw))); c.commit()
emit_event("ssh", "add", {"name": name, "host": host, "pw": _enc(pw)}, sync=True)
print(f"✓ {name} = {host}\nTest: aio ssh {name}")
