#pragma once

#include <QString>

namespace creative_suite::hub {

// Per-user "start at login" integration for the Hub, isolated behind one adapter:
//   Windows: HKCU\...\Run value `CreativeSuiteHub`.
//   Linux:   XDG autostart entry `<config>/autostart/creative-suite-hub.desktop`.
//   macOS:   LaunchAgent `~/Library/LaunchAgents/creative-suite.hub.plist` (RunAtLoad).
// Only the current user's configuration is touched; no administrator rights are involved.
namespace startup_integration {

// XDG Desktop Entry content that launches `executablePath` at login. The path is quoted and escaped
// as the Desktop Entry specification requires for the Exec key.
QString desktopEntryContents(const QString& executablePath);

// LaunchAgent property list that launches `executablePath` at login (XML-escaped).
QString launchAgentContents(const QString& executablePath);

// Creates (enabled) or removes (disabled) `filePath`, creating parent folders and replacing existing
// content atomically. Removing a file that does not exist succeeds. On failure returns false and, when
// `error` is not null, stores the cause.
bool writeOrRemoveFile(const QString& filePath, const QString& contents, bool enabled, QString* error);

// File used by the current platform's file-based integration (Linux/macOS); empty on Windows.
QString integrationFilePath();

// True when the current platform has an implementation.
bool isSupported();

// Whether start-at-login is currently registered for this user.
bool isEnabled();

// Registers or removes start-at-login for `executablePath`.
bool setEnabled(bool enabled, const QString& executablePath, QString* error);

} // namespace startup_integration
} // namespace creative_suite::hub
