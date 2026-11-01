#include "mkv-merge-tool.hpp"
#include "string-utils.hpp"
#include "wx/log.h"
#include <subprocess.h>

#ifdef _WIN32
  #define NOMINMAX
  #include <windows.h>
#else
  #include <subprocess.h>
#endif

namespace {
  // Convert filesystem paths to UTF-8 for command arguments.
  std::string pathToUtf8(const fs::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
  }

#ifdef _WIN32
  std::optional<std::wstring> utf8ToWide(const std::string& value) {
    if (value.empty()) {
      return std::wstring{};
    }
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
      return std::nullopt;
    }
    std::wstring result(static_cast<size_t>(length), L'\\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(),
                            length) != length) {
      return std::nullopt;
    }
    return result;
  }

  // Apply the Windows command-line quoting rules for one argv element.
  std::wstring quoteWindowsArgument(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : value) {
      if (character == L'\\\\') {
        ++backslashes;
      } else if (character == L'\"') {
        result.append(backslashes * 2 + 1, L'\\\\');
        result += L'\"';
        backslashes = 0;
      } else {
        result.append(backslashes, L'\\\\');
        result += character;
        backslashes = 0;
      }
    }
    result.append(backslashes * 2, L'\\\\');
    result += L'\"';
    return result;
  }

  std::optional<int> runSubprocess(const std::vector<std::string>& arguments) {
    std::vector<std::wstring> wideArguments;
    wideArguments.reserve(arguments.size());
    for (const auto& argument : arguments) {
      auto wide = utf8ToWide(argument);
      if (!wide) {
        wxLogError("Cannot convert an mkvmerge argument from UTF-8 to UTF-16");
        return std::nullopt;
      }
      wideArguments.emplace_back(std::move(*wide));
    }

    std::wstring commandLine;
    for (const auto& argument : wideArguments) {
      if (!commandLine.empty()) {
        commandLine += L' ';
      }
      commandLine += quoteWindowsArgument(argument);
    }

    wxString joinedCmd;
    for (const auto& argument : arguments) {
      if (!joinedCmd.empty()) joinedCmd += " ";
      joinedCmd += "\"";
      joinedCmd += wxString::FromUTF8(argument.c_str());
      joinedCmd += "\"";
    }
    wxLogInfo("run mkv merge in a separate console: %s", joinedCmd);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    // CREATE_NEW_CONSOLE gives mkvmerge its own visible console window, with
    // stdout/stderr connected to that console so --gui-mode progress is shown.
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\\0');
    if (!CreateProcessW(wideArguments.front().c_str(), mutableCommandLine.data(),
                        nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr,
                        nullptr, &startupInfo, &processInfo)) {
      const DWORD error = GetLastError();
      wxLogError("Cannot start mkvmerge in a separate console; Windows error: %lu",
                 static_cast<unsigned long>(error));
      return std::nullopt;
    }

    CloseHandle(processInfo.hThread);
    const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, INFINITE);
    if (waitResult != WAIT_OBJECT_0) {
      const DWORD error = GetLastError();
      CloseHandle(processInfo.hProcess);
      wxLogError("Error waiting for mkvmerge; Windows error: %lu",
                 static_cast<unsigned long>(error));
      return std::nullopt;
    }

    DWORD exitCode = 1;
    const BOOL gotExitCode = GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hProcess);
    if (!gotExitCode) {
      wxLogError("Cannot read mkvmerge exit code; Windows error: %lu",
                 static_cast<unsigned long>(GetLastError()));
      return std::nullopt;
    }
    return static_cast<int>(exitCode);
  }
#else
  std::optional<int> runSubprocess(const std::vector<std::string>& arguments) {
    std::vector<const char*> cmd(arguments.size() + 1, nullptr);
    std::transform(arguments.begin(), arguments.end(), cmd.begin(),
                   [](const auto& value) { return value.c_str(); });

    auto subprocess = subprocess_s{};
    constexpr int subprocessOptions =
        subprocess_option_inherit_environment |
        subprocess_option_enable_async |
        subprocess_option_combined_stdout_stderr;
    if (int result = subprocess_create(cmd.data(), subprocessOptions, &subprocess);
        result != 0) {
      wxLogError("Cannot start mkvmerge; subprocess_create error: %d", result);
      return std::nullopt;
    }

    char buffer[4096];
    for (;;) {
      const unsigned bytesRead =
          subprocess_read_stdout(&subprocess, buffer, sizeof(buffer));
      if (bytesRead == 0) {
        break;
      }
      std::fwrite(buffer, 1, bytesRead, stdout);
      std::fflush(stdout);
    }

    int processReturn = -1;
    const int joinResult = subprocess_join(&subprocess, &processReturn);
    const int destroyResult = subprocess_destroy(&subprocess);
    if (joinResult != 0 || destroyResult != 0) {
      return std::nullopt;
    }
    return processReturn;
  }
#endif
}  // namespace

MkvMergeResult runMkvMergeTool(const RunMkvMergeOptions& options) {
  if (!options.subtitlePath && !options.audioPath) {
    return MkvMergeResult{.error = "nothing to merge"};
  }

  const auto& mkvMergePath = options.mkvMergeExecutable;
  if (!fs::exists(mkvMergePath)) {
    return MkvMergeResult{.error = "mkv toolnix path is invalid"};
  }

  auto arguments = std::vector<std::string>{
      pathToUtf8(mkvMergePath),
      "--gui-mode",
      "-o",
      pathToUtf8(options.outputPath),
  };
  if (not options.useOriginalAudio and options.audioPath) {
    arguments.emplace_back("-A");
  }
  arguments.emplace_back(pathToUtf8(options.mkvPath));
  if (options.subtitlePath) {
    arguments.emplace_back(pathToUtf8(*options.subtitlePath));
  }
  if (options.audioPath) {
    if (options.useOriginalAudio) {
      arguments.emplace_back("--default-language");
      arguments.emplace_back("rus");
    }
    if (!options.audioTrackName.empty()) {
      const auto trackNameUtf8 = options.audioTrackName.ToUTF8();
      if (trackNameUtf8.data() != nullptr) {
        arguments.emplace_back("--track-name");
        arguments.emplace_back("0:" + std::string(trackNameUtf8.data()));
      }
    }
    arguments.emplace_back(pathToUtf8(*options.audioPath));
  }

  auto status = runSubprocess(std::move(arguments));
  if (!status) {
    return MkvMergeResult{.error = "error running mkvmerge"};
  }
  if (*status != 0) {
    return MkvMergeResult{.error = "bad mkvmerge exit code"};
  }
  return MkvMergeResult{.error = std::nullopt};
}
