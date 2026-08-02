# Product

## Register

product

## Users

People who run a Windows gaming or workstation PC and use Moonlight-compatible clients for low-latency remote access. They configure the host infrequently, then expect streaming sessions to start reliably without manually rearranging Windows displays.

## Product Purpose

This derivative stays close to upstream Sunshine while adding a focused Windows virtual-display workflow. It can create one virtual display for a streaming session, apply a saved resolution and refresh-rate profile, optionally make that display the only active display, and restore the previous Windows display topology when the session ends.

Success means the host remains compatible with upstream Sunshine updates, virtual-display behavior is predictable and recoverable, and users do not need Apollo-specific client or host features to use the core workflow.

## Brand Personality

Calm, direct, dependable. Configuration copy should describe the exact system action. The interface should feel like a native extension of Sunshine rather than a separate control panel layered on top.

## Anti-references

- Do not reproduce Apollo's broad collection of client-specific and experimental controls.
- Do not expose physical-monitor, dummy-plug, or GPU-dongle management as separate product concepts.
- Do not turn advanced driver details into the primary setup flow.
- Do not add decorative dashboards, nested cards, custom scrollbars, or non-standard form controls.

## Design Principles

1. Keep the official Sunshine path intact. Add isolated Windows modules and small lifecycle hooks.
2. Make the common path short. Enable virtual display, choose a default profile, and choose a display mode.
3. Fail safely. Never deactivate existing displays until the virtual display is confirmed ready.
4. Restore predictably. Persist the previous topology and recover it after disconnect, restart, or a failed launch.
5. Separate policy from drivers. Profiles and session behavior must not depend on Parsec VDD or SudoVDA-specific UI.

## Accessibility & Inclusion

Keep upstream Sunshine's familiar controls and keyboard behavior. New controls must expose visible labels, focus states, error text, and disabled reasons; color must not be the only status signal. Target WCAG 2.2 AA contrast and respect reduced-motion preferences.
