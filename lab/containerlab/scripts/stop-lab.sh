#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
topology="$(cd -- "$script_dir/.." && pwd)/topology.clab.yml"

containerlab destroy --topo "$topology" --cleanup
