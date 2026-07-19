# Salamis

Salamis is a small C++17 vector database prototype. It keeps vectors in memory for fast exact nearest-neighbor search and persists the collection to a physical binary store on disk.

## License

Salamis is licensed under the GNU General Public License version 3.0 only. See [LICENSE](LICENSE) for the full license text.

## Features

- In-memory vector map keyed by string ids
- Physical binary backing file that reloads on startup
- Exact `top-k` search with cosine similarity, L2 distance, or dot product
- Cross-platform build and storage path handling for Linux and Windows
- Snapshot writes through a temporary file followed by replacement
- Dependency-free CMake build
- Minimal CLI and unit tests
- User and admin web consoles for distributed deployments
- Management CLI for node inventory, shard ownership, and health checks

## Modules

- `SalmisCore`: local in-memory vector store, search, and physical persistence
- `SalmisCluster`: distributed shard ownership and fan-out query coordination
- `SalmisRuntime`: cross-platform TCP client/server runtime
- `SalmisWebConsole`: user/admin browser UI and HTTP API
- `SalmisManagement`: command-line management reports and health checks

## Build on Linux

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Linux Packages

Salamis can produce Debian packages for Ubuntu/Debian systems and RPM packages for RHEL-compatible systems.

Ubuntu or Debian prerequisites:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build
```

RHEL-compatible prerequisites:

```sh
sudo dnf install -y gcc-c++ cmake ninja-build rpm-build
```

Build and test packages:

```sh
sh scripts/build-linux-packages.sh
```

Or run CPack directly:

```sh
cmake -S . -B build-package -DCMAKE_BUILD_TYPE=Release
cmake --build build-package --config Release
ctest --test-dir build-package --build-config Release --output-on-failure
cpack --config build-package/CPackConfig.cmake -G DEB
cpack --config build-package/CPackConfig.cmake -G RPM
```

The `.deb` package targets Ubuntu/Debian. The `.rpm` package targets RHEL-compatible distributions such as Red Hat Enterprise Linux, Rocky Linux, AlmaLinux, and Fedora.

To test from Ubuntu WSL on Windows:

```powershell
wsl.exe bash -lc "cd /mnt/c/Work/gitrepo/Salamis && sh scripts/build-linux-packages.sh build-wsl-package"
```

## Build on Windows

PowerShell with Visual Studio, Ninja, MinGW, or another CMake-supported C++17 compiler works:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

## CLI on Linux

```sh
./build/salamis_cli ./data/vectors.svdb put doc-1 0.1 0.2 0.3
./build/salamis_cli ./data/vectors.svdb put doc-2 0.9 0.1 0.0
./build/salamis_cli ./data/vectors.svdb search 2 1.0 0.0 0.0
./build/salamis_cli ./data/vectors.svdb get doc-1
./build/salamis_cli ./data/vectors.svdb delete doc-1
```

## CLI on Windows

```powershell
.\build\Release\salamis_cli.exe .\data\vectors.svdb put doc-1 0.1 0.2 0.3
.\build\Release\salamis_cli.exe .\data\vectors.svdb put doc-2 0.9 0.1 0.0
.\build\Release\salamis_cli.exe .\data\vectors.svdb search 2 1.0 0.0 0.0
.\build\Release\salamis_cli.exe .\data\vectors.svdb get doc-1
.\build\Release\salamis_cli.exe .\data\vectors.svdb delete doc-1
```

Single-config generators such as Ninja may emit `.\build\salamis_cli.exe` instead.

## Distributed Mode

Salamis can run as a 1-node database, a 2-node cluster, a 3-node cluster, or a larger shared-nothing cluster. Each node owns a shard of ids, stores its shard on local disk, and serves a simple TCP protocol. The cluster client hashes vector ids to the owning node for writes and deletes, then fans search requests out to every node and merges the returned top-k results.

Use one of the provided examples:

```text
examples/cluster-1-node.conf
examples/cluster-2-node.conf
examples/cluster-3-node.conf
```

The format is one node per line:

```text
# node-id host port local-store-path
node-a 127.0.0.1 7101 ./data/node-a.svdb
```

Start one process per configured node. A 1-node deployment:

```sh
./build/salamis_cli serve ./examples/cluster-1-node.conf node-a
```

A 2-node deployment:

```sh
./build/salamis_cli serve ./examples/cluster-2-node.conf node-a
./build/salamis_cli serve ./examples/cluster-2-node.conf node-b
```

A 3-node deployment:

```sh
./build/salamis_cli serve ./examples/cluster-3-node.conf node-a
./build/salamis_cli serve ./examples/cluster-3-node.conf node-b
./build/salamis_cli serve ./examples/cluster-3-node.conf node-c
```

Use the distributed client with whichever config you started:

```sh
./build/salamis_cli cluster ./examples/cluster-3-node.conf put doc-1 0.1 0.2 0.3
./build/salamis_cli cluster ./examples/cluster-3-node.conf put doc-2 0.9 0.1 0.0
./build/salamis_cli cluster ./examples/cluster-3-node.conf search 2 1.0 0.0 0.0
./build/salamis_cli cluster ./examples/cluster-3-node.conf delete doc-1
```

On Windows, use the generated `.exe` path for the same commands.

To grow beyond three nodes, add more lines with unique node ids and ports. Salamis will route writes across all configured nodes. Changing the node count changes shard ownership for existing ids; a production deployment would add online rebalancing before resizing a live cluster.

The current distributed implementation is intentionally simple: deterministic hash sharding, one TCP request per connection, exact local search, and fan-out query merge. A production cluster would add replication, membership changes with rebalancing, request timeouts, authentication, and a binary protocol.

## Web Consoles

Start the shard nodes first, then start the cross-platform GUI module:

```sh
./build/salamis_cli gui ./examples/cluster-3-node.conf 127.0.0.1 8080
```

Open:

- User console: `http://127.0.0.1:8080/`
- Admin console: `http://127.0.0.1:8080/admin`

The user console supports vector upsert and similarity search. The admin console shows the configured cluster nodes and backing store paths.

The `web` command is retained as an alias for deployments that prefer that name:

```sh
./build/salamis_cli web ./examples/cluster-3-node.conf 127.0.0.1 8080
```

The same web server exposes API endpoints:

```sh
curl -X POST http://127.0.0.1:8080/api/vectors \
  -d '{"id":"doc-1","values":[0.1,0.2,0.3]}'

curl -X POST http://127.0.0.1:8080/api/search \
  -d '{"limit":5,"values":[1.0,0.0,0.0]}'

curl http://127.0.0.1:8080/api/admin/cluster
```

On Windows PowerShell, use `curl.exe` if `curl` is aliased to `Invoke-WebRequest`.
For JSON requests, `Invoke-RestMethod` is usually cleaner:

```powershell
Invoke-RestMethod -Method Post http://127.0.0.1:8080/api/vectors `
  -Body '{"id":"doc-1","values":[0.1,0.2,0.3]}' `
  -ContentType 'application/json'
```

## Test Coverage

The test suite covers:

- Upsert, get, delete, search, and persistence reloads
- Cosine, L2, and dot-product ranking
- Dimension, empty-vector, and non-finite-value validation
- On-disk replacement of existing records
- Cluster config loading, duplicate-node rejection, and deterministic shard selection
- TCP protocol command handling
- User/admin web console routes and API payload validation
- Management CLI report formatting and node health protocol

## Management CLI

Use the management commands from Linux shells, Windows PowerShell, or Command Prompt:

```sh
./build/salamis_cli manage ./examples/cluster-3-node.conf nodes
./build/salamis_cli manage ./examples/cluster-3-node.conf owner doc-1
./build/salamis_cli manage ./examples/cluster-3-node.conf health
```

Windows example:

```powershell
.\build\salamis_cli.exe manage .\examples\cluster.conf nodes
.\build\salamis_cli.exe manage .\examples\cluster.conf owner doc-1
.\build\salamis_cli.exe manage .\examples\cluster.conf health
```

`health` sends `PING` to each configured shard node and reports `up`, `degraded`, or `down`.

## File Format

The physical store is a binary snapshot:

1. `uint32` magic: `SALV`
2. `uint32` version
3. `uint64` record count
4. `uint64` vector dimensions
5. For each record: `uint64` id byte length, id bytes, then `float32[dimensions]`

The current implementation rewrites the snapshot after each mutation. That is simple and durable enough for a prototype; a production version would likely add a write-ahead log, mmap reads, quantized indexes, and an approximate nearest-neighbor structure such as HNSW.
