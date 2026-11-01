#include "folder-input.hpp"
#include "string-utils.hpp"

FolderInput::FolderInput(wxWindow* parent, const wxString& title, fs::path extension,
                         FolderInputFlags flags)
    : wxBoxSizer{wxVERTICAL}, extension_{extension},
      acceptAudioExtensions_{!!(flags & FolderInputFlags::AudioExtensions)} {
  auto* text = new wxStaticText{parent, wxID_ANY, title};
  this->Add(text, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

  baseDirectoryInput_ = new wxTextCtrl{parent, wxID_ANY};
  this->Add(baseDirectoryInput_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
  baseDirectoryInput_->Bind(wxEVT_TEXT, &FolderInput::onDirectoryUpdate, this);

  if (!!(flags & FolderInputFlags::AskExtension)) {
    auto* extText = new wxStaticText{parent, wxID_ANY, wxT("extension:")};
    this->Add(extText, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

    extensionInput_ = new wxTextCtrl{parent, wxID_ANY, extension.string().c_str()};
    this->Add(extensionInput_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
    extensionInput_->Bind(wxEVT_TEXT, &FolderInput::onExtensionUpdate, this);
  }

  auto* listText = new wxStaticText{parent, wxID_ANY, wxT("files:")};
  this->Add(listText, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

  allFilesList_ =
      new wxListBox{parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_HSCROLL};
  this->Add(allFilesList_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
}

std::vector<fs::path> FolderInput::getFilteredFileList() const {
  std::vector<fs::path> paths;

  for (const auto& path : allFiles_) {
    if (extension_.empty() || path.extension() == extension_ ||
        (acceptAudioExtensions_ && (path.extension() == ".mka" || path.extension() == ".ac3"))) {
      paths.push_back(path);
    }
  }

  return paths;
}

void FolderInput::handleFile(const fs::path& path) {
  wxLogInfo("file input: %s", toWxString(path));
  baseDirectory_ = path.parent_path();
  allFiles_.push_back(path);
  updateControl();
}

void FolderInput::handleDirectory(const fs::path& path) {
  wxLogInfo("directory input: %s", toWxString(path));
  baseDirectory_ = path;

  auto ec       = std::error_code{};
  auto iterator = fs::directory_iterator{path, ec};

  if (ec) {
    wxLogError("can't get directory status: %s", ec.message().c_str());
    return;
  }

  for (const auto& entry : iterator) {
    bool isRegularFile = entry.is_regular_file(ec);

    if (ec) {
      wxLogError("can't get file status: %s", ec.message().c_str());
      continue;
    }

    if (!isRegularFile) {
      continue;
    }

    wxLogInfo("file from directory: %s", toWxString(entry.path()));
    allFiles_.push_back(entry.path());
  }

  updateControl();
}

void FolderInput::resetDrop() {
  wxLogInfo("resetting drop");
  baseDirectory_ = fs::path{};
  allFiles_.clear();
}

void FolderInput::onExtensionUpdate(wxCommandEvent& event) {
  wxLogInfo("onExtensionUpdate");
  if (internalUpdate_.isUpdateInProgress()) {
    return;
  }
  extension_ = fs::path{event.GetString().fn_str()};
  updateControl();
}

void FolderInput::onDirectoryUpdate(wxCommandEvent& event) {
  wxLogInfo("onDirectoryUpdate");
  if (internalUpdate_.isUpdateInProgress()) {
    return;
  }
  resetDrop();
  handleDirectory(event.GetString().fn_str());
}

void FolderInput::updateControl() {
  wxLogInfo("updateControl, ext: %s", toWxString(extension_));

  auto updateLock = UpdateInProgress{internalUpdate_};
  baseDirectoryInput_->ChangeValue(toWxString(baseDirectory_));
  allFilesList_->Clear();

  auto stringArr = wxArrayString{};
  stringArr.Alloc(allFiles_.size());

  for (const auto& path : allFiles_) {
    if (extension_.empty() || path.extension() == extension_ ||
        (acceptAudioExtensions_ && (path.extension() == ".mka" || path.extension() == ".ac3"))) {
      stringArr.Add(toWxString(path.filename()));
    }
  }

  wxLogInfo("allFilesList should be updated to have %d items", (int)stringArr.size());

  if (!stringArr.IsEmpty()) {
    allFilesList_->InsertItems(stringArr, 0);
  }
}
