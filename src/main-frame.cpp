#include "main-frame.hpp"
#include "pch.hpp"
#include "process-mkv.hpp"
#include "wx/msw/checkbox.h"
#include "wx/sizer.h"


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

    auto* menuHelp = new wxMenu{};
    menuHelp->Append(wxID_ABOUT);

    auto* menuBar = new wxMenuBar{};
    menuBar->Append(menuFile, "&File");
    menuBar->Append(menuHelp, "&Help");
    SetMenuBar(menuBar);

    Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
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
    audioFolderInput = new FolderInput{this, "audio dir:", ".mka",\n                                      FolderInputFlags::AskExtension | FolderInputFlags::AudioExtensions};
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

  // TODO: make this persistent setting in app
  auto  mkvToolnixPath = fs::path{"C:\\Program Files\\MKVToolNix"};
  auto* instance       = this;

  auto action = [instance, mkvToolnixPath, tasks]() {
    runMkvCombine(mkvToolnixPath, *tasks, [instance](uint32_t taskRemaining, uint32_t taskCount) {
      auto  tasksDone = taskCount - taskRemaining;
      auto  status    = wxString::Format("processing... %u/%u", tasksDone, taskCount);
      auto* evt       = new wxThreadEvent{wxEVT_MKV_PROCESS_STATUS_UPDATE};
      evt->SetString(status);
      wxQueueEvent(instance, evt);
    });

    auto* evt = new wxThreadEvent{wxEVT_MKV_PROCESS_STATUS_UPDATE};
    evt->SetString("processing done!");
    wxQueueEvent(instance, evt);
  };

  std::thread{std::move(action)}.detach();
}


void MainFrame::OnProcessStatusUpdate(wxThreadEvent& event) {
  SetStatusText(event.GetString());

  if (event.GetString() == "processing done!") {
    updateInProgress_ = false;
    processButton_->Enable(true);
  }
}
