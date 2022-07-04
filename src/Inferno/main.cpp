#include "pch.h"
#include "Shell.h"
#include "Application.h"
#include "FileSystem.h"
#include "SoundSystem.h"
#include "Resources.h"
#include "Editor/Editor.h"
#include "Mission.h"
#include "HogFile.h"
#include "Settings.h"
#include "OutrageBitmap.h"
#include "Hog2.h"

using namespace Inferno;

Quaternion RotationBetweenVectors(const Vector3& v1, const Vector3& v2) {
    auto cosTheta = v1.Dot(v2);
    auto k = std::sqrt(v1.LengthSquared() * v2.LengthSquared());
    Quaternion q(v1.Cross(v2), cosTheta + k);
    q.Normalize();
    return q;
}

void PrintWeaponInfo(const Weapon& weapon) {
    SPDLOG_INFO("   Damage: {}, {}, {}, {}, {}", weapon.Damage[0], weapon.Damage[1], weapon.Damage[2], weapon.Damage[3], weapon.Damage[4]);
    SPDLOG_INFO("   Speed: {}, {}, {}, {}, {}", weapon.Speed[0], weapon.Speed[1], weapon.Speed[2], weapon.Speed[3], weapon.Speed[4]);
    SPDLOG_INFO("   Energy: {}", weapon.EnergyUsage);
    SPDLOG_INFO("   Delay: {}", weapon.FireDelay);
    SPDLOG_INFO("   Mass: {}", weapon.Mass);
    SPDLOG_INFO("   Size: {}", weapon.BlobSize);
    SPDLOG_INFO("   DPS: {}", 1.0f / weapon.FireDelay * weapon.Damage[0]);
}

void PrintWeaponInfo() {
    //Resources::LoadDescent2();

    int weapons[] = {
        0, 1, 2, 3,
        11, 12, 13, 14,
        30, 31,
        32, 33, 34, 35 };
    string names[] = {
        "Laser1", "Laser2", "Laser3", "Laser4",
        "Vulcan", "Spreadfire", "Plasma", "Fusion",
        "Laser5", "Laser6",
        "Gauss", "Helix", "Phoenix", "Omega" };
    int j = 0;
    for (auto& i : weapons) {
        SPDLOG_INFO("Weapon info {}", names[j++]);
        PrintWeaponInfo(Resources::GameData.Weapons[i]);
    }
}

void QuaternionTests() {
    using namespace DirectX;
    {
        Vector3 v1 = { 0, 0, 1 }; // Camera forward
        Vector3 v2 = Vector3::UnitX; // X axis

        auto q = RotationBetweenVectors(v1, v2);
        Quaternion qinv;
        q.Inverse(qinv);

        auto vt = Vector3::Transform(v1, q);
        // should be close to X axis
        auto rotation = Quaternion::CreateFromYawPitchRoll(0.25, 0.5, 0);
        auto vtr = Vector3::Transform(vt, rotation);
        // rotate back
        auto vtf = Vector3::Transform(vtr, qinv);

        auto vtCombined = Vector3::Transform(v1, q * rotation * qinv);
    }

    {
        Vector3 position = { 0, 6, 6 };
        Vector3 target = { 0, 5, 5 };
        Vector3 offset = target - position;

        auto q = RotationBetweenVectors(offset, Vector3::UnitX);
        Quaternion qinv;
        q.Inverse(qinv);
        // (10, 0, 10) * 90 yaw -> (10, 0, -10) (Y axis rotation)
        // (10, 0, 10) * 90 pitch -> (10, -10, 0) (X axis rotation)
        // (10, 0, 10) * 90 roll -> (0, 10, 10) (Z Axis rotation)
        auto rotation = Quaternion::CreateFromYawPitchRoll(XM_PIDIV2, 0, 0);
        rotation.Normalize();

        auto vtx = Vector3::Transform(Vector3::UnitX, rotation);
        auto vty = Vector3::Transform(Vector3::UnitY, rotation);
        auto vtz = Vector3::Transform(Vector3::UnitZ, rotation);

        auto matrix = Matrix::CreateRotationY(XM_PIDIV2);
        auto mvtx = Vector3::Transform(offset, matrix);
        auto mvty = Vector3::Transform(offset, matrix);
        auto mvtz = Vector3::Transform(offset, matrix);

        auto rotated = Vector3::Transform(offset, rotation);
        auto aligned = Vector3::Transform(offset, q);
        auto rotated2 = Vector3::Transform(offset, q * rotation * qinv);
        auto final = rotated2 + target;
    }

    {
        Vector3 position = { 0, 6, 6 };
        Vector3 target = { 0, 5, 5 };
        Vector3 up = { 1, 0, 0 };
        Vector3 offset = target - position;

        auto q = RotationBetweenVectors(offset, Vector3::UnitX);
        Quaternion qinv;
        q.Inverse(qinv);

        auto yaw = Quaternion::CreateFromAxisAngle(up, 0);
        auto pitch = Quaternion::CreateFromAxisAngle(up.Cross(offset), XM_PIDIV2);
        auto result = Vector3::Transform(offset, yaw * pitch) + target;
        auto up2 = Vector3::Transform(up, pitch);
    }

    {
        // Interpolate
        Vector3 p0 = { 1000, 0 , 0 };
        Vector3 p1 = { 0, 1000 , 0 };
        Vector3 v0, v1;
        p0.Normalize(v0);
        p1.Normalize(v1);
        //auto q0 = RotationBetweenVectors(v0, Vector3::Up);
        //auto q1 = RotationBetweenVectors(v1, Vector3::Up);
        auto q1 = RotationBetweenVectors(v0, v1);
        auto id = Quaternion::Identity;

        for (int t = 0; t <= 10; t++) {
            auto qlerp = Quaternion::Lerp(Quaternion::Identity, q1, t * 0.1f);
            auto p = Vector3::Transform(p0, qlerp);
            SPDLOG_INFO("T: {} Point: {:.1f}, {:.1f}, {:.1f}", t, p.x, p.y, p.z);
        }
    }

    {
        // Interpolate
        Vector3 p0 = { 819.91f, -8842.0f, -181.97f };
        Vector3 p1 = { 776.02f, -9456.5f, 519.5f };
        Vector3 center = { 776.02f, -8746.5f, 519.5f };
        auto v0 = p0 - center; // shift points to origin to get vectors
        auto v1 = p1 - center;
        v0.Normalize();
        v1.Normalize();

        auto rotation = RotationBetweenVectors(v0, v1);

        for (int t = 0; t <= 10; t++) {
            auto qlerp = Quaternion::Lerp(Quaternion::Identity, rotation, t * 0.1f);
            auto p = Vector3::Transform(p0 - center, qlerp) + center; // reposition to origin before rotating
            SPDLOG_INFO("T: {} Point: {:.1f}, {:.1f}, {:.1f}", t, p.x, p.y, p.z);
        }
    }
}

void PrintRobotInfo() {
    //Resources::LoadDescent2();
    //Resources::LoadVertigo();

    SPDLOG_INFO("Robot, HP");

    for (uint i = 0; i < Resources::GameData.Robots.size(); i++) {
        auto& robot = Resources::GetRobotInfo(i);
        auto name = Resources::GetRobotName(i);
        SPDLOG_INFO("{}, {:.0f}", name, robot.Strength);
    }
}

//template <class T>
//void draw(const T& x, size_t position) {
//    std::cout << string(position, ' ') << x << "\n";
//}

void TestContext() {
    using namespace Inferno::Editor;

    Level lvl;
    lvl.Vertices.push_back({ 0, 0, 0 });
    lvl.Vertices.push_back({ 10, 0, 0 });
    lvl.Vertices.push_back({ 0, 10, 0 });

    EditorHistory ctx(&lvl, 10);
    assert(!ctx.Dirty());
    assert(ctx.Snapshots() == 1);
    ctx.Undo(); // ensure the first snapshot isn't poppable
    assert(ctx.Snapshots() == 1);

    lvl.Vertices[0] = { 6, 6, 6 };
    ctx.SnapshotLevel("Move1");

    ctx.Undo();
    assert(!ctx.Dirty());
    ctx.Redo();
    assert(ctx.Dirty());

    lvl.Vertices[0] = { 10, 10, 10 };
    ctx.SnapshotLevel("Move2");

    lvl.Vertices[0] = { 20, 20, 20 };
    ctx.SnapshotLevel("Move3");

    ctx.Undo();

    assert(lvl.Vertices[0] == Vector3(10, 10, 10));

    ctx.Redo();
    assert(lvl.Vertices[0] == Vector3(20, 20, 20));

    ctx.Undo();
    ctx.Undo(); // at 1

    lvl.Vertices[0] = { 1, 1, 1 };
    ctx.SnapshotLevel("Move4");
    assert(ctx.Snapshots() == 3);

    // fill the undo buffer
    for (int i = 0; i < 15; i++) {
        ctx.SnapshotLevel("Snapshot");
    }

    ctx.UpdateCleanSnapshot();
    assert(!ctx.Dirty());

    lvl.Vertices[0] = { 1, 1, 1 };
    ctx.SnapshotLevel("Snapshot"); // make sure the latest snapshot works when full
    assert(lvl.Vertices[0] == Vector3(1, 1, 1));

    assert(ctx.Dirty());
    ctx.Undo();
    assert(!ctx.Dirty());

}

void TestSegID() {
    SegID id{};
    id--;
    assert(id == SegID(0)); // don't allow increment going negative

    id += SegID(10);
    assert(id == SegID(10));

    id -= SegID(5);
    assert(id == SegID(5));

    id++;
    assert(id == SegID(6));
}

void DumpOgfHeaders() {
    for (auto& entry : Resources::Descent3Hog->Entries) {
        if (!entry.name.ends_with(".ogf")) continue;
        auto data = Resources::Descent3Hog->ReadEntry(entry.name);
        StreamReader r(data);
        auto ogf = OutrageBitmap::Read(r);
        string type = ogf.Type == 122 ? "1555" : (ogf.Type == 121 ? "4444" : "Unknown");
        //std::cout << fmt::format("{}, {}, {}, {}, {}, {}, {}\n", ogf.Name, ogf.Width, ogf.Height, ogf.BitsPerPixel, ogf.MipLevels, ogf.Type, type);
    }
}

int main() {
    //TestContext();
    //TestSegID();
    //QuaternionTests();

    // https://github.com/gabime/spdlog/wiki/3.-Custom-formatting#pattern-flags
    spdlog::set_pattern("[%M:%S.%e] [%^%l%$] [TID:%t] [%s:%#] %v");
    std::srand((uint)std::time(nullptr)); // seed c-random


    try {
        Shell shell;
        Application app;
        //CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        Settings::Load();
        FileSystem::Init();
        Resources::Init();

        //Resources::MountD3Hog("d3.hog");
        //DumpOgfHeaders();

        //PrintWeaponInfo();
        //PrintRobotInfo();
        shell.Show(&app, 1024, 768);
        Settings::Save();

        //CoUninitialize();
    }
    catch (const std::exception& e) {
        ShowErrorMessage(e);
    }

    return 0;
}
