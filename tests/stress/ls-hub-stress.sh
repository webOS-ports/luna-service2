#!/bin/sh
# On-device stress test for ls-hubd / libluna-service2.
#
# Hammers the hub with parallel luna-send traffic (valid calls, invalid
# JSON, unknown services, registration churn) and, if ls-sock-stress is
# present, raw hostile socket traffic. Verifies afterwards that the hub is
# still alive, still answers, and reports its memory growth.
#
# Usage: ls-hub-stress.sh [rounds] (default 200 per worker)
set -u

ROUNDS=${1:-200}
HUB_SOCKET=${HUB_SOCKET:-/var/run/luna-service2/com.palm.hub}
STRESS_BIN=$(dirname "$0")/ls-sock-stress

hub_pid() { pidof ls-hubd | awk '{print $1}'; }
hub_rss() { awk '/VmRSS/{print $2}' "/proc/$(hub_pid)/status" 2>/dev/null; }
hub_fds() { ls "/proc/$(hub_pid)/fd" 2>/dev/null | wc -l; }

PID_BEFORE=$(hub_pid)
if [ -z "$PID_BEFORE" ]; then
    echo "FAIL: ls-hubd not running"
    exit 1
fi
RSS_BEFORE=$(hub_rss)
FDS_BEFORE=$(hub_fds)
echo "ls-hubd pid=$PID_BEFORE rss=${RSS_BEFORE}kB fds=$FDS_BEFORE, starting stress ($ROUNDS rounds/worker)"

# worker 1: valid calls to the hub's own service
(
    i=0
    while [ $i -lt "$ROUNDS" ]; do
        luna-send -n 1 luna://com.webos.service.bus/signal/registerServerStatus \
            '{"serviceName":"com.webos.service.does.not.exist"}' >/dev/null 2>&1
        i=$((i + 1))
    done
) &
W1=$!

# worker 2: invalid JSON payloads and unknown services
(
    i=0
    while [ $i -lt "$ROUNDS" ]; do
        luna-send -n 1 luna://com.webos.service.bus/signal/addmatch 'this is not json' >/dev/null 2>&1
        luna-send -n 1 "luna://com.nonexistent.service$i/method" '{}' >/dev/null 2>&1
        i=$((i + 1))
    done
) &
W2=$!

# worker 3: registration churn (every luna-send registers + unregisters)
(
    i=0
    while [ $i -lt "$ROUNDS" ]; do
        luna-send -n 1 -a "org.webosports.stress$i" \
            luna://com.webos.service.bus/isCallAllowed \
            '{"uri":"luna://com.webos.service.bus/signal/addmatch","requester":"org.webosports.stress"}' >/dev/null 2>&1
        i=$((i + 1))
    done
) &
W3=$!

# worker 4: raw hostile socket traffic
if [ -x "$STRESS_BIN" ]; then
    "$STRESS_BIN" "$HUB_SOCKET" $((ROUNDS * 10)) &
    W4=$!
else
    echo "note: $STRESS_BIN not found, skipping raw socket stress"
    W4=""
fi

wait $W1 $W2 $W3 ${W4:+$W4}

sleep 2
PID_AFTER=$(hub_pid)
RSS_AFTER=$(hub_rss)
FDS_AFTER=$(hub_fds)

echo "after stress: pid=$PID_AFTER rss=${RSS_AFTER}kB fds=$FDS_AFTER"

FAIL=0
if [ "$PID_BEFORE" != "$PID_AFTER" ]; then
    echo "FAIL: ls-hubd crashed/restarted (pid $PID_BEFORE -> $PID_AFTER)"
    FAIL=1
fi

# hub must still answer a call
if luna-send -n 1 -w 5000 luna://com.webos.service.bus/signal/registerServerStatus \
        '{"serviceName":"com.webos.stress.final"}' >/dev/null 2>&1; then
    echo "hub still answers calls: OK"
else
    echo "FAIL: hub does not answer calls any more"
    FAIL=1
fi

if [ -n "$RSS_BEFORE" ] && [ -n "$RSS_AFTER" ]; then
    GROWTH=$((RSS_AFTER - RSS_BEFORE))
    echo "hub RSS growth: ${GROWTH}kB, fd growth: $((FDS_AFTER - FDS_BEFORE))"
    if [ "$GROWTH" -gt 20480 ]; then
        echo "WARN: hub grew by more than 20MB during stress - possible leak"
    fi
fi

[ $FAIL -eq 0 ] && echo "STRESS TEST PASSED" || echo "STRESS TEST FAILED"
exit $FAIL
