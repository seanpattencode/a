#!/usr/bin/env python3
"""
AIOS Web Server - Provides REST API for orchestrator control
"""

from flask import Flask, jsonify, request
import sqlite3
import json
import time
from pathlib import Path

app = Flask(__name__)

# Database path
DB_PATH = Path(__file__).parent.parent / "orchestrator.db"


def get_db():
    """Get database connection"""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def add_trigger(job_name, args=None, kwargs=None):
    """Add a trigger to the database"""
    conn = get_db()
    conn.execute(
        "INSERT INTO triggers (job_name, args, kwargs, created) VALUES (?, ?, ?, ?)",
        (job_name, json.dumps(args or []), json.dumps(kwargs or {}), time.time())
    )
    conn.commit()
    conn.close()


@app.route('/')
def index():
    """Root endpoint"""
    return jsonify({
        'status': 'running',
        'service': 'AIOS Web Server',
        'endpoints': {
            '/jobs': 'List all jobs',
            '/jobs/<name>': 'Get job status',
            '/restart/<name>': 'Restart a specific job',
            '/restart/all': 'Restart all jobs',
            '/logs': 'View recent logs',
            '/triggers': 'View pending triggers',
            '/health': 'Health check'
        }
    })


@app.route('/jobs')
def list_jobs():
    """List all jobs and their status"""
    conn = get_db()
    jobs = conn.execute("""
        SELECT s.name, s.type, s.enabled, j.status, j.last_update, j.pid
        FROM scheduled_jobs s
        LEFT JOIN jobs j ON s.name = j.job_name
        ORDER BY s.name
    """).fetchall()
    conn.close()

    return jsonify([dict(job) for job in jobs])


@app.route('/jobs/<name>')
def get_job(name):
    """Get specific job status"""
    conn = get_db()
    job = conn.execute("""
        SELECT s.*, j.status, j.last_update, j.pid
        FROM scheduled_jobs s
        LEFT JOIN jobs j ON s.name = j.job_name
        WHERE s.name = ?
    """, (name,)).fetchone()
    conn.close()

    if job:
        return jsonify(dict(job))
    else:
        return jsonify({'error': f'Job {name} not found'}), 404


@app.route('/restart/<name>', methods=['POST', 'GET'])
def restart_job(name):
    """Restart a specific job or all jobs"""
    if name == 'all':
        add_trigger('SYSTEM_RESTART')
        return jsonify({
            'status': 'triggered',
            'action': 'restart_all',
            'message': 'System restart triggered'
        })
    else:
        add_trigger(f'RESTART_{name}')
        return jsonify({
            'status': 'triggered',
            'action': f'restart_{name}',
            'message': f'Restart triggered for job: {name}'
        })


@app.route('/logs')
def get_logs():
    """Get recent logs"""
    limit = request.args.get('limit', 50, type=int)
    level = request.args.get('level', None)

    conn = get_db()
    if level:
        logs = conn.execute("""
            SELECT * FROM logs
            WHERE level = ?
            ORDER BY timestamp DESC
            LIMIT ?
        """, (level, limit)).fetchall()
    else:
        logs = conn.execute("""
            SELECT * FROM logs
            ORDER BY timestamp DESC
            LIMIT ?
        """, (limit,)).fetchall()
    conn.close()

    return jsonify([dict(log) for log in logs])


@app.route('/triggers')
def get_triggers():
    """Get pending triggers"""
    conn = get_db()
    triggers = conn.execute("""
        SELECT * FROM triggers
        WHERE processed IS NULL
        ORDER BY created DESC
    """).fetchall()
    conn.close()

    return jsonify([dict(trigger) for trigger in triggers])


@app.route('/health')
def health():
    """Health check endpoint"""
    conn = get_db()

    # Count running jobs
    running = conn.execute(
        "SELECT COUNT(*) as count FROM jobs WHERE status = 'running'"
    ).fetchone()['count']

    # Get last log entry
    last_log = conn.execute(
        "SELECT timestamp FROM logs ORDER BY timestamp DESC LIMIT 1"
    ).fetchone()

    conn.close()

    return jsonify({
        'status': 'healthy',
        'running_jobs': running,
        'last_log': last_log['timestamp'] if last_log else None,
        'timestamp': time.time()
    })


@app.route('/trigger/<job_name>', methods=['POST'])
def trigger_job(job_name):
    """Manually trigger a job"""
    data = request.get_json() or {}
    args = data.get('args', [])
    kwargs = data.get('kwargs', {})

    add_trigger(job_name, args, kwargs)

    return jsonify({
        'status': 'triggered',
        'job': job_name,
        'args': args,
        'kwargs': kwargs
    })


def run_server():
    """Main entry point for the web server"""
    print("Starting AIOS Web Server on http://localhost:8080")
    app.run(host='0.0.0.0', port=8080, debug=False)


if __name__ == "__main__":
    run_server()