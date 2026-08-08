#!/usr/bin/env python3
"""Structural and RQ7 leakage-risk audit for sumo-per window CSV files."""
from __future__ import annotations
import argparse, csv, json, math
from pathlib import Path

def missing(value: str) -> bool:
    return value.strip().lower() in {"", "nan", "-nan", "na", "null"}

def audit(path: Path) -> dict:
    counts = {"rows": 0, "invalid_counts": 0, "invalid_per": 0,
              "missing_phy": 0, "missing_phy_with_samples": 0,
              "rx_without_phy": 0, "same_window_phy_rows": 0}
    first, last = math.inf, -math.inf
    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        required = {"window_start_s", "window_end_s", "tx", "rx_ok", "per",
                    "rssi_dbm", "snr_db", "phy_n"}
        absent = required - set(reader.fieldnames or [])
        if absent: raise ValueError(f"{path}: missing columns {sorted(absent)}")
        for row in reader:
            counts["rows"] += 1
            tx, rx, per, phy = int(row["tx"]), int(row["rx_ok"]), float(row["per"]), int(row["phy_n"])
            first = min(first, float(row["window_start_s"])); last = max(last, float(row["window_end_s"]))
            if tx < 0 or rx < 0 or rx > tx: counts["invalid_counts"] += 1
            if not 0 <= per <= 1 or (tx and abs(per - (1-rx/tx)) > 1e-9): counts["invalid_per"] += 1
            phy_missing = missing(row["rssi_dbm"]) or missing(row["snr_db"])
            if phy_missing: counts["missing_phy"] += 1
            if phy_missing and phy > 0: counts["missing_phy_with_samples"] += 1
            if rx > 0 and phy == 0: counts["rx_without_phy"] += 1
            if phy > 0: counts["same_window_phy_rows"] += 1
    counts["first_window_s"] = None if first == math.inf else first
    counts["last_window_s"] = None if last == -math.inf else last
    counts["structurally_valid"] = all(counts[k] == 0 for k in
        ("invalid_counts", "invalid_per", "missing_phy_with_samples", "rx_without_phy"))
    counts["leakage_warning"] = (
        "Do not use same-window rssi_dbm, snr_db, phy_n, or a phy_observed flag "
        "to predict the same window's PER. Lag them by >=1 window and split data "
        "by complete run/seed/scenario."
    )
    return counts

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", nargs="+", type=Path)
    ap.add_argument("--output", type=Path, default=Path("rq7_audit.json"))
    args = ap.parse_args()
    report = {str(p): audit(p) for p in args.csv}
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(args.output)

if __name__ == "__main__": main()