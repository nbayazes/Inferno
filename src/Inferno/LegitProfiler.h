#pragma once

#include "imgui.h"
#include "Types.h"
#include <array>
#include <map>
#include <vector>
#include <cstdint>
#include <string>
#include <chrono>

namespace LegitProfiler {
    using DirectX::SimpleMath::Vector2;

    namespace Colors {
        //https://flatuicolors.com/palette/defo
#define RGBA_LE(col) (((col & 0xff000000) >> (3 * 8)) + ((col & 0x00ff0000) >> (1 * 8)) + ((col & 0x0000ff00) << (1 * 8)) + ((col & 0x000000ff) << (3 * 8)))
        static constexpr uint32_t TURQOISE = RGBA_LE(0x1abc9cffu);
        static constexpr uint32_t GREEN_SEA = RGBA_LE(0x16a085ffu);

        static constexpr uint32_t EMERALD = RGBA_LE(0x2ecc71ffu);
        static constexpr uint32_t NEPHRITIS = RGBA_LE(0x27ae60ffu);

        static constexpr uint32_t PETER_RIVER = RGBA_LE(0x3498dbffu); //blue
        static constexpr uint32_t BELIZE_HOLE = RGBA_LE(0x2980b9ffu);

        static constexpr uint32_t AMETHYST = RGBA_LE(0x9b59b6ffu);
        static constexpr uint32_t WISTERIA = RGBA_LE(0x8e44adffu);

        static constexpr uint32_t SUN_FLOWER = RGBA_LE(0xf1c40fffu);
        static constexpr uint32_t ORANGE = RGBA_LE(0xf39c12ffu);

        static constexpr uint32_t CARROT = RGBA_LE(0xe67e22ffu);
        static constexpr uint32_t PUMPKIN = RGBA_LE(0xd35400ffu);

        static constexpr uint32_t ALIZARIN = RGBA_LE(0xe74c3cffu);
        static constexpr uint32_t POMEGRANATE = RGBA_LE(0xc0392bffu);

        static constexpr uint32_t CLOUDS = RGBA_LE(0xecf0f1ffu);
        static constexpr uint32_t SILVER = RGBA_LE(0xbdc3c7ffu);
        static constexpr uint32_t IMGUI_TEXT = RGBA_LE(0xF2F5FAFFu);
#undef RGBA_LE
    }

    inline double GetElapsedFrameTimeSeconds() {
        using namespace std::chrono;
        auto x = steady_clock::now().time_since_epoch();
        auto nano = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
        return (double)nano / 1'000'000'000;
    }

    struct ProfilerTask {
        double startTime = 0; // Time in milliseconds from start of frame
        double endTime = 0; // Time in milliseconds from start of frame
        std::string name = "Unnamed";
        uint32_t color = Colors::EMERALD;

        ProfilerTask() = default;

        ProfilerTask(std::string_view name, uint32_t color = LegitProfiler::Colors::TURQOISE) : name(name), color(color) {
            //startTime = GetElapsedFrameTimeSeconds();
            startTime = Inferno::Clock.GetFrameStartOffsetSeconds();
        }

        double GetLength() const {
            return endTime - startTime;
        }
    };

    class ProfilerGraph {
    public:
        int frameWidth;
        int frameSpacing;
        bool useColoredLegendText;
        float maxFrameTime = 1.0f / 30.0f;

        ProfilerGraph(size_t framesCount) {
            _frames.resize(framesCount);
            for (auto& frame : _frames)
                frame.tasks.reserve(100);
            frameWidth = 3;
            frameSpacing = 1;
            useColoredLegendText = false;
        }

        void LoadFrameData(std::span<ProfilerTask> tasks) {
            auto& currFrame = _frames[_currFrameIndex];
            currFrame.tasks.resize(0);
            for (size_t taskIndex = 0; taskIndex < tasks.size(); taskIndex++) {
                if (taskIndex == 0)
                    currFrame.tasks.push_back(tasks[taskIndex]);
                else {
                    if (tasks[taskIndex - 1].color != tasks[taskIndex].color || tasks[taskIndex - 1].name != tasks[taskIndex].name) {
                        currFrame.tasks.push_back(tasks[taskIndex]);
                    }
                    else {
                        currFrame.tasks.back().endTime = tasks[taskIndex].endTime;
                    }
                }
            }
            currFrame.taskStatsIndex.resize(currFrame.tasks.size());

            for (size_t taskIndex = 0; taskIndex < currFrame.tasks.size(); taskIndex++) {
                auto& task = currFrame.tasks[taskIndex];
                auto it = _taskNameToStatsIndex.find(task.name);
                if (it == _taskNameToStatsIndex.end()) {
                    _taskNameToStatsIndex[task.name] = _taskStats.size();
                    TaskStats taskStat{};
                    taskStat.maxTime = -1.0;
                    _taskStats.push_back(taskStat);
                }
                currFrame.taskStatsIndex[taskIndex] = _taskNameToStatsIndex[task.name];
            }
            _currFrameIndex = (_currFrameIndex + 1) % _frames.size();

            RebuildTaskStats(_currFrameIndex, 300/*frames.size()*/);
        }

        void RenderTimings(int graphWidth, int legendWidth, int height, int frameIndexOffset) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            auto screenPos = ImGui::GetCursorScreenPos();
            const Vector2 widgetPos(screenPos.x, screenPos.y);
            RenderGraph(drawList, widgetPos, Vector2((float)graphWidth, (float)height), frameIndexOffset);
            RenderLegend(drawList, widgetPos + Vector2((float)graphWidth, 0.0f), Vector2((float)legendWidth, (float)height), frameIndexOffset);
            ImGui::Dummy(ImVec2(float(graphWidth + legendWidth), float(height)));
        }

    private:
        void RebuildTaskStats(size_t endFrame, size_t framesCount) {
            for (auto& taskStat : _taskStats) {
                taskStat.maxTime = -1.0f;
                taskStat.priorityOrder = size_t(-1);
                taskStat.onScreenIndex = size_t(-1);
            }

            for (size_t frameNumber = 0; frameNumber < framesCount; frameNumber++) {
                size_t frameIndex = (endFrame - 1 - frameNumber + _frames.size()) % _frames.size();
                auto& frame = _frames[frameIndex];
                for (size_t taskIndex = 0; taskIndex < frame.tasks.size(); taskIndex++) {
                    auto& task = frame.tasks[taskIndex];
                    auto& stats = _taskStats[frame.taskStatsIndex[taskIndex]];
                    stats.maxTime = std::max(stats.maxTime, task.endTime - task.startTime);
                }
            }
            std::vector<size_t> statPriorities;
            statPriorities.resize(_taskStats.size());
            for (size_t statIndex = 0; statIndex < _taskStats.size(); statIndex++)
                statPriorities[statIndex] = statIndex;

            std::ranges::sort(statPriorities, [this](size_t left, size_t right) { return _taskStats[left].maxTime > _taskStats[right].maxTime; });
            for (size_t statNumber = 0; statNumber < _taskStats.size(); statNumber++) {
                size_t statIndex = statPriorities[statNumber];
                _taskStats[statIndex].priorityOrder = statNumber;
            }
        }

        void RenderGraph(ImDrawList* drawList, Vector2 graphPos, Vector2 graphSize, size_t frameIndexOffset) const {
            Rect(drawList, graphPos, graphPos + graphSize, 0xffffffff, false);
            float heightThreshold = 1.0f;

            for (size_t frameNumber = 0; frameNumber < _frames.size(); frameNumber++) {
                size_t frameIndex = (_currFrameIndex - frameIndexOffset - 1 - frameNumber + 2 * _frames.size()) % _frames.size();

                Vector2 framePos = graphPos + Vector2(graphSize.x - 1 - (float)frameWidth - ((float)frameWidth + frameSpacing) * frameNumber, graphSize.y - 1);
                if (framePos.x < graphPos.x + 1)
                    break;
                Vector2 taskPos = framePos + Vector2(0.0f, 0.0f);
                auto& frame = _frames[frameIndex];
                for (const auto& task : frame.tasks) {
                    float taskStartHeight = (float(task.startTime) / maxFrameTime) * graphSize.y;
                    float taskEndHeight = (float(task.endTime) / maxFrameTime) * graphSize.y;
                    //taskMaxCosts[task.name] = std::max(taskMaxCosts[task.name], task.endTime - task.startTime);
                    if (abs(taskEndHeight - taskStartHeight) > heightThreshold)
                        Rect(drawList, taskPos + Vector2(0.0f, -taskStartHeight), taskPos + Vector2((float)frameWidth, -taskEndHeight), task.color, true);
                }
            }
        }

        void RenderLegend(ImDrawList* drawList, Vector2 legendPos, Vector2 legendSize, size_t frameIndexOffset) {
            constexpr float markerLeftRectMargin = 3.0f;
            constexpr float markerLeftRectWidth = 5.0f;
            constexpr float markerMidWidth = 30.0f;
            constexpr float markerRightRectWidth = 10.0f;
            constexpr float markerRigthRectMargin = 3.0f;
            constexpr float markerRightRectHeight = 10.0f;
            constexpr float markerRightRectSpacing = 8.0f;
            constexpr float nameOffset = 40.0f;
            Vector2 textMargin(5.0f, -6.0f);

            auto& currFrame = _frames[(_currFrameIndex - frameIndexOffset - 1 + 2 * _frames.size()) % _frames.size()];
            auto maxTasksCount = size_t(legendSize.y / (markerRightRectHeight + markerRightRectSpacing));

            for (auto& taskStat : _taskStats) {
                taskStat.onScreenIndex = size_t(-1);
            }

            size_t tasksToShow = std::min<size_t>(_taskStats.size(), maxTasksCount);
            size_t tasksShownCount = 0;
            for (size_t taskIndex = 0; taskIndex < currFrame.tasks.size(); taskIndex++) {
                auto& task = currFrame.tasks[taskIndex];
                auto& stat = _taskStats[currFrame.taskStatsIndex[taskIndex]];

                if (stat.priorityOrder >= tasksToShow)
                    continue;

                if (stat.onScreenIndex == size_t(-1)) {
                    stat.onScreenIndex = tasksShownCount++;
                }
                else
                    continue;
                float taskStartHeight = (float(task.startTime) / maxFrameTime) * legendSize.y;
                float taskEndHeight = (float(task.endTime) / maxFrameTime) * legendSize.y;

                Vector2 markerLeftRectMin = legendPos + Vector2(markerLeftRectMargin, legendSize.y);
                Vector2 markerLeftRectMax = markerLeftRectMin + Vector2(markerLeftRectWidth, 0.0f);
                markerLeftRectMin.y -= taskStartHeight;
                markerLeftRectMax.y -= taskEndHeight;

                Vector2 markerRightRectMin = legendPos + Vector2(markerLeftRectMargin + markerLeftRectWidth + markerMidWidth, legendSize.y - markerRigthRectMargin - (markerRightRectHeight + markerRightRectSpacing) * stat.onScreenIndex);
                Vector2 markerRightRectMax = markerRightRectMin + Vector2(markerRightRectWidth, -markerRightRectHeight);
                RenderTaskMarker(drawList, markerLeftRectMin, markerLeftRectMax, markerRightRectMin, markerRightRectMax, task.color);

                uint32_t textColor = useColoredLegendText ? task.color : Colors::IMGUI_TEXT; // task.color;

                float taskTimeMs = float(task.endTime - task.startTime);
                std::ostringstream timeText;
                timeText.precision(2);
                timeText << std::fixed << std::string("[") << (taskTimeMs * 1000.0f);

                Text(drawList, markerRightRectMax + textMargin, textColor, timeText.str().c_str());
                Text(drawList, markerRightRectMax + textMargin + Vector2(nameOffset, 0.0f), textColor, (std::string("ms] ") + task.name).c_str());
            }

            /*
            struct PriorityEntry
            {
              bool isUsed;
              legit::ProfilerTask task;
            };
            std::map<std::string, PriorityEntry> priorityEntries;
            for (auto priorityTask : priorityTasks)
            {
              PriorityEntry entry;
              entry.task = frames[priorityTask.frameIndex].tasks[priorityTask.taskIndex];
              entry.isUsed = false;
              priorityEntries[entry.task.name] = entry;
            }
            size_t shownTasksCount = 0;
            for (size_t taskIndex = 0; taskIndex < currFrame.tasks.size(); taskIndex++)
            {
              auto &task = currFrame.tasks[taskIndex];
              auto it = priorityEntries.find(task.name);
              if (it != priorityEntries.end() && !it->second.isUsed)
              {
                it->second.isUsed = true;
      
                float taskStartHeight = (float(task.startTime) / maxFrameTime) * legendSize.y;
                float taskEndHeight = (float(task.endTime) / maxFrameTime) * legendSize.y;
      
      
                Vector2 markerLeftRectMin = legendPos + Vector2(markerLeftRectMargin, legendSize.y);
                Vector2 markerLeftRectMax = markerLeftRectMin + Vector2(markerLeftRectWidth, 0.0f);
                markerLeftRectMin.y -= taskStartHeight;
                markerLeftRectMax.y -= taskEndHeight;
      
                Vector2 markerRightRectMin = legendPos + Vector2(markerLeftRectMargin + markerLeftRectWidth + markerMidWidth, legendSize.y - markerRigthRectMargin - (markerRightRectHeight + markerRightRectSpacing) * shownTasksCount);
                Vector2 markerRightRectMax = markerRightRectMin + Vector2(markerRightRectWidth, -markerRightRectHeight);
                RenderTaskMarker(drawList, markerLeftRectMin, markerLeftRectMax, markerRightRectMin, markerRightRectMax, task.color);
      
                uint32_t textColor = legit::Colors::imguiText;// task.color;
      
                float taskTimeMs = float(task.endTime - task.startTime);
                std::ostringstream timeText;
                timeText.precision(2);
                timeText << std::fixed << std::string("[") << (taskTimeMs * 1000.0f);
      
                Text(drawList, markerRightRectMax + textMargin, textColor, timeText.str().c_str());
                Text(drawList, markerRightRectMax + textMargin + Vector2(nameOffset, 0.0f), textColor, (std::string("ms] ") + task.name).c_str());
                shownTasksCount++;
              }
            }*/

            /*for (size_t priorityTaskIndex = 0; priorityTaskIndex < priorityTasks.size(); priorityTaskIndex++)
            {
              auto &priorityTask = priorityTasks[priorityTaskIndex];
              auto &globalTask = frames[priorityTask.frameIndex].tasks[priorityTask.taskIndex];
      
              size_t lastFrameTaskIndex = currFrame.FindTask(globalTask.name);
      
              Vector2 taskPos = legendPos + marginSpacing + Vector2(0.0f, markerHeight) + Vector2(0.0f, (markerHeight + itemSpacing) * priorityTaskIndex);
              Rect(drawList, taskPos, taskPos + Vector2(markerHeight, -markerHeight), task.color, true);
              Text(drawList, taskPos + textOffset, 0xffffffff, task.name.c_str());
            }*/
        }

        static void Rect(ImDrawList* drawList, Vector2 minPoint, Vector2 maxPoint, uint32_t col, bool filled = true) {
            if (filled)
                drawList->AddRectFilled(ImVec2(minPoint.x, minPoint.y), ImVec2(maxPoint.x, maxPoint.y), col);
            else
                drawList->AddRect(ImVec2(minPoint.x, minPoint.y), ImVec2(maxPoint.x, maxPoint.y), col);
        }

        static void Text(ImDrawList* drawList, Vector2 point, uint32_t col, const char* text) {
            drawList->AddText(ImVec2(point.x, point.y), col, text);
        }

        static void Triangle(ImDrawList* drawList, const std::array<Vector2, 3>& points, uint32_t col, bool filled = true) {
            if (filled)
                drawList->AddTriangleFilled(ImVec2(points[0].x, points[0].y), ImVec2(points[1].x, points[1].y), ImVec2(points[2].x, points[2].y), col);
            else
                drawList->AddTriangle(ImVec2(points[0].x, points[0].y), ImVec2(points[1].x, points[1].y), ImVec2(points[2].x, points[2].y), col);
        }

        static void RenderTaskMarker(ImDrawList* drawList, Vector2 leftMinPoint, Vector2 leftMaxPoint, Vector2 rightMinPoint, Vector2 rightMaxPoint, uint32_t col) {
            Rect(drawList, leftMinPoint, leftMaxPoint, col, true);
            Rect(drawList, rightMinPoint, rightMaxPoint, col, true);
            std::array points = {
                ImVec2(leftMaxPoint.x, leftMinPoint.y),
                ImVec2(leftMaxPoint.x, leftMaxPoint.y),
                ImVec2(rightMinPoint.x, rightMaxPoint.y),
                ImVec2(rightMinPoint.x, rightMinPoint.y)
            };
            drawList->AddConvexPolyFilled(points.data(), int(points.size()), col);
        }

        struct FrameData {
            /*void BuildPriorityTasks(size_t maxPriorityTasksCount)
            {
              priorityTaskIndices.clear();
              std::set<std::string> usedTaskNames;
      
              for (size_t priorityIndex = 0; priorityIndex < maxPriorityTasksCount; priorityIndex++)
              {
                size_t bestTaskIndex = size_t(-1);
                for (size_t taskIndex = 0; taskIndex < tasks.size(); taskIndex++)
                {
                  auto &task = tasks[taskIndex];
                  auto it = usedTaskNames.find(tasks[taskIndex].name);
                  if (it == usedTaskNames.end() && (bestTaskIndex == size_t(-1) || tasks[bestTaskIndex].GetLength() < task.GetLength()))
                  {
                    bestTaskIndex = taskIndex;
                  }
                }
                if (bestTaskIndex == size_t(-1))
                  break;
                priorityTaskIndices.push_back(bestTaskIndex);
                usedTaskNames.insert(tasks[bestTaskIndex].name);
              }
            }*/
            std::vector<ProfilerTask> tasks;
            std::vector<size_t> taskStatsIndex;
            //std::vector<size_t> priorityTaskIndices;
        };

        struct TaskStats {
            double maxTime;
            size_t priorityOrder;
            size_t onScreenIndex;
        };

        std::vector<TaskStats> _taskStats;
        std::map<std::string, size_t> _taskNameToStatsIndex;

        /*struct PriorityTask
        {
          size_t frameIndex;
          size_t taskIndex;
        };
        std::vector<PriorityTask> priorityTasks;*/
        std::vector<FrameData> _frames;
        size_t _currFrameIndex = 0;
    };

    class ProfilersWindow {
    public:
        ProfilersWindow():
            cpuGraph(300),
            gpuGraph(300) {
            stopProfiling = false;
            frameOffset = 0;
            frameWidth = 3;
            frameSpacing = 1;
            useColoredLegendText = true;
            prevFpsFrameTime = std::chrono::system_clock::now();
            fpsFramesCount = 0;
            avgFrameTime = 1.0f;
        }

        void Render() {
            fpsFramesCount++;
            auto currFrameTime = std::chrono::system_clock::now();
            {
                float fpsDeltaTime = std::chrono::duration<float>(currFrameTime - prevFpsFrameTime).count();
                if (fpsDeltaTime > 0.5f) {
                    this->avgFrameTime = fpsDeltaTime / float(fpsFramesCount);
                    fpsFramesCount = 0;
                    prevFpsFrameTime = currFrameTime;
                }
            }

            std::stringstream title{};
            title.precision(2);
            title << std::fixed << "Legit profiler [" << 1.0f / avgFrameTime << "fps\t" << avgFrameTime * 1000.0f << "ms]###ProfilerWindow";
            //###AnimatedTitle
            ImGui::Begin(title.str().c_str(), nullptr, ImGuiWindowFlags_NoScrollbar);
            ImVec2 canvasSize = ImGui::GetContentRegionAvail();

            int sizeMargin = int(ImGui::GetStyle().ItemSpacing.y);
            int maxGraphHeight = 300;
            int availableGraphHeight = (int(canvasSize.y) - sizeMargin) /*/ 2*/;
            int graphHeight = std::min(maxGraphHeight, availableGraphHeight);
            int legendWidth = 250;
            int graphWidth = int(canvasSize.x) - legendWidth;
            //gpuGraph.RenderTimings(graphWidth, legendWidth, graphHeight, frameOffset);
            cpuGraph.RenderTimings(graphWidth, legendWidth, graphHeight, frameOffset);
            if (graphHeight /** 2*/ + sizeMargin + sizeMargin < canvasSize.y) {
                ImGui::Columns(2);
                //size_t textSize = 50;
                ImGui::Checkbox("Stop profiling", &stopProfiling);
                //ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - textSize);
                ImGui::Checkbox("Colored legend text", &useColoredLegendText);
                ImGui::DragInt("Frame offset", &frameOffset, 1.0f, 0, 400);
                ImGui::NextColumn();

                ImGui::SliderInt("Frame width", &frameWidth, 1, 4);
                ImGui::SliderInt("Frame spacing", &frameSpacing, 0, 2);
                ImGui::Columns(1);
            }
            if (!stopProfiling)
                frameOffset = 0;
            gpuGraph.frameWidth = frameWidth;
            gpuGraph.frameSpacing = frameSpacing;
            gpuGraph.useColoredLegendText = useColoredLegendText;
            cpuGraph.frameWidth = frameWidth;
            cpuGraph.frameSpacing = frameSpacing;
            cpuGraph.useColoredLegendText = useColoredLegendText;

            ImGui::End();
        }

        bool stopProfiling;
        int frameOffset;
        ProfilerGraph cpuGraph;
        ProfilerGraph gpuGraph;
        int frameWidth;
        int frameSpacing;
        bool useColoredLegendText;
        using TimePoint = std::chrono::time_point<std::chrono::system_clock>;
        TimePoint prevFpsFrameTime;
        size_t fpsFramesCount;
        float avgFrameTime;
    };

    inline ProfilersWindow Profiler;

    inline std::vector< LegitProfiler::ProfilerTask> CpuTasks;
    inline std::vector< LegitProfiler::ProfilerTask> GpuTasks;

    inline void AddCpuTask(LegitProfiler::ProfilerTask&& task) {
        //task.endTime = GetElapsedFrameTimeSeconds();
        task.endTime = Inferno::Clock.GetFrameStartOffsetSeconds();
        CpuTasks.push_back(task);
    }

    inline void AddGpuTask(LegitProfiler::ProfilerTask&& task) {
        //task.endTime = GetElapsedFrameTimeSeconds();
        task.endTime = Inferno::Clock.GetFrameStartOffsetSeconds();
        GpuTasks.push_back(task);
    }
}
