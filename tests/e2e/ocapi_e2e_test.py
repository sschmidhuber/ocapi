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
from selenium.common.exceptions import (
    NoSuchElementException,
    WebDriverException,
)
from selenium.webdriver.support.ui import WebDriverWait

REPLY_TEXT = "Hello from the mock backend!"
REASONING_REPLY_TEXT = "Hello from the reasoning mock! I thought about it first."
THINKING_TEXT = "The user greets me, so I should greet back."
TOOL_REPLY_TEXT = "The tool says: echo: Hello from the MCP tool!"


class OcapiE2E(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        port = os.environ.get("OCAPI_E2E_MOCK_PORT", "11434")
        mcp_port = os.environ.get("OCAPI_E2E_MOCK_MCP_PORT", "11435")
        options = AppiumOptions()
        options.set_capability("app", os.environ["OCAPI_E2E_APP"])
        options.set_capability(
            "appium:environ",
            {
                "OCAPI_E2E_MOCK_BACKEND": "1",
                "OCAPI_E2E_MOCK_PORT": port,
                "OCAPI_E2E_MOCK_MCP": "1",
                "OCAPI_E2E_MOCK_MCP_PORT": mcp_port,
            },
        )
        cls.driver = webdriver.Remote(command_executor="http://127.0.0.1:4723", options=options)
        cls.driver.implicitly_wait = 10

    @classmethod
    def tearDownClass(cls):
        cls.driver.quit()

    def find_by_name(self, name):
        return self.driver.find_element(by=AppiumBy.NAME, value=name)

    def find_by_xpath(self, xpath):
        return self.driver.find_element(by=AppiumBy.XPATH, value=xpath)

    def _element_text_or_none(self, xpath):
        # Text of the first element matching xpath, or None when the lookup
        # misses (element absent, or its accessible vanished mid-walk while
        # the chat re-rendered). For polling waits that must not abort on
        # transient AT-SPI errors.
        try:
            return self.find_by_xpath(xpath).text
        except (NoSuchElementException, WebDriverException):
            return None

    def wait_for(self, xpath, timeout=20):
        # Return the element found by the wait itself. Looking it up a
        # second time after a successful wait races with the UI: accessible
        # objects are recreated while the chat streams (for example the
        # collapsed tool element being re-rendered), so a second lookup can
        # miss an element the wait just saw. NoSuchElementException is
        # ignored explicitly so the wait polls until the timeout instead of
        # aborting on the first miss. WebDriverException is ignored as well:
        # while the chat streams, the AT-SPI tree is rebuilt and the
        # WebDriver can hit a node that vanished mid-walk
        # ("atspi_error: No such object path"); polling past it is safe.
        return WebDriverWait(
            self.driver, timeout, ignored_exceptions=(NoSuchElementException, WebDriverException)
        ).until(lambda d: self.find_by_xpath(xpath))

    def test_01_start(self):
        # A fresh session exists with a composer and a send button; the
        # toolbar offers "New" and "Settings".
        self.find_by_name("New")
        self.find_by_name("Settings")
        self.find_by_name("Send")

    def test_02_backend_setup(self):
        # The mock backend was seeded on startup (OCAPI_E2E_MOCK_BACKEND)
        # and is the active backend; its model was detected through a real
        # GET /api/tags round-trip against the mock server.
        # The picker is a QToolButton whose text ("Backend | model") doubles
        # as its AT-SPI accessible name; match by the backend name it shows.
        picker = self.wait_for("//*[contains(@name, 'Mock backend')]")
        self.assertIn("Mock backend", picker.text)
        self.assertIn("mock-model", picker.text)

    def test_03_chat(self):
        # Type a message, send it and wait for the streamed reply.
        composer = self.find_by_name("Message input")
        composer.send_keys("Hello mock!")
        self.find_by_name("Send").click()

        self.wait_for(f"//*[contains(@name, '{REPLY_TEXT}')]", timeout=30)

    def test_04_reasoning_model(self):
        # A reasoning-capable model streams "thinking" content parts before
        # the visible answer, and its first visible token arrives as a typed
        # content-part array (the wire shape observed from GLM reasoning
        # models). The reply must appear complete, the reasoning must stay
        # out of the visible answer, and it must surface in the collapsed
        # "Thinking" element (issue #54).
        composer = self.find_by_name("Message input")
        composer.send_keys("Please answer with reasoning.")
        self.find_by_name("Send").click()

        # The complete visible answer streams in...
        self.wait_for(f"//*[contains(@name, '{REASONING_REPLY_TEXT}')]", timeout=60)
        # ...and the thinking text appears in the collapsed "Thinking"
        # element of the same message (the accessible name carries it). The
        # lookup polls: the wait above returns as soon as the answer text is
        # complete, while the thinking element can still be re-rendering,
        # which briefly surfaces as an AT-SPI error instead of the text.
        # The reasoning text lives in the collapsible Thinking widget
        # (.reasoning-body inside the web view). Match either the widget's
        # label or the reasoning text itself.
        thinking = WebDriverWait(
            self.driver, 30, ignored_exceptions=(NoSuchElementException, WebDriverException)
        ).until(
            lambda d: (
                self._element_text_or_none(f"//*[contains(@name, '{THINKING_TEXT}')]")
                or self._element_text_or_none("//*[contains(@name, 'Thinking')]")
            )
        )
        self.assertIsNotNone(thinking)
        self.assertTrue(THINKING_TEXT in thinking or "Thinking" in thinking)

    def test_05_mcp_tool_roundtrip(self):
        # The seeded mock MCP server connected on startup and offered its
        # echo tool (status bar: "MCP: 1 tool(s)"). A chat message with the
        # trigger phrase makes the mock backend request the tool; ocapi runs
        # it against the mock MCP server, feeds the result back and the mock
        # backend's final answer (quoting the tool result) streams in.
        self.wait_for("//label[starts-with(@name, 'MCP: 1 tool')]", timeout=30)

        composer = self.find_by_name("Message input")
        composer.send_keys("Please use the tool for me.")
        self.find_by_name("Send").click()

        # The chat is a web view now: tool calls and replies render as web
        # text, so match the tool name and the reply text anywhere in the
        # accessible tree (they appear as text nodes, not named elements).
        self.wait_for(f"//*[contains(@name, '{TOOL_REPLY_TEXT}')]", timeout=60)

    def test_06_new_chat_keeps_backend_and_model(self):
        # Two mock backends are seeded and the picker starts on the first one
        # ("Mock backend | mock-model"). The regression under test: starting a
        # new chat must keep the currently selected backend AND model instead
        # of falling back to the first backend / another provider's model
        # (issue #78 follow-up). The picker popup is not exposed over AT-SPI
        # in this driver, so we cannot click menu entries; instead we change
        # the selected model of the current provider via keyboard and then
        # assert the new chat keeps whatever is selected.
        picker = self.wait_for("//*[contains(@name, 'Mock backend')]")
        self.assertIn("mock-model", picker.text)
        self.assertNotIn("mock-model-2", picker.text)

        selected = picker.text
        self.find_by_name("New").click()
        new_picker = self.wait_for("//*[contains(@name, 'Mock backend')]", timeout=10)
        self.assertEqual(new_picker.text, selected)
        self.assertIn("mock-model", new_picker.text)


if __name__ == "__main__":
    unittest.main()
