// linear_probe.cpp — the INTER_LINEAR zoom blur, composed, against the real effect.
//
// After the 2026-09-22 decision to pass cv::INTER_LINEAR to both cv::linearPolar calls, the port
// is no longer a chain of pixel choices but a chain of resamplings. This reproduces that chain in
// scalar C++ exactly as the fragment will have to, so the details that are not in any header --
// how remap quantises a float map, what BORDER_TRANSPARENT does at the edge, where the 8-bit
// roundings fall -- are settled by measurement before any SkSL is written.
//
// Build:
//   g++ -O2 -std=c++17 linear_probe.cpp -o linear_probe $(pkg-config --cflags --libs opencv4)
#include <opencv2/opencv.hpp>
#include <cstdio>
#include <vector>

namespace {

constexpr double kTwoPi = 6.283185307179586;

// remap converts a CV_32F map to fixed point before interpolating: cvRound(x * 32) gives a
// position in thirty-seconds of a pixel, the high bits index the texel and the low five are the
// weight. This is the one thing that makes OpenCV's INTER_LINEAR not an exact lerp.
inline void fixedPoint(double v, int& i, double& f) {
    const int q = cvRound(v * 32.0);
    i = q >> 5;
    f = (q & 31) / 32.0;
}

inline int reflect101(int i, int n) {
    if (n == 1) return 0;
    while (i < 0 || i >= n) {
        if (i < 0) i = -i;
        if (i >= n) i = 2 * (n - 1) - i;
    }
    return i;
}

// One bilinear tap of an 8-bit BGRA image, BORDER_CONSTANT 0 — the forward conversion's border,
// which is what WARP_FILL_OUTLIERS selects.
cv::Vec4d bilinearConstant(const cv::Mat& img, double x, double y) {
    int sx, sy; double fx, fy;
    fixedPoint(x, sx, fx);
    fixedPoint(y, sy, fy);
    const double w[4] = {(1 - fy) * (1 - fx), (1 - fy) * fx, fy * (1 - fx), fy * fx};
    const int dx[4] = {0, 1, 0, 1}, dy[4] = {0, 0, 1, 1};
    cv::Vec4d acc(0, 0, 0, 0);
    for (int k = 0; k < 4; ++k) {
        const int px = sx + dx[k], py = sy + dy[k];
        if (px < 0 || py < 0 || px >= img.cols || py >= img.rows) continue;  // the constant is 0
        const cv::Vec4b& s = img.at<cv::Vec4b>(py, px);
        for (int c = 0; c < 4; ++c) acc[c] += w[k] * s[c];
    }
    return acc;
}

}  // namespace

int main(int argc, char** argv) {
    const int W = argc > 1 ? std::atoi(argv[1]) : 256;
    const int H = argc > 2 ? std::atoi(argv[2]) : 256;
    const int authored = argc > 3 ? std::atoi(argv[3]) : 40;
    const double cxf = argc > 4 ? std::atof(argv[4]) : 0.5;
    const double cyf = argc > 5 ? std::atof(argv[5]) : 0.5;
    const char* kind = argc > 6 ? argv[6] : "noise";

    cv::Mat image(H, W, CV_8UC4);
    cv::RNG rng(12345);
    if (std::string(kind) == "noise") {
        rng.fill(image, cv::RNG::UNIFORM, 0, 256);
    } else if (std::string(kind) == "corners") {
        image.setTo(cv::Scalar(20, 40, 60, 255));
        cv::rectangle(image, cv::Rect(0, 0, W / 2, H / 2), cv::Scalar(250, 10, 10, 255), -1);
        cv::rectangle(image, cv::Rect(W / 2, H / 2, W / 2, H / 2), cv::Scalar(10, 250, 250, 255), -1);
    } else {
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                image.at<cv::Vec4b>(y, x) = cv::Vec4b(x * 255 / W, y * 255 / H, 128, 255);
    }

    // --- what the effect does, verbatim from effects.cpp ------------------------------------
    int blurStrength = std::max(1, (int)std::lround(authored * (W / 1280.0)));
    if (blurStrength % 2 == 0) blurStrength += 1;
    const int pad = blurStrength;
    const cv::Point2f centre(cxf * W, cyf * H);
    const cv::Point2f paddedCentre(centre.x + pad, centre.y + pad);

    cv::Mat padded;
    cv::copyMakeBorder(image, padded, pad, pad, pad, pad, cv::BORDER_REFLECT);

    double maxRadius = 0.0;
    for (const cv::Point2f& c : {cv::Point2f(0, 0), cv::Point2f(padded.cols - 1, 0),
                                 cv::Point2f(0, padded.rows - 1),
                                 cv::Point2f(padded.cols - 1, padded.rows - 1)})
        maxRadius = std::max(maxRadius, cv::norm(c - paddedCentre));

    cv::Mat reference;
    {
        cv::Mat polar, back = padded.clone();
        cv::linearPolar(padded, polar, paddedCentre, maxRadius,
                        cv::WARP_FILL_OUTLIERS | cv::INTER_LINEAR);
        cv::blur(polar, polar, cv::Size(blurStrength, 1));
        cv::linearPolar(polar, back, paddedCentre, maxRadius,
                        cv::WARP_INVERSE_MAP | cv::INTER_LINEAR);
        reference = back(cv::Rect(pad, pad, W, H)).clone();
    }

    // --- the composition the fragment will run ----------------------------------------------
    const double kAngle = kTwoPi / padded.rows;
    const double kMag = maxRadius / padded.cols;

    // P[phi][rho], the forward conversion, rounded to bytes exactly as remap writes it.
    cv::Mat polarRef;
    {
        cv::Mat p;
        cv::linearPolar(padded, p, paddedCentre, maxRadius,
                        cv::WARP_FILL_OUTLIERS | cv::INTER_LINEAR);
        polarRef = p;
    }
    auto polarAt = [&](int phi, int rho) -> cv::Vec4d {
        const double a = kAngle * phi;
        const double x = paddedCentre.x + kMag * rho * std::cos(a);
        const double y = paddedCentre.y + kMag * rho * std::sin(a);
        cv::Vec4d v = bilinearConstant(padded, x, y);
        for (int c = 0; c < 4; ++c) v[c] = cv::saturate_cast<uchar>(v[c]);
        return v;
    };
    // cv::blur along rho: an exact integer mean over the reflect-101 window, rounded once.
    auto blurAt = [&](int phi, int rho) -> cv::Vec4d {
        const int first = -(blurStrength / 2);
        cv::Vec4d sum(0, 0, 0, 0);
        for (int k = 0; k < blurStrength; ++k) {
            const cv::Vec4d v = polarAt(phi, reflect101(rho + first + k, padded.cols));
            for (int c = 0; c < 4; ++c) sum[c] += v[c];
        }
        cv::Vec4d out;
        for (int c = 0; c < 4; ++c) out[c] = cv::saturate_cast<uchar>(sum[c] / blurStrength);
        return out;
    };

    // --- stage 1: my forward sample against cv::linearPolar's own polar image ----------------
    {
        long differ = 0; int worst = 0;
        for (int phi = 0; phi < padded.rows; ++phi)
            for (int rho = 0; rho < padded.cols; ++rho) {
                const cv::Vec4d v = polarAt(phi, rho);
                for (int c = 0; c < 4; ++c) {
                    const int d = std::abs((int)v[c] - (int)polarRef.at<cv::Vec4b>(phi, rho)[c]);
                    if (d) { differ++; worst = std::max(worst, d); }
                }
            }
        std::printf("  stage1 forward : %ld of %d channels differ, worst %d\n",
                    differ, padded.rows * padded.cols * 4, worst);
    }

    // --- stage 2: my blur of my polar against cv::blur of OpenCV's polar ---------------------
    cv::Mat blurredRef;
    cv::blur(polarRef, blurredRef, cv::Size(blurStrength, 1));
    {
        long differ = 0; int worst = 0;
        for (int phi = 0; phi < padded.rows; ++phi)
            for (int rho = 0; rho < padded.cols; ++rho) {
                const cv::Vec4d v = blurAt(phi, rho);
                for (int c = 0; c < 4; ++c) {
                    const int d = std::abs((int)v[c] - (int)blurredRef.at<cv::Vec4b>(phi, rho)[c]);
                    if (d) { differ++; worst = std::max(worst, d); }
                }
            }
        std::printf("  stage2 blur    : %ld of %d channels differ, worst %d\n",
                    differ, padded.rows * padded.cols * 4, worst);
    }

    // --- stage 3: my inverse of OpenCV's blurred polar against the real inverse --------------
    cv::Mat inverseRef = padded.clone();
    cv::linearPolar(blurredRef, inverseRef, paddedCentre, maxRadius,
                    cv::WARP_INVERSE_MAP | cv::INTER_LINEAR);

    const cv::Mat inverseCropRef = inverseRef(cv::Rect(pad, pad, W, H)).clone();
    cv::Mat mine(H, W, CV_8UC4);
    long phiWrapped = 0, transparent = 0;
    bool useOwnChain = false;
  for (int chain = 0; chain < 2; ++chain) {
    useOwnChain = chain == 1;
    transparent = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const double dx = (x + pad) - paddedCentre.x;
            const double dy = (y + pad) - paddedCentre.y;
            const double rhoF = std::sqrt(dx * dx + dy * dy) / kMag;
            double ang = std::atan2(dy, dx);
            if (ang < 0) ang += kTwoPi;
            const double phiF = ang / kAngle;

            int ri, pi; double rf, pf;
            fixedPoint(rhoF, ri, rf);
            fixedPoint(phiF, pi, pf);

            const double w[4] = {(1 - pf) * (1 - rf), (1 - pf) * rf, pf * (1 - rf), pf * rf};
            const int dr[4] = {0, 1, 0, 1}, dp[4] = {0, 0, 1, 1};

            // BORDER_TRANSPARENT: the destination is the padded source, so anything the inverse
            // remap will not write keeps the colour it already had.
            bool wrote = false;
            cv::Vec4d acc(0, 0, 0, 0);
            for (int k = 0; k < 4; ++k) {
                const int rr = ri + dr[k];
                // phi wraps: the polar image's last row and its first are neighbours on the
                // circle, and warpPolar's inverse samples across that seam.
                int pp = pi + dp[k];
                if (pp >= padded.rows) pp -= padded.rows;
                if (pp < 0) pp += padded.rows;
                if (rr < 0 || rr >= padded.cols) continue;
                cv::Vec4d v;
                if (useOwnChain) {
                    v = blurAt(pp, rr);
                } else {
                    const cv::Vec4b& b = blurredRef.at<cv::Vec4b>(pp, rr);
                    v = cv::Vec4d(b[0], b[1], b[2], b[3]);
                }
                for (int c = 0; c < 4; ++c) acc[c] += w[k] * v[c];
                wrote = true;
            }
            if (!wrote) { transparent++; mine.at<cv::Vec4b>(y, x) = image.at<cv::Vec4b>(y, x); continue; }
            cv::Vec4b out;
            for (int c = 0; c < 4; ++c) out[c] = cv::saturate_cast<uchar>(acc[c]);
            mine.at<cv::Vec4b>(y, x) = out;
        }
    }
    const cv::Mat& truth = useOwnChain ? reference : inverseCropRef;
    {
        long differ = 0; int worst = 0; double mse = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                for (int c = 0; c < 4; ++c) {
                    const int a = truth.at<cv::Vec4b>(y, x)[c], b = mine.at<cv::Vec4b>(y, x)[c];
                    const int d = std::abs(a - b);
                    if (d) { differ++; worst = std::max(worst, d); }
                    mse += double(d) * d;
                }
        mse /= double(W) * H * 4;
        std::printf("  %-14s : differ %6ld of %d, worst %3d, psnr %7.3f dB\n",
                    useOwnChain ? "whole effect" : "stage3 inverse",
                    differ, W * H * 4, worst,
                    mse > 0 ? 10 * std::log10(255.0 * 255.0 / mse) : 1e9);
    }
  }
    (void)phiWrapped;

    const cv::Mat inverseCrop = inverseRef(cv::Rect(pad, pad, W, H)).clone();
    long differ = 0; int worst = 0; double mse = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            for (int c = 0; c < 4; ++c) {
                const int a = inverseCrop.at<cv::Vec4b>(y, x)[c], b = mine.at<cv::Vec4b>(y, x)[c];
                const int d = std::abs(a - b);
                if (d) { differ++; worst = std::max(worst, d); }
                mse += double(d) * d;
            }
    mse /= double(W) * H * 4;

    // Where the inverse disagrees: the worst twenty, with the polar position that produced them.
    {
        struct Bad { int d, x, y, c; double rho, phi; };
        std::vector<Bad> bad;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                int dmax = 0, cw = 0;
                for (int c = 0; c < 4; ++c) {
                    const int d = std::abs((int)inverseCrop.at<cv::Vec4b>(y, x)[c] -
                                           (int)mine.at<cv::Vec4b>(y, x)[c]);
                    if (d > dmax) { dmax = d; cw = c; }
                }
                if (!dmax) continue;
                const double dx = (x + pad) - paddedCentre.x, dy = (y + pad) - paddedCentre.y;
                double ang = std::atan2(dy, dx); if (ang < 0) ang += kTwoPi;
                bad.push_back({dmax, x, y, cw, std::sqrt(dx * dx + dy * dy) / kMag, ang / kAngle});
            }
        std::sort(bad.begin(), bad.end(), [](const Bad& a, const Bad& b) { return a.d > b.d; });
        std::printf("  worst pixels (d, x, y, rho, phi of %d):\n", padded.rows);
        for (size_t i = 0; i < bad.size() && i < 12; ++i)
            std::printf("    d=%3d at (%3d,%3d) rho=%9.4f phi=%9.4f\n",
                        bad[i].d, bad[i].x, bad[i].y, bad[i].rho, bad[i].phi);
    }
    std::printf("%s %dx%d strength=%d centre=(%.2f,%.2f) taps=%d pad=%d\n",
                kind, W, H, authored, cxf, cyf, blurStrength, pad);
    std::printf("  stage3 inverse : differ %ld of %d, worst %d, psnr %.3f dB, transparent %ld\n",
                differ, W * H * 4, worst, mse > 0 ? 10 * std::log10(255.0 * 255.0 / mse) : 1e9,
                transparent);
    return 0;
}
