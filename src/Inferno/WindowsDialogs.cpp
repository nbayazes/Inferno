#include "pch.h"
#include "WindowsDialogs.h"
#define NOMINMAX
#include <shobjidl.h>
#include <wrl.h>
#include "Convert.h"
#include "Input.h"
#include "Shell.h"
#include "Utility.h"
#include "vendor/PlatformHelpers.h"

using Microsoft::WRL::ComPtr;

namespace Inferno {
    // Similar to ComPtr, but for windows resources
    template <typename T>
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

    ScopedCursor::ScopedCursor(LPCWSTR cursorName) {
        SetCursor(LoadCursor(nullptr, cursorName));
    }

    ScopedCursor::~ScopedCursor() {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
    }

    void ShowWarningMessage(string_view message, string_view caption) {
        MessageBox(Shell::Hwnd, Widen(message).c_str(), Widen(caption).c_str(), MB_OK | MB_ICONWARNING);
        Input::ResetState(); // Fix for keys getting stuck after showing a dialog
    }

    void ShowErrorMessage(string_view message, string_view caption) {
        MessageBox(Shell::Hwnd, Widen(message).c_str(), Widen(caption).c_str(), MB_OK | MB_ICONERROR);
        Input::ResetState(); // Fix for keys getting stuck after showing a dialog
    }

    void ShowErrorMessage(const std::exception& e, string_view caption) {
        MessageBox(Shell::Hwnd, Widen(e.what()).c_str(), Widen(caption).c_str(), MB_OK | MB_ICONERROR);
        Input::ResetState(); // Fix for keys getting stuck after showing a dialog
    }

    bool ShowYesNoMessage(string_view message, string_view caption) {
        auto result = MessageBox(Shell::Hwnd, Widen(message).c_str(), Widen(caption).c_str(), MB_YESNO | MB_ICONASTERISK) == IDYES;
        Input::ResetState(); // Fix for keys getting stuck after showing a dialog
        return result;
    }

    Option<bool> ShowYesNoCancelMessage(string_view message, string_view caption) {
        auto result = MessageBox(Shell::Hwnd, Widen(message).c_str(), Widen(caption).c_str(), MB_YESNOCANCEL | MB_ICONASTERISK);
        Input::ResetState(); // Fix for keys getting stuck after showing a dialog
        switch (result) {
            case IDYES: return true;
            case IDNO: return false;
            default: return {};
        }
    }

    bool ShowOkCancelMessage(string_view message, string_view caption) {
        auto result = MessageBox(Shell::Hwnd, Widen(message).c_str(), Widen(caption).c_str(), MB_OKCANCEL | MB_ICONASTERISK) == IDOK;
        Input::ResetState(); // Fix for keys getting stuck after showing a dialog
        return result;
    }

    bool ShowOkMessage(string_view message, string_view caption) {
        auto result = MessageBox(Shell::Hwnd, Widen(message).c_str(), Widen(caption).c_str(), MB_OK | MB_ICONASTERISK) == IDOK;
        Input::ResetState(); // Fix for keys getting stuck after showing a dialog
        return result;
    }

    Option<filesystem::path> OpenFileDialog(span<const DialogFilter> filter, string_view title) {
        try {
            ComPtr<IFileOpenDialog> dialog;
            auto filterspec = Seq::map(filter, [](auto& f) { return COMDLG_FILTERSPEC{ f.name, f.filter }; });
            ThrowIfFailed(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)));
            ThrowIfFailed(dialog->SetFileTypes((int)filterspec.size(), filterspec.data()));
            ThrowIfFailed(dialog->SetTitle(Widen(title).c_str()));
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

    List<filesystem::path> OpenMultipleFilesDialog(span<const DialogFilter> filter, string_view title) {
        try {
            ComPtr<IFileOpenDialog> dialog;
            auto filterspec = Seq::map(filter, [](auto& f) { return COMDLG_FILTERSPEC{ f.name, f.filter }; });
            ThrowIfFailed(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)));
            ThrowIfFailed(dialog->SetFileTypes((int)filterspec.size(), filterspec.data()));
            ThrowIfFailed(dialog->SetTitle(Widen(title).c_str()));
            ThrowIfFailed(dialog->SetOptions(FOS_ALLOWMULTISELECT));
            auto hr = dialog->Show(Shell::Hwnd);
            Input::ResetState(); // Fix for keys getting stuck after showing a dialog
            if (FAILED(hr)) return {}; // includes cancelled dialog

            List<filesystem::path> paths;

            auto appendFile = [&paths](const ComPtr<IShellItem>& item) {
                ComMemPtr<WCHAR> filePath;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &filePath)))
                    paths.push_back(*filePath);
            };

            // Check for single selection
            ComPtr<IShellItem> result;
            hr = dialog->GetResult(&result);
            if (SUCCEEDED(hr)) {
                appendFile(result);
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
                        appendFile(item);
                }
            }

            return paths;
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
            return {};
        }
    }

    Option<filesystem::path> SaveFileDialog(span<const DialogFilter> filter, uint selectedFilterIndex, string_view defaultName, string_view title) {
        try {
            ComPtr<IFileSaveDialog> dialog;
            auto filterspec = Seq::map(filter, [](auto& f) { return COMDLG_FILTERSPEC{ f.name, f.filter }; });

            ThrowIfFailed(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)));
            ThrowIfFailed(dialog->SetFileTypes((int)filterspec.size(), filterspec.data()));
            ThrowIfFailed(dialog->SetFileTypeIndex(selectedFilterIndex));
            if (selectedFilterIndex > 0) {
                // note that filter indices are 1 based not 0 based
                auto ext = String::Extension(wstring(filterspec[selectedFilterIndex - 1].pszSpec));
                ext = ext.substr(1); // discard the period, as SetDefaultExtension does not want it
                ThrowIfFailed(dialog->SetDefaultExtension(ext.c_str()));
            }

            ThrowIfFailed(dialog->SetFileName(Widen(defaultName).c_str()));
            ThrowIfFailed(dialog->SetTitle(Widen(title).c_str()));
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

    Option<filesystem::path> BrowseFolderDialog(string_view title) {
        try {
            ComPtr<IFileOpenDialog> dialog;
            ThrowIfFailed(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)));
            ThrowIfFailed(dialog->SetOptions(FOS_PICKFOLDERS));
            ThrowIfFailed(dialog->SetTitle(Widen(title).c_str()));
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
