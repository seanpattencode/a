# ADB Termux Control Methods

## Prerequisites
- USB debugging enabled on Android
- `adb` installed on host
- Termux installed on Android

## Basic ADB Commands

```bash
# Check device connected
adb devices

# Open Termux
adb shell "am start -n com.termux/.app.TermuxActivity"

# Take screenshot
adb shell "screencap -p /sdcard/screen.png"
adb pull /sdcard/screen.png /tmp/screen.png

# Send text input (escape spaces with \)
adb shell "input text 'hello'"
adb shell "input text 'hello\ world'"  # with space

# Press Enter
adb shell "input keyevent 66"

# Swipe/scroll (x1 y1 x2 y2)
adb shell "input swipe 360 1000 360 400"  # scroll down

# Tap at coordinates
adb shell "input tap 360 500"
```

## Running Termux Commands via ADB

### Method 1: Push script then run
```bash
# Create script locally
echo 'your commands here' > /tmp/script.sh

# Push to shared storage
adb push /tmp/script.sh /sdcard/script.sh

# Grant Termux storage access first (run once)
adb shell "input text 'termux-setup-storage'" && adb shell "input keyevent 66"
# Then allow in Android settings

# Run script from Termux
adb shell "input text 'bash\ ~/storage/shared/script.sh'" && adb shell "input keyevent 66"
```

### Method 2: Direct input (simple commands)
```bash
# Open Termux
adb shell "am start -n com.termux/.app.TermuxActivity"
sleep 1

# Type command (escape spaces)
adb shell "input text 'ls\ -la'"
adb shell "input keyevent 66"  # Enter

# Wait and screenshot
sleep 2
adb shell "screencap -p /sdcard/screen.png"
adb pull /sdcard/screen.png /tmp/screen.png
```

### Method 3: Output to pullable file
```bash
# Run command, save output to /sdcard/
adb shell "input text 'ls\ -la\ >\ ~/storage/shared/output.txt'"
adb shell "input keyevent 66"
sleep 2

# Pull output
adb pull /sdcard/output.txt /tmp/output.txt
cat /tmp/output.txt
```

## Termux Storage Paths
- `/sdcard/` = `~/storage/shared/` (after termux-setup-storage)
- Termux home: `/data/data/com.termux/files/home/`
- Termux bin: `/data/data/com.termux/files/usr/bin/`

## Common Issues

### Escaping
- Spaces: use `\ ` or `%s`
- Special chars: `\&\&` for `&&`, `\|` for `|`

### Permissions
- Termux can't access /sdcard by default
- Run `termux-setup-storage` and allow in Android settings
- Then use `~/storage/shared/` path

### Command not found
- Shell functions not loaded in non-interactive adb input
- Use full paths: `python3 ~/.local/bin/aio` instead of `aio`
- Or source bashrc first: `source\ ~/.bashrc\ &&\ aio`
