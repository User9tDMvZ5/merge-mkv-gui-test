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

    std::string joinedCmd = "";
    for (const auto& arg : cmd) {
      if (arg != nullptr) {
        joinedCmd = joinedCmd + " " + arg;
      }
    }
    wxLogInfo("run mkv merge: %s", joinedCmd.c_str());

    auto subprocess = subprocess_s{};
    if (int result = subprocess_create(cmd.data(), 0, &subprocess); result != 0) {
      return std::nullopt;
    }

    //  FILE* processStdOut = subprocess_stdout( &subprocess );
    //  FILE* processStdErr = subprocess_stderr( &subprocess );

    int processReturn;
    if (int result = subprocess_join(&subprocess, &processReturn); result != 0) {
      return std::nullopt;
    }
    if (int result = subprocess_destroy(&subprocess); result != 0) {
      return std::nullopt;
    }
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
