#!/bin/bash

set -e  # Stop on error

# Configuration
MODEL_FILE="model.pt"
EXECUTABLE_NAME="daqmodels_exec"

echo "🔧 Cleaning build folder..."
rm -rf build
mkdir -p build
cd build

echo "🔨 Running CMake..."
cmake ..

echo "📦 Building project..."
make -j$(nproc)

echo "📁 Copying model file..."
if [ -f "../$MODEL_FILE" ]; then
    cp "../$MODEL_FILE" "./$MODEL_FILE"
else
    echo "❌ ERROR: $MODEL_FILE not found in project root!"
    exit 1
fi

echo "🚀 Running executable..."
./$EXECUTABLE_NAME
