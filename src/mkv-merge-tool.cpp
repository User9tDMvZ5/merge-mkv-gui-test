#include "mkv-merge-tool.hpp"
#include "string-utils.hpp"
#include "wx/log.h"
#include <subprocess.h>

namespace {
  // Convert filesystem paths to UTF-8. subprocess.h converts UTF-8 to UTF-16
  // and applies Windows argument quoting when creating the process.
  std::string pathToUtf8(const fs::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
  }

  std::optional<int> runSubprocess(std::vector<std::string> arguments) {
    std::vector<const char*> cmd(arguments.size() + 1, nullptr);
    std::transform(arguments.begin(), arguments.end(), cmd.begin(),
                   [](const auto& s) { return s.c_str(); });

    wxString joinedCmd;
    for (const auto& argument : arguments) {
      if (!joinedCmd.empty()) joinedCmd += " ";
      joinedCmd += "\"";
      joinedCmd += wxString::FromUTF8(argument.c_str());
      joinedCmd += "\"";
    }
    // Decode UTF-8 explicitly: narrow %s logging can use the active Windows
    // code page and corrupt Cyrillic characters.
    wxLogInfo("run mkv merge: %s", joinedCmd);

    auto subprocess = subprocess_s{};
    constexpr int subprocessOptions =
        subprocess_option_inherit_environment |
        subprocess_option_no_window |
        subprocess_option_enable_async |
        subprocess_option_combined_stdout_stderr;

    if (int result = subprocess_create(cmd.data(), subprocessOptions, &subprocess);
        result != 0) {
      wxLogError("Cannot start mkvmerge; subprocess_create error: %d", result);
      return std::nullopt;
    }

    // Read the combined stdout/stderr while mkvmerge is running. Draining the
    // pipe is important: otherwise a full pipe can block the child process.
    char buffer[4096];
    for (;;) {
      const unsigned bytesRead =
          subprocess_read_stdout(&subprocess, buffer, sizeof(buffer));
      if (bytesRead == 0) {
        break;
      }

      // The parent application is expected to be launched from a console.
      // Preserve mkvmerge's UTF-8 output exactly as emitted.
      if (std::fwrite(buffer, 1, bytesRead, stdout) != bytesRead) {
        wxLogWarning("Could not write all mkvmerge output to the parent console");
      }
      std::fflush(stdout);
    }

    int processReturn = -1;
    const int joinResult = subprocess_join(&subprocess, &processReturn);
    const int destroyResult = subprocess_destroy(&subprocess);

    if (joinResult != 0) {
      wxLogError("Could not wait for mkvmerge to finish; subprocess_join error: %d",
                 joinResult);
      return std::nullopt;
    }
    if (destroyResult != 0) {
      wxLogError("Could not release mkvmerge process resources; subprocess_destroy error: %d",
                 destroyResult);
      return std::nullopt;
    }

    std::printf("\n[mkvmerge] Process finished with exit code %d\n", processReturn);
    std::fflush(stdout);
    return processReturn;
  }
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
