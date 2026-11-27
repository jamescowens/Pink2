#!/usr/bin/env bash
# Start pink2d (and, if given, Pinkcoin-Qt) in a throwaway testnet data
# directory with networking off, and check the RPC server and client end to
# end. This exercises the Boost.Asio code the compat branch changed:
#   1. loopback listener: client -> server, getinfo, stop;
#   2. with -rpcallowip the server binds a dual-stack socket, so a client on
#      127.0.0.1 arrives as ::ffff:127.0.0.1 and is allowed only through the
#      IPv4-mapped path in ClientAllowed(); a LAN address must get HTTP 403.
# Pinkcoin-Qt, if given, gets RPC stop too, and its shutdown lines from
# debug.log are printed, so a run shows how far the shutdown got.
# The daemon runs in the foreground (backgrounded here), not with -daemon:
# -daemon aborts on macOS in 2.4.0.0 (objc fork safety), a separate bug.
set -uo pipefail

B=${1:?usage: smoke.sh <pink2d> [Pinkcoin-Qt]}
G=${2:-}
D=$(mktemp -d)
PORT=29135
A=(-datadir="$D" -testnet -connect=0 -listen=0 -staking=0 -upnp=0
   -rpcuser=ci -rpcpassword="ci-$RANDOM$RANDOM" "-rpcport=$PORT")

fail() {
    echo "::error::$*"
    [ -f "$D/testnet/debug.log" ] && tail -40 "$D/testnet/debug.log"
    exit 1
}
cli() { "$B" "${A[@]}" "$@"; }
wait_rpc() {
    for _ in $(seq 1 90); do
        cli getblockcount >/dev/null 2>&1 && return 0
        sleep 1
    done
    return 1
}
wait_exit() {
    for _ in $(seq 1 "$2"); do
        kill -0 "$1" 2>/dev/null || return 0
        sleep 1
    done
    return 1
}
# gui_log: Pinkcoin-Qt's shutdown in its debug.log (the daemon runs' log is
# moved aside before the GUI starts). From the shutdown request on, only the
# shutdown, thread-stop, flush, close and exit lines are printed, and the rest
# are counted.
gui_log() {
    local f="$D/testnet/debug.log"
    if [ ! -f "$f" ]; then
        echo "-- Pinkcoin-Qt: no debug.log in the data directory"
        return 0
    fi
    echo "-- Pinkcoin-Qt debug.log, shutdown lines from the shutdown request on:"
    tr -d '\r' < "$f" | awk '
        /Shutdown: requested/ { on = 1 }
        !on { next }
        /Shutdown: |StopNode\(\)| stopped$| exited\.?$|exiting|Flush|\.dat (refcount=[0-9]+|checkpoint|detach|closed)$|Pinkcoin exited|did not stop|refused: shutting down|Stopping secure messaging/ { print; next }
        { other++ }
        END {
            if (!on) print "-- no \"Shutdown: requested\" line was logged"
            else if (other) print "-- " other " other lines not shown"
        }'
    return 0
}
lan_ip() {
    { ipconfig getifaddr en0 || ipconfig getifaddr en1 || hostname -I; } 2>/dev/null | awk '{print $1; exit}'
}

echo "== pink2d, loopback listener"
"$B" "${A[@]}" &
P=$!
wait_rpc || fail "pink2d RPC did not come up"
cli getinfo | tee "$D/getinfo.json"
grep -q '"testnet" : true' "$D/getinfo.json" || fail "getinfo did not report testnet"
[ "$(cli getblockcount)" = 0 ] || fail "unexpected block count"
cli stop
wait_exit "$P" 60 || fail "pink2d did not exit after stop"

echo "== pink2d, dual-stack listener (-rpcallowip set)"
"$B" "${A[@]}" -rpcallowip=10.255.255.254 &
P=$!
wait_rpc || fail "pink2d RPC did not come up with -rpcallowip"
[ "$(cli -rpcconnect=127.0.0.1 getblockcount)" = 0 ] || fail "127.0.0.1 was not allowed through the IPv4-mapped path"
echo "127.0.0.1 allowed"
IP=$(lan_ip)
if [ -n "$IP" ]; then
    code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 10 -u ci:x \
        --data-binary '{"method":"getblockcount","params":[],"id":1}' "http://$IP:$PORT/")
    [ "$code" = 403 ] || fail "LAN address $IP got HTTP $code, expected 403"
    echo "$IP refused with HTTP 403"
else
    echo "::warning::no LAN address found; skipped the refusal check"
fi
cli stop
wait_exit "$P" 60 || fail "pink2d did not exit after stop"

if [ -n "$G" ]; then
    echo "== Pinkcoin-Qt (${QT_QPA_PLATFORM:-offscreen})"
    [ -f "$D/testnet/debug.log" ] && mv "$D/testnet/debug.log" "$D/testnet/debug-pink2d.log"
    QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen} "$G" "${A[@]}" -server &
    P=$!
    wait_rpc || fail "Pinkcoin-Qt RPC did not come up"
    echo "Pinkcoin-Qt answered getblockcount: $(cli getblockcount)"
    cli stop >/dev/null 2>&1 || true
    # 40 s is longer than the 30 s shutdown deadline, so a forced exit shows
    # here as an exit rather than as a kill.
    if wait_exit "$P" 40; then
        wait "$P"
        echo "Pinkcoin-Qt exited with status $?"
        gui_log
    else
        echo "::warning::Pinkcoin-Qt did not exit within 40 s of RPC stop (as in 2.4.0.0); killing it"
        gui_log
        kill "$P"
        wait_exit "$P" 10 || kill -9 "$P"
    fi
fi

echo "smoke test passed"
