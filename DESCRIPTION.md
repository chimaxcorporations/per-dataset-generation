# Seven-RQ simulation campaign

This framework separates the evidence required by each research question. The
270-run factorial campaign is necessary, but it does not by itself answer RQ5,
RQ6, or the leakage part of RQ7.

| RQ | Evidence-producing campaign | Primary analysis |
|---|---|---|
| RQ1 distance -> PER | 270 core runs | Distance-bin PER with run-level 95% CIs; binomial mixed model controlling density, rate, propagation |
| RQ2 density/rate -> CBR/PER | 270 core runs | Factor contrasts and density x rate interaction, using achieved density |
| RQ3 propagation sensitivity | 270 core runs | Paired model contrasts for RSSI, SNR, PER and within-distance-bin variability |
| RQ4 reproducibility | 10 seeds in each of 27 core cells | Between-seed variance, CI, coefficient of variation and identical-seed checksum test |
| RQ5 window validity | 12 paired runs at 0.5, 1, 2, 5 s | Invariants, packet-count reconciliation, PER variance and missing-PHY fraction |
| RQ6 unseen road | 90 runs on a second calibrated topology | Repeat validity tests and compare effect direction with Juelich |
| RQ7 missingness/leakage | Every dataset plus modelling pipeline | Missingness audit; lagged PHY features; split by run/seed/scenario |

## Required implementation gates

Do not execute the scientific campaign until all gates pass:

1. The three Juelich SUMO configurations exist and their retained-period mean
   achieved densities are within the predeclared tolerance (recommended +/-5%)
   of 10, 30 and 60 vehicles/km2.
2. `sumo-per-example --PrintHelp` exposes all arguments used by the runner.
3. Propagation accepts exactly `log-distance`, `log-distance-nakagami`, and
   `urban`. The current helper only supports the first two; `urban` must be
   implemented with an obstruction-aware model and map/building data before the
   runner can complete.
4. Every CSV records target and achieved density (or active vehicles and study
   area), effective PHY/MAC settings, scenario, propagation, and all seeds.
5. A repeated identical-seed pilot produces the same normalized checksum.
6. No pool exhaustion, duplicate run ID, output append, or invalid record occurs.

## Campaign sizes

- Core: 3 densities x 3 CAM rates x 3 models x 10 seeds = **270**.
- Window validation: 1 paired treatment x 3 seeds x 4 windows = **12**.
- Unseen road: 3 densities x 3 rates x 1 selected model x 10 seeds = **90**.
- Total planned executions: **372** (some 1-second window evidence may be reused
  only if the configurations and seeds are identical and this is documented).

The 600-second measurement interval is represented by `warmup=60` and
`simEnd=660`; rows must cover `[60,660]`. CAM intervals are 0.5, 0.2 and 0.1 s.

## Safe execution sequence

Generate and inspect matrices without executing simulations:

```bash
bash simulation_campaign/run_campaign.sh core
bash simulation_campaign/run_campaign.sh windows
bash simulation_campaign/run_campaign.sh unseen
```

First run one manually selected cell per propagation model. Then execute a
54-run pilot (the 27 core cells with seeds 1 and 2). Only after its gates pass,
run the full core campaign:

```bash
bash simulation_campaign/run_campaign.sh core --execute
```

The runner resumes only runs containing `SUCCESS`. It refuses ambiguous partial
directories so failed evidence is not silently overwritten.

Audit generated CSVs:

```bash
python3 simulation_campaign/audit_dataset.py \
  per_dataset/campaign_v1/*/window.csv \
  --output per_dataset/campaign_v1/rq7_audit.json
```

## RQ7 modelling rule

`rssi_dbm`, `snr_db`, `phy_n`, and `phy_observed=(phy_n>0)` are reception-derived.
They must not predict PER from the same observation window. Use values lagged by
at least one complete window, retain missing values plus a justified missingness
strategy, and keep all rows from one run in the same train/validation/test split.
For the cross-scenario test, train/tune on Juelich only and evaluate the frozen
pipeline on the unseen road scenario.
