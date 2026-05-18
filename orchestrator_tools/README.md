# Orchestrator Tools

本地工具，给 Orchestrator (你 + 我) 用。Remote worker 不用这些。

## poll_inbox.sh

读 Remote worker 最新发布的 DONE/FAIL 通知 + 心跳状态。

```bash
bash orchestrator_tools/poll_inbox.sh          # 不 git pull
bash orchestrator_tools/poll_inbox.sh --pull   # 先 git pull 再读
```

## make_ack.py

生成 ACK JSON（Orchestrator → Remote 的决策回执）。

```bash
# T0042 验收通过, 解锁 T0043
python3 orchestrator_tools/make_ack.py T0042 proceed \
    --reason "DSP 318 < cap 612 ✓ csim PASS" --next-task T0043

# T0042 失败, 让 worker 重做（新 ID）
python3 orchestrator_tools/make_ack.py T0042 retry --reason "timing miss, retry with k=128"

# 不要做了
python3 orchestrator_tools/make_ack.py T0042 abort --reason "scope changed"
```

写完后 `git add coord/ack && git commit && git push`。

## new_task.py

scaffold 一个新 task YAML。

```bash
python3 orchestrator_tools/new_task.py T0020 hls_csynth \
    --depends T0012 --priority P1 --deadline 60 \
    --notes "synthesize top-k engine v1"
```

会写到 `coord/tasks/pending/T0020_hls_csynth.yaml`，review 后 git push。

## 典型 Orchestrator 循环

```bash
# 1. 看进度
bash orchestrator_tools/poll_inbox.sh --pull

# 2. 分析 inbox 里的 DONE / FAIL

# 3. 如有 FAIL 或 human_ack_needed → 写 ACK
python3 orchestrator_tools/make_ack.py T0042 proceed --reason "..."

# 4. 起下一个任务
python3 orchestrator_tools/new_task.py T0043 hls_cosim --depends T0042

# 5. push
git add coord/ && git commit -m "ack T0042, queue T0043" && git push

# 6. 把 inbox 已读条目归档（避免重复处理）
mv coord/inbox/<filename>.json coord/inbox/_processed/
git add coord/inbox && git commit -m "archive processed inbox" && git push
```
