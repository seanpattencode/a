# a cloudcp <src> <dst> - copy all files src: into dst:<src>/; write status to /tmp/.a_cloudcp.status
ST=/tmp/.a_cloudcp.status;SRC="$1";DST="$2";F="$SRC"
[ -z "$SRC" ]||[ -z "$DST" ]&&{ echo "error|need src and dst" >"$ST";exit 1;}
echo "running|$SRC -> $DST" >"$ST"
if rclone copy "$SRC:" "$DST:$F/" --ignore-size --transfers=8 --drive-chunk-size=64M >/tmp/.a_cloudcp.log 2>&1;then
  if [ "$(rclone config show "$DST"|awk -F' = ' '/^type/{print $2;exit}')" = drive ];then
    ID=$(rclone lsjson "$DST:" --dirs-only 2>/dev/null|python3 -c "import sys,json;print(next((x['ID'] for x in json.load(sys.stdin) if x['Name']=='$F'),''))")
    URL="https://drive.google.com/drive/folders/$ID"
  else URL="$DST:$F/";fi
  echo "done|$URL" >"$ST"
else echo "error|copy failed - see /tmp/.a_cloudcp.log" >"$ST";fi
