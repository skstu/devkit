# Changelog

## 0.1.1 preview — 2026-10-08

Initial independently consumable macOS arm64 binary SDK; not a production or
cross-platform certification.

- Versioned C ABI with 23 exported functions; optional standard-library C++20 layer.
- XML/state binding sample and retained desktop controls, virtual lists, editable
  decimal strings, native windows/dialogs/timers/files and semantic theme tokens.
- Batched view updates and stable control identity to retain editor state during
  quote refreshes. Business logic and validation stay in consumer applications.
- Locked Release/AOT Flutter runtime, private engine/assets, relocatable CMake
  package and a reusable producer packaging tool with source/payload checksums.
- Independent quote example with one business controller and optional wxui adapter.
- C ABI, model, renderer and shared-controller regressions. A separate Desk
  consumer exercised its migrated UI offline; no live trading is part of this SDK.

Known limits: only macOS arm64 validated; full accessibility, older OS support,
long-run reliability and external distribution signing/notarization remain open.
Rapid engine shutdown can print a dead-channel diagnostic. Performance varies by
view: the Desk card fixture improved CPU, but its list fixture remained more
expensive than wxui. No across-the-board performance guarantee is made.
