# a cloudcp - copy all files from one rclone remote into another; open dest folder URL when done
LS=$(sh "$(dirname "$0")/cloudls.sh")   # lines: name:|type|account|used / total
NAMES=$(echo "$LS"|cut -d'|' -f1|tr -d ':')
echo "Remotes:";n=1;echo "$LS"|while IFS='|' read nm ty acct st;do printf "  %d) %-11s %s\n" "$n" "$nm" "$acct";n=$((n+1));done
printf "Copy FROM number: ";read a;SRC=$(echo "$NAMES"|sed -n "${a}p")
printf "Copy TO number:   ";read b;DST=$(echo "$NAMES"|sed -n "${b}p")
[ -z "$SRC" ]||[ -z "$DST" ]&&{ echo "x invalid pick";exit 1;}
F="$SRC"
echo "-- copy $SRC: into $DST:$F/  (live progress) --"
rclone copy "$SRC:" "$DST:$F/" -P --transfers=8 --drive-chunk-size=64M||{ echo "x copy failed";exit 1;}
if [ "$(rclone config show "$DST"|awk -F' = ' '/^type/{print $2;exit}')" = drive ];then
  ID=$(rclone lsjson "$DST:" --dirs-only 2>/dev/null|python3 -c "import sys,json;print(next((x['ID'] for x in json.load(sys.stdin) if x['Name']=='$F'),''))")
  URL="https://drive.google.com/drive/folders/$ID";command -v open >/dev/null&&open "$URL"
else URL="$DST:$F/";fi
echo;echo "done - $SRC: copied into $DST:$F/";echo "$URL"
printf "press enter to close...";read _
