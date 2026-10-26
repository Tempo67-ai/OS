#!/usr/bin/env python3
"""Build and validate both allocators in QEMU; fail on missing checks/panics."""
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
LOG = ROOT.parent / "report" / "logs"
LOG.mkdir(parents=True, exist_ok=True)
subprocess.run(["make", "-j4"], cwd=ROOT, check=True)
subprocess.run(["make", "buddy-build"], cwd=ROOT, check=True)
for name, image, manager in [
    ("extensions-bestfit", "bin/ucore.img", "best_fit_pmm_manager"),
    ("extensions-buddy", "bin-buddy/ucore.img", "buddy_pmm_manager"),
]:
    for memory in (64, 128, 256):
        command = ["qemu-system-riscv64", "-machine", "virt", "-m", str(memory),
                   "-nographic", "-bios", "default", "-kernel", image]
        process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT)
        try:
            output, _ = process.communicate(timeout=8)
        except subprocess.TimeoutExpired:
            process.terminate()
            output, _ = process.communicate(timeout=5)
        text = output.decode("utf-8", errors="replace")
        suffix = "" if memory == 128 else "-" + str(memory) + "m"
        (LOG / (name + suffix + ".txt")).write_text(text, encoding="utf-8")
        required = ["memory management: " + manager,
                    "[Buddy] rounding, split, merge and accounting: PASS",
                    "[SLUB] size classes, alignment, isolation, reuse, reclaim: PASS",
                    "satp physical address:"]
        if name.endswith("bestfit"):
            required.append("[Buddy] exhaustion, non-overlap, boundaries, recovery: PASS")
        if "panic" in text.lower() or any(item not in text for item in required):
            print(text)
            sys.exit("FAIL: " + name + " memory=" + str(memory))
        print("PASS:", name, "memory=" + str(memory), flush=True)
print("All extension configurations passed.")
