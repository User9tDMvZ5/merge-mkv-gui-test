#include "main-frame.hpp"
#include "pch.hpp"
#include "process-mkv.hpp"
#include "wx/msw/checkbox.h"
#include "wx/sizer.h"
#include "wx/config.h"
#include "wx/filedlg.h"
#include "wx/stdpaths.h"


namespace {
  class ButtonDisableScope {
    wxButton* button_;

   public:
    explicit ButtonDisableScope(wxButton* button) : button_{button} { button_->Disable(); }

    ~ButtonDisableScope() { button_->Enable(true); }
  };

  wxDEFINE_EVENT(wxEVT_MKV_PROCESS_STATUS_UPDATE, wxThreadEvent);

}  // namespace


MainFrame::MainFrame() : wxFrame{nullptr, wxID_ANY, "merge mkv"} {
  // MENU
  {
    auto* menuFile = new wxMenu{};
    menuFile->Append(wxID_EXIT);

    auto* menuSettings = new wxMenu{};
    menuSettings->Append(ID_SelectMkvMerge, "Select MKVToolNix executable...");

    auto* menuHelp = new wxMenu{};
    menuHelp->Append(wxID_ABOUT);

    auto* menuBar = new wxMenuBar{};
    menuBar->Append(menuFile, "&File");
    menuBar->Append(menuSettings, "&Settings");
    menuBar->Append(menuHelp, "&Help");
    SetMenuBar(menuBar);

    Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
    Bind(wxEVT_MENU, &MainFrame::OnSelectMkvMerge, this, ID_SelectMkvMerge);
    Bind(wxEVT_MENU, &MainFrame::OnExit, this, wxID_EXIT);
  }

  // STATUS BAR
  {
    CreateStatusBar();
    SetStatusText("...");
  }

  // LAYOUT
  {
    mkvFolderInput   = new FolderInput{this, "mkv dir:", ".mkv", FolderInputFlags::AskExtension};
    subsFolderInput  = new FolderInput{this, "subs dir:", ".ass", FolderInputFlags::AskExtension};
    audioFolderInput = new FolderInput{this, "audio dir:", ".mka",
                                      FolderInputFlags::AskExtension | FolderInputFlags::AudioExtensions};
    SetDropTarget(new DropBox{{mkvFolderInput, subsFolderInput, audioFolderInput}});

    constexpr int rows = 1;
    constexpr int cols = 3;
    const auto    gap  = wxSize{20, 0};

    auto* grid = new wxGridSizer{rows, cols, gap};
    grid->Add(mkvFolderInput, 1, wxEXPAND);
    grid->Add(subsFolderInput, 1, wxEXPAND);
    grid->Add(audioFolderInput, 1, wxEXPAND);

    processButton_ = new wxButton{this, ID_Process, wxT("process")};
    Bind(wxEVT_BUTTON, &MainFrame::OnProcess, this, ID_Process);

    wxBoxSizer* processThing = new wxBoxSizer(wxHORIZONTAL);
    useOriginalAudio_        = new wxCheckBox(this, ID_UseOriginalAudio, wxT("Use original audio"));
    processThing->Add(useOriginalAudio_, 0, wxCENTRE);
    processThing->Add(processButton_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 0);

    auto* topBox = new wxBoxSizer{wxVERTICAL};
    topBox->Add(grid, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
    topBox->Add(processThing, 0, wxGROW | wxALL, 15);
    SetSizer(topBox);

    SetMinSize(wxSize{1400, 800});
#ifdef _WIN32
    SetBackgroundColour(wxColour{"#f8f8f8ff"});
#endif
  }

  Connect(wxEVT_MKV_PROCESS_STATUS_UPDATE, wxThreadEventHandler(MainFrame::OnProcessStatusUpdate));

  LoadMkvMergePath();
  wxLogInfo("main frame created");
}


void MainFrame::OnExit(wxCommandEvent&) {
  constexpr bool force = true;
  Close(force);
}


void MainFrame::OnAbout(wxCommandEvent&) {
  auto aboutInfo = wxAboutDialogInfo{};
  aboutInfo.SetName("merge-mkv-gui");
  aboutInfo.SetVersion("1.0.0");
  aboutInfo.SetDescription(
      _("Simple app to merge shitty mkv files with subtitles and audio tracks"));
  aboutInfo.SetCopyright("Chuprakov Vadim (C) 2023");
  aboutInfo.SetWebSite("https://vy.ru.net");
  aboutInfo.AddDeveloper("Vadim Chuprakov");
  wxAboutBox(aboutInfo);
}


void MainFrame::OnProcess(wxCommandEvent&) {
  if (updateInProgress_) {
    return;
  }
  updateInProgress_ = true;
  processButton_->Disable();
  bool useOriginalAudio = useOriginalAudio_->GetValue();

  auto tasks = makeMkvCombineTasks(ProcessMkvInput{
      .mkvsDirectory    = mkvFolderInput->getBaseDirectory(),
      .mkvs             = std::move(mkvFolderInput->getFilteredFileList()),
      .subs             = std::move(subsFolderInput->getFilteredFileList()),
      .audios           = std::move(audioFolderInput->getFilteredFileList()),
      .useOriginalAudio = useOriginalAudio,
  });

  if (!tasks) {
    updateInProgress_ = false;
    processButton_->Enable(true);
    return;
  }

  if (mkvMergeExecutable_.empty() || !fs::exists(mkvMergeExecutable_)) {
    wxMessageBox("MKVToolNix was not found. Use Settings > Select MKVToolNix executable.",
                 "MKVToolNix not found", wxICON_ERROR | wxOK | wxCENTRE);
    updateInProgress_ = false;
    processButton_->Enable(true);
    return;
  }

  auto  mkvMergeExecutable = mkvMergeExecutable_;
  auto* instance = this;

  auto action = [instance, mkvMergeExecutable, tasks]() {
    auto summary = runMkvCombine(mkvMergeExecutable, *tasks, [instance](uint32_t taskRemaining, uint32_t taskCount) {
      auto  tasksDone = taskCount - taskRemaining;
      auto  status    = wxString::Format("processing... %u/%u", tasksDone, taskCount);
      auto* evt       = new wxThreadEvent{wxEVT_MKV_PROCESS_STATUS_UPDATE};
      evt->SetString(status);
      wxQueueEvent(instance, evt);
    });

    auto* evt = new wxThreadEvent{wxEVT_MKV_PROCESS_STATUS_UPDATE};
    evt->SetString(wxString::Format("processing finished: %u succeeded, %u failed",
                                    summary.succeeded, summary.failed));
    wxQueueEvent(instance, evt);
  };

  std::thread{std::move(action)}.detach();
}


void MainFrame::OnSelectMkvMerge(wxCommandEvent&) {
  wxFileDialog dialog(this, "Select mkvmerge.exe", "", "mkvmerge.exe",
                      "mkvmerge.exe (mkvmerge.exe)|mkvmerge.exe|Executable files (*.exe)|*.exe",
                      wxFD_OPEN | wxFD_FILE_MUST_EXIST);
  if (dialog.ShowModal() != wxID_OK) return;

  mkvMergeExecutable_ = fs::path{dialog.GetPath().ToStdWstring()};
  wxConfig config("MergeMKV");
  config.Write("MkvMergeExecutable", dialog.GetPath());
  config.Flush();
  SetStatusText("MKVToolNix: " + dialog.GetFilename());
}

void MainFrame::LoadMkvMergePath() {
  wxConfig config("MergeMKV");
  wxString savedPath;
  if (config.Read("MkvMergeExecutable", &savedPath) && !savedPath.empty()) {
    fs::path candidate{savedPath.ToStdWstring()};
    if (fs::exists(candidate)) {
      mkvMergeExecutable_ = candidate;
      return;
    }
  }

  std::vector<fs::path> candidates;
  const auto executable = fs::path{wxStandardPaths::Get().GetExecutablePath().ToStdWstring()};
  candidates.push_back(executable.parent_path() / "mkvmerge.exe");
  candidates.push_back(fs::current_path() / "mkvmerge.exe");
  candidates.push_back(fs::path{L"C:\\Program Files\\MKVToolNix\\mkvmerge.exe"});
  candidates.push_back(fs::path{L"C:\\Program Files (x86)\\MKVToolNix\\mkvmerge.exe"});

  for (const auto& candidate : candidates) {
    if (fs::exists(candidate)) {
      mkvMergeExecutable_ = candidate;
      config.Write("MkvMergeExecutable", wxString{candidate.wstring()});
      config.Flush();
      return;
    }
  }
}

void MainFrame::OnProcessStatusUpdate(wxThreadEvent& event) {
  SetStatusText(event.GetString());

  if (event.GetString().StartsWith("processing finished:")) {
    updateInProgress_ = false;
    processButton_->Enable(true);
  }
}
