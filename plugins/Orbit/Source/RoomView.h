#pragma once

#include <dd_gui/dd_gui.h>

#include "PluginProcessor.h"
#include "Room.h"

/**
    The room, drawn in perspective from behind the listener (or from above, or the side): floor grid with
    distance rings, glass walls that glow while the room rings, the listener's head in the middle, and the
    sound as a lamp hanging in the space with its shadow and height stem. Width is the arc of light it
    spans around the head; the first reflections are drawn as rays bouncing off the walls; motion shows
    its path and a fading trail; every hit sends a ring of light out through the room.

    Drag the lamp (or anywhere on the floor) to place the sound; Shift-drag or scroll for height;
    right-drag turns the view; double-click the lamp to reset it. Every move is a parameter, so it records
    as automation.
*/
class RoomView : public juce::Component, private juce::Timer
{
public:
    explicit RoomView (OrbitProcessor&);
    ~RoomView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    using Vec3 = orbit::Vec3;
    struct Camera { float yaw = 0.0f, pitch = 0.42f; };
    struct Projected { juce::Point<float> p; float depth = 1.0f; float scale = 1.0f; };

    void timerCallback() override;
    void setView (int view);

    // Geometry: "display" space is metres scaled so the room's longer half-length is 1.
    float unit() const noexcept;
    Vec3 toDisplay (Vec3 metres) const noexcept { return metres * unit(); }
    Projected project (Vec3 display) const noexcept;
    bool rayToPlaneZ (juce::Point<float> screen, float zDisplay, Vec3& hit) const noexcept;
    bool rayToPlaneX (juce::Point<float> screen, float xDisplay, Vec3& hit) const noexcept;
    void cameraBasis (Vec3& pos, Vec3& fwd, Vec3& right, Vec3& up) const noexcept;
    void fitFrame();

    // Painting.
    void paintRoom (juce::Graphics&);
    void paintRays (juce::Graphics&, Vec3 srcM, float level);
    void paintWidth (juce::Graphics&, Vec3 srcM);
    void paintPath (juce::Graphics&);
    void paintTrail (juce::Graphics&);
    void paintRings (juce::Graphics&);
    void paintListener (juce::Graphics&);
    void paintSource (juce::Graphics&, Vec3 srcM, float level);
    void paintReadouts (juce::Graphics&, Vec3 srcM);
    juce::Path polyline (const std::vector<Vec3>& displayPoints) const;

    // Interaction.
    juce::Point<float> sourceScreen() const;
    void placeFromMouse (const juce::MouseEvent&);
    void setParam (juce::RangedAudioParameter* p, float value);

    OrbitProcessor& proc;
    juce::RangedAudioParameter *pX, *pY, *pZ, *pWidth, *pRoom, *pSize, *pMotion, *pAmount, *pOutput;

    // Animated state.
    orbit::RoomState room;                // smoothed toward the parameters
    Vec3 heard { 0.0f, 0.5f, 0.4f };      // normalised, from the engine
    float level = 0.0f, late = 0.0f, levelPeak = 0.0f;
    Camera cam, camTarget;
    int view = 0;
    float frameScale = 300.0f, frameScaleTarget = 300.0f;
    juce::Point<float> frameOffset, frameOffsetTarget;
    juce::Rectangle<float> stage;

    std::vector<Vec3> trail;              // metres, newest last
    struct Ring { Vec3 centre; double born; float strength; };
    std::vector<Ring> rings;
    double lastRing = 0.0;

    enum class Drag { None, Place, Height, Orbit };
    Drag drag = Drag::None;
    juce::Point<float> dragStart;
    float dragStartZ = 0.0f;
    Camera dragStartCam;
    bool overSource = false;

    juce::OwnedArray<dd::gui::Key> viewKeys;
};
