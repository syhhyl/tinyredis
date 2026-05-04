#!/usr/bin/env sh
set -eu

# ===== Benchmark Config =====
# Edit this block for normal benchmark runs.
HOST="127.0.0.1"
PORT="6379"
REQUESTS="300000"
CLIENTS="1 16 32 64 128 200"
PIPELINES="1 8 16 32 64"
VALUE_SIZES="64 128 256 1024"
COMMANDS="PING GET SET EXISTS"
REPORT_FILE="benchmark-report.txt"
TARGET_QPS="250000"
TARGET_P99_MS="8"

# Usually no need to change this.
BENCHMARK_BIN="./build/tinyredis-benchmark"

print_usage() {
  cat <<'EOF'
Usage:
  ./benchmark.sh

Build first:
  ./build.sh

Start server in another terminal:
  ./build/tinyredis-server

Config:
  Edit the "Benchmark Config" block at the top of benchmark.sh.

Temporary overrides are still supported:
  ./benchmark.sh --commands "PING" --clients "1" --pipelines "1" --requests 1000
  ./benchmark.sh --commands "GET SET" --value-sizes "64 1024" --clients "16 64"
EOF
}

SUPPORTED_COMMANDS="PING GET SET EXISTS"

while [ "$#" -gt 0 ]; do
  case "$1" in
    -h|--help)
      print_usage
      exit 0
      ;;
    --host)
      HOST=${2:?missing value after --host}
      shift 2
      ;;
    --port)
      PORT=${2:?missing value after --port}
      shift 2
      ;;
    --requests)
      REQUESTS=${2:?missing value after --requests}
      shift 2
      ;;
    --clients)
      CLIENTS=${2:?missing value after --clients}
      shift 2
      ;;
    --pipeline|--pipelines)
      PIPELINES=${2:?missing value after $1}
      shift 2
      ;;
    --value-sizes)
      VALUE_SIZES=${2:?missing value after --value-sizes}
      shift 2
      ;;
    --commands)
      COMMANDS=${2:?missing value after --commands}
      shift 2
      ;;
    --benchmark-bin)
      BENCHMARK_BIN=${2:?missing value after --benchmark-bin}
      shift 2
      ;;
    --report-file)
      REPORT_FILE=${2:?missing value after --report-file}
      shift 2
      ;;
    --target-qps)
      TARGET_QPS=${2:?missing value after --target-qps}
      shift 2
      ;;
    --target-p99-ms)
      TARGET_P99_MS=${2:?missing value after --target-p99-ms}
      shift 2
      ;;
    *)
      echo "unknown option: $1" >&2
      echo >&2
      print_usage >&2
      exit 1
      ;;
  esac
done

is_supported_command() {
  case "$1" in
    PING|GET|SET|EXISTS)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

command_uses_value_size() {
  case "$1" in
    GET|SET)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

if [ ! -x "$BENCHMARK_BIN" ]; then
  echo "benchmark binary not found or not executable: $BENCHMARK_BIN" >&2
  echo "run ./build.sh first" >&2
  echo >&2
  print_usage >&2
  exit 1
fi

for command in $COMMANDS; do
  if ! is_supported_command "$command"; then
    echo "unsupported command in COMMANDS: $command" >&2
    echo "supported commands: $SUPPORTED_COMMANDS" >&2
    exit 1
  fi
done

cat <<EOF
tinyredis benchmark config
  host:          $HOST
  port:          $PORT
  requests:      $REQUESTS per case
  clients:       $CLIENTS
  pipelines:     $PIPELINES
  value sizes:   $VALUE_SIZES bytes
  commands:      $COMMANDS
  binary:        $BENCHMARK_BIN
  report file:   $REPORT_FILE
  target:        qps >= $TARGET_QPS, p99 <= ${TARGET_P99_MS}ms

Tip: start server first with ./build/tinyredis-server
EOF

echo

RECORDS=$(mktemp /tmp/tinyredis-benchmark-records.XXXXXX)
trap 'rm -f "$RECORDS"' EXIT

echo "command,value_size,clients,pipeline,requests,qps,avg_ms,p95_ms,p99_ms" > "$RECORDS"

run_case() {
  command=$1
  value_size=$2
  clients=$3
  pipeline=$4

  echo "$command value_size=${value_size}B clients=$clients pipeline=$pipeline requests=$REQUESTS"

  if ! output=$("$BENCHMARK_BIN" \
    --host "$HOST" \
    --port "$PORT" \
    --clients "$clients" \
    --requests "$REQUESTS" \
    --pipeline "$pipeline" \
    --command "$command" \
    --value-size "$value_size" 2>&1); then
    echo "$output" >&2
    echo >&2
    echo "benchmark case failed" >&2
    echo "check that tinyredis-server is running on $HOST:$PORT" >&2
    exit 1
  fi

  qps=$(printf '%s\n' "$output" | awk '/^qps: / {print $2}')
  avg_ms=$(printf '%s\n' "$output" | awk '/^avg_ms: / {print $2}')
  p95_ms=$(printf '%s\n' "$output" | awk '/^p95_ms: / {print $2}')
  p99_ms=$(printf '%s\n' "$output" | awk '/^p99_ms: / {print $2}')

  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "$command" "$value_size" "$clients" "$pipeline" "$REQUESTS" \
    "$qps" "$avg_ms" "$p95_ms" "$p99_ms" >> "$RECORDS"
}

for command in $COMMANDS; do
  if command_uses_value_size "$command"; then
    command_value_sizes=$VALUE_SIZES
  else
    command_value_sizes=64
  fi

  for value_size in $command_value_sizes; do
    for clients in $CLIENTS; do
      for pipeline in $PIPELINES; do
        run_case "$command" "$value_size" "$clients" "$pipeline"
      done
    done
  done
done

{
  echo "# tinyredis benchmark report"
  echo
  echo "## Scenario"
  echo "generated_at: $(date '+%Y-%m-%d %H:%M:%S')"
  echo "server: $HOST:$PORT"
  echo "requests_per_case: $REQUESTS"
  echo "target_qps: $TARGET_QPS"
  echo "target_p99_ms: $TARGET_P99_MS"
  echo "clients: $CLIENTS"
  echo "pipelines: $PIPELINES"
  echo "value_sizes: $VALUE_SIZES bytes"
  echo "commands: $COMMANDS"
  echo
  echo "## Summary"
  echo
  awk -F, -v target_qps="$TARGET_QPS" -v target_p99="$TARGET_P99_MS" '
    NR == 1 { next }
    {
      command = $1
      value_size = $2
      key = command SUBSEP value_size
      case_desc = "clients=" $3 ";pipeline=" $4
      qps = $6 + 0
      avg = $7 + 0
      p95 = $8 + 0
      p99 = $9 + 0

      if (!(command in command_seen)) {
        command_seen[command] = 1
        command_order[++command_count] = command
      }
      if (!(key in key_seen)) {
        key_seen[key] = 1
        key_order[command, ++key_count[command]] = key
        key_value_size[key] = value_size
      }

      cases[key]++
      total_cases++
      if (qps >= target_qps && p99 <= target_p99) {
        target_pass[key]++
        total_target_pass++
      }
      if (cases[key] == 1 || qps > best_qps[key]) {
        best_qps[key] = qps
        best_qps_case[key] = case_desc
      }
      p99_sum[key] += p99
      if (cases[key] == 1 || p99 > worst_p99[key]) {
        worst_p99[key] = p99
        worst_p99_case[key] = case_desc
      }
    }
    END {
      print "total_cases: " total_cases
      print "target_pass: " total_target_pass "/" total_cases
      print ""
      for (i = 1; i <= command_count; i++) {
        command = command_order[i]
        print "### " command
        for (j = 1; j <= key_count[command]; j++) {
          key = key_order[command, j]
          print "value_size: " key_value_size[key] "B"
          print "cases: " cases[key]
          print "target_pass: " target_pass[key] "/" cases[key]
          printf "best_qps: %.3f\n", best_qps[key]
          print "best_qps_at: " best_qps_case[key]
          printf "avg_p99_ms: %.3f\n", p99_sum[key] / cases[key]
          printf "worst_p99_ms: %.3f\n", worst_p99[key]
          print "worst_p99_at: " worst_p99_case[key]
          print ""
        }
      }
    }
  ' "$RECORDS"
} > "$REPORT_FILE"

echo
echo "report written to: $REPORT_FILE"
