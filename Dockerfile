# FROM python:3.10

# WORKDIR /app

# COPY api/ ./api/
# COPY shared/ ./shared/

# WORKDIR /app/api

# RUN pip install flask flask-cors

# CMD ["python", "app.py"]

FROM python:3.10

# Install build tools
RUN apt-get update && apt-get install -y g++ make cmake

WORKDIR /app

COPY api/ ./api/
COPY shared/ ./shared/
COPY core/ ./core/
COPY jobs/ ./jobs/

# Build C++ scheduler (CMake-based)
WORKDIR /app/core/build
RUN cmake ..
RUN make

# Install Python deps
WORKDIR /app/api
RUN pip install flask flask-cors

WORKDIR /app

# Run scheduler + Flask
CMD ["bash", "-c", "./core/build/scheduler & python api/app.py"]