#pragma once

#include "Pig.h"
#include "WindowBase.h"
#include "Concurrent.h"

namespace Inferno::Editor {
    enum class FilterGroup : uint64 {
        None = 0,
        GrayRock = BIT(0),
        BrownRock = BIT(1),
        RedRock = BIT(2),
        GreenRock = BIT(3),
        YellowRock = BIT(4),
        BlueRock = BIT(5),
        Ice = BIT(6),
        Stones = BIT(7),
        Grass = BIT(8),
        Sand = BIT(9),
        Lava = BIT(10),
        Water = BIT(11),
        Steel = BIT(12),
        Concrete = BIT(13),
        Brick = BIT(14),
        Tarmac = BIT(15),
        Wall = BIT(16),
        Floor = BIT(17),
        Ceiling = BIT(18),
        Grate = BIT(19),
        Fan = BIT(20),
        Light = BIT(21),
        Energy = BIT(22),
        Forcefield = BIT(23),
        Sign = BIT(24),
        Switch = BIT(25),
        Tech = BIT(26),
        Door = BIT(27),
        Label = BIT(28),
        Monitor = BIT(29),
        Stripes = BIT(30),
        Moving = BIT(31),
        All = 0xFFFFFFFF
    };

    struct TextureBrowserState {
        bool GrayRock, BrownRock, RedRock, GreenRock, YellowRock, BlueRock;
        bool Ice, Stones, Grass, Sand, Lava, Water;
        bool Steel, Concrete, Brick, Tarmac, Wall, Floor, Ceiling;
        bool Grate, Fan, Door;
        bool Light, Energy, ForceField, Tech, Switches;
        bool Labels, Monitors, Stripes;

        void SelectRock(bool state) {
            GrayRock = BrownRock = RedRock = GreenRock = YellowRock = BlueRock = state;
        }

        bool SelectRock() {
            return GrayRock && BrownRock && RedRock && GreenRock && YellowRock && BlueRock;
        }

        void SelectNatural(bool state) {
            Ice = Stones = Grass = Sand = Lava = Water = state;
        }

        bool SelectNatural() {
            return Ice && Stones && Grass && Sand && Lava && Water;
        }

        void SelectBuilding(bool state) {
            Steel = Concrete = Brick = Tarmac = Wall = Floor = Ceiling = state;
        }

        bool SelectBuilding() {
            return Steel && Concrete && Brick && Tarmac && Wall && Floor && Ceiling;
        }

        void SelectMisc(bool state) { Grate = Fan = Door = state; }
        bool SelectMisc() { return Grate && Fan && Door; }

        void SelectTechnical(bool state) { Light = Energy = ForceField = Tech = Switches = state; }
        bool SelectTechnical() { return Light && Energy && ForceField && Tech && Switches; }

        void SelectSigns(bool state) { Labels = Monitors = Stripes = state; }
        bool SelectSigns() { return Labels && Monitors && Stripes; }

        FilterGroup GetState() const {
            FilterGroup state = FilterGroup::None;
            if (GrayRock) state |= FilterGroup::GrayRock;
            if (BrownRock) state |= FilterGroup::BrownRock;
            if (GreenRock) state |= FilterGroup::GreenRock;
            if (YellowRock) state |= FilterGroup::YellowRock;
            if (BlueRock) state |= FilterGroup::BlueRock;
            if (RedRock) state |= FilterGroup::RedRock;

            if (Ice) state |= FilterGroup::Ice;
            if (Stones) state |= FilterGroup::Stones;
            if (Grass) state |= FilterGroup::Grass;
            if (Sand) state |= FilterGroup::Sand;
            if (Lava) state |= FilterGroup::Lava;
            if (Water) state |= FilterGroup::Water;

            if (Steel) state |= FilterGroup::Steel;
            if (Concrete) state |= FilterGroup::Concrete;
            if (Brick) state |= FilterGroup::Brick;
            if (Tarmac) state |= FilterGroup::Tarmac;
            if (Wall) state |= FilterGroup::Wall;
            if (Floor) state |= FilterGroup::Floor;
            if (Ceiling) state |= FilterGroup::Ceiling;

            if (Grate) state |= FilterGroup::Grate;
            if (Fan) state |= FilterGroup::Fan;
            if (Door) state |= FilterGroup::Door;

            if (Light) state |= FilterGroup::Light;
            if (Energy) state |= FilterGroup::Energy;
            if (ForceField) state |= FilterGroup::Forcefield;
            if (Tech) state |= FilterGroup::Tech;
            if (Switches) state |= FilterGroup::Switch;

            if (Labels) state |= FilterGroup::Label;
            if (Monitors) state |= FilterGroup::Monitor;
            if (Stripes) state |= FilterGroup::Stripes;

            return state;
        }
    };

    class TextureBrowserUI : public WindowBase {
        TextureBrowserState _state{};
        FilterGroup _filter = FilterGroup::None;
        List<LevelTexID> _textureIds;
        bool _showInUse = true;
        bool _showEverything = false;
    public:
        TextureBrowserUI();

    protected:
        void OnUpdate() override;
        void UpdateTextureList(FilterGroup, bool loadMaterials);

    private:
        void DrawFilter();
    };

    void LoadTextureFilter(const Level&);
}

