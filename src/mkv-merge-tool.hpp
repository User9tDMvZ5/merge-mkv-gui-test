#pragma once
#include "pch.hpp"


struct RunMkvMergeOptions {
  fs::path                mkvMergeExecutable;
  fs::path                outputPath;
  fs::path                mkvPath;
  std::optional<fs::path> subtitlePath;
  std::optional<fs::path> audioPath;
  bool                    useOriginalAudio;
  wxString                audioTrackName;
};

struct MkvMergeResult {
  std::optional<const char*> error;
};

MkvMergeResult runMkvMergeTool(const RunMkvMergeOptions& options);
