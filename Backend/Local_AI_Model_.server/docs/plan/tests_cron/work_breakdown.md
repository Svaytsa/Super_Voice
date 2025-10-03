# Work Breakdown: Integration Tests & Cron Pipeline

## Objectives
- Cover integration test matrix for model artifacts sized at **5 MB**, **≈200 MB**, and **≈1 GB**.
- Validate artifact integrity via **SHA-256 checksum comparison before and after test runs**.
- Execute end-to-end (E2E) scenarios inside Docker to ensure reproducible environments.
- Define container port mapping strategy to support local and CI executions.
- Configure `mcp-cron` automation for scheduled build and E2E runs with reporting back into PRs/logs.

## Deliverables
1. **Test Specifications**
   - Test data preparation scripts for three artifact size tiers (5 MB, 200 MB, 1 GB).
   - Shared utility to compute and compare SHA-256 hashes pre/post test lifecycle.
   - Integration test harness capable of orchestrating Dockerized runs.
2. **Dockerized E2E Pipeline**
   - Dockerfiles/compose definitions for server, client stubs, and mock dependencies.
   - Port mapping documentation (host ↔ container) for API, metrics, and debugging endpoints.
   - CI-ready Makefile or script entry points to trigger E2E suites with configurable artifact size.
3. **mcp-cron Automation**
   - Cron job definitions for periodic builds (nightly) and E2E suites (daily/weekly tiers).
   - Mechanism to capture run metadata (commit hash, artifact size, test duration, pass/fail).
   - Reporting channel integration: PR status checks on active branches + rolling log/Slack (TBD).

## Work Breakdown Structure

### 1. Test Asset Preparation
1.1 Inventory existing sample models and select candidates per size tier.
1.2 Create scripts to generate or download artifacts; enforce storage quotas and cleanup.
1.3 Implement SHA-256 checksum generator and persistence (e.g., JSON manifest).

### 2. Integration Test Harness
2.1 Extend test runner to accept artifact size parameter.
2.2 Implement pre-run checksum logging and post-run verification.
2.3 Add assertions for service health, latency thresholds, and output validation.
2.4 Integrate harness with Docker network (shared bridge) for multi-service scenarios.

### 3. Docker Environment
3.1 Author Docker Compose file with services: model server, dependencies, auxiliary tools.
3.2 Document port mappings:
   - `8080:8080` → REST API
   - `9090:9090` → metrics/prometheus
   - `9229:9229` → optional debugger
3.3 Provide scripts to build/push images for CI use (tagging strategy aligned with git SHA).
3.4 Optimize layer caching to handle large artifact injections efficiently.

### 4. CI Integration & Reporting
4.1 Add Makefile targets (`make e2e SIZE=5mb|200mb|1gb`).
4.2 Wire targets into CI pipeline with matrix strategy (parallel jobs per size tier).
4.3 Capture logs/artifacts, upload to storage, and publish summary to PR status.
4.4 Implement failure triage hooks (auto-attach logs to PR comment).

### 5. mcp-cron Scheduling
5.1 Define cron YAML for nightly build job (pull latest main, run build/tests, publish status).
5.2 Define cron YAML for periodic E2E (daily 5 MB, weekly 200 MB/1 GB to manage cost).
5.3 Configure environment provisioning (Docker registry auth, storage access).
5.4 Implement reporting sink: update PR status when branch referenced, else log to dashboard.
5.5 Add alerting on consecutive failures (n>=2) via existing notification channel.

### 6. Documentation & Onboarding
6.1 Create README section describing test matrix and cron behavior.
6.2 Provide quickstart guide for local execution (Docker + checksum verification).
6.3 Record troubleshooting steps for large artifact handling (disk space, timeouts).

## Timeline & Dependencies
- **Week 1:** Tasks 1.1–2.2 (test assets, checksum tooling, harness parameterization).
- **Week 2:** Tasks 2.3–3.4 (assertions, Docker compose, port mapping, image pipeline).
- **Week 3:** Tasks 4.1–5.3 (CI integration, cron definitions, environment configuration).
- **Week 4:** Tasks 5.4–6.3 (reporting, alerting, documentation, polish).

Dependencies:
- Access to storage for large artifacts.
- Credentials for Docker registry and notification channels.
- Availability of staging infrastructure for E2E verification.

## Risk & Mitigation
- **Large Artifact Handling:** Use streaming download/upload and incremental cleanup scripts to avoid disk exhaustion.
- **Checksum Drift:** Centralize manifest storage; fail fast if mismatch detected post-run.
- **Cron Resource Contention:** Stagger schedules and include pre-run resource checks.
- **Reporting Noise:** Aggregate repeated failures before alerting, provide clear log links.

## Success Criteria
- Automated pipeline runs covering all artifact size tiers with checksum verification.
- Docker E2E suite reproducible locally and in CI with documented ports.
- `mcp-cron` jobs executing on schedule and posting status updates to PRs/logs.
- Documentation enabling new contributors to run tests within one day of onboarding.
