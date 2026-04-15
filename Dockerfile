FROM python:3.10

# Install all required build tools and language runtimes
RUN apt-get update && apt-get install -y \
    g++ make cmake \
    nodejs \
    default-jdk \
    nginx \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy source code
COPY api/ ./api/
COPY shared/ ./shared/
COPY core/ ./core/
COPY jobs/ ./jobs/
COPY dashboard/ /usr/share/nginx/html/
COPY nginx/default.conf /etc/nginx/conf.d/default.conf

# Remove default Nginx site to avoid conflicts
RUN rm -f /etc/nginx/sites-enabled/default

# Build C++ scheduler from the correct directory
WORKDIR /app/core/build
RUN cmake .. && make

# Install Python dependencies
WORKDIR /app/api
RUN pip install flask flask-cors

# Go back to app root and expose port 80
WORKDIR /app
EXPOSE 80

# Start scheduler from its own build directory so relative paths work,
# then start Flask and Nginx from the app root.
CMD ["bash", "-c", "cd /app/core/build && ./scheduler & cd /app && python api/app.py & nginx -g 'daemon off;'"]