// Transform: the service's bounding-box → scale/location/rotation mapping, bezier keyframes, flips.
#include "Recipes.h"

#include "Timeline.h"

using namespace golden;
using namespace golden::recipes;

namespace {

openshot::Clip* clipA(Scene& s, const Transform& t, int priority = 0) {
    MediaSpec m; m.path = s.media("clip_a_640x360_30.mp4"); m.end = 3.0; m.priority = priority; m.transform = t;
    return mediaClip(s, m);
}

void withBackground(Scene& s) {
    s.makeTimeline().AddClip(backgroundClip(s, s.media("background_960x540.png")));
}

} // namespace

void golden::registerTransformScenarios() {
    add("transform.static_bbox_rotation", {"transform", "exact"}, {1, 45},
        [](Scene& s) {
            withBackground(s);
            s.timeline->AddClip(clipA(s, Transform{BBox{0.35f, 0.45f, 0.5f, 0.5f}, 1.f, 1.f, 15.f}));
            s.timeline->Open();
        });

    add("transform.keyframed_bezier", {"transform", "keyframes", "exact"}, {1, 20, 40, 60},
        [](Scene& s) {
            withBackground(s);
            Transform t;
            t.scaleKfs = {{0.0, 1.f, 1.f, kEaseInOut}, {2.0, 0.4f, 0.4f, kEaseInOut}};
            t.positionKfs = {{0.0, 0.5f, 0.5f, kEaseInOut}, {2.0, 0.75f, 0.3f, kEaseInOut}};
            t.rotationKfs = {{0.0, 0.f, kEaseInOut}, {2.0, 90.f, kEaseInOut}};
            s.timeline->AddClip(clipA(s, t));
            s.timeline->Open();
        });

    add("transform.keyframed_linear_scale_only", {"transform", "keyframes", "exact"}, {1, 30, 60, 89},
        [](Scene& s) {
            withBackground(s);
            Transform t;
            t.hScale = 0.8f; t.vScale = 0.8f;
            t.scaleKfs = {{0.0, 0.8f, 0.8f, kLinear}, {3.0, 0.2f, 0.2f, kLinear}};
            s.timeline->AddClip(clipA(s, t));
            s.timeline->Open();
        });

    add("transform.rotation_full_turn", {"transform", "exact"}, {1, 23, 45, 68},
        [](Scene& s) {
            withBackground(s);
            Transform t{BBox{0.5f, 0.5f, 0.6f, 0.6f}};
            t.rotationKfs = {{0.0, 0.f, kLinear}, {3.0, 360.f, kLinear}};
            s.timeline->AddClip(clipA(s, t));
            s.timeline->Open();
        });

    add("transform.constant_steps", {"transform", "keyframes", "exact"}, {14, 15, 16, 45},
        [](Scene& s) {
            withBackground(s);
            Transform t{BBox{0.3f, 0.5f, 0.4f, 0.4f}};
            t.positionKfs = {{0.5, 0.7f, 0.5f, kConstant}, {1.5, 0.5f, 0.25f, kConstant}};
            s.timeline->AddClip(clipA(s, t));
            s.timeline->Open();
        });

    add("transform.flip_h_v", {"transform", "flip", "exact"}, {30},
        [](Scene& s) {
            withBackground(s);
            auto* h = clipA(s, Transform{BBox{0.25f, 0.5f, 0.45f, 0.45f}}, 0);
            h->FlipHorizontal(true);
            auto* v = clipA(s, Transform{BBox{0.75f, 0.5f, 0.45f, 0.45f}}, 1);
            v->FlipVertical(true);
            s.timeline->AddClip(h);
            s.timeline->AddClip(v);
            s.timeline->Open();
        });

    add("transform.flip_both_rotated", {"transform", "flip", "exact"}, {30},
        [](Scene& s) {
            withBackground(s);
            auto* c = clipA(s, Transform{BBox{0.5f, 0.5f, 0.6f, 0.6f}, 1.f, 1.f, 30.f});
            c->FlipHorizontal(true);
            c->FlipVertical(true);
            s.timeline->AddClip(c);
            s.timeline->Open();
        });
}
