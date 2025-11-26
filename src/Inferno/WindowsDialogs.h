#pragma once

#include "Types.h"

namespace Inferno {
    // Changes the cursor then resets it to the default arrow when going out of scope
    class ScopedCursor {
    public:
        ScopedCursor(LPCWSTR cursorName);
        ~ScopedCursor();

        ScopedCursor(const ScopedCursor&) = delete;
        ScopedCursor(ScopedCursor&&) = default;
        ScopedCursor& operator=(const ScopedCursor&) = delete;
        ScopedCursor& operator=(ScopedCursor&&) = default;
    };

    void ShowWarningMessage(string_view message, string_view caption = "Warning");

    void ShowErrorMessage(string_view message, string_view caption = "Error");

    void ShowErrorMessage(const std::exception& e, string_view caption = "Error");

    bool ShowYesNoMessage(string_view message, string_view caption);

    Option<bool> ShowYesNoCancelMessage(string_view message, string_view caption);

    bool ShowOkCancelMessage(string_view message, string_view caption);

    bool ShowOkMessage(string_view message, string_view caption);

    struct DialogFilter {
        LPCWSTR name;
        LPCWSTR filter;
    };

    Option<filesystem::path> OpenFileDialog(span<const DialogFilter> filter, string_view title);

    List<filesystem::path> OpenMultipleFilesDialog(span<const DialogFilter> filter, string_view title);

    Option<filesystem::path> SaveFileDialog(span<const DialogFilter> filter, uint selectedFilterIndex,
                                            string_view defaultName, string_view title = "Save File As");

    Option<filesystem::path> BrowseFolderDialog(string_view title = "Browse For Folder");
}
