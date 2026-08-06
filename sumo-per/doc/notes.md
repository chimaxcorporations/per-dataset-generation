# Documentation notes (for your paper)

## Paper-matching description
- Coupling: libsumo/TraCI online step synchronization, Δt = stepSec
- Feature extraction:
  - Mobility: position, speed, inter-vehicle distance, neighbor density (extend)
  - PHY/MAC: RSSI/SNR/MCS, channel busy ratio, Tx attempts, Rx OK, contention (extend)
- Labels:
  - PER per link over window W: 1 - RxOk/Tx
- Validation:
  - Compare PER-vs-distance curves and CDFs with field campaigns (simTD, DRIVE C2X, etc.)
