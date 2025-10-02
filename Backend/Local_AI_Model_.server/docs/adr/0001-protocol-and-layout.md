# ADR 0001: Protocol and Layout

## Status
Proposed

## Context
The multimedia forwarding subsystem requires a consistent protocol definition and server layout to coordinate client and server components, including integration with downstream processing services.

## Decision
- **Patch Header:** All protocol exchanges and code contributions referencing this ADR must include the header tag `MultimediaForwarding-ProtoLayout-0001` for traceability.
- **System Messages:** Server-to-client bootstrap messages will begin with a YAML-formatted control block describing session metadata (session ID, codec negotiation parameters, authentication tokens) followed by binary media frames framed using length-prefix encoding.
- **Ports:**
  - gRPC control channel on port `7420`.
  - WebRTC relay service on port `7443` (TLS required).
  - Metrics endpoint on port `9742` (HTTP/Prometheus).
- **Server Directories:**
  - `apps/forwarder/` for the primary forwarding service.
  - `apps/control/` for signaling and orchestration components.
  - `configs/` for static protocol schemas and environment configuration.
  - `scripts/` for operational tooling, migrations, and maintenance.

## Consequences
- Provides a single source of truth for protocol negotiations across teams.
- Establishes port assignments to avoid collisions and simplify infrastructure automation.
- Clarifies server directory expectations for onboarding and deployment workflows.
