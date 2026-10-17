# HoverNet authoritative race server image.
#   docker build -f packaging/server/Dockerfile -t hovernet-server .
#   docker run -p 127.0.0.1:9610:9610 -v hovernet-data:/var/lib/hovernet hovernet-server
# Build stage: compile the server from this checkout (no SDL, no graphics: it needs only a C++14 compiler and CMake).
FROM debian:12-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake python3 git ca-certificates libsdl2-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF && cmake --build build --target HoverNetAuthServer HoverNetModTool --parallel

FROM debian:12-slim
RUN useradd --system --home /var/lib/hovernet --create-home hovernet
COPY --from=build /src/build/HoverNetAuthServer /src/build/HoverNetModTool /usr/bin/
COPY NetTarget/Tracks /usr/share/hovernet/NetTarget/Tracks
COPY packaging/server/hovernet-server.conf /etc/hovernet/server.conf
WORKDIR /usr/share/hovernet
USER hovernet
# Inside a container the server must listen on all interfaces (publish the port to loopback on the host, as above).
EXPOSE 9610
ENTRYPOINT ["/usr/bin/HoverNetAuthServer", "--config", "/etc/hovernet/server.conf", "--bind", "0.0.0.0"]
