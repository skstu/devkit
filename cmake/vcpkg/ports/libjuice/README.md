# SovKit libjuice overlay

This port pins libjuice 1.7.2 (#10), with upstream patches and SovKit patches,
including `sovkit-check-stats.diff`, `sovkit-receive-stats.diff` and
`sovkit-windows-poll-wakeup.diff`. The root `vcpkg-configuration.json` selects this port
for every manifest-based SDK build. Do not replace it with an unpatched system
libjuice: `libnet_ice.cc` requires the private statistics accessor.

The patch adds per-agent numeric ICE check statistics. Updates run at existing
libjuice connection-locked call sites; the accessor copies under `conn_lock`.
It does not change packets, ICE roles, retransmission timers, nomination,
credential validation, or candidate selection. No global log callback, SDP,
address, password, or transaction ID is collected. The accessor is internal to
our static dependency; SovKit's shared-library export allowlist remains intact.

Counter semantics and verification are documented in
`docs/networking/NAT_CHECK_DIAGNOSTICS.md` and `docs/networking/NAT_IPV4_RECEIVE_DIAGNOSTICS.md`.
Receive counters include gathering traffic, unlike accepted peer-check counters.
The receive patch also removes a password value from an upstream warning.
Direct sends are instrumented; do not interpret
these counters as complete TURN traffic statistics. Production remains
direct-only, with no relay enablement in this patch.

On Windows, the connection thread caps a single poll wait at 250 ms. UDP
self-interruption can fail to wake the thread when local IPv6 delivery is
unavailable even though sendto succeeds. The cap lets shutdown and queued work
proceed without waiting for the next long ICE timer. Normal UDP wakeups remain
in use; POSIX polling, wire packets and ICE timer deadlines are unchanged.
`sovkit.ice_shutdown` covers stopping idle agents without prior gathering.

The upstream MPL-2.0 headers are retained. The port installs upstream `LICENSE`
as its copyright file. Distributors must preserve the upstream license and
make the corresponding modified libjuice source available; the pinned source
version, SHA-512, upstream patches, and SovKit patch are all in this directory.
When updating libjuice, rebase this patch and rerun the success, silent-peer,
partial-gather and exact-export tests on every supported build target.
# Network path selection (port revision 11)

`sovkit-network-path.diff` adds an opt-in, THREAD-only UDP preparation callback.
The callback is copied into agent configuration and runs before bind or traffic;
failure closes the socket. All unrelated socket configurations remain zero-initialized.
SovKit supplies a numeric bind address plus OS interface/Android Network scoping.
The SDK and the private static dependency must be rebuilt together. See
`docs/networking/NETWORK_PATH_MANAGEMENT.md` for ABI scope and platform validation limits.
