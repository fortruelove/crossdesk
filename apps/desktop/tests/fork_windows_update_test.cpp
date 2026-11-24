#include "version_checker.h"
#include "updater/update_core.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
template <typename Operation>
void ExpectRejected(const char* reason, Operation operation) {
  try {
    operation();
  } catch (const std::exception&) {
    return;
  }
  throw std::runtime_error(reason);
}
}  // namespace

int main(int argc, char** argv) {
  using crossdesk::ParseForkRelease;
  using namespace crossdesk::updater;
  if (argc < 6 || argc > 7) {
    std::cerr << "Usage: fork_windows_update_test <version> <public-key-hex> "
                 "<signed-notes> <installer> <scratch-directory> [--download]\n";
    return 2;
  }
  try {
    const std::string version = argv[1];
    const std::string key = argv[2];
    const auto release = ParseForkRelease(nlohmann::json{
        {"tag_name", "v" + version}, {"body", ReadText(argv[3])}}.dump());
    if (!release || release->update_envelope.empty())
      throw std::runtime_error("GitHub release did not provide the update envelope");
    const auto& envelope = release->update_envelope;
    const auto artifact = VerifyEnvelope(envelope, key, "windows-x64", "0.0.0");
    if (artifact.version != version)
      throw std::runtime_error("Verified unexpected version");
    VerifyPackage(argv[4], artifact);
    ExpectRejected("accepted a same-version update", [&] {
      VerifyEnvelope(envelope, key, "windows-x64", version);
    });
    ExpectRejected("accepted an update in the portable edition", [&] {
      VerifyEnvelope(envelope, key, "windows-x64-portable", "0.0.0");
    });
    auto modified = nlohmann::json::parse(envelope);
    auto& signature = modified.at("signature").get_ref<std::string&>();
    signature[0] = signature[0] == '0' ? '1' : '0';
    ExpectRejected("accepted a tampered signature", [&] {
      VerifyEnvelope(modified.dump(), key, "windows-x64", "0.0.0");
    });
    const auto scratch = fs::path(argv[5]);
    const auto staged = scratch / "staged.exe";
    CopyPackage(argv[4], staged, artifact);
    {
      std::fstream file(staged, std::ios::binary | std::ios::in | std::ios::out);
      const auto first_byte = file.get();
      file.seekp(0);
      file.put(static_cast<char>(first_byte ^ 1));
      if (!file) throw std::runtime_error("could not tamper with the staging fixture");
    }
    ExpectRejected("accepted a modified installer with the original size", [&] {
      VerifyPackage(staged, artifact);
    });
    if (argc == 7 && std::string_view(argv[6]) == "--download") {
      std::atomic<int> progress{0};
      const std::atomic<bool> cancel{false};
      DownloadPackage(artifact, scratch / "download.exe", progress, cancel);
      if (progress != 100) throw std::runtime_error("download did not finish");
      std::cout << "Verified live GitHub installer download\n";
    }
    std::cout << "Verified fork release parsing, signature, version and package integrity\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
