# HoverNet Race Server

`RaceServer` provides the shared lobby and authoritative transport for multiplayer races. It accepts TCP client connections, manages hosted race sessions, starts races for all participants, and relays gameplay state and events between Windows and Linux clients.

## Build on Linux

From the repository root:

```bash
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --target RaceServer --parallel 2
```

## Run

```bash
./build/linux/RaceServer 9600 raceserver.log \
  --config NetTarget/RaceServer/config.xml \
  --max-races 50
```

The production clients use `outiva.com:9600`. A deployment must expose port `9600` and preserve `/etc/hovernet/config.xml` when upgrading the Debian package.

## Package and service

Build the Debian package with:

```bash
packaging/debian/build-raceserver-deb.sh VERSION build/linux/RaceServer
```

The package installs:

- `/usr/lib/hovernet/RaceServer`
- `/etc/hovernet/config.xml`
- `hovernet-raceserver.service`

The systemd unit runs the server as the unprivileged `hovernet` account. Production deployment is performed through the protected GitLab pipeline and its narrowly scoped deployment wrapper.

## Source layout

- `NetworkInterface/` — sockets, connections, and message dispatch
- `RaceManager/` — race creation, membership, and lifecycle
- `GameSimulation/` — server-side race state
- `Config/` — XML configuration
- `Logging/` — server logging
