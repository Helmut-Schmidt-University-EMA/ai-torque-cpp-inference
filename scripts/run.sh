#!/bin/bash
set -e

EXECUTABLE_NAME="daqmodels_exec"
MODEL_FILE="model.pt"

cd build || {
    echo "❌ Build directory not found. Run build.sh first."
    exit 1  
    echo "❌ Executable not found. Run build.sh first."
    exit 1
fi

if [ -f "../$MODEL_FILE" ]; then
    cp -u "../$MODEL_FILE" "./$MODEL_FILE"
fi

echo "🚀 Running executable..."
./$EXECUTABLE_NAME
