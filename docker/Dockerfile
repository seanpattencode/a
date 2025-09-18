FROM python:3.11-slim

WORKDIR /app

# Copy requirements file if it exists
COPY requirements.txt* ./

# Install dependencies if requirements.txt exists
RUN if [ -f requirements.txt ]; then pip install --no-cache-dir -r requirements.txt; fi

# Create Programs directory
RUN mkdir -p /app/Programs

# Set environment variables
ENV PYTHONUNBUFFERED=1
ENV DEVICE_ID=docker-container
ENV DEVICE_TAGS=""

# Note: orchestrator.py will be mounted at runtime
CMD ["python", "orchestrator.py"]