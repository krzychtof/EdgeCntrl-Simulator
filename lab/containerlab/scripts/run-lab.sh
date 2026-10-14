#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
lab_dir="$(cd -- "$script_dir/.." && pwd)"
topology="$lab_dir/topology.clab.yml"
prefix="clab-edgecntrl-frr-"

cd "$lab_dir"

# CRLF changes "no" into "no\r" in FRR's shell-sourced daemon settings.
find "$lab_dir/configs" -type f \( -name daemons -o -name '*.conf' \) \
    -exec sed -i 's/\r$//' {} +

containerlab deploy --topo "$topology" --max-workers 1

wait_for_bgp() {
    local router="$1"
    local neighbor="$2"
    local attempt
    for attempt in $(seq 1 40); do
        if docker exec "${prefix}${router}" vtysh \
            -c "show ip bgp neighbor ${neighbor}" 2>/dev/null |
            grep -q "BGP state = Established"; then
            return 0
        fi
        sleep 1
    done
    echo "BGP neighbor ${neighbor} on ${router} did not reach Established." >&2
    docker exec "${prefix}${router}" vtysh -c "show ip bgp summary" || true
    return 1
}

echo "Waiting for all eBGP sessions..."
wait_for_bgp edge-west 10.0.12.2
wait_for_bgp edge-west 10.0.13.2
wait_for_bgp core-north 10.0.24.2
wait_for_bgp core-south 10.0.34.2

echo "Configuring VPC bridges and VXLAN VNI 5001..."
docker exec "${prefix}edge-west" sh -ec '
    ip link add br-vpc type bridge
    ip link set eth3 up
    ip link set eth3 master br-vpc
    ip addr add 10.10.0.1/24 dev br-vpc
    ip link add vxlan5001 type vxlan id 5001 local 10.255.0.1 remote 10.255.0.4 dstport 4789
    ip link set vxlan5001 master br-vpc
    bridge fdb append 00:00:00:00:00:00 dev vxlan5001 dst 10.255.0.4 self permanent
    ip link set vxlan5001 up
    ip link set br-vpc up
'
docker exec "${prefix}edge-east" sh -ec '
    ip link add br-vpc type bridge
    ip link set eth3 up
    ip link set eth3 master br-vpc
    ip addr add 10.20.0.1/24 dev br-vpc
    ip link add vxlan5001 type vxlan id 5001 local 10.255.0.4 remote 10.255.0.1 dstport 4789
    ip link set vxlan5001 master br-vpc
    bridge fdb append 00:00:00:00:00:00 dev vxlan5001 dst 10.255.0.1 self permanent
    ip link set vxlan5001 up
    ip link set br-vpc up
'

docker exec "${prefix}tenant-west-host" sh -ec '
    ip link set eth1 up
    ip addr add 192.0.2.10/24 dev eth1
'
docker exec "${prefix}tenant-east-host" sh -ec '
    ip link set eth1 up
    ip addr add 192.0.2.20/24 dev eth1
'

wait_for_next_hop() {
    local expected="$1"
    local attempt
    local route
    for attempt in $(seq 1 30); do
        route="$(docker exec "${prefix}edge-west" ip route get 10.255.0.4)"
        if grep -q "via ${expected}" <<<"$route"; then
            printf '%s\n' "$route"
            return 0
        fi
        sleep 1
    done
    printf '%s\n' "$route" >&2
    echo "Route to remote VTEP did not converge via ${expected}." >&2
    return 1
}

echo "Checking preferred BGP underlay route via core-north..."
wait_for_next_hop 10.0.12.2

echo "Checking tenant host-to-host traffic across the VXLAN overlay..."
docker exec "${prefix}tenant-west-host" ping -c 3 -W 3 192.0.2.20

echo "Checking route failover and tunnel recovery..."
restore_primary_link() {
    if ! docker exec "${prefix}edge-west" ip link set eth1 up; then
        echo "Failed to restore edge-west eth1; inspect the lab before continuing." >&2
        return 1
    fi
}
trap restore_primary_link EXIT
docker exec "${prefix}edge-west" ip link set eth1 down

sleep 5
echo "Waiting for BGP failover via core-south..."
wait_for_next_hop 10.0.13.2
docker exec "${prefix}tenant-west-host" ping -c 3 -W 3 192.0.2.20

restore_primary_link
echo "Checking BGP failback and VXLAN recovery via core-north..."
wait_for_next_hop 10.0.12.2
docker exec "${prefix}tenant-west-host" ping -c 3 -W 3 192.0.2.20
trap - EXIT

echo "Real FRR/BGP/VXLAN failover and recovery passed. Lab containers remain running."
