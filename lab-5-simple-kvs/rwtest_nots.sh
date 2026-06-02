#!/bin/bash
# ts-free reimplementation of ref/rwtest.sh.
# Records each response's arrival wall-clock time (s) and checks the
# relative intervals between consecutive responses (±1s), plus the
# expected response string. Run the server with -d 5 on the same port.
PORT=8080
CLIENT=./ref/client
while getopts "p:c:" opt; do
  case $opt in
    p) PORT=$OPTARG ;;
    c) CLIENT=$OPTARG ;;
  esac
done
shift $((OPTIND-1))
TEST_SET=$1
[ -z "$TEST_SET" ] && { echo "usage: $0 [-p port] [-c client] 1|2|3|4|5"; exit 1; }

case $TEST_SET in
 1) REQ=("CREATE hello 1" "CREATE hello 2" "CREATE hello 3" "CREATE hello 4")
    RES=("CREATE OK" "COLLISION" "COLLISION" "COLLISION"); INT=(0 5 5 5);;
 2) REQ=("CREATE hello world" "READ hello" "READ hello" "READ hello")
    RES=("CREATE OK" "world" "world" "world"); INT=(0 5 0 0);;
 3) REQ=("CREATE hello world" "READ hello" "CREATE bye snu" "READ bye")
    RES=("CREATE OK" "world" "CREATE OK" "snu"); INT=(0 5 -5 5);;
 4) REQ=("CREATE hello world" "DELETE bye" "READ hello" "UPDATE hello snu" "DELETE hello" "READ hello")
    RES=("CREATE OK" "NOT FOUND" "world" "UPDATE OK" "DELETE OK" "NOT FOUND"); INT=(0 0 5 5 5 5);;
 5) REQ=("CREATE hello world" "DELETE bye" "READ hello" "UPDATE hello snu" "DELETE hello" "QREAD hello")
    RES=("CREATE OK" "NOT FOUND" "world" "UPDATE OK" "DELETE OK" "world"); INT=(0 0 5 5 5 -10);;
 *) echo "invalid set"; exit 1;;
esac

OUT=/tmp/rwnots; rm -rf $OUT; mkdir -p $OUT
echo "=== Test Set $TEST_SET (port $PORT) ==="
for i in "${!REQ[@]}"; do
  echo "Sending $i: '${REQ[$i]}'"
  ( echo "${REQ[$i]}" | $CLIENT -p $PORT > "$OUT/r_$i.log" 2>&1
    date +%s.%N > "$OUT/t_$i" ) &
  sleep 0.1
done
wait

fail=0
for i in "${!REQ[@]}"; do
  if grep -q "${RES[$i]}" "$OUT/r_$i.log"; then
    echo "  resp $i OK: '${RES[$i]}'"
  else
    echo "  resp $i FAIL: expected '${RES[$i]}', got: $(tr '\n' '|' < $OUT/r_$i.log)"; fail=1
  fi
done

echo "--- intervals (expected ±1s) ---"
for ((i=1;i<${#REQ[@]};i++)); do
  t0=$(cat "$OUT/t_$((i-1))"); t1=$(cat "$OUT/t_$i")
  d=$(awk "BEGIN{printf \"%.2f\", $t1-$t0}")
  exp=${INT[$i]}
  lo=$(awk "BEGIN{print $exp-1}"); hi=$(awk "BEGIN{print $exp+1}")
  okstr="OK"; awk "BEGIN{exit !($d>=$lo && $d<=$hi)}" || { okstr="FAIL"; fail=1; }
  echo "  $((i-1))->$i: ${d}s (expected ${exp}s) $okstr"
done
[ $fail -eq 0 ] && echo -e "Test Set $TEST_SET: PASSED" || echo -e "Test Set $TEST_SET: FAILED"
exit $fail
