# AIOS - Python Workflow Orchestrator

A lightweight Python workflow orchestrator with Docker support and multi-device coordination.

## Quick Start

### Using the Debug Script (Recommended)

```bash
./debug.sh
```

This provides an interactive menu for all operations.

### Manual Docker Commands

#### Build the Docker image:
```bash
docker build -t aios-orchestrator:latest .
```

#### Run tests:
```bash
docker run --rm \
  -v $(pwd)/Common:/app/Common \
  -v $(pwd)/Programs:/app/Programs \
  -e DEVICE_ID=docker-test \
  -e DEVICE_TAGS=test \
  aios-orchestrator:latest \
  python orchestrator.py --test
```

#### Run the orchestrator:
```bash
docker run -d \
  --name aios-orchestrator \
  -v $(pwd)/Common:/app/Common \
  -v $(pwd)/Programs:/app/Programs \
  -e DEVICE_ID=docker-main \
  -e DEVICE_TAGS=gpu,storage,browser \
  aios-orchestrator:latest
```

### Using Docker Compose

#### Start services:
```bash
docker-compose up -d
```

#### View logs:
```bash
docker-compose logs -f
```

#### Stop services:
```bash
docker-compose down
```

## Docker Slim Optimization

To create an optimized minimal image:

1. Install docker-slim from: https://github.com/docker-slim/docker-slim

2. Run optimization:
```bash
./debug.sh
# Select option 10
```

Or manually:
```bash
docker-slim build \
  --target aios-orchestrator:latest \
  --tag aios-orchestrator:slim \
  --http-probe=false \
  --continue-after=30 \
  --cmd "python orchestrator.py --test"
```

## Features

- **Multi-device coordination**: Supports running across multiple containers/devices
- **Job scheduling**: Daily, interval, random, trigger-based, and always-running jobs
- **Docker isolation**: Jobs can run in isolated Docker containers
- **Automatic retry**: Configurable retry logic with exponential backoff
- **State persistence**: Shared state across all devices
- **Comprehensive logging**: Detailed logs for debugging
- **Test mode**: Built-in comprehensive test suite

## Directory Structure

```
AIOS/
├── orchestrator.py       # Main orchestrator script
├── Dockerfile           # Docker image definition
├── docker-compose.yml   # Multi-container setup
├── debug.sh            # Interactive debug script
├── README_DOCKER.md     # This file
├── Common/             # Shared data directory
│   ├── Results/        # Job results
│   ├── Logs/          # Application logs
│   ├── Triggers/      # Job triggers
│   ├── TriggersProcessed/ # Processed triggers
│   └── state.json     # Shared state file
└── Programs/          # Job scripts directory
```

## Environment Variables

- `DEVICE_ID`: Unique identifier for this device/container
- `DEVICE_TAGS`: Comma-separated tags (e.g., "gpu,storage,browser")
- `PYTHONUNBUFFERED`: Set to 1 for immediate log output

## Job Types

1. **always**: Continuously running daemons
2. **daily**: Runs once per day at specified time
3. **interval**: Runs at fixed intervals
4. **random_daily**: Runs randomly within a time window
5. **trigger**: Runs when triggered via trigger files
6. **idle**: Runs when no other tasks are active

## Debugging

### Check container status:
```bash
docker ps -a | grep aios
```

### View real-time logs:
```bash
docker logs -f aios-orchestrator
```

### Interactive debugging:
```bash
docker run -it --rm \
  -v $(pwd)/Common:/app/Common \
  -v $(pwd)/Programs:/app/Programs \
  aios-orchestrator:latest \
  /bin/bash
```

### Check state file:
```bash
cat Common/state.json | python -m json.tool
```

## Troubleshooting

### Issue: Jobs not running
- Check device tags match job requirements
- Verify state file permissions
- Check logs in `Common/Logs/`

### Issue: Docker jobs failing
- Ensure Docker daemon is running
- Check volume mount permissions
- Verify Python scripts in `Programs/` directory

### Issue: State not syncing
- Check file lock at `Common/.state.lock`
- Verify shared volume is properly mounted
- Check file permissions

## Performance Optimization

1. **Use docker-slim** to reduce image size by up to 30x
2. **Mount volumes** for frequently changing data
3. **Adjust worker threads** in JobScheduler for your workload
4. **Use selective device tags** to distribute load

## Security Notes

- Never commit sensitive data to state.json
- Use Docker secrets for credentials
- Regularly update base images
- Review job scripts before deployment

## License

MIT