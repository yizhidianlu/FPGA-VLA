#!/bin/bash
# install_remote_worker.sh — one-shot setup on the Vitis host
# Prereqs: git, python3 with pyyaml, sudo/root access, Vitis 2024.1 installed
# Run from inside the cloned FPGA-VLA repo: bash coord_worker/install_remote_worker.sh
set -e

REPO=$(cd "$(dirname "$0")/.." && pwd)
echo "[install] repo=$REPO"

# 1. python deps
echo "[install] installing pyyaml"
python3 -m pip install --quiet --upgrade pyyaml || python3 -m pip install --user --quiet --upgrade pyyaml

# 2. chmod scripts
chmod +x $REPO/coord_worker/*.sh $REPO/coord_worker/handlers/*.sh 2>/dev/null
chmod +x $REPO/coord_worker/*.py 2>/dev/null

# 3. create dirs that .gitkeep doesn't preserve
mkdir -p $REPO/coord/tasks/{pending,claimed,completed,_failed}
mkdir -p $REPO/coord/{reports,inbox,inbox/_processed,ack,ack/_processed,heartbeat}
mkdir -p $REPO/runs

# 4. environment file for systemd (so Xilinx tools are in PATH)
SETTINGS=$(ls /tools/Xilinx/Vitis/2024.*/settings64.sh /opt/Xilinx/Vitis/2024.*/settings64.sh 2>/dev/null | head -1)
if [ -z "$SETTINGS" ]; then
    echo "[install] WARN: Vitis settings64.sh not found; worker will try to source at runtime"
else
    echo "[install] Vitis settings: $SETTINGS"
    # Extract env vars after sourcing, into /etc/fpga-vla-worker.env
    bash -c "source $SETTINGS && env | grep -E '^(PATH|XILINX|LD_LIBRARY_PATH|LM_LICENSE_FILE|XILINXD_LICENSE_FILE)='" \
        | sudo tee /etc/fpga-vla-worker.env >/dev/null
    echo "[install] wrote /etc/fpga-vla-worker.env"
fi

# 5. systemd unit
SVC=/etc/systemd/system/fpga-vla-worker.service
sudo cp $REPO/coord_worker/systemd/fpga-vla-worker.service $SVC
sudo systemctl daemon-reload
sudo systemctl enable fpga-vla-worker.service
echo "[install] systemd unit installed"

# 6. git identity (for worker commits)
if [ -z "$(git config user.email)" ]; then
    git config --global user.email "remote-worker@fpga-vla.local"
    git config --global user.name "FPGA-VLA Remote Worker"
    echo "[install] git identity set"
fi

# 7. start
sudo systemctl restart fpga-vla-worker.service
sleep 3
echo "[install] daemon status:"
sudo systemctl status fpga-vla-worker.service --no-pager | head -15
echo
echo "[install] tail of log:"
sudo tail -20 /var/log/fpga-vla-worker.log 2>/dev/null || echo "(log not yet)"

echo
echo "===================================================================="
echo "Worker installed and started."
echo "  status:  sudo systemctl status fpga-vla-worker.service"
echo "  logs:    sudo journalctl -u fpga-vla-worker -f"
echo "  logs:    tail -f /var/log/fpga-vla-worker.log"
echo "  stop:    sudo systemctl stop fpga-vla-worker.service"
echo ""
echo "First task T0010_env_probe should complete in ~1 min;"
echo "watch coord/inbox/ for the DONE notification (will appear after git push)."
echo "===================================================================="
