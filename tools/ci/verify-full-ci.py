#!/usr/bin/env python3
"""Require one successful same-source CI run containing all ten native targets."""

import json
import os
import subprocess
import sys

EXPECTED = {
    "Linux arch tests (x86_64)",
    "Linux arch tests (arm64)",
    "Linux debian tests (x86_64)",
    "Linux debian tests (arm64)",
    "Linux fedora tests (x86_64)",
    "Linux fedora tests (arm64)",
    "Windows tests (x86_64)",
    "Windows tests (arm64)",
    "macOS tests (x86_64)",
    "macOS tests (arm64)",
}


def api(endpoint):
    result = subprocess.run(
        ["gh", "api", endpoint], check=True, capture_output=True, text=True
    )
    return json.loads(result.stdout)


def main():
    event = os.environ["GITHUB_EVENT_NAME"]
    source_sha = os.environ["CIRRUS_SOURCE_SHA"]
    tests_passed = (
        os.environ.get("CIRRUS_CALLER_TESTS_PASSED", "false").lower() == "true"
    )
    repo = os.environ["GITHUB_REPOSITORY"]

    if event == "workflow_call":
        if os.environ.get("GITHUB_WORKFLOW") != "Release":
            raise SystemExit(
                "Linux package workflow_call is restricted to the Release workflow"
            )
        if not tests_passed:
            raise SystemExit("Release caller did not confirm its successful CI dependency")
        print(f"Reusable build is gated by the caller's same-source CI dependency: {source_sha}")
        return
    if event != "workflow_dispatch":
        raise SystemExit(f"Linux packages cannot run for event {event!r}")

    runs = api(
        f"repos/{repo}/actions/workflows/ci.yml/runs?head_sha={source_sha}&per_page=100"
    )["workflow_runs"]
    best_missing = EXPECTED
    for run in runs:
        if run.get("head_sha") != source_sha or run.get("conclusion") != "success":
            continue
        jobs = api(f"repos/{repo}/actions/runs/{run['id']}/jobs?per_page=100")["jobs"]
        successful = {job["name"] for job in jobs if job.get("conclusion") == "success"}
        missing = EXPECTED - successful
        if not missing:
            print(
                f"Same-source full CI passed: {source_sha} "
                f"(run {run['id']}, all {len(EXPECTED)} native targets)"
            )
            return
        if len(missing) < len(best_missing):
            best_missing = missing
    raise SystemExit(
        "No successful CI run for source SHA "
        f"{source_sha} contains every native test target; best candidate is missing: "
        f"{', '.join(sorted(best_missing))}"
    )


if __name__ == "__main__":
    main()
