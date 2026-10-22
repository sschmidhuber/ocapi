#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2025 ocapi Contributors
#
# End-to-end tests for ocapi, driven through KDE's selenium-webdriver-at-spi
# (AT-SPI accessibility). Run via `selenium-webdriver-at-spi-run`:
#
#   OCAPI_E2E_APP=./build/src/ocapi selenium-webdriver-at-spi-run \
#       tests/e2e/ocapi_e2e_test.py
#
# Requires the mock Ollama server from tests/e2e/mock_ollama.py running on
# http://127.0.0.1:11434 (OCAPI_E2E_MOCK_PORT to override) and the mock MCP
# server from tests/e2e/mock_mcp.py on http://127.0.0.1:11435
# (OCAPI_E2E_MOCK_MCP_PORT to override).
#
# Covers the most essential user flows:
#   1. the app starts with a chat, composer and send button
#   2. the seeded mock backend is active and its model was detected
#   3. a message can be sent and the mock backend's reply streams back
#   4. the seeded mock MCP server connects, and a chat drives a full tool
#      round-trip: the model requests the tool, ocapi runs it on the mock
#      MCP server, feeds the result back and the model's answer appears
#
# Note on menus: Qt excludes Qt::Popup windows (QMenu) from the AT-SPI
# application tree (QAccessibleApplication::topLevelObjects), so popup
# menu items cannot be driven with this stack. The backend setup flow is
# therefore verified through the widgets it populates instead of the
# model picker's popup menu.

import os
import unittest

from appium import webdriver
from appium.options.common.base import AppiumOptions
from appium.webdriver.common.appiumby import AppiumBy
from selenium.common.exceptions import TimeoutException
from selenium.webdriver.support.ui import WebDriverWait

REPLY_TEXT = "Hello from the mock backend!"
TOOL_REPLY_TEXT = "The tool says: echo: Hello from the MCP tool!"


class OcapiE2E(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        port = os.environ.get("OCAPI_E2E_MOCK_PORT", "11434")
        mcp_port = os.environ.get("OCAPI_E2E_MOCK_MCP_PORT", "11435")
        options = AppiumOptions()
        options.set_capability("app", os.environ["OCAPI_E2E_APP"])
        options.set_capability("appium:environ", {
            "OCAPI_E2E_MOCK_BACKEND": "1",
            "OCAPI_E2E_MOCK_PORT": port,
            "OCAPI_E2E_MOCK_MCP": "1",
            "OCAPI_E2E_MOCK_MCP_PORT": mcp_port,
        })
        cls.driver = webdriver.Remote(command_executor="http://127.0.0.1:4723",
                                      options=options)
        cls.driver.implicitly_wait = 10

    @classmethod
    def tearDownClass(cls):
        cls.driver.quit()

    def find_by_name(self, name):
        return self.driver.find_element(by=AppiumBy.NAME, value=name)

    def find_by_xpath(self, xpath):
        return self.driver.find_element(by=AppiumBy.XPATH, value=xpath)

    def wait_for(self, xpath, timeout=20):
        WebDriverWait(self.driver, timeout).until(
            lambda d: self.find_by_xpath(xpath))
        return self.find_by_xpath(xpath)

    def test_01_start(self):
        # A fresh session exists with a composer and a send button.
        self.find_by_name("+ New chat")
        self.find_by_name("Send")

    def test_02_backend_setup(self):
        # The mock backend was seeded on startup (OCAPI_E2E_MOCK_BACKEND)
        # and is the active backend; its model was detected through a real
        # GET /api/tags round-trip against the mock server.
        # The picker is a QToolButton; its AT-SPI role is exposed as
        # "toggle button" on some stacks, so match by accessible name
        # regardless of the role the platform reports.
        picker = self.wait_for("//*[@name='Model backend and model']")
        self.assertIn("Mock backend", picker.text)
        self.assertIn("mock-model", picker.text)

    def test_03_chat(self):
        # Type a message, send it and wait for the streamed reply.
        composer = self.find_by_name("Message input")
        composer.send_keys("Hello mock!")
        self.find_by_name("Send").click()

        self.wait_for("//*[contains(@name, '%s')]" % REPLY_TEXT, timeout=30)


    def test_04_mcp_tool_roundtrip(self):
        # The seeded mock MCP server connected on startup and offered its
        # echo tool (status bar: "MCP: 1 tool(s)"). A chat message with the
        # trigger phrase makes the mock backend request the tool; ocapi runs
        # it against the mock MCP server, feeds the result back and the mock
        # backend's final answer (quoting the tool result) streams in.
        self.wait_for("//label[starts-with(@name, 'MCP: 1 tool')]", timeout=30)

        composer = self.find_by_name("Message input")
        composer.send_keys("Please use the tool for me.")
        self.find_by_name("Send").click()

        self.wait_for("//*[contains(@name, '%s')]" % TOOL_REPLY_TEXT, timeout=60)

if __name__ == "__main__":
    unittest.main()
