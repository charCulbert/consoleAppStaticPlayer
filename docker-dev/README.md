# Docker Linux Development Environment

## Quick Start

```bash
# Navigate to docker-dev folder
cd docker-dev

# Build and run the container
docker-compose up --build

# Or run directly with Docker
docker build -t audio-dev .
docker run -it --rm -v $(pwd)/..:/workspace audio-dev
```

## Inside the Container

Your project will be mounted at `/workspace`. You can:

```bash
# See your files
ls -la

# Check ALSA is available
aplay -l

# Build with your existing CMake setup
mkdir build && cd build
cmake ..
make

# Or build directly
g++ -std=c++17 -o consoleAppStaticPlayer \
    main.cpp AudioFilePlayerModule.cpp BufferedAudioFilePlayerModule.cpp \
    -I./include -lasound -lpthread

# Test ALSA devices
cat /proc/asound/cards
```

## What's Included

- Ubuntu 22.04 LTS
- Build tools (gcc, cmake, make)
- ALSA development libraries
- Debugging tools (gdb, valgrind)
- Your project mounted as volume

## Audio Testing

Note: Audio hardware access in Docker is limited. This environment is mainly for:
- Building and testing compilation
- Validating ALSA API calls
- Development and debugging

Real audio testing should be done on actual Pi hardware.