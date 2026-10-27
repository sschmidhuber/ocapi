#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2025 ocapi Contributors
#
# Runs the ocapi E2E test suite and produces a review-friendly report:
#
#   - one screenshot per test (OCAPI_E2E_SCREENSHOT_DIR, captured with the
#     ImageMagick "import" tool from the X display; the appium screenshot
#     endpoint is unavailable in the driver's headless mode)
#   - JUnit XML (OCAPI_E2E_JUNIT_XML) for tooling
#   - a Markdown report (OCAPI_E2E_REPORT_MD) with a results table and a
#     screenshot gallery, ready to paste into a GitHub job summary
#
# Screenshots are taken through X and not through the WebDriver because the
# driver's screenshot endpoint shells out to a KWin-only helper binary that
# is not built in the headless (no nested kwin) mode used on CI.

import os
import subprocess
import sys
import time
import unittest

SCREENSHOT_DIR = os.environ.get("OCAPI_E2E_SCREENSHOT_DIR", "e2e-report")
JUNIT_XML = os.environ.get("OCAPI_E2E_JUNIT_XML")
REPORT_MD = os.environ.get("OCAPI_E2E_REPORT_MD")
DISPLAY = os.environ.get("DISPLAY", ":99")


def screenshot(name):
    os.makedirs(SCREENSHOT_DIR, exist_ok=True)
    for _attempt in range(3):
        path = os.path.join(SCREENSHOT_DIR, name)
        try:
            subprocess.run(
                ["import", "-window", "root", "-display", DISPLAY, path],
                check=True,
                timeout=30,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            if os.path.getsize(path) > 0:
                return path
        except (subprocess.SubprocessError, OSError):
            time.sleep(1)
    return None


def run():
    # Imported here so the reporting functions below stay usable without
    # the appium/selenium dependencies installed.
    import ocapi_e2e_test

    suite = unittest.defaultTestLoader.loadTestsFromModule(ocapi_e2e_test)
    runner = unittest.TextTestRunner(verbosity=2, resultclass=TestResult)
    result = runner.run(suite)
    write_junit(result)
    write_markdown(result)
    return 0 if result.wasSuccessful() else 1


class TestResult(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.records = []

    def startTest(self, test):
        super().startTest(test)
        self.records.append(
            {"name": str(test), "status": "ran", "seconds": None, "screenshot": None, "error": None}
        )
        self._started = time.monotonic()

    def addSuccess(self, test):
        super().addSuccess(test)
        self._finish("passed")

    def addError(self, test, err):
        super().addError(test, err)
        self._finish("error", err)

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self._finish("failed", err)

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self._finish("skipped")

    def _finish(self, status, err=None):
        record = self.records[-1]
        record["status"] = status
        record["seconds"] = round(time.monotonic() - self._started, 1)
        if err:
            import traceback

            exctype, value, tb = err
            record["error"] = "".join(traceback.format_exception(exctype, value, tb))
        shot = screenshot(record["name"].split(" ")[0] + ".png")
        record["screenshot"] = shot


def write_junit(result):
    if not JUNIT_XML:
        return
    import xml.etree.ElementTree as ET

    testsuite = ET.Element(
        "testsuite",
        name="ocapi-e2e",
        tests=str(len(result.records)),
        failures=str(sum(1 for r in result.records if r["status"] == "failed")),
        errors=str(sum(1 for r in result.records if r["status"] == "error")),
        time=str(sum(r["seconds"] or 0 for r in result.records)),
    )
    for r in result.records:
        tc = ET.SubElement(
            testsuite,
            "testcase",
            classname="ocapi-e2e",
            name=r["name"],
            time=str(r["seconds"] or 0),
        )
        if r["error"]:
            child = ET.SubElement(tc, "error" if r["status"] == "error" else "failure")
            child.text = r["error"]
    ET.ElementTree(testsuite).write(JUNIT_XML, encoding="utf-8", xml_declaration=True)


def write_markdown(result):
    if not REPORT_MD:
        return
    icon = {"passed": "✅", "failed": "❌", "error": "💥", "skipped": "⏭️", "ran": "⏳"}
    lines = ["## ocapi E2E test results", ""]
    passed = sum(1 for r in result.records if r["status"] == "passed")
    lines.append(f"**{passed}/{len(result.records)} tests passed**")
    lines.append("")
    lines += ["| # | Test | Result | Duration |", "|---|------|--------|----------|"]
    for i, r in enumerate(result.records, 1):
        name = r["name"].split(" ")[0]
        seconds = f"{r['seconds']}s" if r["seconds"] is not None else "–"
        status = f"{icon.get(r['status'], '⏳')} {r['status']}"
        lines.append(f"| {i} | `{name}` | {status} | {seconds} |")
    lines.append("")
    lines.append("### Screenshots")
    lines.append("")
    lines.append(
        "Screenshots of the app after each test are uploaded as the "
        "`e2e-report` artifact of this run."
    )
    lines.append("")
    shots = [r for r in result.records if r["screenshot"]]
    for r in shots:
        name = r["name"].split(" ")[0]
        rel = os.path.relpath(r["screenshot"], SCREENSHOT_DIR)
        status = f"{icon.get(r['status'], '')} {r['status']}"
        lines.append(f"| `{name}` | `screenshots/{rel}` | {status} |")
    if shots:
        lines.insert(
            len(lines) - len(shots),
            "| Test | Screenshot | Result |\n|------|------------|--------|",
        )
    lines.append("")
    failed = [r for r in result.records if r["error"]]
    if failed:
        lines.append("### Failures")
        lines.append("")
        for r in failed:
            name = r["name"].split(" ")[0]
            lines.append(f"#### `{name}`")
            lines.append("")
            lines.append("```")
            lines.append(r["error"])
            lines.append("```")
            lines.append("")
    with open(REPORT_MD, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    sys.exit(run())
