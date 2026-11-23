"""Check the fork's endpoint defaults and the transport's retry target."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]
DESKTOP = ROOT / "apps/desktop/src"
IOS = ROOT / "apps/ios/CrossDeskMobile"
RTC = ROOT / "deps/submodules/minirtc/src"


class DefaultSignalingTest(unittest.TestCase):
    def test_desktop_uses_the_fork_default(self):
        settings = (DESKTOP / "config_center/config_center.h").read_text(encoding="utf-8")
        self.assertIn(
            '#if defined(_WIN32) || (defined(__linux__) && !defined(__OHOS__))\n'
            '  std::string signal_server_host_default_ = "124.220.68.117";',
            settings,
        )
        self.assertRegex(settings, r"server_port_default_\s*=\s*9099")
        runtime = (DESKTOP / "gui/runtime/gui_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("signal_server_ip = config_center_->GetDefaultServerHost()", runtime)
        self.assertIn("signal_server_port = config_center_->GetDefaultSignalServerPort()", runtime)
        self.assertNotIn("api.crossdesk.cn", (DESKTOP / "gui/runtime/runtime_state.h").read_text(encoding="utf-8"))

    def test_ios_uses_the_fork_default(self):
        model = (IOS / "RemoteSessionModel.swift").read_text(encoding="utf-8")
        self.assertIn('static let defaultServer = Self(host: "124.220.68.117", signalPort: 9099)', model)
        self.assertIn("let configuration = customServer ?? .defaultServer", model)
        self.assertNotIn("api.crossdesk.cn", model)

    def test_reconnect_uses_the_existing_signaling_address(self):
        websocket = (RTC / "ws/ws_client.cpp").read_text(encoding="utf-8")
        start = websocket.index("int WsClient::ReConnect()")
        end = websocket.index("void WsClient::AsyncReConnect()", start)
        self.assertIn("return Connect(uri_);", websocket[start:end])


if __name__ == "__main__":
    unittest.main()
