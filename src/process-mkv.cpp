#include "process-mkv.hpp"
#include "string-utils.hpp"
#include "mkv-merge-tool.hpp"
#include <cctype>


#define mErrorIf(cond, message)                                             \
  if (cond) {                                                               \
    wxMessageBox(message, "error process", wxICON_ERROR | wxOK | wxCENTRE); \
    return std::nullopt;                                                    \
  }


namespace {
  using PathDiff = std::map<std::u8string, fs::path>;


  std::optional<PathDiff> diffPath(const std::vector<fs::path>& paths) {
    std::vector<std::u8string> pathStrings(paths.size());
    std::transform(paths.begin(), paths.end(), pathStrings.begin(),
                   [](auto& p) { return p.u8string(); });

    auto commonPrefix = commonPathPrefix(pathStrings);
    auto commonSuffix = commonPathSuffix(pathStrings);

    PathDiff result;

    for (size_t i = 0; i < pathStrings.size(); ++i) {
      auto& pathStr    = pathStrings[i];
      auto  differPart = pathStr.substr(
          commonPrefix.length(), pathStr.length() - commonPrefix.length() - commonSuffix.length());

      if (result.contains(differPart)) {
        return std::nullopt;
      }

      result[differPart] = paths[i];
    }

    return result;
  }


  // Match media by a leading episode number: "12) title.mkv", "12) audio.ac3".
  std::optional<std::string> numberedPrefix(const fs::path& path) {
    auto name = path.stem().string();
    size_t pos = 0;
    while (pos < name.size() && std::isdigit(static_cast<unsigned char>(name[pos]))) ++pos;
    if (pos == 0) return std::nullopt;
    const auto digitsEnd = pos;
    while (pos < name.size() && std::isspace(static_cast<unsigned char>(name[pos]))) ++pos;
    if (pos >= name.size() || name[pos] != ')') return std::nullopt;
    auto number = name.substr(0, digitsEnd);
    const auto firstNonZero = number.find_first_not_of('0');
    return firstNonZero == std::string::npos ? std::optional<std::string>{"0"}
                                             : std::optional<std::string>{number.substr(firstNonZero)};
  }

  using NumberedPaths = std::map<std::string, fs::path>;

  std::optional<NumberedPaths> makeNumberedPaths(const std::vector<fs::path>& paths) {
    NumberedPaths result;
    for (const auto& path : paths) {
      auto number = numberedPrefix(path);
      if (!number || result.contains(*number)) return std::nullopt;
      result.emplace(*number, path);
    }
    return result;
  }


  bool hasSmthInCommon(const PathDiff& a, const PathDiff& b) {
    return std::any_of(a.begin(), a.end(), [&](auto& p) { return b.contains(p.first); });
  }


  void logTasks(const MkvCombineTasks& tasks) {
    for (const auto& task : tasks) {
      wxLogInfo("mkv combine task: %s + %s + %s", toWxString(task.mkvFile.filename()),
                task.subFile ? toWxString(task.subFile->filename()) : wxString{"<none>"},
                task.audioFile ? toWxString(task.audioFile->filename()) : wxString{"<none>"});
    }
  }


  std::optional<fs::path> makeMergedDirectory(const fs::path& parent) {
    for (uint32_t i = 0; i < 100; ++i) {
      char folderName[64] = {0};
      sprintf(folderName, "merged-%02u", i);
      auto path = parent / folderName;

      if (std::error_code ec; !fs::exists(path, ec) && !ec) {
        if (fs::create_directory(path, ec); !ec) {
          return path;
        }
      }
    }

    return std::nullopt;
  }
}  // namespace


std::optional<MkvCombineTasks> makeMkvCombineTasks(ProcessMkvInput input) {
  wxLogInfo("processMkv: mkv directory: %s", toWxString(input.mkvsDirectory));
  wxLogInfo("processMkv: mkv files count: %d", (int)input.mkvs.size());
  wxLogInfo("processMkv: sub files count: %d", (int)input.subs.size());
  wxLogInfo("processMkv: audio files count: %d", (int)input.audios.size());

  mErrorIf(input.mkvsDirectory.empty() || input.mkvs.empty(), "no mkv directory selected");
  mErrorIf(input.subs.empty() && input.audios.empty(), "nothing to merge");

  auto mkvDiffs = diffPath(input.mkvs);
  mErrorIf(!mkvDiffs, "can't determine different parts in mkv files");

  auto subDiffs = std::optional<PathDiff>{};
  if (!input.subs.empty()) {
    subDiffs = diffPath(input.subs);
    mErrorIf(!subDiffs, "can't determine different parts in subtitle files");
  }

  auto audioNumbers = std::optional<NumberedPaths>{};
  if (!input.audios.empty()) {
    auto mkvNumbers = makeNumberedPaths(input.mkvs);
    mErrorIf(!mkvNumbers, "MKV filenames must start with a number followed by ')', e.g. 1) video.mkv");
    audioNumbers = makeNumberedPaths(input.audios);
    mErrorIf(!audioNumbers, "Audio filenames must start with a number followed by ')', e.g. 1) audio.ac3");
    for (const auto& [number, mkvPath] : *mkvNumbers) {
      mErrorIf(!audioNumbers->contains(number),
               (std::string{"No matching audio file for: "} + mkvPath.filename().string()).c_str());
    }
  }

  auto destination = makeMergedDirectory(input.mkvsDirectory);
  mErrorIf(!destination, "can't create merged folder");

  auto tasks = MkvCombineTasks{};
  for (const auto& [mkvDiff, mkvPath] : *mkvDiffs) {
    auto task = MkvCombineTask{
        .mkvFile = mkvPath,
        .destination = *destination / mkvPath.filename(),
        .useOriginalAudio = input.useOriginalAudio,
        .audioTrackName   = input.audioTrackName,
    };

    if (subDiffs) {
      if (auto it = subDiffs->find(mkvDiff); it != subDiffs->end()) {
        task.subFile = it->second;
      }
    }

    if (audioNumbers) {
      auto number = numberedPrefix(mkvPath);
      if (number) task.audioFile = audioNumbers->at(*number);
    }
    tasks.emplace_back(std::move(task));
  }

  logTasks(tasks);
  return tasks;
}


MkvCombineSummary runMkvCombine(const fs::path& mkvMergeExecutable, const MkvCombineTasks& tasks,
                                const std::function<void(uint32_t, uint32_t)>& notifyRemaining) {
  wxLogInfo("runMkvCombine(%d);", (int)tasks.size());

  MkvCombineSummary summary;
  const auto taskCount = static_cast<uint32_t>(tasks.size());

  // Process files one by one. This avoids launching several heavy mkvmerge
  // instances at once and makes progress correspond to completed files.
  for (size_t i = 0; i < tasks.size(); ++i) {
    const auto& task = tasks[i];
    auto opts = RunMkvMergeOptions{
        .mkvMergeExecutable = mkvMergeExecutable,
        .outputPath         = task.destination,
        .mkvPath            = task.mkvFile,
        .subtitlePath       = task.subFile,
        .audioPath          = task.audioFile,
        .useOriginalAudio   = task.useOriginalAudio,
        .audioTrackName     = task.audioTrackName,
    };

    wxLogInfo("Starting merge task %u/%u: %s",
              static_cast<unsigned>(i + 1), taskCount,
              toWxString(task.mkvFile.filename()));

    const auto result = runMkvMergeTool(opts);
    if (result.error) {
      ++summary.failed;
      wxLogError("Task %u/%u failed: %s",
                 static_cast<unsigned>(i + 1), taskCount, *result.error);
    } else {
      ++summary.succeeded;
      wxLogInfo("Task %u/%u completed successfully",
                static_cast<unsigned>(i + 1), taskCount);
    }

    // taskRemaining counts tasks not yet started; completed count is therefore
    // accurate even if a task failed (failures do not stop the queue).
    notifyRemaining(taskCount - static_cast<uint32_t>(i + 1), taskCount);
  }

  return summary;
}
