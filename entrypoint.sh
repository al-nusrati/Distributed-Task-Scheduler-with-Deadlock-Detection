#!/bin/bash
# Start the C++ scheduler in the background
./core/build/scheduler &

# Start the Flask API in the foreground
python api/app.py