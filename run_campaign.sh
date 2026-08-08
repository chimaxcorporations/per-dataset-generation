#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: ./run_campaign.sh MODE [--execute]

MODE:
  pilot       18 short Juelich smoke-test runs (50 s, seeds 1-2)
  core        270 Juelich factorial runs (RQ1-RQ4)
  windows     12 observation-window runs (RQ5)
  unseen      90 unseen-road runs (RQ6)
  all         all 372 full-campaign runs (pilot excluded)

Without --execute, the script validates the design and writes run_matrix.tsv.

Environment variables:
  OUTPUT_ROOT                 Output root (default: per_dataset/campaign_v1)
  UNSEEN_ROOT                 SUMO root for unseen scenario (default: unseen_sumo)
  UNSEEN_SCENARIO             Unseen scenario ID (default: unseen_urban)
  UNSEEN_PROPAGATION          Selected unseen model (default: urban)
  PROPAGATION_CLI_SUPPORTED   Set to true only after --propagation is implemented
EOF
}

[[ $# -ge 1 && $# -le 2 ]] || { usage >&2; exit 2; }

mode="$1"
execute=false

if [[ $# -eq 2 ]]; then
  [[ "$2" == "--execute" ]] || { usage >&2; exit 2; }
  execute=true
fi

case "$mode" in
  pilot|core|windows|unseen|all) ;;
  *) usage >&2; exit 2 ;;
esac

output_root="${OUTPUT_ROOT:-per_dataset/campaign_v1}"
matrix="${output_root}/run_matrix.tsv"
propagation_cli_supported="${PROPAGATION_CLI_SUPPORTED:-false}"

densities=(10 30 60)
rates=(2 5 10)
full_propagations=(log-distance log-distance-nakagami urban)
seeds=(1 2 3 4 5 6 7 8 9 10)
observation_windows=(0.5 1 2 5)

declare -A juelich_cfg=(
  [10]="juelich_sumo/density_10/osm.sumocfg"
  [30]="juelich_sumo/density_30/osm.sumocfg"
  [60]="juelich_sumo/density_60/osm.sumocfg"
)

unseen_scenario="${UNSEEN_SCENARIO:-unseen_urban}"
unseen_root="${UNSEEN_ROOT:-unseen_sumo}"
declare -A unseen_cfg=(
  [10]="${unseen_root}/density_10/osm.sumocfg"
  [30]="${unseen_root}/density_30/osm.sumocfg"
  [60]="${unseen_root}/density_60/osm.sumocfg"
)

mkdir -p "$output_root"
printf 'campaign\trun_id\tscenario\ttarget_density_vpkm2\tcam_rate_hz\tpropagation\tns3_seed\tns3_run\tsumo_seed\twindow_s\tsumocfg\tstatus\n' > "$matrix"

add_run() {
  local campaign="$1" scenario="$2" density="$3" rate="$4"
  local propagation="$5" seed="$6" window="$7" cfg="$8"
  local ptag="${propagation//-/_}"
  local wtag="${window//./p}"
  local run_id

  run_id="${scenario}_${campaign}_d${density}_r${rate}_${ptag}_s$(printf '%02d' "$seed")_w${wtag}"

  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\tplanned\n' \
    "$campaign" "$run_id" "$scenario" "$density" "$rate" \
    "$propagation" 12345 "$seed" "$seed" "$window" "$cfg" >> "$matrix"
}

make_pilot() {
  local density rate seed

  # The current executable has one hard-coded propagation configuration and
  # does not accept --propagation. The pilot therefore uses one honest label.
  for density in "${densities[@]}"; do
    for rate in "${rates[@]}"; do
      for seed in 1 2; do
        add_run pilot juelich "$density" "$rate" log-distance "$seed" 1 \
          "${juelich_cfg[$density]}"
      done
    done
  done
}

make_core() {
  local density rate propagation seed

  for density in "${densities[@]}"; do
    for rate in "${rates[@]}"; do
      for propagation in "${full_propagations[@]}"; do
        for seed in "${seeds[@]}"; do
          add_run core juelich "$density" "$rate" "$propagation" "$seed" 1 \
            "${juelich_cfg[$density]}"
        done
      done
    done
  done
}

make_windows() {
  local seed window

  for seed in 1 2 3; do
    for window in "${observation_windows[@]}"; do
      add_run windows juelich 30 5 log-distance-nakagami "$seed" "$window" \
        "${juelich_cfg[30]}"
    done
  done
}

make_unseen() {
  local selected="${UNSEEN_PROPAGATION:-urban}"
  local density rate seed

  for density in "${densities[@]}"; do
    for rate in "${rates[@]}"; do
      for seed in "${seeds[@]}"; do
        add_run unseen "$unseen_scenario" "$density" "$rate" "$selected" \
          "$seed" 1 "${unseen_cfg[$density]}"
      done
    done
  done
}

case "$mode" in
  pilot)   make_pilot ;;
  core)    make_core ;;
  windows) make_windows ;;
  unseen)  make_unseen ;;
  all)     make_core; make_windows; make_unseen ;;
esac

planned=$(( $(wc -l < "$matrix") - 1 ))

case "$mode" in
  pilot)   expected=18 ;;
  core)    expected=270 ;;
  windows) expected=12 ;;
  unseen)  expected=90 ;;
  all)     expected=372 ;;
esac

[[ "$planned" -eq "$expected" ]] || {
  echo "Run-count error: $planned != $expected" >&2
  exit 1
}

duplicates=$(cut -f2 "$matrix" | tail -n +2 | sort | uniq -d | wc -l)
[[ "$duplicates" -eq 0 ]] || {
  echo "Duplicate run IDs: $duplicates" >&2
  exit 1
}

echo "Planned runs: $planned"
echo "Run matrix: $matrix"

if [[ "$execute" != true ]]; then
  exit 0
fi

# The pilot is a fast smoke test. Full campaigns retain their analysis times.
if [[ "$mode" == "pilot" ]]; then
  warmup=5
  sim_end=50
  sumo_begin=0
  sumo_end=50
else
  warmup=60
  sim_end=660
  sumo_begin=0
  sumo_end=660
fi

[[ -x ./ns3 ]] || {
  echo "Error: execute this script from the ns-3 source root" >&2
  exit 2
}

# Full campaigns require real run-time propagation selection. This guard
# prevents differently labelled rows from using the same hard-coded model.
if [[ "$mode" != "pilot" && "$propagation_cli_supported" != true ]]; then
  echo "Error: $mode execution requires an implemented --propagation option." >&2
  echo "Generate its matrix without --execute, or implement the option and run with:" >&2
  echo "  PROPAGATION_CLI_SUPPORTED=true ./run_campaign.sh $mode --execute" >&2
  exit 2
fi

# Validate every SUMO configuration and its density-calibration metadata before
# starting any simulation.
missing=0
declared_area=""

while IFS=$'\t' read -r \
  campaign run_id scenario density rate propagation \
  ns3_seed ns3_run sumo_seed window cfg status
do
  [[ "$campaign" == "campaign" ]] && continue

  if [[ ! -f "$cfg" ]]; then
    echo "Missing calibrated SUMO config: $cfg" >&2
    missing=1
    continue
  fi

  calibration="$(dirname "$cfg")/calibration.env"
  if [[ ! -f "$calibration" ]]; then
    echo "Missing calibration metadata: $calibration" >&2
    missing=1
    continue
  fi

  target=$(sed -n 's/^target_density_vpkm2=//p' "$calibration")
  achieved=$(sed -n 's/^achieved_density_vpkm2=//p' "$calibration")
  error_pct=$(sed -n 's/^error_pct=//p' "$calibration")
  area=$(sed -n 's/^study_area_km2=//p' "$calibration")

  [[ "$target" == "$density" ]] || {
    echo "Density metadata mismatch: run=$density calibration=$target ($calibration)" >&2
    missing=1
  }

  awk -v error="$error_pct" \
    'BEGIN { exit !(error != "" && error + 0 >= -5 && error + 0 <= 5) }' || {
      echo "Calibration outside +/-5%: achieved=$achieved error=$error_pct ($calibration)" >&2
      missing=1
    }

  [[ -n "$area" ]] || {
    echo "Missing study_area_km2: $calibration" >&2
    missing=1
  }

  if [[ -z "$declared_area" ]]; then
    declared_area="$area"
  elif [[ "$area" != "$declared_area" ]]; then
    echo "Inconsistent study areas: $declared_area versus $area ($calibration)" >&2
    missing=1
  fi
done < "$matrix"

[[ "$missing" -eq 0 ]] || exit 2

while IFS=$'\t' read -r \
  campaign run_id scenario density rate propagation \
  ns3_seed ns3_run sumo_seed window cfg status
do
  [[ "$campaign" == "campaign" ]] && continue

  run_dir="${output_root}/${run_id}"

  if [[ -f "${run_dir}/SUCCESS" ]]; then
    echo "Skip successful: $run_id"
    continue
  fi

  [[ ! -e "$run_dir" ]] || {
    echo "Refusing partial/unknown output: $run_dir" >&2
    exit 1
  }

  mkdir -p "$run_dir"
  interval=$(awk -v rate="$rate" 'BEGIN { printf "%.10g", 1 / rate }')

  command="sumo-per-example \
--sumocfg=${cfg} --gui=false \
--runId=${run_id} --scenarioId=${scenario} \
--outputDir=${run_dir} --outCsv=window.csv \
--ns3Seed=${ns3_seed} --ns3Run=${ns3_run} --sumoSeed=${sumo_seed} \
--step=0.1 --window=${window} --warmup=${warmup} --simEnd=${sim_end} \
--beaconInterval=${interval} --beaconSize=300 --trafficStart=0 \
--maxVehicles=1000 --sumoBegin=${sumo_begin} --sumoEnd=${sumo_end}"

  if [[ "$propagation_cli_supported" == true ]]; then
    command+=" --propagation=${propagation}"
  fi

  if ! ./ns3 run "$command" >"${run_dir}/stdout.log" 2>"${run_dir}/stderr.log"; then
    touch "${run_dir}/FAILED"
    echo "Failed: $run_id" >&2
    continue
  fi

  if [[ ! -s "${run_dir}/window.csv" ]]; then
    touch "${run_dir}/FAILED"
    echo "Failed (missing or empty window.csv): $run_id" >&2
    continue
  fi

  {
    printf 'campaign=%s\n' "$campaign"
    printf 'target_density_vpkm2=%s\n' "$density"
    printf 'cam_rate_hz=%s\n' "$rate"
    printf 'propagation=%s\n' "$propagation"
    printf 'ns3_seed=%s\n' "$ns3_seed"
    printf 'ns3_run=%s\n' "$ns3_run"
    printf 'sumo_seed=%s\n' "$sumo_seed"
    printf 'window_s=%s\n' "$window"
    printf 'warmup_s=%s\n' "$warmup"
    printf 'sim_end_s=%s\n' "$sim_end"
  } > "${run_dir}/campaign.env"

  touch "${run_dir}/SUCCESS"
  echo "Completed: $run_id"
done < "$matrix"