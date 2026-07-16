#!/usr/bin/env bash
set -eu

REDIS_BENCHMARK_BIN=${REDIS_BENCHMARK_BIN:-redis-benchmark}
TINYREDIS_SERVER_BIN=${TINYREDIS_SERVER_BIN:-./build/tinyredis-server}
TINYREDIS_HOST=${TINYREDIS_HOST:-127.0.0.1}
TINYREDIS_PORT=${TINYREDIS_PORT:-6379}
TINYREDIS_OUTPUT=${TINYREDIS_OUTPUT:-tinyredis-benchmark}
BENCHMARK_BUILD_LABEL=${BENCHMARK_BUILD_LABEL:-unknown}
SERVER_START_TIMEOUT=${SERVER_START_TIMEOUT:-50}
BENCHMARK_KEYSPACE=${BENCHMARK_KEYSPACE:-}
BENCHMARK_KEYSPACE_MULTIPLIER=${BENCHMARK_KEYSPACE_MULTIPLIER:-10}
BENCHMARK_KEYSPACE_MIN=${BENCHMARK_KEYSPACE_MIN:-100000}
BENCHMARK_SEED=${BENCHMARK_SEED:-926}
SAVE_DATASET_REQUESTS=${SAVE_DATASET_REQUESTS:-10000}
SAVE_DATASET_VALUE_SIZE=${SAVE_DATASET_VALUE_SIZE:-128}
SAVE_BENCHMARK_REQUESTS=${SAVE_BENCHMARK_REQUESTS:-20}
DEFAULT_BENCHMARK_PROFILES=${DEFAULT_BENCHMARK_PROFILES:-'
-n 10000 -c 1 -P 1 -d 128
-n 100000 -c 50 -P 1 -d 128
-n 200000 -c 50 -P 16 -d 128
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
  -n 10000 -c 1 -P 1 -d 128
  -n 100000 -c 50 -P 1 -d 128
  -n 200000 -c 50 -P 16 -d 128

Environment:
  TINYREDIS_HOST          default: 127.0.0.1
  TINYREDIS_PORT          default: 6379
  REDIS_BENCHMARK_BIN     default: redis-benchmark
  TINYREDIS_SERVER_BIN    default: ./build/tinyredis-server
  TINYREDIS_OUTPUT        default: tinyredis-benchmark
  BENCHMARK_BUILD_LABEL   default: unknown (for example: release)
  BENCHMARK_KEYSPACE      default: auto from request count
  BENCHMARK_KEYSPACE_MULTIPLIER default: 10
  BENCHMARK_KEYSPACE_MIN  default: 100000
  BENCHMARK_SEED          default: 926
  SAVE_DATASET_REQUESTS   default: 10000
  SAVE_DATASET_VALUE_SIZE default: 128
  SAVE_BENCHMARK_REQUESTS default: 20
  DEFAULT_BENCHMARK_PROFILES multiline redis-benchmark options

Do not pass -h, -p, -r, or --seed here. This script sets host, port, random
keyspace, and random seed itself. Each command/profile case starts from a fresh
tinyredis server process so reports can be compared across revisions.
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
  local benchmark_version
  local git_revision
  local git_state

  benchmark_version=$("$REDIS_BENCHMARK_BIN" --version 2>/dev/null || printf '%s' unknown)
  git_revision=$(git rev-parse --short HEAD 2>/dev/null || printf '%s' unknown)
  git_state=clean
  if [ -n "$(git status --porcelain 2>/dev/null || true)" ]; then
    git_state=dirty
  fi

  {
    echo "$name ${host}:${port}"
    echo "generated_at_utc: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "git_revision: $git_revision ($git_state)"
    echo "system: $(uname -sm)"
    echo "build_label: $BENCHMARK_BUILD_LABEL"
    echo "server_binary: $TINYREDIS_SERVER_BIN"
    echo "benchmark_tool: $benchmark_version"
    echo "method: isolated tinyredis server per case, preload data, then measure with fixed seed and keyspace"
    echo "note: single-version regression baseline; single-run results, no statistical averaging"
    echo "commands: PING, SET, SET_EX, GET, INCR, MGET, SAVE"
    echo "seed: $BENCHMARK_SEED"
    echo "keyspace: ${BENCHMARK_KEYSPACE:-auto} (auto = max(requests * $BENCHMARK_KEYSPACE_MULTIPLIER, $BENCHMARK_KEYSPACE_MIN))"
    echo "save_preload_requests: $SAVE_DATASET_REQUESTS"
    echo "save_dataset_value_size: $SAVE_DATASET_VALUE_SIZE"
    echo "save_note: SAVE runs once with one client and no pipeline; it measures end-to-end snapshot command latency"
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
    GET)
      run_seeded_command "$host" "$port" "$keyspace" $profile_args set tinyredis:bench:lookup:__rand_int__ "$payload"
      ;;
    INCR)
      run_seeded_command "$host" "$port" "$keyspace" $profile_args set tinyredis:bench:incr:__rand_int__ 0
      ;;
    MGET)
      run_seeded_command "$host" "$port" "$keyspace" -n 1 -c 1 -P 1 set tinyredis:bench:mget:a "$payload"
      run_seeded_command "$host" "$port" "$keyspace" -n 1 -c 1 -P 1 set tinyredis:bench:mget:b "$payload"
      ;;
    SAVE)
      run_seeded_command "$host" "$port" "$keyspace" -n "$SAVE_DATASET_REQUESTS" -c 50 -P 16 set tinyredis:bench:save:__rand_int__ "$payload"
      ;;
  esac
}

cleanup_current_server() {
  stop_server "$current_pid"
  current_pid=

  if [ -n "$current_tinyredis_dump" ]; then
    rm -f "$current_tinyredis_dump"
    current_tinyredis_dump=
  fi
}

start_case_server() {
  local host=$1
  local port=$2

  cleanup_current_server
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

  start_case_server "$host" "$port"
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
  local keyspace
  local payload
  keyspace=$(case_keyspace "$SAVE_DATASET_REQUESTS")
  payload=$(make_payload "$SAVE_DATASET_VALUE_SIZE")

  start_case_server "$host" "$port"
  prepare_command_state "$host" "$port" "$keyspace" "SAVE" "$payload" ""

  echo "== $name SAVE [persistence] =="
  {
    echo "## SAVE [persistence]"
    echo "profile: -n $SAVE_BENCHMARK_REQUESTS -c 1 -P 1 -d $SAVE_DATASET_VALUE_SIZE"
    echo "keyspace: $keyspace"
    echo "save_preload_requests: $SAVE_DATASET_REQUESTS"
    echo "save_dataset_value_size: $SAVE_DATASET_VALUE_SIZE"
    echo "save_requests: $SAVE_BENCHMARK_REQUESTS"
    echo
    "$REDIS_BENCHMARK_BIN" \
      -h "$host" \
      -p "$port" \
      -c 1 \
      -P 1 \
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
  run_command_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "INCR" "random-update" "$profile_args" incr tinyredis:bench:incr:__rand_int__
  run_command_benchmark_case "$name" "$host" "$port" "$output" "$profile_label" "MGET" "hot-read-hit" "$profile_args" mget tinyredis:bench:mget:a tinyredis:bench:mget:b
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
  run_save_benchmark_case "$name" "$host" "$port" "$output"
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

if [ ! -x "$TINYREDIS_SERVER_BIN" ]; then
  echo "tinyredis server not found or not executable: $TINYREDIS_SERVER_BIN" >&2
  echo "run ./build.sh first or set TINYREDIS_SERVER_BIN=/path/to/tinyredis-server" >&2
  exit 1
fi

current_pid=
current_tinyredis_dump=
benchmark_profiles=$(mktemp /tmp/tinyredis-benchmark-profiles.XXXXXX)
printf '%s\n' "$DEFAULT_BENCHMARK_PROFILES" > "$benchmark_profiles"
trap 'cleanup_current_server; rm -f "$benchmark_profiles"' EXIT INT TERM

run_benchmarks "tinyredis" "$TINYREDIS_HOST" "$TINYREDIS_PORT" "$TINYREDIS_OUTPUT" "$@"
