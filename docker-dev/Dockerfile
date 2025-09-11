FROM ubuntu:22.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install development tools and ALSA
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    g++ \
    libasound2-dev \
    alsa-utils \
    pkg-config \
    git \
    vim \
    gdb \
    valgrind \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /workspace

# Create a non-root user for development
RUN groupadd -r devuser && useradd -r -g devuser -u 1000 devuser
RUN chown -R devuser:devuser /workspace

# Switch to non-root user
USER devuser

# Default command
CMD ["/bin/bash"]