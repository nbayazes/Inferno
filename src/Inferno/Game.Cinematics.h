#pragma once
#include "Camera.h"
#include "Utility.h"

namespace Inferno {
    //enum class CinematicTransition {
    //    None,
    //    In,
    //    Out,
    //    InOut,
    //};

    struct CinematicInfo {
        float Duration;
        bool Letterbox = true; // Add bars to the top and bottom of the screen
        bool FadeIn = false;
        bool FadeOut = false;
        Color FadeColor;

        // Range that the player is able to skip the cinematic
        //NumericRange<float> SkipRange = { 0.0f, 1.0f };
        bool Skippable = false;

        bool MoveObjectToEndOfPathOnSkip = false;

        // The active camera will track this object if it is alive.
        // Takes priority over the target vector
        ObjRef TargetObject;

        // Target position that the camera points towards
        Vector3 Target;

        //List<Vector3> TargetPath; // Target will move along this path over the duration
        List<Vector3> CameraPath; // Camera will move along this path over the duration

        string Text;
        int TextMode; // Static, Wipe, Fade In/Out
        NumericRange<float> TextRange = { 0.0f, 1.0f }; // Range that text is visible
    };

    void StartCinematic(CinematicInfo& info, float duration);

    // Cinematics disable input for the duration and make the player invulnerable.
    // Pressing escape will skip the cinematic if enabled

    //void ShowCinematicText(string_view& text, int mode);

    //void SetCameraTarget(ObjRef target);

    // Stops a cinematic and returns control to the player
    void StopCinematic();

    void UpdateDeathSequence(float dt);
}

