#pragma once
#include "pch.hpp"

struct ProcessMkvInput {
  fs::path              mkvsDirectory;
  std::vector<fs::path> mkvs;
  std::vector<fs::path> subs;
  std::vector<fs::path> audios;
  bool                  useOriginalAudio;
  wxString              audioTrackName;
};

struct MkvCombineTask {
  fs::path                mkvFile;
  fs::path                destination;
  std::optional<fs::path> subFile;
  std::optional<fs::path> audioFile;
  bool                    useOriginalAudio;
  wxString                audioTrackName;
};

using MkvCombineTasks = std::vector<MkvCombineTask>;


std::optional<MkvCombineTasks> makeMkvCombineTasks(ProcessMkvInput input);

// notifyRemaining: (uint32_t taskRemaining, uint32_t taskCount) -> void
struct MkvCombineSummary {
  uint32_t succeeded{0};
  uint32_t failed{0};
};

MkvCombineSummary runMkvCombine(const fs::path& mkvMergeExecutable, const MkvCombineTasks& tasks,
                                const std::function<void(uint32_t, uint32_t)>& notifyRemaining);
