# tinyredis

tinyredis reproduces the core functionality of Redis.

A tiny Redis-compatible server written in C++17.

---

tinyredis is a compact Redis-style server with a real networking path, RESP protocol support, an in-memory database, TTL, snapshot persistence, and a small CLI.

It is inspired by Redis, but stays intentionally small enough to read, modify, and benchmark end-to-end.

## Highlights

- **Redis protocol path**: RESP parser, encoder, TCP server, and CLI
- **Non-blocking IO**: `kqueue` on macOS, `epoll` on Linux
- **Core string commands**: `PING`, `SET`, `GET`, `DEL`, `INCR`, `MGET`, `MSET`, TTL commands, and `SAVE`
- **Persistence**: snapshot load/save through a compact local dump format
- **Tests and benchmarks**: CTest-based tests plus `redis-benchmark` comparison

## Support matrix

tinyredis implements a small, explicit Redis-compatible surface. Anything not listed here should be treated as unsupported.

| Area | Status | Support |
| --- | --- | --- |
| Protocol | Supported | RESP array requests with bulk string arguments; simple string, error, integer, bulk string, null bulk string, and array responses |
| Networking | Supported | TCP server on IPv4, persistent client connections, multiple clients, and pipelined commands |
| Event loop | Supported | Non-blocking IO with `kqueue` on macOS and `epoll` on Linux |
| CLI | Supported | One-shot commands, interactive mode, quoted arguments, nested array response printing, and `-p` port selection |
| String commands | Supported | `PING`, `SET`, `GET`, `EXISTS`, `DEL`, `INCR`, `DECR`, `MGET`, `MSET` |
| Expiration commands | Supported | `SET key value EX seconds`, `EXPIRE`, `TTL`, `PERSIST` |
| Persistence | Supported | Snapshot load on startup, `SAVE`, and snapshot save on graceful shutdown |
| Limits | Supported | Max key size: 1 KiB; max value size: 1 MiB; max RESP array length: 1024; max request size: 4 MiB |
| Redis compatibility | Partial | Command names are case-insensitive, but only the commands and options above are implemented |
| Redis server features | Not supported | Replication, clustering, Sentinel, Lua scripting, ACL, pub/sub, transactions, modules, streams, and advanced data structures |

## Benchmark

`benchmark.sh` compares tinyredis with official Redis over loopback TCP using `redis-benchmark`. It is designed to show realistic performance differences rather than prove that either server is faster.

Each command/profile case starts from a fresh server process, preloads data using a fixed random seed, then runs the benchmark command using the same seed and keyspace. Both servers receive identical random key sequences, making the comparison reproducible and fair. State-changing commands (DEL, EXPIRE, PERSIST) include a realistic mix of successful and repeated operations.

This isolation avoids cross-command state pollution at the cost of a slower benchmark run. Results are from single runs and should be interpreted as indicative, not statistical conclusions.

It covers every supported tinyredis command: `PING`, `SET`, `SET key value EX seconds`, `GET`, `EXISTS`, `DEL`, `INCR`, `DECR`, `MGET`, `MSET`, `EXPIRE`, `TTL`, `PERSIST`, and `SAVE`.

The default run uses multiple profiles to cover single-client latency, non-pipelined concurrency, pipelined throughput, and larger payload pressure:

| Profile | Purpose |
| --- | --- |
| `-n 10000 -c 1 -P 1 -d 16` | Baseline single-client latency |
| `-n 100000 -c 50 -P 1 -d 128` | Concurrent non-pipelined requests |
| `-n 200000 -c 50 -P 16 -d 128` | Pipelined throughput |
| `-n 50000 -c 200 -P 32 -d 1024` | High concurrency with larger payloads |

The command cases are grouped by behavior in the generated reports: baseline, random write, random read hit, random update, stateful (mixed), hot read hit, and persistence. Random-key cases use a fixed seed and an automatically sized keyspace by default (max(requests × 10, 100000)), producing approximately 5% key reuse per case.

`SAVE` is benchmarked separately with `SAVE_DATASET_KEYS` keys and `SAVE_BENCHMARK_REQUESTS` requests per profile. Note that Redis RDB and tinyredis snapshots are different persistence backends; this comparison measures SAVE command latency, not raw IO throughput.

## Results (pipelined throughput)

The following results are from profile-3 (`-n 200000 -c 50 -P 16 -d 128`) on a local loopback connection with a release build (macOS, Apple M-series). Higher throughput is better; lower p50 is better.

| Command | tinyredis throughput | Redis throughput | tinyredis p50 | Redis p50 |
| --- | ---: | ---: | ---: | ---: |
| `PING` | 2.70M req/s | 2.67M req/s | 0.072 ms | 0.088 ms |
| `SET` | 1.92M req/s | 1.53M req/s | 0.200 ms | 0.288 ms |
| `SET_EX` | 1.27M req/s | 0.94M req/s | 0.136 ms | 0.144 ms |
| `GET` | 2.02M req/s | 2.13M req/s | 0.104 ms | 0.144 ms |
| `EXISTS` | 2.33M req/s | 2.30M req/s | 0.176 ms | 0.104 ms |
| `DEL` | 2.17M req/s | 1.98M req/s | 0.168 ms | 0.112 ms |
| `INCR` | 2.30M req/s | 2.17M req/s | 0.192 ms | 0.104 ms |
| `DECR` | 2.17M req/s | 2.20M req/s | 0.128 ms | 0.104 ms |
| `MGET` | 2.11M req/s | 1.74M req/s | 0.144 ms | 0.136 ms |
| `MSET` | 1.04M req/s | 0.89M req/s | 0.216 ms | 0.328 ms |
| `EXPIRE` | 1.69M req/s | 1.29M req/s | 0.192 ms | 0.184 ms |
| `TTL` | 2.17M req/s | 2.30M req/s | 0.200 ms | 0.128 ms |
| `PERSIST` | 1.64M req/s | 2.00M req/s | 0.184 ms | 0.160 ms |

tinyredis is generally competitive on the hot path: SET, SET_EX, MGET, MSET, EXPIRE, and DEL show higher throughput, while Redis leads on PERSIST and TTL, and the remaining commands are within a small margin.

### High concurrency limit

Profile-4 (`-n 50000 -c 200 -P 32 -d 1024`) tests throughput with 200 concurrent clients and 1 KiB payloads. Redis handles this profile on all commands. tinyredis crashes with a connection reset during the first command (PING). The event loop and single-threaded design hit a limit with this concurrency pattern — the server cannot keep up with 200 clients in pipeline-32 mode. This is a known limitation of the current implementation.

### Persistence

`SAVE` benchmarks with a 10,000-key dataset:

| Profile | tinyredis throughput | Redis throughput |
| --- | ---: | ---: |
| Single client, 14 B payload | 20.73 req/s | 144.93 req/s |
| Concurrent, 128 B payload | 20.62 req/s | 130.72 req/s |
| Pipelined, 128 B payload | 20.71 req/s | 130.08 req/s |
| High concurrency, 1 KiB payload | (crashed) | 83.12 req/s |

Redis RDB persists to a compact binary format with optimized IO. tinyredis uses a simple custom binary format written through `write`+`fsync`. The throughput difference is expected and reflects the complexity gap between the two persistence engines.

```sh
./benchmark.sh
```

## Quick start

```sh
./build.sh
./build/tinyredis-server
```

```sh
./build/tinyredis-cli SET hello world
./build/tinyredis-cli GET hello
```

## Development

```sh
./test.sh
```

The core implementation lives in `src/`: protocol parsing, command dispatch, storage, networking, event loop backends, and CLI code.

## Status

tinyredis focuses on the Redis core path: protocol, networking, commands, TTL, and persistence.

It is not a full Redis replacement. There is no replication, clustering, Lua scripting, ACL, pub/sub, or advanced data structures yet.
