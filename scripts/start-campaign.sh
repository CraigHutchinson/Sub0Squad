#!/usr/bin/env bash
# Start (or resume) a campaign from a clean boot: the contained engine service, then the contained campaign service.
#
#   scripts/start-campaign.sh [NAME] [RUNS] [SEED_BASE]      defaults: baseline 20 1000
#   scripts/start-campaign.sh status                          what is running and the report so far
#   scripts/start-campaign.sh stop                            stop the campaign (the engine is left up; `stop-all` also stops it)
#
# Idempotent: units that are already active are left alone; a campaign resumes from its campaign.jsonl. Transient systemd units do not survive a
# reboot, which is why this script exists: run it after every boot. Nothing leaves the machine (frontier off, engine on 127.0.0.1).
# Environment overrides: MODEL, ENGINE_PORT, CTX, SLOTS, THREADS, EXECUTOR, PORTAL_PORT, BINARY.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GITHUB_DIR="$(dirname "$REPO")"
MODEL="${MODEL:-/mnt/development/Models/Qwen3.6-35B-A3B-GGUF/Qwen3.6-35B-A3B-UD-Q4_K_M.gguf}"
LLAMA="${LLAMA:-$GITHUB_DIR/Sub0Llm/out/build/llama-optimizer/bin/llama-server}"
ENGINE_PORT="${ENGINE_PORT:-8092}"
CTX="${CTX:-32768}"
SLOTS="${SLOTS:-4}"
THREADS="${THREADS:-16}"
EXECUTOR="${EXECUTOR:-fork}"
PORTAL_PORT="${PORTAL_PORT:-8780}"
BINARY="${BINARY:-$REPO/out/build/dev/sub0squad}"

cmd="${1:-run}"

unitActive() { systemctl --user is-active --quiet "$1"; }

case "$cmd" in
  status)
    for u in sub0-llama35 sub0-campaign; do printf '%-16s %s\n' "$u" "$(systemctl --user is-active "$u" || true)"; done
    for d in "$REPO"/out/campaign/*/; do [ -f "$d/campaign-report.md" ] && { echo "== $d"; sed -n 1,12p "$d/campaign-report.md"; }; done
    exit 0 ;;
  stop)
    systemctl --user stop sub0-campaign 2>/dev/null || true
    echo "campaign stopped (the engine is still up; the campaign resumes with: scripts/start-campaign.sh)"
    exit 0 ;;
  stop-all)
    systemctl --user stop sub0-campaign sub0-llama35 2>/dev/null || true
    echo "campaign and engine stopped"
    exit 0 ;;
esac

NAME="${1:-baseline}"; RUNS="${2:-20}"; SEED_BASE="${3:-1000}"

# --- preflight --------------------------------------------------------------------------------------------------------------------------
[ -x "$BINARY" ] || { echo "no binary at $BINARY: build it first (cmake --build out/build/dev -j2)"; exit 1; }
[ -x "$LLAMA" ] || { echo "no llama-server at $LLAMA (set LLAMA=...)"; exit 1; }
[ -f "$MODEL" ] || { echo "no model at $MODEL (set MODEL=...)"; exit 1; }
avail_mib=$(awk '/MemAvailable/ {print int($2/1024)}' /proc/meminfo)
if ! unitActive sub0-llama35 && [ "$avail_mib" -lt 26000 ]; then
  echo "only ${avail_mib} MiB of memory is available; the 35B needs about 25 GiB. Close something first."; exit 1
fi

# --- the engine: a contained service (memory capped, no swap, the first thing the kernel kills if memory runs out) ---------------------------
if unitActive sub0-llama35; then
  echo "engine: sub0-llama35 already running"
else
  systemctl --user reset-failed sub0-llama35 2>/dev/null || true
  systemd-run --user --unit=sub0-llama35 \
    -p MemoryHigh=24G -p MemoryMax=25G -p MemorySwapMax=0 -p OOMScoreAdjust=900 -p OOMPolicy=stop -p ManagedOOMMemoryPressure=kill -p Nice=5 \
    "$LLAMA" -m "$MODEL" --host 127.0.0.1 --port "$ENGINE_PORT" -c "$CTX" -np "$SLOTS" -t "$THREADS" --no-warmup --alias qwen35 --metrics >/dev/null
  echo "engine: started sub0-llama35 (loading the model; this takes a few minutes)"
fi
printf 'engine: waiting for it to answer'
for _ in $(seq 1 180); do
  if curl -fsS "http://127.0.0.1:$ENGINE_PORT/health" >/dev/null 2>&1; then break; fi
  printf '.'; sleep 5
done
echo
curl -fsS "http://127.0.0.1:$ENGINE_PORT/health" >/dev/null || { echo "the engine did not come up: journalctl --user -u sub0-llama35 -n 30"; exit 1; }
curl -fsS "http://127.0.0.1:$ENGINE_PORT/metrics" >/dev/null || { echo "the engine answers but has no /metrics (needs --metrics): the tick counters would be missing"; exit 1; }
model_id=$(curl -fsS "http://127.0.0.1:$ENGINE_PORT/v1/models" | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["data"][0]["id"])' || true)
echo "engine: ready (model alias: ${model_id:-unknown})"

# --- the campaign: a contained service; it resumes from <home>/campaign.jsonl --------------------------------------------------------------
if unitActive sub0-campaign; then
  echo "campaign: sub0-campaign already running (scripts/start-campaign.sh status)"; exit 0
fi
HOME_DIR="$REPO/out/campaign/$NAME"
systemctl --user reset-failed sub0-campaign 2>/dev/null || true
systemd-run --user --unit=sub0-campaign \
  -p MemoryHigh=6G -p MemoryMax=8G -p TasksMax=3000 -p OOMPolicy=continue -p Nice=5 \
  -p "WorkingDirectory=$REPO" \
  --setenv="PATH=$PATH" --setenv="SUB0_GITHUB_DIR=$GITHUB_DIR" \
  python3 -I "$REPO/scripts/campaign.py" run --name "$NAME" --runs "$RUNS" --seed-base "$SEED_BASE" \
    --home "$HOME_DIR" --binary "$BINARY" --executor "$EXECUTOR" --portal-port "$PORTAL_PORT" >/dev/null
echo "campaign: started '$NAME' ($RUNS runs from seed $SEED_BASE)"
echo "  portal:  http://127.0.0.1:$PORTAL_PORT/"
echo "  report:  $HOME_DIR/campaign-report.md"
echo "  logs:    journalctl --user -u sub0-campaign -f"
echo "  stop:    scripts/start-campaign.sh stop"
