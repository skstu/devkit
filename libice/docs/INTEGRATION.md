# libice SDK integration

Use the installed `devkitIce` CMake package, link `devkit::ice`, and call
`devkit_ice_bundle(target)`. The installed pure-C example needs no provider
headers, libjuice installation, C++ compiler or Sovkit source tree.

## Scope

ABI 1 wraps one UDP ICE component: IPv4/IPv6 candidate gathering, optional
explicit STUN, candidate description exchange, selected-path policy, datagrams,
status and lifecycle. Relay is forbidden by default; opt-in TURN credentials
and `DKICE_ALLOW_RELAY` are required to permit relay. A direct-only agent rejects
a mixed remote description containing relay candidates. No hardcoded public
STUN, coordinator, TURN credentials, domain, account or telemetry endpoint exists.

Numeric server addresses are required in this version. Host DNS resolution
and endpoint selection belong to the consumer. Empty STUN/TURN configuration
uses host candidates, but ICE peer checks still use STUN protocol packets.
Neither host candidates nor successful local fixtures imply NAT/WAN reachability.

The SDK intentionally does not expose the legacy product's signed invitation,
assistance ticket, helper-node operation, PCP/diagnostic probes, custom TCP
bearer or multi-interface recovery manager. These are not silently enabled.
The pinned provider sources retain their existing overlay for compatibility;
this is NOT a claim that every inactive implementation is absent from the binary.

## Use and ownership

Create on the owner thread with an explicit config. Gathering starts only when
requested. The internal libjuice worker invokes wake; the callback only signals
the owner and never reenters the SDK. The owner drains a bounded event batch
through dispatch; event bytes/strings expire when the callback returns.

Exchange local descriptions through the consumer's own signaling. Authenticate
that exchange in the business layer, especially on restart. Descriptions contain
short-lived ICE credentials and endpoint addresses: do not write them to generic
logs or advertise them as anonymous data. Selected path / connected state proves
connectivity only; it does not prove a Sovkit relationship or encrypt payloads.

`dkice_local_description` uses a caller-owned buffer and reports the required
size including NUL. `dkice_remote_description` takes explicit byte length and
rejects embedded NULs. Configuration strings are copied before create returns.
All API calls except version queries are owner-thread operations. Callback
reentrancy rejects dispatch/destroy; stop during dispatch cancels the remaining
batch. Stop joins the provider worker, is terminal, and prevents future wakes.
Destroy only after foreign callers stop using the handle. Recreate to restart
with new credentials; authorize the fresh generation in the consumer.

Datagrams remain unreliable. Provider buffering is limited to 256 events
(each payload at most 64 KiB); overload is counted as dropped datagrams.
`send` success reports provider acceptance, never remote delivery. If using
libnet QUIC, route its custom datagram sender through `dkice_send`, and feed ICE
DATAGRAM events into `dknet_quic_receive` on the libnet owner thread. Preserve
the selected ICE socket; opening a new UDP socket to the reported address can
lose the NAT mapping. Cross-thread bridging and authenticated peer association
remain the consumer's responsibility.

## Provenance and validation

The backend is pinned to libjuice 1.7.2 with devkit overlay revision 13. The SDK
ships license notices, provider archive hashes and the exact overlay source and
upstream-source reference. Do not substitute an unpatched system libjuice:
private statistics and socket preparation hooks are required by the provider.
MPL-covered provider modifications retain their upstream license.

Local checks cover C ABI/buffer validation, relay-policy rejection, two loopback
agents exchanging binary datagrams, loopback STUN gathering, repeated create /
stop, wrong-thread rejection, callback reentrancy and wake drain. These do not
claim public STUN availability, TURN field validation, NAT traversal success,
network-change recovery or Apple/mobile background behavior. Joint Sovkit and
physical-device polishing follows libble/libice packaging; existing product
runtime and dependency locks remain unchanged in this preparation stage.
