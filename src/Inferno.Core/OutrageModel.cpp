#include "pch.h"
#include "OutrageModel.h"

namespace Inferno::Outrage {
    constexpr auto MAX_MODEL_TEXTURES = 35;

    string ReadModelString(StreamReader& r) {
        int mlen = r.ReadInt32();
        return r.ReadString(mlen);
    }

    // Gets the real center of a polygon and total area
    Tuple<Vector3, float> GetCentroid(span<Vector3> src) {
        if (src.size() < 3) return { Vector3::Zero, 1.0f };
        // First figure out the total area of this polygon
        auto normal = (src[1] - src[0]).Cross(src[2] - src[0]);
        auto totalArea = normal.Length() / 2;

        for (int i = 2; i < src.size() - 1; i++) {
            auto n = (src[i] - src[0]).Cross(src[i + 1] - src[0]);
            totalArea += n.Length() / 2;
        }

        // Now figure out how much weight each triangle represents 
        // to the overall polygon
        normal = (src[1] - src[0]).Cross(src[2] - src[0]);
        auto area = normal.Length() / 2; // copy of initial?

        // Get the center of the first polygon
        Vector3 center;
        for (int i = 0; i < 3; i++)
            center += src[i];

        center /= 3;

        Vector3 centroid = center * (area / totalArea);

        // Now do the same for the rest	
        for (int i = 2; i < src.size() - 1; i++) {
            normal = (src[i] - src[0]).Cross(src[i + 1] - src[0]);
            area = normal.Length() / 2;

            center += src[0] + src[i] + src[i + 1];
            center /= 3;

            centroid += center * (area / totalArea);
        }

        return { centroid, totalArea };
    }

    void ParseSubmodelProperties(Submodel& sm) {
        const auto& props = sm.Props;
        const auto len = props.length();

        if (len < 3)
            return;

        auto i = Seq::indexOf(sm.Props, '=').value_or(len);

        auto command = String::Trim(String::ToLower(props.substr(0, i + 1)));
        auto data = i == len ? "" : String::Trim(props.substr(i + 1));

        switch (String::Hash(command)) {
            case String::Hash("$rotate="):
            {
                auto spinRate = std::stof(data);
                if (spinRate <= 0 || spinRate > 20)
                    return; // bad data

                sm.SetFlag(SubmodelFlag::Rotate);
                sm.Rotation = 1.0f / spinRate;
                return;
            }

            case String::Hash("$jitter"):
                sm.SetFlag(SubmodelFlag::Jitter);
                return;

            case String::Hash("$shell"):
                sm.SetFlag(SubmodelFlag::Shell);
                return;

            case String::Hash("$facing"):
                sm.SetFlag(SubmodelFlag::Facing);
                return;

            case String::Hash("$frontface"):
                sm.SetFlag(SubmodelFlag::Frontface);
                return;

            case String::Hash("$thruster="):
            case String::Hash("$glow="):
            {
                auto split = String::Split(data, ',');
                if (split.size() != 4) return; // warn, invalid

                bool isGlow = String::Hash("$glow=") == String::Hash(command);
                sm.SetFlag(isGlow ? SubmodelFlag::Glow : SubmodelFlag::Thruster);
                sm.Glow.x = std::stof(split[0]);
                sm.Glow.y = std::stof(split[1]);
                sm.Glow.z = std::stof(split[2]);
                sm.GlowSize = std::stof(split[3]);
                return;
            }

            case String::Hash("$fov="):
            {
                // todo: FOV data
                return;
            }
            // monitors

            case String::Hash("$viewer"):
                sm.SetFlag(SubmodelFlag::Viewer);
                return;

            case String::Hash("$layer"):
                sm.SetFlag(SubmodelFlag::Layer);
                return;

            case String::Hash("$custom"):
                sm.SetFlag(SubmodelFlag::Custom);
                return;
        }
    }

    void UpdateMinMax(Submodel& sm) {
        Vector3 min = { 90000, 90000, 90000 };
        Vector3 max = { -90000, -90000, -90000 };

        for (auto& v : sm.Vertices) {
            min = Vector3::Min(min, v.Position);
            max = Vector3::Max(max, v.Position);
        }

        sm.Min = min;
        sm.Max = max;
    }

    void Postprocess(Submodel& sm) {
        // build angle matrices

        // check if parent equals self

        if (sm.NumKeyAngles == 0 && sm.HasFlag(SubmodelFlag::Rotate)) {
            fmt::print("Submodel is rotator without keyframe\n");
            sm.ClearFlag(SubmodelFlag::Rotate);
        }

        if (sm.NumKeyAngles == 0 && sm.HasFlag(SubmodelFlag::Turret)) {
            fmt::print("Submodel is turret without keyframe\n");
            sm.ClearFlag(SubmodelFlag::Turret);
        }

        if (sm.HasFlag(SubmodelFlag::Facing)) {
            List<Vector3> verts = Seq::map(sm.Vertices, [](const auto& v) { return v.Position; });
            auto [centroid, area] = GetCentroid(verts);
            sm.Radius = sqrt(area) / 2;
        };

        // todo: decode animations
    }

    Model Model::Read(StreamReader& r) {
        // can also load data from oof, but let's assume POFs
        if (r.ReadInt32() != MakeFourCC("PSPO"))
            throw Exception("Not a model file");

        Model pm{};
        pm.Version = r.ReadInt32();

        if (pm.Version < 18)
            pm.Version *= 100; // fix old version

        if (pm.Version < MIN_OBJFILE_VERSION || pm.Version > OBJFILE_VERSION)
            throw Exception("Bad version");

        pm.MajorVersion = pm.Version / 100;

        if (pm.MajorVersion >= 21)
            pm.SetFlag(ModelFlag::LightmapRes);

        bool timed = false;
        if (pm.MajorVersion >= 22) {
            timed = true;
            pm.SetFlag(ModelFlag::Timed);
        }

        while (!r.EndOfStream()) {
            auto id = r.ReadInt32();
            auto len = r.ReadInt32();
            auto chunkStart = r.Position();
            if (len <= 0) throw Exception("bad chunk length");

            switch (id) {
                case MakeFourCC("OHDR"): // POF file header
                {
                    auto submodels = r.ReadInt32Checked(1000, "bad submodel count");
                    pm.Submodels.reserve(submodels);
                    pm.Radius = r.ReadFloat();
                    pm.Min = r.ReadVector3();
                    pm.Max = r.ReadVector3();

                    int detail = r.ReadInt32();
                    for (int i = 0; i < detail; i++)
                        r.ReadInt32(); // Skip details

                    break;
                }

                case MakeFourCC("TXTR"): // Texture filename list
                {
                    auto count = r.ReadInt32Checked(MAX_MODEL_TEXTURES, "exceeded max model textures");
                    pm.Textures.resize(count);

                    for (auto& tex : pm.Textures)
                        tex = ReadModelString(r) + ".ogf";

                    break;
                }

                case MakeFourCC("SOBJ"): // Subobject header
                {
                    auto& sm = pm.Submodels.emplace_back();

                    r.ReadInt32Checked((int)pm.Submodels.size(), "too many submodels");
                    sm.Parent = r.ReadInt32();
                    sm.Normal = r.ReadVector3();

                    /*auto d =*/ r.ReadFloat();
                    sm.Point = r.ReadVector3();
                    sm.Offset = r.ReadVector3();
                    sm.Radius = r.ReadFloat();

                    sm.TreeOffset = r.ReadInt32();
                    sm.DataOffset = r.ReadInt32();

                    if (pm.Version > 1805)
                        sm.GeometricCenter = r.ReadVector3();

                    sm.Name = ReadModelString(r);
                    sm.Props = ReadModelString(r);

                    try {
                        ParseSubmodelProperties(sm);
                    }
                    catch (const std::exception&) {
                        throw Exception(fmt::format("Error parsing submodel props: {}", sm.Props));
                    }

                    sm.MovementType = r.ReadInt32();
                    sm.MovementAxis = r.ReadInt32();

                    // skip freespace chunks
                    auto chunks = r.ReadInt32();
                    for (int i = 0; i < chunks; i++)
                        r.ReadInt32();

                    sm.Vertices.resize(r.ReadInt32Checked(2500, "too many verts"));

                    for (auto& vert : sm.Vertices)
                        vert.Position = r.ReadVector3();

                    for (auto& vert : sm.Vertices)
                        vert.Normal = r.ReadVector3();

                    if (pm.MajorVersion >= 23) {
                        for (auto& vert : sm.Vertices) {
                            vert.Alpha = r.ReadFloat();
                            if (vert.Alpha < 0.99f)
                                pm.SetFlag(ModelFlag::Alpha);
                        }
                    }

                    sm.Faces.resize(r.ReadInt32Checked(20000, "too many faces"));

                    for (auto& face : sm.Faces) {
                        face.Normal = r.ReadVector3();
                        face.Vertices.resize(r.ReadInt32Checked(100, "bad nverts"));

                        bool textured = (bool)r.ReadInt32();
                        if (textured)
                            face.TexNum = (short)r.ReadInt32();
                        else
                            face.Color = r.ReadRGB();

                        for (auto& v : face.Vertices) {
                            v.Index = (short)r.ReadInt32();
                            v.UV.x = r.ReadFloat();
                            v.UV.y = r.ReadFloat();
                        }

                        // Lightmap stuff we don't care about
                        if (pm.MajorVersion >= 21) {
                            /*auto xdiff =*/ r.ReadFloat();
                            /*auto ydiff =*/ r.ReadFloat();
                        }
                    }

                    break;
                }

                case MakeFourCC("GPNT"): // gun points
                {
                    pm.Guns.resize(r.ReadInt32Checked(100, "bad number of guns"));

                    for (auto& gun : pm.Guns) {
                        // In Version 19.08 and beyond, gunpoints are associated to their parent object.
                        if (pm.Version >= 19'08)
                            gun.Parent = r.ReadInt32();

                        gun.Point = r.ReadVector3();
                        gun.Normal = r.ReadVector3();
                    }
                    break;
                }

                case MakeFourCC("WBAT"): // weapon batteries
                {
                    auto num = r.ReadInt32Checked(100, "bad number of weapon batteries");
                    pm.WeaponBatteries.resize(num);

                    for (auto& battery : pm.WeaponBatteries) {
                        auto gunpoints = r.ReadInt32Checked(100, "bad number of weapon battery gunpoints");
                        battery.Gunpoints.resize(gunpoints);
                        for (auto& gp : battery.Gunpoints)
                            gp = (int8)r.ReadInt32();

                        auto turrets = r.ReadInt32Checked(100, "bad turret num");
                        battery.Turrets.resize(turrets);
                        for (auto& turret : battery.Turrets)
                            turret = (ushort)r.ReadInt32();
                    }

                    break;
                }

                case MakeFourCC("PANI"): // positional animation data
                {
                    int nframes = timed ? 0 : r.ReadInt32();

                    for (auto& sm : pm.Submodels) {
                        if (timed) {
                            sm.NumKeyPos = r.ReadInt32();
                            sm.PosTrackMin = r.ReadInt32();
                            sm.PosTrackMax = r.ReadInt32();

                            // clamp
                            if (sm.PosTrackMin < pm.FrameMin)
                                pm.FrameMin = sm.PosTrackMin;

                            if (sm.PosTrackMax < pm.FrameMax)
                                pm.FrameMax = sm.PosTrackMax;

                            //int numTicks = sm.PosTrackMax - sm.PosTrackMin;

                            // lookup
                            //if (numTicks > 0)
                            //    sm.TickPosRemap.resize(numTicks * 2);
                        }
                        else {
                            sm.NumKeyPos = nframes;
                        }

                        for (auto& key : sm.Keyframes) {
                            if (timed)
                                key.PosStartTime = r.ReadInt32();

                            key.Position = r.ReadVector3();
                        }
                    }

                    break;
                }

                case MakeFourCC("RANI"): // rotational animation data
                case MakeFourCC("ANIM"): // animation data
                {
                    int nframes = 0;

                    if (!timed) {
                        nframes = r.ReadInt32();
                        // pm.num key angles = nframes
                    }

                    for (auto& sm : pm.Submodels) {
                        if (timed) {
                            sm.NumKeyAngles = r.ReadInt32();
                            sm.RotTrackMin = r.ReadInt32();
                            sm.RotTrackMax = r.ReadInt32();

                            if (sm.RotTrackMin < pm.FrameMin)
                                pm.FrameMin = sm.RotTrackMin;

                            if (sm.RotTrackMax > pm.FrameMax)
                                pm.FrameMax = sm.RotTrackMax;
                        }
                        else {
                            sm.NumKeyAngles = nframes;
                        }

                        if (sm.NumKeyAngles > 10000)
                            throw Exception("Bad number of key angles");

                        sm.Keyframes.resize(sm.NumKeyAngles /*+ 1*/); // why the +1?

                        if (timed) {
                            //int numTicks = sm.RotTrackMax - sm.RotTrackMin;

                            // todo: Some kind of lookup...
                            //if (numTicks > 0) {
                            //    sm.TickAngleRemap.resize(numTicks * 2);
                            //}
                        }

                        for (auto& keyframe : sm.Keyframes) {
                            if (timed)
                                keyframe.RotStartTime = r.ReadInt32();

                            keyframe.Axis = r.ReadVector3();
                            keyframe.Axis.Normalize();
                            keyframe.Angle = r.ReadInt32();

                            // todo: some stuff here about keyframe angle wrapping?
                        }
                    }

                    break;
                }

                case MakeFourCC("GRND"): // ground plane info
                {
                    auto slots = r.ReadInt32Checked(100, "bad ground plane count");
                    pm.GroundPlanes.resize(slots);
                    for (auto& plane : pm.GroundPlanes) {
                        plane.Parent = r.ReadInt32();
                        plane.Point = r.ReadVector3();
                        plane.Normal = r.ReadVector3();
                    }
                    break;
                }

                case MakeFourCC("ATCH"): // attach points
                {
                    auto points = r.ReadInt32Checked(100, "Bad number of attach points");
                    pm.AttachPoints.resize(points);
                    pm.AttachPointsUsed.resize(points);

                    for (auto& point : pm.AttachPoints) {
                        point.Parent = r.ReadInt32();
                        point.Point = r.ReadVector3();
                        point.Normal = r.ReadVector3();
                    }

                    break;
                }

                case MakeFourCC("NATH"): // attach normals
                {
                    auto normalCount = r.ReadInt32();
                    if (pm.AttachPoints.size() != normalCount)
                        throw Exception("Invalid ATTACH normals - total number doesn't match number of attach points");

                    for (int i = 0; i < normalCount; i++) {
                        pm.AttachPoints[i].Point = r.ReadVector3();
                        pm.AttachPoints[i].Normal = r.ReadVector3();
                        pm.AttachPointsUsed[i] = true;
                    }

                    break;
                }

                case MakeFourCC("PINF"): // POF file information, like command line, etc
                {
                    //List<ubyte> buffer(len);
                    //r.ReadBytes(buffer);
                    break;
                }

                case MakeFourCC("SPCL"): // Only contains dummy data
                {
                    //List<ubyte> buffer(len);
                    //r.ReadBytes(buffer);
                    break;
                }

                default:
                    fmt::print("unknown chunk id {}\n", id);
                    break;
            }

            r.Seek(chunkStart + len); // seek to next chunk (prevents read errors due to individual chunks)
        }

        for (auto& submodel : pm.Submodels) {
            UpdateMinMax(submodel);
            Postprocess(submodel);
        }

        return pm;
    }
}