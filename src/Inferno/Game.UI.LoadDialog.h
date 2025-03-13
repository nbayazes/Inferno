#pragma once
#include "Game.Save.h"
#include "Game.UI.Controls.h"

namespace Inferno::UI {
    class SaveGameControl : public ControlBase {
        SaveGameInfo _save;
        string _playTime;
        string _header;
        string _lives;
        string _level;
        string _dateTime;
        float _livesWidth = 0;
        float _playTimeWidth = 0;
        float _fontHeight = 0;
        float _difficultyWidth = 0;
        float _titleWidth = 0;

    public:
        SaveGameControl(const SaveGameInfo& save) : _save(save) {
            Padding = Vector2(SMALL_CONTROL_HEIGHT / 2, SMALL_CONTROL_HEIGHT / 2);
            ActionSound = MENU_SELECT_SOUND;

            auto time = _save.totalTime;
            auto hours = int(time / 3600);
            time -= hours * 3600;

            if (hours > 0)
                _playTime = fmt::format("play time: {}:{:02}:{:02}", hours, int(time) / 60, int(time) % 60);
            else
                _playTime = fmt::format("play time: {:02}:{:02}", int(time) / 60, int(time) % 60);

            auto size = MeasureString(_playTime, FontSize::Small);
            _playTimeWidth = size.x;
            _fontHeight = size.y;

            //_header = fmt::format("{} - Level {}", _save.missionName, _save.levelNumber);
            //_header = _save.missionName;
            _titleWidth = MeasureString(_header, FontSize::Small).x;
            _header = _save.autosave ? fmt::format("{} - AUTOSAVE", _save.missionName) : _save.missionName;

            _lives = fmt::format("lives: {}", _save.lives);
            _livesWidth = MeasureString(_lives, FontSize::Small).x;

            if (_save.levelNumber < 0)
                _level = fmt::format("Secret Level {}: {}", -_save.levelNumber, _save.levelName);
            else
                _level = fmt::format("Level {}: {}", _save.levelNumber, _save.levelName);

            _difficultyWidth = MeasureString(DifficultyToString(_save.difficulty), FontSize::Small).x;
            _dateTime = FormatTimestamp(_save.timestamp);
        }

        bool OnConfirm() override {
            Sound::Play2D(SoundResource{ ActionSound });
            LoadSave(_save);
            return true;
        }

        void RequestDelete() const {
            auto confirmDialog = make_unique<ConfirmDialog>("Delete save?");
            confirmDialog->Layer = Layer + 1;

            confirmDialog->CloseCallback = [this](CloseState state) {
                if (state == CloseState::Accept) {
                    DeleteSave(_save);
                    if (SaveChangedHandler) SaveChangedHandler();
                }
            };

            ShowScreen(std::move(confirmDialog));
        }

        void OnUpdate() override {
            if (!Focused) return;

            if (Input::OnKeyPressed(Input::Keys::Delete) || Input::OnControllerButtonPressed(SDL_GAMEPAD_BUTTON_NORTH)) {
                Sound::Play2D(SoundResource{ MENU_SELECT_SOUND });
                RequestDelete();
            }

            if (Input::OnKeyPressed(Input::Keys::S) || Input::OnControllerButtonPressed(SDL_GAMEPAD_BUTTON_WEST)) {
                if (_save.autosave && !_save.saveFilePath.empty()) {
                    _save.autosave = false;
                    WriteSave(_save.saveFilePath, _save);
                    Sound::Play2D({ MENU_SELECT_SOUND });
                    if (SaveChangedHandler) SaveChangedHandler();
                }
            }
        }

        Action SaveChangedHandler;

        void OnDraw() override {
            Render::DrawTextInfo dti;
            dti.Font = FontSize::Small;
            auto scale = GetScale();

            //{
            //    // Background
            //    Render::CanvasBitmapInfo cbi;
            //    //cbi.Position = ScreenPosition;
            //    //cbi.Size = ScreenSize;
            //    //const auto border = Vector2(1, 1) * scale;
            //    //cbi.Position = ScreenPosition + border;
            //    //cbi.Size = ScreenSize - border * 2;
            //    cbi.Position = ScreenPosition;
            //    cbi.Size = ScreenSize;
            //    cbi.Texture = Render::Materials->White().Handle();
            //    cbi.Color = ACCENT_COLOR;
            //    Render::UICanvas->DrawBitmap(cbi, Layer);

            //    //cbi.Position = ScreenPosition + Vector2(1, 1) * scale;
            //    //cbi.Size = ScreenSize - Vector2(2, 2) * scale;
            //    cbi.Position = ScreenPosition + Vector2(0, 1) * scale;
            //    cbi.Size = ScreenSize - Vector2(0, 2) * scale;
            //    cbi.Texture = Render::Materials->White().Handle();
            //    cbi.Color = Color(0, 0, 0, 1);
            //    Render::UICanvas->DrawBitmap(cbi, Layer);
            //}

            //{
            //    dti.Position.x = ScreenPosition.x + ScreenSize.x - (Padding.x - _playTimeWidth) * scale;
            //    dti.Position.y = ScreenPosition.y + ScreenSize.y - (Padding.y - _fontHeight) * scale;

            //    Render::UICanvas->DrawRaw(_playTime, dti, Layer + 1);
            //}

            {
                const auto leftAlign = ScreenPosition.x + Padding.x * scale;

                // Header
                dti.Position.x = leftAlign;
                dti.Position.y = ScreenPosition.y + Padding.y * scale + 5 * scale;
                dti.Color = Focused ? GOLD_TEXT_GLOW : WHITE_TEXT;
                Render::UICanvas->DrawRaw(_header, dti, Layer + 1);

                //if (_save.autosave) {
                //    //dti.Color = Focused ? GREEN_TEXT_GLOW : GREEN_TEXT;
                //    dti.Position.x += (_titleWidth + 5) * scale;
                //    Render::UICanvas->DrawRaw("[AUTOSAVE]", dti, Layer + 1);
                //}

                // Difficulty
                dti.Position.x = ScreenPosition.x + ScreenSize.x - (Padding.x + _difficultyWidth) * scale;
                if (_save.difficulty == DifficultyLevel::Insane)
                    dti.Color = Focused ? INSANE_TEXT_FOCUSED : INSANE_TEXT;
                Render::UICanvas->DrawRaw(DifficultyToString(_save.difficulty), dti, Layer + 1);

                // row 2
                dti.Color = Focused ? GOLD_TEXT : GREY_TEXT;

                dti.Position.x = leftAlign;
                dti.Position.y += 15 * scale;
                Render::UICanvas->DrawRaw(_level, dti, Layer + 1);
                //Render::UICanvas->DrawRaw(_save.levelName, dti, Layer + 1);

                dti.Position.x = ScreenPosition.x + ScreenSize.x - (Padding.x + _livesWidth) * scale;
                Render::UICanvas->DrawRaw(_lives, dti, Layer + 1);

                // row 3
                dti.Position.x = leftAlign;
                dti.Position.y += 15 * scale;
                Render::UICanvas->DrawRaw(_dateTime, dti, Layer + 1);

                dti.Position.x = ScreenPosition.x + ScreenSize.x - (Padding.x + _playTimeWidth) * scale;
                Render::UICanvas->DrawRaw(_playTime, dti, Layer + 1);
            }
        }
    };

    // Loads saved games
    class LoadDialog : public DialogBase {
        ListBox2* _saveList;
        static constexpr auto ROW_HEIGHT = SMALL_CONTROL_HEIGHT * 5;
        static constexpr auto VISIBLE_ROWS = 6;
        Label* _footer = nullptr;

        //void OnSavesChanged() {
        //    RefreshSaveList();
        //}

        void RefreshSaveList() {
            auto saves = ReadAllSaves();
            _saveList->Children.clear();

            for (auto& save : saves) {
                auto ctrl = _saveList->AddChild<SaveGameControl>(save);
                ctrl->SaveChangedHandler = std::bind_front(&LoadDialog::RefreshSaveList, this);
            }
        }

    public:
        LoadDialog() : DialogBase("Load Game") {
            //auto visibleRows = int((Size.y - DIALOG_PADDING - DIALOG_HEADER_PADDING) / rowHeight);

            Size.x = 600;
            Size.y = VISIBLE_ROWS * ROW_HEIGHT + DIALOG_PADDING + DIALOG_HEADER_PADDING + 20;

            auto saveList = AddChild<ListBox2>(VISIBLE_ROWS, Size.x - DIALOG_PADDING * 3);
            saveList->Size.x = Size.x - 20 * 2;
            saveList->Size.y = VISIBLE_ROWS * ROW_HEIGHT;
            saveList->Position.y = DIALOG_HEADER_PADDING;
            saveList->Position.x = 20;
            saveList->RowHeight = ROW_HEIGHT;
            _saveList = saveList;

            //saveList->AddChild<SaveGameControl>(SaveGameInfo());
            RefreshSaveList();
        }

        void OnDraw() override {
            DialogBase::OnDraw();

            if (!_saveList) return;

            for (size_t row = 0; row < _saveList->GetVisibleItemCount(); row++) {
                if (row > 0 && row < _saveList->GetVisibleItemCount()) {
                    Render::CanvasBitmapInfo cbi;
                    cbi.Position = _saveList->ScreenPosition;
                    cbi.Position.y += row * ROW_HEIGHT * GetScale() + 2 * GetScale();
                    //cbi.Position.x += DIALOG_PADDING * GetScale();
                    //cbi.Position.y += row * SMALL_CONTROL_HEIGHT * 4 * GetScale() - 2 * GetScale();

                    cbi.Size.x = _saveList->ScreenSize.x - GetScale() * 4;
                    cbi.Size.y = 2 * GetScale();

                    cbi.Texture = Render::Materials->White().Handle();
                    cbi.Color = Color(0.15f, 0.15f, 0.15f);
                    Render::UICanvas->DrawBitmap(cbi, Layer + 1);
                }
            }

            Render::DrawTextInfo dti;
            dti.Font = FontSize::Small;
            dti.Color = IDLE_BUTTON;
            dti.HorizontalAlign = AlignH::Center;
            dti.Position.y = ScreenPosition.y + ScreenSize.y - 22 * GetScale();
            //dti.Position = ScreenPosition + Vector2(DIALOG_PADDING + 30, ScreenSize.y - 22 * GetScale());
            Render::UICanvas->DrawRaw("del to delete, S to keep autosave", dti, Layer);
        }
    };
}
