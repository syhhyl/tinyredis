<div align="center">
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

## Benchmark

Local benchmark profile: `redis-benchmark -n 1000000 -c 50 -P 16 -d 128`, loopback TCP, single-threaded benchmark client.

| Command | tinyredis throughput | Redis throughput | tinyredis p50 | Redis p50 |
| --- | ---: | ---: | ---: | ---: |
| `PING_MBULK` | 3.15M req/s | 3.07M req/s | 0.135 ms | 0.135 ms |
| `SET` | 2.66M req/s | 2.07M req/s | 0.295 ms | 0.327 ms |
| `GET` | 3.01M req/s | 2.56M req/s | 0.255 ms | 0.255 ms |
| `INCR` | 2.97M req/s | 2.48M req/s | 0.263 ms | 0.271 ms |
| `MSET` | 689K req/s | 410K req/s | 1.143 ms | 1.207 ms |
| `EXISTS` | 3.16M req/s | 2.77M req/s | 0.247 ms | 0.239 ms |
| `TTL` | 2.96M req/s | 2.76M req/s | 0.263 ms | 0.239 ms |
| `PERSIST` | 3.16M req/s | 2.76M req/s | 0.247 ms | 0.239 ms |
| `DEL` | 3.22M req/s | 2.89M req/s | 0.247 ms | 0.231 ms |

The result is intentionally mixed: tinyredis has higher throughput in this local run, while Redis is slightly better on median latency for some small commands. That is the useful comparison: tinyredis keeps the hot path small and fast, but does not claim Redis parity.

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
