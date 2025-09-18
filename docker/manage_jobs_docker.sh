#!/bin/bash
# Docker-aware wrapper for manage_jobs.py
# This script ensures manage_jobs.py works with the Docker container

cd /home/ubuntu/AIOS

# Run the manage_jobs.py script
python3 manage_jobs.py "$@"

# If we're triggering a job, give the container a moment to process it
if [ "$1" = "trigger" ]; then
    echo "Trigger sent. Checking container logs..."
    sleep 2
    docker-compose -f docker/docker-compose.yml logs --tail=20 | grep -E "(Starting job|completed|failed|ERROR)" | tail -5
fi