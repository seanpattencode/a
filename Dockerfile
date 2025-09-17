# Multi-stage build for minimal image size
FROM python:3.11-slim as base

# Set working directory
WORKDIR /app

# Install system dependencies if needed
RUN apt-get update && apt-get install -y --no-install-recommends \
    && rm -rf /var/lib/apt/lists/*

# Copy the orchestrator script
COPY orchestrator.py /app/

# Create necessary directories
RUN mkdir -p /app/Common /app/Programs /app/Common/Results \
    /app/Common/Logs /app/Common/Triggers /app/Common/TriggersProcessed

# Set environment variables
ENV PYTHONUNBUFFERED=1
ENV DEVICE_ID=docker-container
ENV DEVICE_TAGS=""

# Make script executable
RUN chmod +x orchestrator.py

# Default command
CMD ["python", "orchestrator.py"]