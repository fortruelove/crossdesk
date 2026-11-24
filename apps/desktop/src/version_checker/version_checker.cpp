/*
 * @Author: DI JUNKUN
 * @Date: 2025-11-11
 * Copyright (c) 2025 by DI JUNKUN, All Rights Reserved.
 */

#include "version_checker.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "rd_log.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace crossdesk {

namespace {

constexpr std::string_view kWindowsUpdateMarker = "<!-- crossdesk-windows-update: ";

std::string TakeWindowsUpdateEnvelope(std::string& notes, std::string_view version) {
  const auto start = notes.rfind(kWindowsUpdateMarker);
  const auto end = notes.find_last_not_of(" \t\r\n");
  if (start == std::string::npos || (start != 0 && notes[start - 1] != '\n') ||
      end < start + kWindowsUpdateMarker.size() + 3 ||
      notes.compare(end - 3, 4, " -->") != 0) return {};

  const auto begin = start + kWindowsUpdateMarker.size();
  auto envelope = notes.substr(begin, end - 3 - begin);
  const auto last_note = start == 0 ? std::string::npos :
      notes.find_last_not_of(" \t\r\n", start - 1);
  notes.resize(last_note == std::string::npos ? 0 : last_note + 1);
  if (envelope.size() > 65536 || envelope.find_first_of("\r\n") != std::string::npos)
    return {};
  try {
    const auto wrapper = nlohmann::json::parse(envelope);
    const auto payload = nlohmann::json::parse(wrapper.at("payload").get<std::string>());
    if (payload.at("version").get_ref<const std::string&>() == version) return envelope;
  } catch (const nlohmann::json::exception&) {}
  return {};
}

void LogHttpError(const httplib::Result& result) {
  LOG_WARN("Failed to check GitHub releases: error={}, message={}",
           static_cast<int>(result.error()), httplib::to_string(result.error()));
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  LOG_WARN("GitHub release TLS error={}, OpenSSL error={}", result.ssl_error(),
           result.ssl_openssl_error());
#endif
}

#if defined(CPPHTTPLIB_OPENSSL_SUPPORT) && defined(__linux__)
bool PathExists(const std::string& path) {
  if (path.empty()) {
    return false;
  }

  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

std::string GetEnvPathIfExists(const char* key) {
  const char* value = std::getenv(key);
  if (!value) {
    return "";
  }

  const std::string path = value;
  return PathExists(path) ? path : "";
}

std::string FindFirstExistingPath(
    const std::vector<std::string>& candidates) {
  for (const auto& candidate : candidates) {
    if (PathExists(candidate)) {
      return candidate;
    }
  }
  return "";
}

void ConfigureLinuxCaCerts(httplib::Client* cli) {
  const std::string ca_file = [&]() {
    const std::string env_path = GetEnvPathIfExists("SSL_CERT_FILE");
    if (!env_path.empty()) {
      return env_path;
    }

    return FindFirstExistingPath({
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/cert.pem",
        "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
    });
  }();

  const std::string ca_dir = [&]() {
    const std::string env_path = GetEnvPathIfExists("SSL_CERT_DIR");
    if (!env_path.empty()) {
      return env_path;
    }

    return FindFirstExistingPath({
        "/etc/ssl/certs",
        "/etc/pki/tls/certs",
        "/etc/openssl/certs",
    });
  }();

  if (ca_file.empty() && ca_dir.empty()) {
    LOG_WARN("No Linux CA bundle found for GitHub request; relying on OpenSSL defaults");
    return;
  }

  cli->set_ca_cert_path(ca_file, ca_dir);
  LOG_DEBUG("Configured GitHub TLS CA bundle: file={}, dir={}",
            ca_file.empty() ? "<none>" : ca_file,
            ca_dir.empty() ? "<none>" : ca_dir);
}
#endif

}  // namespace

std::optional<VersionInfo> ParseForkRelease(std::string_view response) {
  try {
    const auto release = nlohmann::json::parse(response);
    if (!release.is_object() || release.value("draft", false) ||
        release.value("prerelease", false)) return std::nullopt;
    const auto tag = release.value("tag_name", std::string{});
    if (tag.empty() || tag.front() != 'v') return std::nullopt;
    const auto numeric = std::string_view(tag).substr(1);
    const auto dots = std::count(numeric.begin(), numeric.end(), '.');
    if (dots < 2 || dots > 3 || numeric.find_first_not_of("0123456789.") != std::string_view::npos)
      return std::nullopt;
    auto info = ParseVersionInfo({{"version", tag},
                                  {"releaseName", release.value("name", std::string{})},
                                  {"releaseNotes", release.value("body", std::string{})}});
    if (info) {
      info->download_url = std::string(kForkReleasesUrl) + "/tag/" + tag;
      info->update_envelope = TakeWindowsUpdateEnvelope(info->release_notes, info->version);
    }
    return info;
  } catch (const nlohmann::json::exception&) {
    return std::nullopt;
  }
}

std::optional<VersionInfo> CheckUpdate() {
  httplib::Client cli("https://api.github.com");

  cli.set_connection_timeout(5);
  cli.set_read_timeout(5);
  cli.set_write_timeout(5);
  cli.set_max_timeout(15000);
  cli.set_default_headers({{"User-Agent", "CrossDesk"},
                           {"Accept", "application/vnd.github+json"}});

#if defined(CPPHTTPLIB_OPENSSL_SUPPORT) && defined(__linux__)
  ConfigureLinuxCaCerts(&cli);
#endif

  auto res = cli.Get("/repos/fortruelove/crossdesk/releases/latest");
  if (!res) {
    LogHttpError(res);
    return std::nullopt;
  }
  if (res->status != 200) {
    LOG_WARN("Failed to check GitHub releases: HTTP status={}", res->status);
    return std::nullopt;
  }
  auto info = ParseForkRelease(res->body);
  if (info) LOG_INFO("Latest fork release: {}", info->version);
  else LOG_WARN("The latest GitHub release does not have a valid desktop version");
  return info;
}

}  // namespace crossdesk
