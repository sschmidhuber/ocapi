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
# http://127.0.0.1:11434 (OCAPI_E2E_MOCK_PORT to override).
#
# Covers the most essential user flows:
#   1. the app starts with a chat, composer and send button
#   2. a backend can be configured and its connection tested
#   3. a message can be sent and the mock backend's reply streams back

import os
import unittest

from appium import webdriver
from appium.options.common.base import AppiumOptions
from appium.webdriver.common.appiumby import AppiumBy
from selenium.webdriver.support.ui import WebDriverWait

REPLY_TEXT = "Hello from the mock backend!"


class OcapiE2E(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        port = os.environ.get("OCAPI_E2E_MOCK_PORT", "11434")
        options = AppiumOptions()
        options.set_capability("app", os.environ["OCAPI_E2E_APP"])
        options.set_capability("appium:environ", {
            "OCAPI_E2E_MOCK_BACKEND": "1",
            "OCAPI_E2E_MOCK_PORT": port,
        })
        cls.driver = webdriver.Remote(command_executor="http://127.0.0.1:4723",
                                      options=options)
        cls.driver.implicitly_wait = 10

    @classmethod
    def tearDownClass(cls):
        cls.driver.quit()

    def find_by_name(self, name):
        return self.driver.find_element(by=AppiumBy.NAME, value=name)

    def find_reply(self):
        # Chat bubbles are list items whose accessible name is the message
        # text; match on a prefix so trailing/painted text does not matter.
        return self.driver.find_element(
            by=AppiumBy.XPATH,
            value="//*[contains(@name, '%s')]" % REPLY_TEXT)

    def test_01_start(self):
        # A fresh session exists with a composer and a send button.
        self.find_by_name("+ New chat")
        self.find_by_name("Send")

    def test_02_backend_setup(self):
        # Tools -> Model backends -> add a backend, point it at the mock
        # server and test the connection.
        self.find_by_name("Tools").click()
        self.find_by_name("Model backends…").click()

        self.find_by_name("Add").click()
        url_edit = self.find_by_name("Base URL")
        url_edit.clear()
        url_edit.send_keys("http://127.0.0.1:" +
                           os.environ.get("OCAPI_E2E_MOCK_PORT", "11434"))
        self.find_by_name("Test connection").click()

        wait = WebDriverWait(self.driver, 20)
        wait.until(lambda d: self.driver.find_element(
            by=AppiumBy.XPATH,
            value="//*[contains(@name, 'Connection successful')]").text)

        self.find_by_name("Close").click()

    def test_03_chat(self):
        # Type a message, send it and wait for the streamed reply.
        composer = self.find_by_name("Message input")
        composer.send_keys("Hello mock!")
        self.find_by_name("Send").click()

        wait = WebDriverWait(self.driver, 30)
        wait.until(lambda d: self.find_reply().text)


if __name__ == "__main__":
    unittest.main()
