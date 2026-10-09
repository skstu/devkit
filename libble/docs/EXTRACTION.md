# Extraction contract — 2026-10-09

Source: Sovkit `projects/client/flutter/apple/NearbyBleBridge.swift` at
`0311f9bc064ec7768d6a1aac5fcbac8786420a68`, SHA-256 `fb1e68da195c8c126825880a73394031aa3ad58894f98fe506c640985963cd65`. This is an extraction of the working Apple implementation,
not a new BLE wire protocol. Historical on-device results are recorded in
Sovkit `docs/networking/NEARBY_DISCOVERY_RECOVERY.md`; they do not certify this build.

| Existing behavior | Destination / treatment |
| --- | --- |
| CoreBluetooth roles, characteristic setup, ATT fragmentation, notification backpressure | libble Apple engine, retained |
| Four-link admission, send deadlines, bounded reconnect intent, one-shot probe cancellation/adoption, inbound-link isolation | libble transport operations, retained and state-fixture checked |
| Service/receive/notify UUIDs | caller configuration; no Sovkit constants in SDK |
| Advertisement hint reassociation | explicit opt-in only; never identity proof |
| Flutter method/event channels | removed; typed C ABI and bounded event dispatch |
| Length prefix and SDK packet framing | remain in Sovkit; bytes are not re-encoded by libble |
| Noise, safety code, bilateral grants, relationship matching, application ACK | remain in Sovkit |
| Which missing trusted relationship to probe, retry authorization and UI | remain in Sovkit/controller until joint integration |

The existing Sovkit BLE profile uses service `7E2A2000-6D8F-4D8A-A773-534F564B4954`,
receive `7E2A2001-6D8F-4D8A-A773-534F564B4954`, and notification
`7E2A2002-6D8F-4D8A-A773-534F564B4954`. These are documentation for the consuming
adapter, never library defaults. Existing framing uses a four-byte big-endian
length before a 20..65556-byte SDK packet. The current BLE product contract
covers encrypted short messages and ACK, NOT file transfer.

Extraction changes: a stable C boundary, caller-owned UUID configuration,
main-thread validation, callbacks outside native delegates, bounded/coalesced
mailbox, explicit send busy, and stop-generation invalidation. Stop detaches
old delegates; manager callbacks reject an older manager instance. Zero-length
ATT capacity fails closed instead of spinning. None changes protocol bytes.

Preserved state fixtures cover probe hint isolation, cancellation removing retry
intent, normal retry retention, inbound reset isolation, rejection of replacing
a ready old connection, preserving a busy send, and completing a closed send
once. ABI fixtures cover lifetime, wrong thread, reentrancy and lazy creation.
No radio scans, real devices, identities or stores were used. Physical packet
capture, role interchange, address rotation, power toggles and background tests
are deliberately deferred to the joint libble/libice integration requested by
the user. Superseding user instruction on 2026-10-10: remove the legacy
consumer implementation immediately and then regress. SovKit has now deleted
the old engines, invitation GATT, Flutter channels and dedicated build/test
wiring. libble is the only radio implementation; NFC and business authentication,
framing, recovery policy and durable receipts remain. Current radio/product
limitations are recorded in RADIO_VALIDATION_20261010.md; deletion is not
acceptance. The source commit above remains extraction provenance only.
