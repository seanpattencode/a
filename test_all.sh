#!/bin/bash
echo "Testing all operations..."

echo -e "\n1. TODO OPERATIONS"
python3 programs/todo/todo.py add "Test task 1"
python3 programs/todo/todo.py add "Test task 2"
python3 programs/todo/todo.py list
python3 programs/todo/todo.py done 1
python3 programs/todo/todo.py list
python3 programs/todo/todo.py clear
python3 programs/todo/todo.py list

echo -e "\n2. SERVICE OPERATIONS"
python3 programs/service/service.py start webapp
python3 programs/service/service.py start database
python3 programs/service/service.py list
python3 programs/service/service.py status webapp
python3 programs/service/service.py stop webapp
python3 programs/service/service.py list

echo -e "\n3. BACKUP OPERATION"
mkdir -p /tmp/test_src && echo "data" > /tmp/test_src/file.txt
python3 programs/backup/backup.py

echo -e "\n4. SCRAPER OPERATION"
python3 programs/scraper/scraper.py

echo -e "\n5. PLANNER OPERATION"  
python3 programs/planner/planner.py

echo -e "\n6. API TEST"
python3 aios_api.py &
API_PID=$!
sleep 2
curl -s http://localhost:8000/status
kill $API_PID 2>/dev/null

echo -e "\nAll tests completed!"
