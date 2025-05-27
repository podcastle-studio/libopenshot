#include "LensFlare.h"
#include "Exceptions.h"
#include <cmath>
#include <vector>
#include <algorithm>

using namespace openshot;

struct FlareElement {
    float offset;      // along vector center-to-flare, -1=behind, +1=past
    float radius;      // size, as a fraction of image width
    QColor color;      // flare color
    float alpha;       // 0..1
    int   type;        // 0: orb, 1: ring, 2: soft halo
    float chromaMix;   // 0: pure tint, 1: pure element color
};

static QColor mix_color(const QColor& a, const QColor& b, float t) {
    float r = a.redF()   * (1-t) + b.redF()   * t;
    float g = a.greenF() * (1-t) + b.greenF() * t;
    float b_ = a.blueF() * (1-t) + b.blueF()  * t;
    float af = a.alphaF() * (1-t) + b.alphaF() * t;
    QColor c; c.setRgbF(r, g, b_, af);
    return c;
}

static QColor scale_alpha(const QColor& c, float a) {
    QColor o = c;
    float alpha = std::clamp(float(c.alphaF() * a), 0.0f, 1.0f);
    o.setAlphaF(alpha);
    return o;
}

LensFlare::LensFlare()
    : x(0.0), y(0.0), brightness(1.0), size(1.0), spread(0.5),
      blades(6), iris_shape(0), color(Color("#FFA500"))
{
    init_effect_details();
}

LensFlare::~LensFlare() {}

LensFlare::LensFlare(const Keyframe &xPos,
                     const Keyframe &yPos,
                     const Keyframe &intensity,
                     const Keyframe &scale,
                     const Keyframe &spreadVal,
                     const Keyframe &bladeCount,
                     const Keyframe &shapeType,
                     const Color &tint)
    : x(xPos), y(yPos), brightness(intensity), size(scale), spread(spreadVal),
      blades(bladeCount), iris_shape(shapeType), color(tint)
{
    init_effect_details();
}

void LensFlare::init_effect_details()
{
    InitEffectInfo();
    info.class_name = "LensFlare";
    info.name       = "Lens Flare";
    info.description = "Realistic lens flare with bloom, ghosts, rings, streaks, chromatic split, and customizable aperture.";
    info.has_video = true;
    info.has_audio = false;
}

std::shared_ptr<openshot::Frame>
LensFlare::GetFrame(int64_t frame_number)
{
    return GetFrame(std::make_shared<openshot::Frame>(), frame_number);
}

std::shared_ptr<openshot::Frame>
LensFlare::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t f)
{
    auto img = frame->GetImage();
    int w = img->width(), h = img->height();

    // Keyframes
    float Xn = x.GetValue(f), Yn = y.GetValue(f);
    float I  = brightness.GetValue(f);
    float S  = size.GetValue(f);
    float D  = spread.GetValue(f);
    int   B  = std::clamp(blades.GetInt(f), 3, 12);
    int   M  = iris_shape.GetInt(f); // 0: round, 1: polygonal

    // Center, position, vector
    float cx = w*0.5f, cy = h*0.5f;
    float px = (Xn*0.5f+0.5f)*w, py = (Yn*0.5f+0.5f)*h;
    float dx = px-cx, dy = py-cy;
    float diag = std::hypot((float)w, (float)h);
    float base = std::min(w,h);

    // Tint color for the core
    QColor userTint(
        color.red.GetInt(f),
        color.green.GetInt(f),
        color.blue.GetInt(f),
        int(color.alpha.GetValue(f))
    );

    // Overlay for drawing
    QImage overlay(w, h, QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);
    QPainter p(&overlay);
    p.setRenderHint(QPainter::Antialiasing);

    // ---- Core flare and halos ----
    float r0 = base * 0.08f * S;
    QRadialGradient g0(px, py, r0);
    QColor coreCol = scale_alpha(userTint, I*0.9f);
    g0.setColorAt(0, coreCol); g0.setColorAt(1, Qt::transparent);
    p.setBrush(g0); p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(px, py), r0, r0);

    // Inner and outer glow/rings
    {
        QRadialGradient g1(px, py, r0*2.1);
        QColor glowCol = scale_alpha(userTint, I*0.3f);
        g1.setColorAt(0, glowCol); g1.setColorAt(1, Qt::transparent);
        p.setBrush(g1); p.drawEllipse(QPointF(px, py), r0*2.1, r0*2.1);
    }
    {
        QRadialGradient g2(px, py, r0*3.5);
        QColor ringCol = scale_alpha(userTint, I*0.17f);
        g2.setColorAt(0.90, Qt::transparent); g2.setColorAt(0.96, ringCol); g2.setColorAt(1.0, Qt::transparent);
        p.setBrush(g2); p.drawEllipse(QPointF(px, py), r0*3.5, r0*3.5);
    }
    {
        QRadialGradient g3(px, py, r0*7.5);
        QColor haloCol = scale_alpha(userTint, I*0.08f);
        g3.setColorAt(0, Qt::transparent); g3.setColorAt(1, haloCol);
        p.setBrush(g3); p.drawEllipse(QPointF(px, py), r0*7.5, r0*7.5);
    }

    // (offset, radius, base color, alpha, type, chromaMix)
    std::vector<FlareElement> ghosts = {
        {  0.67f,  0.035f, QColor(0,14,113),    0.35f, 0, 0.85f },
        {  0.27f,  0.017f, QColor(90,181,142),  0.31f, 0, 0.80f },
        { -0.01f,  0.012f, QColor(56,140,106),  0.24f, 0, 0.75f },
        {  0.65f,  0.032f, QColor(9,29,19),     0.12f, 1, 0.90f },
        {  0.45f,  0.022f, QColor(24,14,0),     0.11f, 0, 0.70f },
        {  0.41f,  0.046f, QColor(24,14,0),     0.10f, 1, 0.80f },
        { -0.20f,  0.030f, QColor(42,19,0),     0.12f, 0, 0.65f },
        { -0.41f,  0.038f, QColor(0,9,17),      0.13f, 1, 0.95f },
        { -0.45f,  0.060f, QColor(0,4,10),      0.09f, 0, 1.00f },
        { -0.51f,  0.025f, QColor(5,5,14),      0.14f, 0, 0.93f },
        { -1.35f,  0.115f, QColor(9,4,0),       0.07f, 1, 0.70f },
        {  1.30f,  0.175f, QColor(9,0,17),      0.08f, 1, 0.99f }
    };

    float spread_scale = D + std::min(1.0f, std::hypot(Xn,Yn));

    for (const auto& g : ghosts) {
        float gx = cx + dx * g.offset * spread_scale;
        float gy = cy + dy * g.offset * spread_scale;
        float gr = base * g.radius * S;
        QColor gc = mix_color(userTint, g.color, g.chromaMix);
        gc.setAlphaF(std::clamp(g.alpha * I, 0.0f, 1.0f));

        if (g.type == 0) { // orb
            QRadialGradient grad(gx, gy, gr);
            grad.setColorAt(0, gc);
            grad.setColorAt(1, Qt::transparent);
            p.setBrush(grad); p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(gx, gy), gr, gr);
        } else if (g.type == 1) { // ring
            QRadialGradient grad(gx, gy, gr);
            grad.setColorAt(0.90, Qt::transparent);
            grad.setColorAt(0.96, gc);
            grad.setColorAt(1.0, Qt::transparent);
            p.setBrush(grad); p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(gx, gy), gr, gr);
        }
    }

    // ---- Streaks (aperture rays/star) ----
    // “iris_shape” 0: smooth round, 1: polygonal, for sharp rays
    if (B >= 3) {
        float flare_angle = std::atan2(dy, dx);
        float r_streak = base * 0.35f * S * (0.7f + 0.7f * spread_scale);
        float streak_alpha = 0.18f * I;
        for (int i = 0; i < B; ++i) {
            float a = flare_angle + (float(i) * 2.0f * M_PI / B);
            float strength = (M==1) ? (0.5f + 0.5f * std::cos(B*a)) : 1.0f;
            QColor rayCol = scale_alpha(userTint, streak_alpha * strength);
            QLinearGradient lg(px, py, px + std::cos(a)*r_streak, py + std::sin(a)*r_streak);
            lg.setColorAt(0, rayCol);
            lg.setColorAt(0.25, Qt::transparent);
            p.setPen(QPen(QBrush(lg), r0*0.19f, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(px, py), QPointF(px + std::cos(a)*r_streak, py + std::sin(a)*r_streak));
        }
    }

    // ---- Chromatic split (ghost lines) ----
    // Red, green, blue ghosts along the main axis
    for (int c = 0; c < 3; ++c) {
        float frac = (c==0)?0.67f:(c==1)?0.77f:0.87f;
        float gx = cx + dx * frac * spread_scale;
        float gy = cy + dy * frac * spread_scale;
        float gr = base * 0.03f * S * (1 + 0.2f*c);
        QColor col = userTint;
        if (c==0) { col.setRedF(std::min(1.0, col.redF()*1.1)); col.setGreenF(col.greenF()*0.7); col.setBlueF(col.blueF()*0.7); }
        if (c==1) { col.setGreenF(std::min(1.0, col.greenF()*1.1)); col.setRedF(col.redF()*0.7); col.setBlueF(col.blueF()*0.7); }
        if (c==2) { col.setBlueF(std::min(1.0, col.blueF()*1.1)); col.setRedF(col.redF()*0.7); col.setGreenF(col.greenF()*0.7); }
        col.setAlphaF(0.16f * I);
        QRadialGradient grad(gx, gy, gr);
        grad.setColorAt(0, col);
        grad.setColorAt(1, Qt::transparent);
        p.setBrush(grad); p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(gx, gy), gr, gr);
    }

    p.end();

    // Composite
    QPainter c(img.get()); c.setCompositionMode(QPainter::CompositionMode_Screen);
    c.drawImage(0,0,overlay); c.end();

    return frame;
}

// ----- JSON/Properties -----

std::string LensFlare::Json() const
{ return JsonValue().toStyledString(); }

Json::Value LensFlare::JsonValue() const
{
    Json::Value r=EffectBase::JsonValue();
    r["type"]      =info.class_name;
    r["x"]         =x.JsonValue();
    r["y"]         =y.JsonValue();
    r["brightness"]=brightness.JsonValue();
    r["size"]      =size.JsonValue();
    r["spread"]    =spread.JsonValue();
    r["blades"]    =blades.JsonValue();
    r["iris_shape"]=iris_shape.JsonValue();
    r["color"]     =color.JsonValue();
    return r;
}

void LensFlare::SetJson(const std::string v)
{ try{SetJsonValue(openshot::stringToJson(v));}catch(...){throw InvalidJSON("LensFlare JSON");} }

void LensFlare::SetJsonValue(const Json::Value r)
{
    EffectBase::SetJsonValue(r);
    if(!r["x"].isNull())          x.SetJsonValue(r["x"]);
    if(!r["y"].isNull())          y.SetJsonValue(r["y"]);
    if(!r["brightness"].isNull()) brightness.SetJsonValue(r["brightness"]);
    if(!r["size"].isNull())       size.SetJsonValue(r["size"]);
    if(!r["spread"].isNull())     spread.SetJsonValue(r["spread"]);
    if(!r["blades"].isNull())     blades.SetJsonValue(r["blades"]);
    if(!r["iris_shape"].isNull()) iris_shape.SetJsonValue(r["iris_shape"]);
    if(!r["color"].isNull())      color.SetJsonValue(r["color"]);
}

std::string LensFlare::PropertiesJSON(int64_t f) const
{
    Json::Value r=BasePropertiesJSON(f);
    r["x"]           =add_property_json("X",x.GetValue(f),"float","-1..1",&x,-1,1,false,f);
    r["y"]           =add_property_json("Y",y.GetValue(f),"float","-1..1",&y,-1,1,false,f);
    r["brightness"]  =add_property_json("Brightness",brightness.GetValue(f),"float","0..1",&brightness,0,1,false,f);
    r["size"]        =add_property_json("Size",size.GetValue(f),"float","0.1..3",&size,0.1,3,false,f);
    r["spread"]      =add_property_json("Spread",spread.GetValue(f),"float","0..1",&spread,0,1,false,f);
    r["blades"]      =add_property_json("Aperture Blades",blades.GetValue(f),"int","3..12",&blades,3,12,false,f);
    r["iris_shape"]  =add_property_json("Iris Shape",iris_shape.GetValue(f),"int","0:Circular,1:Polygonal",&iris_shape,0,1,false,f);
    r["iris_shape"]["choices"].append(add_property_choice_json("Circular",0,iris_shape.GetValue(f)));
    r["iris_shape"]["choices"].append(add_property_choice_json("Polygonal",1,iris_shape.GetValue(f)));
    r["color"]       =add_property_json("Tint Color",0.0,"color","",&color.red,0,255,false,f);
    r["color"]["red"]   =add_property_json("Red",color.red.GetInt(f),"float","0..255",&color.red,0,255,false,f);
    r["color"]["green"] =add_property_json("Green",color.green.GetInt(f),"float","0..255",&color.green,0,255,false,f);
    r["color"]["blue"]  =add_property_json("Blue",color.blue.GetInt(f),"float","0..255",&color.blue,0,255,false,f);
    r["color"]["alpha"] =add_property_json("Alpha",color.alpha.GetInt(f),"float","0..255",&color.alpha,0,255,false,f);
    return r.toStyledString();
}
