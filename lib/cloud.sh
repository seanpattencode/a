# a cloud — add iCloud Drive to rclone (SRP signin + 2FA; needs rclone >=1.74)
command -v rclone >/dev/null||{ echo "x rclone not installed";exit 1;}
S(){ rclone config create icloud iclouddrive "$@" --non-interactive 2>&1;}
echo "── Add iCloud Drive ──"
printf "Step 1/2 — Apple ID (email): ";read AID
printf "Step 2/2 — Password (shown): ";read PW
rclone config delete icloud 2>/dev/null
R=$(S apple_id="$AID" password="$PW")
ST=$(printf %s "$R"|sed -n 's/.*"State": "\([^"]*\)".*/\1/p')
while [ -n "$ST" ];do
  printf %s "$R"|grep -q config_2fa&&echo "Enter the 2FA code from your Apple device (or 'sms'):"||echo "$R"
  printf "code: ";read CODE
  R=$(S --continue --state "$ST" --result "$CODE")
  ST=$(printf %s "$R"|sed -n 's/.*"State": "\([^"]*\)".*/\1/p')
done
printf %s "$R"|grep -qi 'error\|fatal'&&{ printf %s "$R"|grep -i 'error';echo "x failed — rerun";exit 1;}
D=$(python3 -c "import os;print(os.path.dirname(os.path.realpath('$(command -v a)')))" 2>/dev/null);[ -n "$D" ]&&rm -f "$D/.cloud.html"
echo "✓ icloud added — reload the cloud page";printf "press enter to close…";read _
