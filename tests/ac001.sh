#!/bin/sh
# AC-001: make run 后 2 s 内输出 IFIRM-BOOT stage=0 与 hello ifirm
set -e
LOG=build/ac001.log
mkdir -p build
python3 - "$LOG" <<'PYEOF'
import subprocess, sys, time

log = sys.argv[1]
cmd = ["qemu-system-riscv64", "-machine", "virt", "-m", "256M", "-smp", "1",
       "-nographic", "-bios", "default", "-kernel", "build/ifirm.bin"]
p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
t0 = time.time()
lines, t_hello = [], None
try:
    for line in p.stdout:
        lines.append(line)
        if "hello ifirm" in line and t_hello is None:
            t_hello = time.time() - t0
            break
finally:
    p.kill()
    p.wait()

out = "".join(lines)
open(log, "w").write(out)

ok_stage = "IFIRM-BOOT stage=0" in out
ok_hello = "hello ifirm" in out
ok_time = t_hello is not None and t_hello < 2.0
ms = int((t_hello or 0) * 1000)
print("TEST AC-001 %s %d" % ("PASS" if (ok_stage and ok_hello and ok_time) else "FAIL", ms))
print("  stage=0: %s  hello: %s  t_hello=%dms (budget 2000ms)" % (ok_stage, ok_hello, ms))
sys.exit(0 if (ok_stage and ok_hello and ok_time) else 1)
PYEOF
