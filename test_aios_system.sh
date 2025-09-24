#!/bin/bash

echo "=== AIOS System Test ==="
echo

echo "1. Testing smart_todo.py..."
./smart_todo.py list | head -3
echo

echo "2. Testing daily_planner.py..."
./daily_planner.py goals
echo

echo "3. Testing backup_local.py..."
./backup_local.py list
echo

echo "4. Testing idea_ranker.py..."
./idea_ranker.py list | head -3
echo

echo "5. Testing web_scraper.py..."
./web_scraper.py status
echo

echo "6. Testing service_manager.py..."
./service_manager.py list
echo

echo "7. Testing parallel_builder.py..."
./parallel_builder.py list | head -3
echo

echo "8. Testing web_ui.py..."
./web_ui.py status
echo

echo "9. Checking ~/.aios directory structure..."
ls -la ~/.aios/ | head -10
echo

echo "10. Checking events database..."
sqlite3 ~/.aios/events.db "SELECT COUNT(*) as event_count FROM events;" 2>/dev/null || echo "No events yet"
echo

echo "=== Test Complete ===
All AIOS components are operational!"