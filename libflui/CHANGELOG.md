# Changelog

## Unreleased

- Consumer-owned translation catalogs with system/explicit Simplified Chinese,
  Traditional Chinese and English, fallback, named parameters and plural messages.
  Locale changes preserve editor identity, focus and composing text.
- Safe-area, scroll and wrap layouts for narrow screens; experimental SDK-owned
  Android/iOS/Windows/Linux host with an event-driven C ABI bridge. See HOST_PREVIEW.md
  for evidence and limitations; this is not a released cross-platform SDK.
- Windows uses the official locked engine plus Release AOT; native host modules
  use the static MSVC runtime. No private static Flutter engine is built.

- Linux x86_64 producer profile uses an enforced Ubuntu 22.04 baseline, static
  native C++ runtime linkage and executable-relative runtime library loading.

- Retained documents can follow system light/dark appearance with separate token
  maps, without changing the C ABI or rebuilding the editor tree.
- Classic 2000 palette, tokenized corner radius and raised/sunken control edges.

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
