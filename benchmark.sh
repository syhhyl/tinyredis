#!/usr/bin/env sh
set -eu

REDIS_BENCHMARK_BIN=${REDIS_BENCHMARK_BIN:-redis-benchmark}
OFFICIAL_REDIS_SERVER_BIN=${OFFICIAL_REDIS_SERVER_BIN:-redis-server}
TINYREDIS_SERVER_BIN=${TINYREDIS_SERVER_BIN:-./build/tinyredis-server}
REDIS_BENCHMARK_TESTS=${REDIS_BENCHMARK_TESTS:-ping_mbulk,set,get}
TINYREDIS_HOST=${TINYREDIS_HOST:-127.0.0.1}
TINYREDIS_PORT=${TINYREDIS_PORT:-6379}
OFFICIAL_REDIS_HOST=${OFFICIAL_REDIS_HOST:-127.0.0.1}
OFFICIAL_REDIS_PORT=${OFFICIAL_REDIS_PORT:-6379}
TINYREDIS_OUTPUT=${TINYREDIS_OUTPUT:-tinyredis-benchmark}
OFFICIAL_REDIS_OUTPUT=${OFFICIAL_REDIS_OUTPUT:-redis-benchmark}
SERVER_START_TIMEOUT=${SERVER_START_TIMEOUT:-50}
DEFAULT_BENCHMARK_PROFILES=${DEFAULT_BENCHMARK_PROFILES:-'
-n 1000000 -c 50 -P 16 -d 128
'}

print_usage() {
  cat <<'EOF'
Usage:
  ./benchmark.sh [redis-benchmark options]

Examples:
  ./benchmark.sh
  ./benchmark.sh -n 100000 -c 50 -P 16
  REDIS_BENCHMARK_TESTS=ping_mbulk ./benchmark.sh

Default benchmark profile when no arguments are passed:
  -n 1000000 -c 50 -P 16 -d 128

Environment:
  TINYREDIS_HOST          default: 127.0.0.1
  TINYREDIS_PORT          default: 6379
  OFFICIAL_REDIS_HOST     default: 127.0.0.1
  OFFICIAL_REDIS_PORT     default: 6379
  REDIS_BENCHMARK_BIN     default: redis-benchmark
  OFFICIAL_REDIS_SERVER_BIN default: redis-server
  TINYREDIS_SERVER_BIN    default: ./build/tinyredis-server
  REDIS_BENCHMARK_TESTS   default: ping_mbulk,set,get
  TINYREDIS_OUTPUT        default: tinyredis-benchmark
  OFFICIAL_REDIS_OUTPUT   default: redis-benchmark
  DEFAULT_BENCHMARK_PROFILES multiline redis-benchmark options

Do not pass -h or -p here. This script applies the same benchmark options to both
servers and sets host/port itself. It starts and stops official Redis first, then
tinyredis, so both can use the same port by default.
EOF
}

wait_for_server() {
  host=$1
  port=$2
  name=$3

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
  pid=$1
  if [ -n "$pid" ] && kill -0 "$pid" >/dev/null 2>&1; then
    kill "$pid"
    wait "$pid" 2>/dev/null || true
  fi
}

write_report_header() {
  name=$1
  host=$2
  port=$3
  output=$4

  {
    echo "$name ${host}:${port}"
    echo "tests: $REDIS_BENCHMARK_TESTS"
    echo
  } > "$output"
}

run_benchmark_case() {
  name=$1
  host=$2
  port=$3
  output=$4
  shift 4

  echo "== $name ${host}:${port} $* =="
  {
    echo "## $*"
    echo
    "$REDIS_BENCHMARK_BIN" \
      -h "$host" \
      -p "$port" \
      -t "$REDIS_BENCHMARK_TESTS" \
      "$@"
    echo
  } >> "$output" 2>&1
}

run_benchmarks() {
  name=$1
  host=$2
  port=$3
  output=$4
  shift 4

  write_report_header "$name" "$host" "$port" "$output"
  if [ "$BENCHMARK_ARGS_PROVIDED" -eq 1 ]; then
    run_benchmark_case "$name" "$host" "$port" "$output" "$@"
  else
    while IFS= read -r profile; do
      [ -n "$profile" ] || continue
      set -- $profile
      run_benchmark_case "$name" "$host" "$port" "$output" "$@"
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
    -h|-p|--host|--port)
      echo "do not pass host/port to compare script; use environment variables instead" >&2
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

official_pid=
tinyredis_pid=
tinyredis_dump=$(mktemp /tmp/tinyredis-benchmark-dump.XXXXXX)
benchmark_profiles=$(mktemp /tmp/tinyredis-benchmark-profiles.XXXXXX)
rm -f "$tinyredis_dump"
printf '%s\n' "$DEFAULT_BENCHMARK_PROFILES" > "$benchmark_profiles"
trap 'stop_server "$official_pid"; stop_server "$tinyredis_pid"; rm -f "$tinyredis_dump" "$benchmark_profiles"' EXIT INT TERM

echo "starting redis on ${OFFICIAL_REDIS_HOST}:${OFFICIAL_REDIS_PORT}"
"$OFFICIAL_REDIS_SERVER_BIN" \
  --bind "$OFFICIAL_REDIS_HOST" \
  --port "$OFFICIAL_REDIS_PORT" \
  --save "" \
  --appendonly no \
  --loglevel warning >/tmp/tinyredis-official-redis-benchmark.log 2>&1 &
official_pid=$!
wait_for_server "$OFFICIAL_REDIS_HOST" "$OFFICIAL_REDIS_PORT" "official redis"
run_benchmarks "redis" "$OFFICIAL_REDIS_HOST" "$OFFICIAL_REDIS_PORT" "$OFFICIAL_REDIS_OUTPUT" "$@"
stop_server "$official_pid"
official_pid=

echo
echo "starting tinyredis on ${TINYREDIS_HOST}:${TINYREDIS_PORT}"
"$TINYREDIS_SERVER_BIN" \
  --port "$TINYREDIS_PORT" \
  --dump-file "$tinyredis_dump" >/tmp/tinyredis-server-benchmark.log 2>&1 &
tinyredis_pid=$!
wait_for_server "$TINYREDIS_HOST" "$TINYREDIS_PORT" "tinyredis"
run_benchmarks "tinyredis" "$TINYREDIS_HOST" "$TINYREDIS_PORT" "$TINYREDIS_OUTPUT" "$@"
stop_server "$tinyredis_pid"
tinyredis_pid=
