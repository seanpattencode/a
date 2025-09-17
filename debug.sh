#!/bin/bash

echo "AIOS Docker Debug Script"
echo "========================"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to check Docker installation
check_docker() {
    echo -e "${YELLOW}Checking Docker installation...${NC}"
    if command -v docker &> /dev/null; then
        echo -e "${GREEN}✓ Docker is installed${NC}"
        docker --version
    else
        echo -e "${RED}✗ Docker is not installed${NC}"
        exit 1
    fi

    if command -v docker-compose &> /dev/null; then
        echo -e "${GREEN}✓ Docker Compose is installed${NC}"
        docker-compose --version
    else
        echo -e "${RED}✗ Docker Compose is not installed${NC}"
        exit 1
    fi
    echo ""
}

# Function to build the Docker image
build_image() {
    echo -e "${YELLOW}Building Docker image...${NC}"
    docker build -t aios-orchestrator:latest .
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ Image built successfully${NC}"
    else
        echo -e "${RED}✗ Failed to build image${NC}"
        exit 1
    fi
    echo ""
}

# Function to run tests
run_tests() {
    echo -e "${YELLOW}Running tests in Docker...${NC}"
    docker run --rm \
        -v $(pwd)/Common:/app/Common \
        -v $(pwd)/Programs:/app/Programs \
        -e DEVICE_ID=docker-test \
        -e DEVICE_TAGS=test \
        aios-orchestrator:latest \
        python orchestrator.py --test

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ Tests passed${NC}"
    else
        echo -e "${RED}✗ Tests failed${NC}"
    fi
    echo ""
}

# Function to run orchestrator interactively
run_interactive() {
    echo -e "${YELLOW}Running orchestrator interactively...${NC}"
    docker run -it --rm \
        -v $(pwd)/Common:/app/Common \
        -v $(pwd)/Programs:/app/Programs \
        -e DEVICE_ID=docker-debug \
        -e DEVICE_TAGS=gpu,storage,browser \
        aios-orchestrator:latest \
        /bin/bash
}

# Function to check logs
check_logs() {
    echo -e "${YELLOW}Checking logs...${NC}"
    if [ -d "./Common/Logs" ]; then
        echo "Latest log files:"
        ls -lt ./Common/Logs | head -5
        echo ""
        if [ -n "$(ls -A ./Common/Logs 2>/dev/null)" ]; then
            latest_log=$(ls -t ./Common/Logs/*.log 2>/dev/null | head -1)
            if [ -n "$latest_log" ]; then
                echo "Last 20 lines from latest log ($latest_log):"
                tail -20 "$latest_log"
            fi
        fi
    else
        echo "No logs directory found"
    fi
    echo ""
}

# Function to check state
check_state() {
    echo -e "${YELLOW}Checking state file...${NC}"
    if [ -f "./Common/state.json" ]; then
        echo "State file exists. Content:"
        python3 -m json.tool ./Common/state.json 2>/dev/null || cat ./Common/state.json
    else
        echo "No state file found"
    fi
    echo ""
}

# Function to clean up
cleanup() {
    echo -e "${YELLOW}Cleaning up...${NC}"
    read -p "Are you sure you want to clean up all generated files? (y/N) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        rm -rf ./Common ./Programs
        echo -e "${GREEN}✓ Cleanup complete${NC}"
    else
        echo "Cleanup cancelled"
    fi
    echo ""
}

# Function to optimize with docker-slim
optimize_with_slim() {
    echo -e "${YELLOW}Optimizing image with docker-slim...${NC}"

    # Check if docker-slim is installed
    if ! command -v docker-slim &> /dev/null; then
        echo -e "${RED}docker-slim is not installed${NC}"
        echo "Install it from: https://github.com/docker-slim/docker-slim"
        return 1
    fi

    # Build the image first
    build_image

    # Run docker-slim
    echo "Running docker-slim build..."
    docker-slim build \
        --target aios-orchestrator:latest \
        --tag aios-orchestrator:slim \
        --http-probe=false \
        --continue-after=30 \
        --cmd "python orchestrator.py --test"

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ Slim image created${NC}"
        echo ""
        echo "Image size comparison:"
        docker images | grep aios-orchestrator
    else
        echo -e "${RED}✗ Failed to create slim image${NC}"
    fi
    echo ""
}

# Main menu
show_menu() {
    echo "Select an option:"
    echo "1) Check Docker installation"
    echo "2) Build Docker image"
    echo "3) Run tests"
    echo "4) Run orchestrator interactively (debug shell)"
    echo "5) Start with docker-compose"
    echo "6) Stop docker-compose"
    echo "7) View docker-compose logs"
    echo "8) Check logs"
    echo "9) Check state"
    echo "10) Optimize with docker-slim"
    echo "11) Clean up all files"
    echo "12) Run all checks"
    echo "q) Quit"
    echo ""
    read -p "Enter choice: " choice

    case $choice in
        1) check_docker ;;
        2) build_image ;;
        3) run_tests ;;
        4) run_interactive ;;
        5) docker-compose up -d && echo -e "${GREEN}✓ Started${NC}" ;;
        6) docker-compose down && echo -e "${GREEN}✓ Stopped${NC}" ;;
        7) docker-compose logs -f ;;
        8) check_logs ;;
        9) check_state ;;
        10) optimize_with_slim ;;
        11) cleanup ;;
        12)
            check_docker
            build_image
            run_tests
            check_logs
            check_state
            ;;
        q|Q) exit 0 ;;
        *) echo -e "${RED}Invalid option${NC}" ;;
    esac

    echo ""
    read -p "Press Enter to continue..."
    clear
    show_menu
}

# Start the script
clear
show_menu