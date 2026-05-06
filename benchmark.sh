#!/usr/bin/env bash
set -eu

REDIS_BENCHMARK_BIN=${REDIS_BENCHMARK_BIN:-redis-benchmark}
OFFICIAL_REDIS_SERVER_BIN=${OFFICIAL_REDIS_SERVER_BIN:-redis-server}
TINYREDIS_SERVER_BIN=${TINYREDIS_SERVER_BIN:-./build/tinyredis-server}
TINYREDIS_HOST=${TINYREDIS_HOST:-127.0.0.1}
TINYREDIS_PORT=${TINYREDIS_PORT:-6379}
OFFICIAL_REDIS_HOST=${OFFICIAL_REDIS_HOST:-127.0.0.1}
OFFICIAL_REDIS_PORT=${OFFICIAL_REDIS_PORT:-6379}
TINYREDIS_OUTPUT=${TINYREDIS_OUTPUT:-tinyredis-benchmark}
OFFICIAL_REDIS_OUTPUT=${OFFICIAL_REDIS_OUTPUT:-redis-benchmark}
SERVER_START_TIMEOUT=${SERVER_START_TIMEOUT:-50}
BENCHMARK_KEYSPACE=${BENCHMARK_KEYSPACE:-}
BENCHMARK_KEYSPACE_MULTIPLIER=${BENCHMARK_KEYSPACE_MULTIPLIER:-10}
BENCHMARK_KEYSPACE_MIN=${BENCHMARK_KEYSPACE_MIN:-100000}
BENCHMARK_SEED=${BENCHMARK_SEED:-926}
SAVE_DATASET_KEYS=${SAVE_DATASET_KEYS:-10000}
SAVE_BENCHMARK_REQUESTS=${SAVE_BENCHMARK_REQUESTS:-20}
DEFAULT_BENCHMARK_PROFILES=${DEFAULT_BENCHMARK_PROFILES:-'
-n 10000 -c 1 -P 1 -d 16
-n 100000 -c 50 -P 1 -d 128
-n 200000 -c 50 -P 16 -d 128
-n 50000 -c 200 -P 32 -d 1024
'}

print_usage() {
  cat <<'EOF'
Usage:
  ./benchmark.sh [redis-benchmark options]

Examples:
  ./benchmark.sh
  ./benchmark.sh -n 100000 -c 50 -P 16
  SAVE_BENCHMARK_REQUESTS=5 ./benchmark.sh

Default benchmark profiles when no arguments are passed:
  -n 10000 -c 1 -P 1 -d 16
  -n 100000 -c 50 -P 1 -d 128
  -n 200000 -c 50 -P 16 -d 128
  -n 50000 -c 200 -P 32 -d 1024

Environment:
  TINYREDIS_HOST          default: 127.0.0.1
  TINYREDIS_PORT          default: 6379
  OFFICIAL_REDIS_HOST     default: 127.0.0.1
  OFFICIAL_REDIS_PORT     default: 6379
  REDIS_BENCHMARK_BIN     default: redis-benchmark
  OFFICIAL_REDIS_SERVER_BIN default: redis-server
  TINYREDIS_SERVER_BIN    default: ./build/tinyredis-server
  TINYREDIS_OUTPUT        default: tinyredis-benchmark
  OFFICIAL_REDIS_OUTPUT   default: redis-benchmark
  BENCHMARK_KEYSPACE      default: auto from request count
  BENCHMARK_KEYSPACE_MULTIPLIER default: 10
  BENCHMARK_KEYSPACE_MIN  default: 100000
  BENCHMARK_SEED          default: 926
  SAVE_DATASET_KEYS       default: 10000
  SAVE_BENCHMARK_REQUESTS default: 20
  DEFAULT_BENCHMARK_PROFILES multiline redis-benchmark options

Do not pass -h, -p, -r, or --seed here. This script applies the same benchmark
options to both servers and sets host, port, random keyspace, and random seed
itself. Each command/profile case starts from a fresh server process.

Redis runs first, then tinyredis, so both can use the same port by default.
This fixed order may introduce small system-level bias between the two runs.
EOF
}

wait_for_server() {
  local host=$1
  local port=$2
  local name=$3
  local i

  i=0
  while [ "$i" -lt "$SERVER_START_TIMEOUT" ]; do
    if "$REDIS_BENCHMARK_BIN" -h "$host" -p "$port" -t ping_mbulk -n 1 -c 1 >/dev/null 2>&1; then
      return 0
    fi
    i=$((i + 1))
    sleep 0.1
  done

  echo "$name did not start on $host:$port" >&2
  return 1
}

stop_server() {
  local pid=$1
  if [ -n "$pid" ] && kill -0 "$pid" >/dev/null 2>&1; then
    kill "$pid"
    wait "$pid" 2>/dev/null || true
  fi
}

write_report_header() {
  local name=$1
  local host=$2
  local port=$3
  local output=$4

  {
    echo "$name ${host}:${port}"
    echo "method: isolated server per case, preload data, then measure with identical seed and keyspace"
    echo "note: Redis always runs first in each case; single-run results, no statistical averaging"
    echo "commands: PING, SET, SET_EX, GET, EXISTS, DEL, INCR, DECR, MGET, MSET, EXPIRE, TTL, PERSIST, SAVE"
    echo "seed: $BENCHMARK_SEED"
    echo "keyspace: ${BENCHMARK_KEYSPACE:-auto} (auto = max(requests * $BENCHMARK_KEYSPACE_MULTIPLIER, $BENCHMARK_KEYSPACE_MIN))"
    echo "save_dataset_keys: $SAVE_DATASET_KEYS"
    echo "save_note: SAVE benchmarks different persistence backends; comparison measures SAVE command latency, not IO throughput"
    echo
  } > "$output"
}

profile_requests() {
  local value
  while [ "$#" -gt 0 ]; do
    if [ "$1" = "-n" ] && [ "$#" -gt 1 ]; then
      case "$2" in
        ''|*[!0-9]*)
          printf '%s\n' 100000
          return
          ;;
        *)
          printf '%s\n' "$2"
          return
          ;;
      esac
    fi
    shift
  done

  printf '%s\n' 100000
}

case_keyspace() {
  local requests=$1
  local keyspace

  if [ -n "$BENCHMARK_KEYSPACE" ]; then
    printf '%s\n' "$BENCHMARK_KEYSPACE"
    return
  fi

  keyspace=$((requests * BENCHMARK_KEYSPACE_MULTIPLIER))
  if [ "$keyspace" -lt "$BENCHMARK_KEYSPACE_MIN" ]; then
    keyspace=$BENCHMARK_KEYSPACE_MIN
  fi

  printf '%s\n' "$keyspace"
}

profile_data_size() {
  local value
  while [ "$#" -gt 0 ]; do
    if [ "$1" = "-d" ] && [ "$#" -gt 1 ]; then
      case "$2" in
        ''|*[!0-9]*)
          printf '%s\n' 5
          return
          ;;
        *)
          printf '%s\n' "$2"
          return
          ;;
      esac
    fi
    shift
  done

  printf '%s\n' 5
}

profile_without_requests() {
  local skip_next=0

  while [ "$#" -gt 0 ]; do
    if [ "$skip_next" -eq 1 ]; then
      skip_next=0
      shift
      continue
    fi

    if [ "$1" = "-n" ]; then
      skip_next=1
      shift
      continue
    fi

    printf '%s\n' "$1"
    shift
  done
}

make_payload() {
  local size=$1
  local payload=

  while [ "${#payload}" -lt "$size" ]; do
    payload=${payload}xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
  done

  printf '%.*s' "$size" "$payload"
}

run_seeded_command() {
  local host=$1
  local port=$2
  local keyspace=$3
  shift 3

  "$REDIS_BENCHMARK_BIN" \
    -h "$host" \
    -p "$port" \
    -r "$keyspace" \
    --seed "$BENCHMARK_SEED" \
    "$@" >/dev/null 2>&1
}

prepare_command_state() {
  local host=$1
  local port=$2
  local keyspace=$3
  local label=$4
  local payload=$5
  local profile_args=$6

  case "$label" in
    GET|EXISTS)
      run_seeded_command "$host" "$port" "$keyspace" $profile_args set tinyredis:bench:lookup:__rand_int__ "$payload"
      ;;
    DEL)
      run_seeded_command "$host" "$port" "$keyspace" $profile_args set tinyredis:bench:del:__rand_int__ "$payload"
      ;;
    INCR)
      run_seeded_command "$host" "$port" "$keyspace" $profile_args set tinyredis:bench:incr:__rand_int__ 0
      ;;
    DECR)
      run_seeded_command "$host" "$port" "$keyspace" $profile_args set tinyredis:bench:decr:__rand_int__ 0
      ;;
    MGET)
      run_seeded_command "$host" "$port" "$keyspace" -n 1 -c 1 -P 1 set tinyredis:bench:mget:a "$payload"
      run_seeded_command "$host" "$port" "$keyspace" -n 1 -c 1 -P 1 set tinyredis:bench:mget:b "$payload"
      ;;
    EXPIRE)
      run_seeded_command "$host" "$port" "$keyspace" $profile_args set tinyredis:bench:expire:__rand_int__ "$payload"
      ;;
    TTL)
      run_seeded_command "$host" "$port" "$keyspace" $profile_args set tinyredis:bench:ttl:__rand_int__ "$payload" ex 60
      ;;
    PERSIST)
      run_seeded_command "$host" "$port" "$keyspace" $profile_args set tinyredis:bench:persist:__rand_int__ "$payload" ex 60
      ;;
    SAVE)
      run_seeded_command "$host" "$port" "$keyspace" -n "$SAVE_DATASET_KEYS" -c 50 -P 16 set tinyredis:bench:save:__rand_int__ "$payload"
      ;;
  esac
}

cleanup_current_server() {
  stop_server "$current_pid"
  current_pid=

  if [ -n "$current_redis_dir" ]; then
    rm -rf "$current_redis_dir"
    current_redis_dir=
  fi
  if [ -n "$current_tinyredis_dump" ]; then
    rm -f "$current_tinyredis_dump"
    current_tinyredis_dump=
  fi
}

start_case_server() {
  local name=$1
  local host=$2
  local port=$3

  cleanup_current_server
  if [ "$name" = "redis" ]; then
    current_redis_dir=$(mktemp -d /tmp/tinyredis-official-redis-benchmark.XXXXXX)
    "$OFFICIAL_REDIS_SERVER_BIN" \
      --bind "$host" \
      --port "$port" \
      --dir "$current_redis_dir" \
      --dbfilename dump.rdb \
      --save "" \
      --appendonly no \
      --loglevel warning >/tmp/tinyredis-official-redis-benchmark.log 2>&1 &
    current_pid=$!
    wait_for_server "$host" "$port" "official redis"
    return
  fi

  current_tinyredis_dump=$(mktemp /tmp/tinyredis-benchmark-dump.XXXXXX)
  rm -f "$current_tinyredis_dump"
  "$TINYREDIS_SERVER_BIN" \
    --port "$port" \
    --dump-file "$current_tinyredis_dump" >/tmp/tinyredis-server-benchmark.log 2>&1 &
  current_pid=$!
  wait_for_server "$host" "$port" "tinyredis"
}

run_command_benchmark_case() {
  local name=$1
  local host=$2
  local port=$3
  local output=$4
  local profile_label=$5
  local label=$6
  local category=$7
  local profile_args=$8
  local requests
  local keyspace
  local payload
  shift 8
  requests=$(profile_requests $profile_args)
  keyspace=$(case_keyspace "$requests")
  payload=$(make_payload "$(profile_data_size $profile_args)")

  start_case_server "$name" "$host" "$port"
  prepare_command_state "$host" "$port" "$keyspace" "$label" "$payload" "$profile_args"

  echo "== $name $profile_label $label [$category] =="
  {
    echo "## $profile_label $label [$category]"
    echo "profile: $profile_args"
    echo "keyspace: $keyspace"
    echo
    "$REDIS_BENCHMARK_BIN" \
      -h "$host" \
      -p "$port" \
      -r "$keyspace" \
      --seed "$BENCHMARK_SEED" \
      $profile_args \
      "$@"
    echo
  } >> "$output" 2>&1
  cleanup_current_server
}

run_save_benchmark_case() {
  local name=$1
  local host=$2
  local port=$3
  local output=$4
  local profile_label=$5
  local profile_args=$6
  local requests
  local keyspace
  local payload
  local save_profile_args
  requests=$(profile_requests $profile_args)
  keyspace=$(case_keyspace "$requests")
  payload=$(make_payload "$(profile_data_size $profile_args)")
  save_profile_args=$(profile_without_requests $profile_args)

  start_case_server "$name" "$host" "$port"
  prepare_command_state "$host" "$port" "$keyspace" "SAVE" "$payload" "$profile_args"

  echo "== $name $profile_label SAVE [persistence] =="
  {
    echo "## $profile_label SAVE [persistence]"
    echo "profile: $profile_args"
    echo "keyspace: $keyspace"
    echo "save_dataset_keys: $SAVE_DATASET_KEYS"
    echo "save_requests: $SAVE_BENCHMARK_REQUESTS"
    echo
    "$REDIS_BENCHMARK_BIN" \
      -h "$host" \
      -p "$port" \
      -r "$keyspace" \
      --seed "$BENCHMARK_SEED" \
      $save_profile_args \
      -n "$SAVE_BENCHMARK_REQUESTS" \
      save
    echo
  } >> "$output" 2>&1
  cleanup_current_server
}

run_supported_command_benchmark_cases() {
  local name=$1
  local host=$2
  local port=$3
  local output=$4
  local profile_label=$5
  local profile_args=$6
  local payload
  payload=$(make_payload "$(profile_data_size $profile_args)")

  run_command_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "PING" "baseline" "$profile_args" ping
  run_command_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "SET" "random-write" "$profile_args" set tinyredis:bench:set:__rand_int__ "$payload"
  run_command_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "SET_EX" "random-write" "$profile_args" set tinyredis:bench:setex:__rand_int__ "$payload" ex 60
  run_command_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "GET" "random-read-hit" "$profile_args" get tinyredis:bench:lookup:__rand_int__
  run_command_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "EXISTS" "random-read-hit" "$profile_args" exists tinyredis:bench:lookup:__rand_int__
  run_command_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "DEL" "stateful (mixed)" "$profile_args" del tinyredis:bench:del:__rand_int__
  run_command_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "INCR" "random-update" "$profile_args" incr tinyredis:bench:incr:__rand_int__
  run_command_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "DECR" "random-update" "$profile_args" decr tinyredis:bench:decr:__rand_int__
  run_command_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "MGET" "hot-read-hit" "$profile_args" mget tinyredis:bench:mget:a tinyredis:bench:mget:b
  run_command_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "MSET" "random-write" "$profile_args" mset tinyredis:bench:mset-a:__rand_int__ "$payload" tinyredis:bench:mset-b:__rand_int__ "$payload"
  run_command_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "EXPIRE" "stateful (mixed)" "$profile_args" expire tinyredis:bench:expire:__rand_int__ 60
  run_command_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "TTL" "random-read-hit" "$profile_args" ttl tinyredis:bench:ttl:__rand_int__
  run_command_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "PERSIST" "stateful (mixed)" "$profile_args" persist tinyredis:bench:persist:__rand_int__
  run_save_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "$profile_args"
}

run_benchmarks() {
  local name=$1
  local host=$2
  local port=$3
  local output=$4
  local profile_index
  local profile
  shift 4

  write_report_header "$name" "$host" "$port" "$output"
  if [ "$BENCHMARK_ARGS_PROVIDED" -eq 1 ]; then
    run_supported_command_benchmark_cases "$name" "$host" "$port" "$output" "custom" "$*"
  else
    profile_index=1
    while IFS= read -r profile; do
      [ -n "$profile" ] || continue
      run_supported_command_benchmark_cases "$name" "$host" "$port" "$output" "profile-$profile_index" "$profile"
      profile_index=$((profile_index + 1))
    done < "$benchmark_profiles"
  fi
  echo "wrote: $output"
}

case "${1:-}" in
  -h|--help)
    print_usage
    exit 0
    ;;
esac

BENCHMARK_ARGS_PROVIDED=1
if [ "$#" -eq 0 ]; then
  BENCHMARK_ARGS_PROVIDED=0
fi

for arg in "$@"; do
  case "$arg" in
    -h|-p|-r|--host|--port|--seed)
      echo "do not pass host/port/keyspace/seed to compare script; use environment variables instead" >&2
      exit 1
      ;;
  esac
done

if ! command -v "$REDIS_BENCHMARK_BIN" >/dev/null 2>&1; then
  echo "redis-benchmark not found: $REDIS_BENCHMARK_BIN" >&2
  echo "install it with: brew install redis" >&2
  echo "or set REDIS_BENCHMARK_BIN=/path/to/redis-benchmark" >&2
  exit 1
fi

if ! command -v "$OFFICIAL_REDIS_SERVER_BIN" >/dev/null 2>&1; then
  echo "redis-server not found: $OFFICIAL_REDIS_SERVER_BIN" >&2
  echo "install it with: brew install redis" >&2
  echo "or set OFFICIAL_REDIS_SERVER_BIN=/path/to/redis-server" >&2
  exit 1
fi

if [ ! -x "$TINYREDIS_SERVER_BIN" ]; then
  echo "tinyredis server not found or not executable: $TINYREDIS_SERVER_BIN" >&2
  echo "run ./build.sh first or set TINYREDIS_SERVER_BIN=/path/to/tinyredis-server" >&2
  exit 1
fi

current_pid=
current_redis_dir=
current_tinyredis_dump=
benchmark_profiles=$(mktemp /tmp/tinyredis-benchmark-profiles.XXXXXX)
printf '%s\n' "$DEFAULT_BENCHMARK_PROFILES" > "$benchmark_profiles"
trap 'cleanup_current_server; rm -f "$benchmark_profiles"' EXIT INT TERM

run_benchmarks "redis" "$OFFICIAL_REDIS_HOST" "$OFFICIAL_REDIS_PORT" "$OFFICIAL_REDIS_OUTPUT" "$@"

echo
run_benchmarks "tinyredis" "$TINYREDIS_HOST" "$TINYREDIS_PORT" "$TINYREDIS_OUTPUT" "$@"
