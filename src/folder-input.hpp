#pragma once
#include "pch.hpp"
#include "drop-box.hpp"
#include "internal-update.hpp"

enum class FolderInputFlags {
  None,
  AskExtension = 1,
  AudioExtensions = 2,
};

class FolderInput : public wxBoxSizer, public IDropHandler {
  fs::path    extension_;
  wxTextCtrl* extensionInput_ = nullptr;

  fs::path    baseDirectory_;
  wxTextCtrl* baseDirectoryInput_ = nullptr;

  std::vector<fs::path> allFiles_;
  wxListBox*            allFilesList_ = nullptr;

  InternalUpdate internalUpdate_;

 public:
  FolderInput(wxWindow* parent, const wxString& title, fs::path extension,
              FolderInputFlags flags = FolderInputFlags::None);

  std::vector<fs::path> getFilteredFileList() const;
  fs::path              getBaseDirectory() const { return baseDirectory_; }
  void                  handleFile(const fs::path& path) override;
  void                  handleDirectory(const fs::path& path) override;
  void                  resetDrop() override;
  wxRect                getBounds() const override { return {GetPosition(), GetSize()}; }

 private:
  void onExtensionUpdate(wxCommandEvent& event);
  void onDirectoryUpdate(wxCommandEvent& event);
  void updateControl();
};
