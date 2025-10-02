# Protocol Common Work Breakdown

- **bytes.hpp (LE put/get)**
  - Implement templated helpers for little-endian encoding/decoding of integral types and byte spans.
  - Provide bounds-checked variants for buffer writes and reads to avoid overruns.
  - Document usage patterns for protocol serialization layers.
- **crc32**
  - Implement table-driven CRC32 calculator exposed via constexpr-friendly API.
  - Add helper to compute CRC32 over std::span/std::string_view inputs.
  - Supply precomputed reference values for validation.
- **sha256 (pure C++)**
  - Write dependency-free SHA-256 implementation covering block processing, padding, and digest extraction.
  - Expose incremental hasher plus convenience function for single-shot hashing.
  - Verify against known test vectors from NIST.
- **protocol.hpp (structures/serialization)**
  - Define message header/body structs shared between client and server.
  - Integrate bytes.hpp helpers for deterministic encoding/decoding routines.
  - Ensure versioning and backward-compatibility metadata is captured.
- **config.hpp (CLI/ENV)**
  - Provide configuration loader combining command-line flags and environment variables with precedence rules.
  - Supply schema/validation for network endpoints, timeouts, and authentication tokens.
  - Include defaults and documentation for deployment scenarios.
- **socket_utils.hpp (ASIO helpers, reconnect, tcp_nodelay)**
  - Wrap boost::asio primitives to simplify connection lifecycle management.
  - Implement reconnection policy with jittered backoff and tcp_nodelay toggling.
  - Surface observability hooks for connection state transitions.

## Testing Focus
- Deterministic serialization/deserialization round-trip tests for protocol.hpp using fixed message fixtures.
- CRC32 checksum verification against constant payloads to match documented reference outputs.
