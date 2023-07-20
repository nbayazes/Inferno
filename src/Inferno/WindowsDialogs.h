#pragma once

#define NOMINMAX
#include <shobjidl.h>
#include <wrl.h>
#include "Types.h"
#include "Shell.h"
#include "Input.h"
#include "Utility.h"
#include "Convert.h"
#include "vendor/PlatformHelpers.h"

using Microsoft::WRL::ComPtr;

namespace Inferno {
    // Changes the cursor then resets it to the default arrow when going out of scope
    class ScopedCursor {
    public:
        ScopedCursor(LPCWSTR cursorName) {
            SetCursor(LoadCursor(nullptr, cursorName));
        }

        ~ScopedCursor() {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
        }

        ScopedCursor(const ScopedCursor&) = delete;
        ScopedCursor(ScopedCursor&&) = default;
        ScopedCursor& operator=(const ScopedCursor&) = delete;
        ScopedCursor& operator=(ScopedCursor&&) = default;
    };

    template<typename T>
    class ComMemPtr {
        T* _resource = nullptr;
        void Release() const { CoTaskMemFree(_resource); }
    public:
        auto operator&() {
            Release();
            return &_resource;
        }

        auto operator*() const { return _resource; }
        explicit operator bool() const { return _resource != nullptr; }

        ComMemPtr() = default;
        ComMemPtr(const ComMemPtr&) = delete;
        ComMemPtr(ComMemPtr&&) = delete;
        ComMemPtr& operator=(const ComMemPtr&) = delete;
        ComMemPtr& operator=(ComMemPtr&&) = delete;
        ~ComMemPtr() { Release(); }
    };

    inline void ShowWarningMessage(const wstring& message, const wstring& caption = L"Warning") {
        MessageBox(Shell::Hwnd, message.c_str(), caption.c_str(), MB_OK | MB_ICONWARNING);
        Input::ResetState(); // Fix for keys getting stuck after showing a dialog
    }

    inline void ShowErrorMessage(const wstring& message, const wstring& caption = L"Error") {
        MessageBox(Shell::Hwnd, message.c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
        Input::ResetState(); // Fix for keys getting stuck after showing a dialog
    }

    inline void ShowErrorMessage(const std::exception& e, const wstring& caption = L"Error") {
        MessageBox(Shell::Hwnd, Convert::ToWideString(e.what()).c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
        Input::ResetState(); // Fix for keys getting stuck after showing a dialog
    }

    inline bool ShowYesNoMessage(const wstring& message, const wstring& caption) {
        auto result = MessageBox(Shell::Hwnd, message.c_str(), caption.c_str(), MB_YESNO | MB_ICONASTERISK) == IDYES;
        Input::ResetState(); // Fix for keys getting stuck after showing a dialog
        return result;
    }

    inline Option<bool> ShowYesNoCancelMessage(const wstring& message, const wstring& caption) {
        auto result = MessageBox(Shell::Hwnd, message.c_str(), caption.c_str(), MB_YESNOCANCEL | MB_ICONASTERISK);
        Input::ResetState(); // Fix for keys getting stuck after showing a dialog
        switch (result) {
            case IDYES: return true;
            case IDNO: return false;
            default: return {};
        }
    }

    inline bool ShowOkCancelMessage(const wstring& message, const wstring& caption) {
        auto result = MessageBox(Shell::Hwnd, message.c_str(), caption.c_str(), MB_OKCANCEL | MB_ICONASTERISK) == IDOK;
        Input::ResetState(); // Fix for keys getting stuck after showing a dialog
        return result;
    }

    inline bool ShowOkMessage(const wstring& message, const wstring& caption) {
        auto result = MessageBox(Shell::Hwnd, message.c_str(), caption.c_str(), MB_OK | MB_ICONASTERISK) == IDOK;
        Input::ResetState(); // Fix for keys getting stuck after showing a dialog
        return result;
    }

    inline Option<filesystem::path> OpenFileDialog(span<const COMDLG_FILTERSPEC> filter, wstring title) {
        try {
            ComPtr<IFileOpenDialog> dialog;
            ThrowIfFailed(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)));
            dialog->SetFileTypes((int)filter.size(), filter.data());
            dialog->SetTitle(title.c_str());
            auto hr = dialog->Show(Shell::Hwnd);
            Input::ResetState(); // Fix for keys getting stuck after showing a dialog
            if (!SUCCEEDED(hr)) return {}; // includes cancelled dialog

            ComPtr<IShellItem> result;
            ThrowIfFailed(dialog->GetResult(&result));

            ComMemPtr<WCHAR> filePath;
            ThrowIfFailed(result->GetDisplayName(SIGDN_FILESYSPATH, &filePath));

            if (!filePath) return {};
            return { *filePath };
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
            return {};
        }
    }

    inline List<filesystem::path> OpenMultipleFilesDialog(span<const COMDLG_FILTERSPEC> filter, wstring title) {
        try {
            ComPtr<IFileOpenDialog> dialog;
            ThrowIfFailed(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)));
            dialog->SetFileTypes((int)filter.size(), filter.data());
            dialog->SetTitle(title.c_str());
            dialog->SetOptions(FOS_ALLOWMULTISELECT);
            auto hr = dialog->Show(Shell::Hwnd);
            Input::ResetState(); // Fix for keys getting stuck after showing a dialog
            if (!SUCCEEDED(hr)) return {}; // includes cancelled dialog

            List<filesystem::path> paths;

            auto AppendFile = [&paths](ComPtr<IShellItem>& item) {
                ComMemPtr<WCHAR> filePath;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &filePath)))
                    paths.push_back(*filePath);
            };

            // Check for single selection
            ComPtr<IShellItem> result;
            hr = dialog->GetResult(&result);
            if (SUCCEEDED(hr)) {
                AppendFile(result);
            }
            else {
                // check for multiple selections
                ComPtr<IShellItemArray> results;
                hr = dialog->GetResults(&results);
                if (SUCCEEDED(hr)) {
                    ComPtr<IEnumShellItems> items;
                    ThrowIfFailed(results->EnumItems(&items));

                    ComPtr<IShellItem> item;
                    ULONG fetched{};
                    while (items->Next(1, &item, &fetched) == S_OK)
                        AppendFile(item);
                }
            }

            return paths;
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
            return {};
        }
    }

    inline Option<filesystem::path> SaveFileDialog(span<const COMDLG_FILTERSPEC> filter, uint selectedFilterIndex, wstring defaultName, wstring title = L"Save File As") {
        try {
            ComPtr<IFileSaveDialog> dialog;
            ThrowIfFailed(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)));
            dialog->SetFileTypes((int)filter.size(), filter.data());
            dialog->SetFileTypeIndex(selectedFilterIndex);
            if (selectedFilterIndex > 0) { // note that filter indices are 1 based not 0 based
                auto ext = String::Extension(wstring(filter[selectedFilterIndex - 1].pszSpec));
                dialog->SetDefaultExtension(ext.c_str());
            }

            dialog->SetFileName(defaultName.c_str());
            dialog->SetTitle(title.c_str());
            auto hr = dialog->Show(Shell::Hwnd);
            Input::ResetState(); // Fix for keys getting stuck after showing a dialog
            if (FAILED(hr)) return {}; // includes cancelled dialog

            ComPtr<IShellItem> result;
            ThrowIfFailed(dialog->GetResult(&result));

            ComMemPtr<WCHAR> filePath;
            ThrowIfFailed(result->GetDisplayName(SIGDN_FILESYSPATH, &filePath));

            if (!filePath) return {};
            return { *filePath };
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
            return {};
        }
    }

    inline Option<filesystem::path> BrowseFolderDialog(wstring title = L"Browse For Folder") {
        try {
            ComPtr<IFileOpenDialog> dialog;
            ThrowIfFailed(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)));
            dialog->SetOptions(FOS_PICKFOLDERS);
            dialog->SetTitle(title.c_str());
            auto hr = dialog->Show(Shell::Hwnd);
            Input::ResetState(); // Fix for keys getting stuck after showing a dialog
            if (FAILED(hr)) return {}; // includes cancelled dialog

            ComPtr<IShellItem> result;
            ThrowIfFailed(dialog->GetResult(&result));

            ComMemPtr<WCHAR> path;
            ThrowIfFailed(result->GetDisplayName(SIGDN_FILESYSPATH, &path));

            if (!path) return {};
            return { *path };
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
            return {};
        }
    }
}