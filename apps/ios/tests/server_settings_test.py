"""Regression checks for server settings commit points in the iOS client."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import uuid


class ServerSettingsTest(unittest.TestCase):
    def setUp(self):
        ios = Path(__file__).resolve().parents[1] / "CrossDeskMobile"
        self.settings = (ios / "ContentView.swift").read_text(encoding="utf-8").split(
            "private struct ServerSettingsView: View", 1
        )[1]
        self.model = (ios / "RemoteSessionModel.swift").read_text(encoding="utf-8")

    def test_save_button_commits_before_releasing_focus(self):
        self.assertRegex(
            self.settings,
            r'Button\(NSLocalizedString\("保存", comment: ""\)\)\s*\{\s*'
            r'validateAndApplyServerConfiguration\(\)\s*focusedServerField = nil',
        )

    def test_background_commits_and_foreground_applies_pending_server(self):
        self.assertRegex(
            self.settings,
            r'\.onValueChange\(of: scenePhase\)\s*\{ phase in\s*'
            r'if phase == \.background \{ validateAndApplyServerConfiguration\(\) \}',
        )
        self.assertIn("applyImmediately: scenePhase == .active", self.settings)
        self.assertRegex(
            self.model,
            r'func applicationDidBecomeActive\(\) \{[^}]*'
            r'if hasNetworkConsent, bridgeConfiguration != \(customServer \?\? \.official\) \{\s*configureBridge\(\)',
        )
        self.assertRegex(
            self.model,
            r'try SignalServerConfiguration\.saveCustom\(server\)\s*'
            r'\} catch \{\s*return false\s*\}\s*'
            r'customServer = server\s*if applyImmediately \{ configureBridge\(\) \}',
        )

    def test_storage_is_written_before_publishing_the_active_server(self):
        self.assertIn('write(to: destination, options: .atomic)', self.model)
        self.assertIn('guard session.setCustomServer(', self.settings)


@unittest.skipUnless(sys.platform == "darwin", "requires the Apple Swift runtime")
class SignalServerPersistenceTest(unittest.TestCase):
    def test_migrate_reopen_and_return_to_default(self):
        source = Path(__file__).resolve().parents[1] / "CrossDeskMobile/RemoteSessionModel.swift"
        code = source.read_text(encoding="utf-8").split("struct SignalServerConfiguration:", 1)[1]
        code = code.split("final class RemoteSessionModel:", 1)[0]
        with tempfile.TemporaryDirectory(prefix="crossdesk-signal-server-test-") as tmp:
            root = Path(tmp)
            settings = root / "SignalServerConfiguration.swift"
            settings.write_text("import Foundation\nstruct SignalServerConfiguration:" + code, encoding="utf-8")
            main = root / "main.swift"
            main.write_text(r'''
import Foundation

let file = URL(fileURLWithPath: CommandLine.arguments[1])
let suite = CommandLine.arguments[2]
let defaults = UserDefaults(suiteName: suite)!
let key = "crossdesk.mobile.custom-signal-server"
let server = SignalServerConfiguration(host: "test.example.org", signalPort: 9099)
switch CommandLine.arguments[3] {
case "migrate":
    defaults.set(["host": server.host, "port": server.signalPort], forKey: key)
    precondition(SignalServerConfiguration.loadCustom(file: file, defaults: defaults) == server)
    precondition(FileManager.default.fileExists(atPath: file.path))
    defaults.removePersistentDomain(forName: suite)
case "reopen":
    precondition(SignalServerConfiguration.loadCustom(file: file, defaults: defaults) == server)
case "default":
    try SignalServerConfiguration.saveCustom(nil, file: file, defaults: defaults)
case "reopen-default":
    defaults.set(["host": server.host, "port": server.signalPort], forKey: key)
    precondition(SignalServerConfiguration.loadCustom(file: file, defaults: defaults) == nil)
    defaults.removePersistentDomain(forName: suite)
default:
    fatalError("Unknown test case")
}
''', encoding="utf-8")
            binary = root / "persistence-test"
            subprocess.run(["xcrun", "swiftc", str(settings), str(main), "-o", str(binary)],
                           check=True)
            suite = f"crossdesk-test-{uuid.uuid4()}"
            for operation in ("migrate", "reopen", "default", "reopen-default"):
                subprocess.run([str(binary), str(root / "config" / "signal.json"), suite, operation],
                               check=True)


if __name__ == "__main__":
    unittest.main()
