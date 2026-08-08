#!/usr/bin/env python3
"""Generate and empirically calibrate SUMO demand to target vehicle densities.

Density is defined as the time-mean number of active vehicles during the
retained interval divided by a predeclared study area in km^2.
"""

from __future__ import annotations

import argparse
import csv
import gzip
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET


TARGETS = (10.0, 30.0, 60.0)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--sumo-root", type=Path, default=Path("juelich_sumo"))
    p.add_argument("--study-area-km2", type=float, required=True,
                   help="Predeclared Juelich evaluation area, in km^2")
    p.add_argument("--targets", nargs="+", type=float, default=TARGETS)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--begin", type=float, default=0.0)
    p.add_argument("--warmup", type=float, default=60.0)
    p.add_argument("--end", type=float, default=660.0)
    p.add_argument("--step-length", type=float, default=0.1)
    p.add_argument("--tolerance-pct", type=float, default=5.0)
    p.add_argument("--max-iterations", type=int, default=12)
    p.add_argument("--initial-period", type=float, default=1.0,
                   help="Initial seconds between generated departures")
    p.add_argument("--random-trips", type=Path,
                   help="Path to SUMO tools/randomTrips.py")
    p.add_argument("--sumo-binary", default="sumo")
    return p.parse_args()


def resolve_random_trips(explicit: Path | None) -> Path:
    if explicit:
        return explicit.resolve()
    sumo_home = os.environ.get("SUMO_HOME")
    candidates = []
    if sumo_home:
        candidates.append(Path(sumo_home) / "tools" / "randomTrips.py")
    candidates.extend((Path("/usr/share/sumo/tools/randomTrips.py"),
                       Path("/usr/local/share/sumo/tools/randomTrips.py")))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit("randomTrips.py not found; set SUMO_HOME or use --random-trips")


def check_inputs(a: argparse.Namespace) -> tuple[Path, Path]:
    if not (a.study_area_km2 > 0):
        raise SystemExit("--study-area-km2 must be greater than zero")
    if not (a.begin <= a.warmup < a.end):
        raise SystemExit("Require begin <= warmup < end")
    net = (a.sumo_root / "osm.net.xml.gz").resolve()
    if not net.is_file():
        raise SystemExit(f"Missing network: {net}")
    random_trips = resolve_random_trips(a.random_trips)
    if shutil.which(a.sumo_binary) is None:
        raise SystemExit(f"SUMO binary not found: {a.sumo_binary}")
    return net, random_trips


def write_sumocfg(path: Path, net: Path, trips: Path, a: argparse.Namespace) -> None:
    def rel(target: Path) -> str:
        return os.path.relpath(target.resolve(), path.parent.resolve())
    additional = []
    for name in ("osm.poly.xml.gz", "output.add.xml"):
        candidate = (a.sumo_root / name).resolve()
        if candidate.is_file():
            additional.append(rel(candidate))
    additional_xml = ""
    if additional:
        additional_xml = f'\n        <additional-files value="{",".join(additional)}"/>'
    text = f'''<?xml version="1.0" encoding="UTF-8"?>
<sumoConfiguration xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
 xsi:noNamespaceSchemaLocation="http://sumo.dlr.de/xsd/sumoConfiguration.xsd">
    <input>
        <net-file value="{rel(net)}"/>
        <route-files value="{trips.name}"/>{additional_xml}
    </input>
    <time>
        <begin value="{a.begin:g}"/>
        <end value="{a.end:g}"/>
        <step-length value="{a.step_length:g}"/>
    </time>
    <processing>
        <ignore-route-errors value="false"/>
        <time-to-teleport value="-1"/>
    </processing>
    <routing>
        <device.rerouting.adaptation-steps value="18"/>
        <device.rerouting.adaptation-interval value="10"/>
    </routing>
    <report>
        <no-step-log value="true"/>
        <duration-log.statistics value="true"/>
    </report>
</sumoConfiguration>
'''
    path.write_text(text, encoding="utf-8")


def generate_trips(random_trips: Path, net: Path, output: Path,
                   period: float, seed: int, a: argparse.Namespace) -> None:
    cmd = [sys.executable, str(random_trips), "-n", str(net),
           "-o", str(output), "--begin", str(a.begin), "--end", str(a.end),
           "--period", f"{period:.10g}", "--seed", str(seed),
           "--prefix", "veh", "--vehicle-class", "passenger",
           "--validate", "--remove-loops",
           "--min-distance", "300", "--fringe-factor", "5",
           "--trip-attributes", 'departLane="best"']
    subprocess.run(cmd, check=True)


def simulate(cfg: Path, fcd: Path, a: argparse.Namespace) -> None:
    cmd = [a.sumo_binary, "-c", str(cfg), "--seed", str(a.seed),
           "--fcd-output", str(fcd), "--fcd-output.geo", "false",
           "--no-warnings", "true"]
    subprocess.run(cmd, check=True, cwd=cfg.parent,
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)


def count_density(fcd: Path, warmup: float, end: float, area: float) -> tuple[float, float, int]:
    opener = gzip.open if fcd.suffix == ".gz" else open
    total = samples = 0
    with opener(fcd, "rt", encoding="utf-8") as stream:
        for event, elem in ET.iterparse(stream, events=("end",)):
            if elem.tag.endswith("timestep"):
                t = float(elem.attrib["time"])
                if warmup <= t < end:
                    total += sum(1 for child in elem if child.tag.endswith("vehicle"))
                    samples += 1
                elem.clear()
    if not samples:
        raise RuntimeError("FCD contains no retained-interval timesteps")
    mean_active = total / samples
    return mean_active / area, mean_active, samples


def calibrate_target(target: float, net: Path, random_trips: Path,
                     a: argparse.Namespace, writer: csv.DictWriter) -> dict[str, float | int | str]:
    label = f"{target:g}"
    outdir = a.sumo_root / f"density_{label}"
    outdir.mkdir(parents=True, exist_ok=True)
    trips = outdir / "osm.passenger.trips.xml"
    cfg = outdir / "osm.sumocfg"
    fcd = outdir / "calibration_fcd.xml.gz"
    period = a.initial_period
    best = None
    tolerance = a.tolerance_pct / 100.0
    for iteration in range(1, a.max_iterations + 1):
        generate_trips(random_trips, net, trips, period, a.seed, a)
        write_sumocfg(cfg, net, trips, a)
        simulate(cfg.resolve(), fcd.resolve(), a)
        achieved, mean_active, samples = count_density(fcd, a.warmup, a.end,
                                                        a.study_area_km2)
        error_pct = 100.0 * (achieved - target) / target
        row = {"target_density_vpkm2": target, "iteration": iteration,
               "period_s": period, "mean_active_vehicles": mean_active,
               "achieved_density_vpkm2": achieved, "error_pct": error_pct,
               "samples": samples, "seed": a.seed, "status": "candidate"}
        if abs(error_pct) <= a.tolerance_pct:
            row["status"] = "accepted"
        writer.writerow(row)
        best = row if best is None or abs(error_pct) < abs(float(best["error_pct"])) else best
        print(f"d={target:g} iteration={iteration} period={period:.6g}s "
              f"active={mean_active:.3f} achieved={achieved:.3f} error={error_pct:+.2f}%")
        if abs(error_pct) <= a.tolerance_pct:
            break
        if achieved <= 0:
            period *= 0.25
        else:
            # Active population is approximately inversely proportional to period.
            ratio = achieved / target
            period *= min(2.5, max(0.4, ratio))
    assert best is not None
    if abs(float(best["error_pct"])) > a.tolerance_pct:
        raise RuntimeError(f"Density {target:g} did not converge within ±{a.tolerance_pct:g}%")
    # The accepted iteration is already the final on-disk configuration.
    if fcd.exists():
        fcd.unlink()
    (outdir / "calibration.env").write_text(
        "\n".join((f"target_density_vpkm2={target:g}",
                    f"study_area_km2={a.study_area_km2:.10g}",
                    f"mean_active_vehicles={float(best['mean_active_vehicles']):.10g}",
                    f"achieved_density_vpkm2={float(best['achieved_density_vpkm2']):.10g}",
                    f"error_pct={float(best['error_pct']):.10g}",
                    f"calibration_seed={a.seed}",
                    f"warmup_s={a.warmup:g}", f"end_s={a.end:g}", "")),
        encoding="utf-8")
    return best


def main() -> int:
    a = parse_args()
    net, random_trips = check_inputs(a)
    report = a.sumo_root / "density_calibration.csv"
    fields = ["target_density_vpkm2", "iteration", "period_s",
              "mean_active_vehicles", "achieved_density_vpkm2", "error_pct",
              "samples", "seed", "status"]
    accepted = []
    with report.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for target in a.targets:
            accepted.append(calibrate_target(target, net, random_trips, a, writer))
    print(f"Calibration report: {report}")
    print("All requested densities are within tolerance.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())