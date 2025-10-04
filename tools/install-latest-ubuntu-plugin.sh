#!/bin/bash
set -euo pipefail

# Configurable paths - adjust these as needed
DOWNLOAD_DIR="${HOME}/Downloads/c64stream-tmp"
OBS_PLUGIN_LIB_DIR="${HOME}/.local/lib/obs-plugins"
OBS_PLUGIN_DATA_DIR="${HOME}/.local/share/obs/obs-plugins"
GITHUB_REPO="chrisgleissner/c64stream"

# Create temporary download directory
mkdir -p "${DOWNLOAD_DIR}"
cd "${DOWNLOAD_DIR}"
trap 'rm -rf "${DOWNLOAD_DIR}"' EXIT

echo "Fetching latest successful workflow run..."
# Get latest successful workflow run ID for the main branch
WORKFLOW_RUN_ID=$(curl -s \
    -H "Accept: application/vnd.github+json" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    "https://api.github.com/repos/${GITHUB_REPO}/actions/runs?status=success&branch=main" \
    | jq -r '.workflow_runs[0].id')

if [ -z "${WORKFLOW_RUN_ID}" ]; then
    echo "Error: Could not find latest successful workflow run"
    exit 1
fi

echo "Fetching artifacts for workflow run ${WORKFLOW_RUN_ID}..."
# Get artifact ID for Ubuntu build
ARTIFACT_ID=$(curl -s \
    -H "Accept: application/vnd.github+json" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    "https://api.github.com/repos/${GITHUB_REPO}/actions/runs/${WORKFLOW_RUN_ID}/artifacts" \
    | jq -r '.artifacts[] | select(.name | contains("ubuntu")) | .id')

if [ -z "${ARTIFACT_ID}" ]; then
    echo "Error: Could not find Ubuntu artifact in workflow run ${WORKFLOW_RUN_ID}"
    exit 1
fi

echo "Downloading artifact ${ARTIFACT_ID}..."
# Download the artifact
curl -s -L \
    -H "Accept: application/vnd.github+json" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    "https://nightly.link/chrisgleissner/c64stream/actions/artifacts/${ARTIFACT_ID}.zip" \
    --output plugin.zip

echo "Extracting files..."
# Unzip the downloaded artifact
unzip -q plugin.zip
# Extract the tar.xz file (using the actual name from inside the zip)
tar xf *.tar.xz

echo "Installing plugin..."
# Create target directories
mkdir -p "${OBS_PLUGIN_LIB_DIR}"
mkdir -p "${OBS_PLUGIN_DATA_DIR}"

# Copy plugin library
cp -r lib/x86_64-linux-gnu/obs-plugins/* "${OBS_PLUGIN_LIB_DIR}/"

# Copy plugin data
cp -r share/obs/obs-plugins/* "${OBS_PLUGIN_DATA_DIR}/"

echo "Plugin installed successfully!"
echo "Library installed to: ${OBS_PLUGIN_LIB_DIR}"
echo "Data files installed to: ${OBS_PLUGIN_DATA_DIR}"