"""
SYNC ARCHITECTURE REQUIREMENTS
==============================

Problem Statement:
------------------
The current db_sync() implementation syncs aio.db (binary SQLite) via git.
This causes corruption when multiple devices sync simultaneously because:
1. Git cannot merge binary files
2. git stash/pop on binary SQLite corrupts data
3. Race conditions between concurrent db_sync() calls

Requirements:
-------------
1. GITHUB ONLY - No external services (no S3, no cloud sync services)
2. GIT VERSIONED - Full history, can restore any version via git checkout
3. SINGLE SQLITE FILE - aio.db remains the source of truth for queries
4. APPEND ONLY - Never DELETE or UPDATE, only INSERT (soft deletes via archive flag)
5. ZERO MERGE CONFLICTS - Architecture must make conflicts impossible
6. RASPBERRY PI COMPATIBLE - Lightweight, no heavy dependencies
7. NO EXTERNAL TOOLS - Only sqlite3 + git (no cr-sqlite, no litestream)

Solution: Append-Only Event Log
-------------------------------
Structure:
    events.sql (text, synced via git) <--> aio.db (SQLite, local query cache)

    - events.sql is append-only INSERT statements
    - Git auto-merges appends (no conflicts possible)
    - aio.db is rebuilt from events.sql
    - Both representations are equivalent/isomorphic

Schema Design:
    CREATE TABLE events (
        id TEXT PRIMARY KEY,      -- UUID, globally unique
        ts REAL NOT NULL,         -- Unix timestamp with subseconds
        device TEXT NOT NULL,     -- Device ID that created this event
        tbl TEXT NOT NULL,        -- Target table: "projects", "notes", etc.
        op TEXT NOT NULL,         -- Operation: "add" or "archive"
        data TEXT NOT NULL        -- JSON payload
    );

    -- Views reconstruct current state from events
    CREATE VIEW projects_live AS
    SELECT
        json_extract(data, '$.id') as id,
        json_extract(data, '$.path') as path,
        ts as created_at
    FROM events
    WHERE tbl = 'projects' AND op = 'add'
    AND json_extract(data, '$.id') NOT IN (
        SELECT json_extract(data, '$.id')
        FROM events
        WHERE tbl = 'projects' AND op = 'archive'
    );

Sync Flow:
    1. WRITE: Insert event into events table + append to events.sql
    2. PUSH:  git add events.sql && git commit && git push
    3. PULL:  git pull (auto-merges appends) && replay events.sql into db
    4. QUERY: Use views that reconstruct current state from events

Why This Works:
    - Append-only text file = git auto-merges, never conflicts
    - UUIDs = no collision between devices
    - Timestamps = deterministic ordering for replay
    - SQLite views = fast queries on current state
    - Full git history = can restore any point in time

Migration Path:
    1. Add events table and views to schema
    2. Migrate existing data to events format
    3. Update all INSERT/UPDATE/DELETE to emit events instead
    4. Update db_sync() to sync events.sql only
    5. Add aio.db to .gitignore (local cache, not synced)

Performance Considerations:
    - Incremental replay: Track last_applied_event, only replay new events
    - Periodic compaction: Snapshot state, archive old events
    - Views are fast: SQLite optimizes json_extract with indexes

Reference Implementations:
    - Chrome Sync: Operation-based sync, local db is cache
    - Linear: Custom sync engine with operation log
    - Notion: SQLite cache + server-authoritative sync
    - Kafka: Append-only log with consumer offsets

Author: Claude + Sean
Date: 2026-01-26
"""

# Implementation will go here after requirements are approved
