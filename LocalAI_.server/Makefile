SHELL := /bin/bash
COMPOSE_FILE := docker-compose.yml
COMPOSE := docker compose -f $(COMPOSE_FILE)

APP_PORT ?= 8000
-include .env

.PHONY: up down logs bash rebuild test-smoke

up:
	$(COMPOSE) up -d

down:
	$(COMPOSE) down

logs:
	$(COMPOSE) logs -f localai

bash:
	$(COMPOSE) exec localai bash

rebuild:
	$(COMPOSE) up --build -d

test-smoke:
	@set -euo pipefail; \
	port=$${APP_PORT:-8000}; \
	endpoints=(healthz v1/models v1/audio/transcriptions v1/audio/speech v1/images/generations v1/images/edits); \
	for path in "$${endpoints[@]}"; do \
		echo "-> GET http://localhost:$$port/$$path"; \
		status=$$(curl -sS -o /dev/null -w "%{http_code}" "http://localhost:$$port/$$path"); \
		if [[ "$$status" == "000" ]]; then \
			echo "Endpoint $$path is unreachable" >&2; \
			exit 1; \
		fi; \
		done; \
		echo "Smoke tests completed successfully."
