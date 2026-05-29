# emit one "remote:|type|account|used / total" line per rclone remote (for /cloud UI)
for r in $(rclone listremotes);do
  b="${r%:}";ty=$(rclone config show "$b"|awk -F' = ' '/^type/{print $2;exit}');id="";u="";t=""
  if [ "$ty" = drive ];then
    a=$(rclone about "$r" 2>/dev/null);u=$(echo "$a"|awk '/^Used:/{print $2,$3}');t=$(echo "$a"|awk '/^Total:/{print $2,$3}')
    k=$(rclone config show "$b"|grep -o '"access_token":"[^"]*"'|head -1|cut -d'"' -f4)
    id=$(curl -s 'https://www.googleapis.com/drive/v3/about?fields=user' -H "Authorization: Bearer $k"|grep -o '"emailAddress": *"[^"]*"'|cut -d'"' -f4)
  elif [ "$ty" = iclouddrive ];then
    id=$(rclone config show "$b"|awk -F' = ' '/^apple_id/{print $2;exit}')
    ck=$(rclone config show "$b"|sed -n 's/^cookies = //p')
    j=$(curl -s -m 15 'https://setup.icloud.com/setup/ws/1/storageUsageInfo' -X POST -H 'Origin: https://www.icloud.com' -H "Cookie: $ck" --data '{}')
    ub=$(echo "$j"|grep -o '"usedStorageInBytes":[0-9]*'|grep -o '[0-9]*$');tb=$(echo "$j"|grep -o '"totalStorageInBytes":[0-9]*'|grep -o '[0-9]*$')
    [ -n "$ub" ]&&u=$(awk "BEGIN{printf \"%.1f GiB\",$ub/1073741824}");[ -n "$tb" ]&&t=$(awk "BEGIN{printf \"%.0f GiB\",$tb/1073741824}")
  fi
  echo "$r|$ty|$id|$u / $t"
done
