#!/bin/bash

# C64 Stream - Local GitHub Actions Runner using 'act'
# This script runs GitHub Actions workflows locally for testing

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

# Default values
WORKFLOW=""
EVENT_TYPE="push"
PLATFORM=""
JOB=""
DRY_RUN=false
VERBOSE=false
PULL_IMAGES=true
REUSE_CONTAINERS=false

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1" 
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

usage() {
    cat << EOF
C64 Stream - Local GitHub Actions Runner

Usage: $0 [options]

OPTIONS:
    --workflow WORKFLOW     Workflow to run (build-project, check-format, or all)
    --event EVENT          Event type (push, pull_request, workflow_dispatch)
    --platform PLATFORM   Specific platform to test (ubuntu, macos, windows)
    --job JOB              Specific job to run
    --dry-run              Show what would be executed without running
    --no-pull              Don't pull Docker images (use cached)
    --reuse                Reuse containers between runs
    --verbose              Enable verbose output
    --help                 Show this help message

WORKFLOWS:
    build-project          Full multi-platform build and test
    check-format           Code formatting validation only  
    all                    Run all workflows

EXAMPLES:
    $0                                          # Run default build-project workflow
    $0 --workflow build-project --event push   # Run build workflow as push event
    $0 --platform ubuntu --verbose             # Run only Ubuntu build with verbose output
    $0 --workflow check-format --dry-run       # Check what format workflow would do
    $0 --job ubuntu-build                      # Run specific job only

PREREQUISITES:
    - Docker installed and running
    - 'act' tool installed (https://github.com/nektos/act)
    - At least 4GB free disk space for images

NOTES:
    - First run will download large Docker images (~2GB)
    - Use --no-pull to speed up subsequent runs
    - Windows builds run in Linux containers with cross-compilation
    - Some GitHub-specific features may not work exactly as in CI
EOF
}

check_prerequisites() {
    log_info "Checking prerequisites..."
    
    # Check for act
    if ! command -v act >/dev/null 2>&1; then
        log_error "'act' tool is not installed"
        log_info "Install with: curl --proto '=https' --tlsv1.2 -sSf https://raw.githubusercontent.com/nektos/act/master/install.sh | sudo bash"
        exit 1
    fi
    
    # Check for Docker
    if ! command -v docker >/dev/null 2>&1; then
        log_error "Docker is not installed"
        log_info "Install Docker: https://docs.docker.com/engine/install/"
        exit 1
    fi
    
    # Check if Docker daemon is running
    if ! docker info >/dev/null 2>&1; then
        log_error "Docker daemon is not running"
        log_info "Start Docker and try again"
        exit 1
    fi
    
    # Check available disk space (Docker images are large)
    local available_space
    available_space=$(df . | tail -1 | awk '{print $4}')
    if [[ $available_space -lt 4000000 ]]; then # 4GB in KB
        log_warning "Less than 4GB free space available. Docker images require significant space."
    fi
    
    log_success "Prerequisites check passed"
}

setup_act_configuration() {
    log_info "Setting up act configuration..."
    
    # Create .actrc if it doesn't exist
    if [[ ! -f .actrc ]]; then
        cat > .actrc << 'EOF'
# Act configuration for C64 Stream
--container-architecture linux/amd64
--artifact-server-path /tmp/act-artifacts
--env CI=true
--env GITHUB_ACTIONS=true
EOF
        log_info "Created .actrc configuration file"
    fi
    
    # Create platform-specific runner mappings
    if [[ ! -f .github/act-platforms.json ]]; then
        mkdir -p .github
        cat > .github/act-platforms.json << 'EOF'
{
    "ubuntu-24.04": "catthehacker/ubuntu:act-24.04",
    "ubuntu-latest": "catthehacker/ubuntu:act-24.04", 
    "ubuntu-22.04": "catthehacker/ubuntu:act-22.04",
    "macos-15": "catthehacker/ubuntu:act-24.04",
    "macos-latest": "catthehacker/ubuntu:act-24.04",
    "windows-2022": "catthehacker/ubuntu:act-24.04",
    "windows-latest": "catthehacker/ubuntu:act-24.04"
}
EOF
        log_info "Created platform mappings for act"
    fi
}

run_workflow() {
    local workflow="$1"
    local event="$2"
    
    log_info "Running workflow: $workflow (event: $event)"
    
    # Determine workflow file
    local workflow_file
    case "$workflow" in
        build-project)
            # For build-project, we need to run the trigger workflows
            case "$event" in
                push) workflow_file=".github/workflows/push.yaml" ;;
                pull_request) workflow_file=".github/workflows/pr-pull.yaml" ;;
                workflow_dispatch) workflow_file=".github/workflows/dispatch.yaml" ;;
                *) 
                    log_error "Invalid event type for build-project: $event"
                    exit 1
                    ;;
            esac
            ;;
        check-format)
            workflow_file=".github/workflows/check-format.yaml"
            ;;
        *)
            log_error "Unknown workflow: $workflow"
            exit 1
            ;;
    esac
    
    # Build act command
    local -a act_args=(
        "--workflows" "$workflow_file"
        "--eventpath" "/dev/null"
        "-P" "ubuntu-24.04=catthehacker/ubuntu:act-24.04"
        "-s" "GITHUB_TOKEN=$(gh auth token)"
    )
    
    if [[ "$VERBOSE" == "true" ]]; then
        act_args+=("--verbose")
    fi
    
    if [[ "$DRY_RUN" == "true" ]]; then
        act_args+=("--dryrun")
    fi
    
    if [[ "$PULL_IMAGES" == "false" ]]; then
        act_args+=("--pull=false")
    fi
    
    if [[ "$REUSE_CONTAINERS" == "true" ]]; then
        act_args+=("--reuse")
    fi
    
    if [[ -n "$JOB" ]]; then
        act_args+=("--job" "$JOB")
    fi
    
    # Add event-specific arguments
    case "$event" in
        push)
            act_args+=("push")
            ;;
        pull_request)
            act_args+=("pull_request")
            ;;
        workflow_dispatch)
            act_args+=("workflow_dispatch")
            ;;
    esac
    
    log_info "Executing: act ${act_args[*]}"
    
    # Run act
    if act "${act_args[@]}"; then
        log_success "Workflow completed successfully"
    else
        log_error "Workflow failed"
        return 1
    fi
}

run_platform_specific() {
    local platform="$1"
    
    case "$platform" in
        ubuntu)
            log_info "Running Ubuntu-specific build..."
            run_workflow "build-project" "$EVENT_TYPE"
            ;;
        macos)
            log_info "Running macOS build simulation (in Ubuntu container)..."
            log_warning "Note: macOS builds run in Linux containers with limitations"
            JOB="macos-build" run_workflow "build-project" "$EVENT_TYPE"
            ;;
        windows)
            log_info "Running Windows build simulation (cross-compilation)..."
            JOB="windows-build" run_workflow "build-project" "$EVENT_TYPE"
            ;;
        *)
            log_error "Unknown platform: $platform"
            exit 1
            ;;
    esac
}

main() {
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --workflow)
                WORKFLOW="$2"
                shift 2
                ;;
            --event)
                EVENT_TYPE="$2"
                shift 2
                ;;
            --platform)
                PLATFORM="$2"
                shift 2
                ;;
            --job)
                JOB="$2"
                shift 2
                ;;
            --dry-run)
                DRY_RUN=true
                shift
                ;;
            --no-pull)
                PULL_IMAGES=false
                shift
                ;;
            --reuse)
                REUSE_CONTAINERS=true
                shift
                ;;
            --verbose)
                VERBOSE=true
                shift
                ;;
            --help|-h)
                usage
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                usage
                exit 1
                ;;
        esac
    done
    
    # Set defaults
    if [[ -z "$WORKFLOW" ]]; then
        if [[ -n "$PLATFORM" ]]; then
            WORKFLOW="build-project"
        else
            WORKFLOW="build-project"
        fi
    fi
    
    log_info "C64 Stream - Local GitHub Actions Runner"
    log_info "Workflow: $WORKFLOW"
    log_info "Event: $EVENT_TYPE"
    [[ -n "$PLATFORM" ]] && log_info "Platform: $PLATFORM"
    [[ -n "$JOB" ]] && log_info "Job: $JOB"
    
    # Check prerequisites
    check_prerequisites
    
    # Setup act configuration
    setup_act_configuration
    
    # Execute workflow
    if [[ -n "$PLATFORM" ]]; then
        run_platform_specific "$PLATFORM"
    elif [[ "$WORKFLOW" == "all" ]]; then
        log_info "Running all workflows..."
        run_workflow "check-format" "push"
        run_workflow "build-project" "$EVENT_TYPE"
    else
        run_workflow "$WORKFLOW" "$EVENT_TYPE"
    fi
    
    log_success "Local GitHub Actions testing completed!"
    
    # Cleanup recommendations
    log_info ""
    log_info "Cleanup recommendations:"
    log_info "  - Remove containers: docker container prune"
    log_info "  - Remove images: docker image prune" 
    log_info "  - Remove act artifacts: rm -rf /tmp/act-artifacts"
}

# Ensure script is run from correct directory
cd "$PROJECT_ROOT"

main "$@"