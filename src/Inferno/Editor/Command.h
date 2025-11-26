#pragma once

namespace Inferno::Editor {
    // Executes either Action or SnapshotAction when CanExecute returns true
    class Command {
    public:
        std::function<void()> Action; // Action to perform
        std::function<string()> SnapshotAction; // Snapshots the result using the name of the returned string
        std::function<bool()> CanExecute = [] { return true; };
        string Name = "Unknown";

        void Execute() const;

        void operator()() const { Execute(); }
    };
}