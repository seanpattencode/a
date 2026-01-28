# Dual Nervous System Architecture

aio implements a biological-inspired dual communication system for distributed device control.

## Overview

| System | Analogy | Speed | Mechanism |
|--------|---------|-------|-----------|
| **Fast** | Motor neurons, reflexes | Milliseconds | SSH direct connection |
| **Slow** | Hormones, autonomic | Minutes | events.jsonl sync via git |

## Fast Nervous System (SSH)

**Purpose:** Immediate control, real-time commands, confirmation required

**Implementation:**
```
aio ssh <host> <cmd>     # Direct command execution
aio hub on/off <job>     # Remote timer control via SSH
aio run <host> "task"    # Remote task execution
aio ssh all "cmd"        # Broadcast to all devices
```

**Code path (hub.py:91-95):**
```python
if j[4].lower() != DEVICE_ID.lower():  # Job on different device
    hosts = {r[0].lower(): r[0] for r in db().execute("SELECT name FROM ssh")}
    r = sp.run([sys.executable, aio, 'ssh', hosts[j[4].lower()], 'aio', 'hub', wda, j[1]])
    print(r.stdout.strip() or f"x {j[4]} failed")
```

**Characteristics:**
- Point-to-point communication
- Immediate feedback (success/failure)
- Fails if target offline
- Requires SSH access (port 22/8022)

## Slow Nervous System (Events)

**Purpose:** State synchronization, eventual consistency, offline-tolerant

**Implementation:**
```
events.jsonl              # Append-only event log (source of truth)
db_sync()                 # Git push/pull to sync events
replay_events()           # Rebuild local db from events
```

**Event types:**
```python
emit_event("notes", "add", {"t": text})           # Note created
emit_event("hub", "add", {"name": n, ...})        # Job created
emit_event("projects", "add", {"path": p})        # Project added
emit_event("ssh", "add", {"name": n, "host": h})  # SSH host added
```

**Sync triggers:**
- After note/ssh/hub/project changes (immediate push)
- auto_backup() every 10 minutes
- Manual: `aio backup setup` initializes git sync

**Characteristics:**
- Broadcast to all devices (via git)
- No confirmation (fire & forget)
- Works offline (syncs when back online)
- Append-only (no conflicts, auto-merge)

## Architecture Split

### What belongs where:

| Fast (SSH) | Slow (Events) |
|------------|---------------|
| `hub on/off` - enable/disable timer | `hub add/rm` - create/delete job |
| `ssh <host> cmd` - run command | notes - create/edit/ack |
| Kill/restart processes | projects - add/remove |
| Real-time status queries | ssh hosts - add/remove |
| Emergency stop | Config/settings changes |

### Decision criteria:

Use **Fast (SSH)** when:
- Action must happen NOW
- Need confirmation it worked
- Target device is reachable
- Command is imperative ("do this")

Use **Slow (Events)** when:
- Eventual consistency is OK
- Target may be offline
- State should propagate to ALL devices
- Data is declarative ("this should exist")

## Backup System Integration

The backup system shows both nervous systems:

```
Local:  ~/.local/share/aios/events.jsonl (source)
Git:    github.com/user/aio-sync (slow sync - immediate on write)
GDrive: account /aio-backup (slow sync - scheduled via hub)
```

GDrive sync uses **both** systems:
1. **Slow:** Hub job `gdrive-sync` created via events
2. **Fast:** Timer controlled via SSH if on remote device
3. **Slow:** Actual sync runs on schedule, syncs via rclone

## Current Implementation Status

| Component | Fast | Slow | Notes |
|-----------|------|------|-------|
| Hub jobs | on/off via SSH | add/rm via events | Hybrid |
| Notes | - | Full event sync | Slow only |
| SSH hosts | - | Full event sync | Slow only |
| Projects | - | Full event sync | Slow only |
| Remote commands | SSH direct | - | Fast only |
| Backup | - | Git + GDrive sync | Slow only |

## Future Possibilities

1. **Health monitoring:** Fast heartbeat pings via SSH
2. **Fast broadcast:** Parallel SSH to all devices for urgent commands
3. **Slow triggers fast:** Event that causes recipient to SSH back
4. **Presence detection:** Track which devices are online
5. **Fallback chain:** Try SSH, fall back to event if unreachable

## Trade-offs

| | Fast | Slow |
|--|------|------|
| Latency | ~100ms | ~10min (sync interval) |
| Reliability | Fails if offline | Eventually consistent |
| Complexity | SSH setup required | Git setup required |
| Scalability | O(n) connections | O(1) git sync |
| Security | SSH keys/passwords | Git auth |

## OODA Loop Constraint

A system's decision speed is governed by its slowest component. In the OODA loop (Observe, Orient, Decide, Act), bottlenecks compound:

```
[Observe] -> [Orient] -> [Decide] -> [Act]
    |           |           |          |
   slow       slow        slow       slow  = very slow overall
   fast       fast        fast       fast  = fast overall
```

For distributed systems across devices, **internet latency is the floor**. No local optimization beats network round-trip time.

**Why SSH:**
- Not the absolute fastest option
- But it **always works** - universal, reliable, well-understood
- Available on every device (Linux, Mac, Termux, WSL)
- No additional infrastructure needed
- Encrypted, authenticated by default

**Future faster options (not needed yet):**
- WebSockets for persistent connections
- UDP for fire-and-forget commands
- Local mesh networking
- Message queues (Redis, ZeroMQ)

The pragmatic choice: use SSH until it becomes the bottleneck. Currently, human decision-making is slower than SSH round-trip, so SSH is not the limiting factor.

## Conclusion

The dual system provides:
- **Responsiveness** when needed (SSH)
- **Resilience** for state (events)
- **Simplicity** by using existing tools (SSH, git)

Design principle: **Use reliable tools that "always work" over theoretically faster but complex alternatives.** Optimize only when the current system becomes the bottleneck.

This mirrors biological systems where fast reflexes handle immediate threats while slower hormonal systems maintain homeostasis.
