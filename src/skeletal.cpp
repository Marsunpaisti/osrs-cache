#include <osrs/cache/skeletal.h>

#include "buffer.h"
#include "undecodable.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace osrs::cache {

namespace {

// gl-matrix 3.4.3's mat4 and quat functions that skeleton.ts calls, on
// Float32Array storage.
using Quat = std::array<float, 4>;

Matrix4 identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

Matrix4 multiply(const Matrix4& a, const Matrix4& b) {
    Matrix4 out;
    for (int column = 0; column < 4; ++column) {
        const double b0 = b[column * 4], b1 = b[column * 4 + 1], b2 = b[column * 4 + 2], b3 = b[column * 4 + 3];
        for (int row = 0; row < 4; ++row) {
            out[column * 4 + row] = static_cast<float>(b0 * a[row] + b1 * a[4 + row] + b2 * a[8 + row] + b3 * a[12 + row]);
        }
    }
    return out;
}

// Nothing for a singular matrix.
std::optional<Matrix4> invert(const Matrix4& a) {
    const double a00 = a[0], a01 = a[1], a02 = a[2], a03 = a[3];
    const double a10 = a[4], a11 = a[5], a12 = a[6], a13 = a[7];
    const double a20 = a[8], a21 = a[9], a22 = a[10], a23 = a[11];
    const double a30 = a[12], a31 = a[13], a32 = a[14], a33 = a[15];
    const double b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10, b02 = a00 * a13 - a03 * a10;
    const double b03 = a01 * a12 - a02 * a11, b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12;
    const double b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30, b08 = a20 * a33 - a23 * a30;
    const double b09 = a21 * a32 - a22 * a31, b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
    double det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (det == 0) return std::nullopt;
    det = 1.0 / det;
    const auto f = [](double v) { return static_cast<float>(v); };
    return Matrix4{f((a11 * b11 - a12 * b10 + a13 * b09) * det), f((a02 * b10 - a01 * b11 - a03 * b09) * det),
                   f((a31 * b05 - a32 * b04 + a33 * b03) * det), f((a22 * b04 - a21 * b05 - a23 * b03) * det),
                   f((a12 * b08 - a10 * b11 - a13 * b07) * det), f((a00 * b11 - a02 * b08 + a03 * b07) * det),
                   f((a32 * b02 - a30 * b05 - a33 * b01) * det), f((a20 * b05 - a22 * b02 + a23 * b01) * det),
                   f((a10 * b10 - a11 * b08 + a13 * b06) * det), f((a01 * b08 - a00 * b10 - a03 * b06) * det),
                   f((a30 * b04 - a31 * b02 + a33 * b00) * det), f((a21 * b02 - a20 * b04 - a23 * b00) * det),
                   f((a11 * b07 - a10 * b09 - a12 * b06) * det), f((a00 * b09 - a01 * b07 + a02 * b06) * det),
                   f((a31 * b01 - a30 * b03 - a32 * b00) * det), f((a20 * b03 - a21 * b01 + a22 * b00) * det)};
}

Quat axisAngle(int axis, double radians) {
    radians *= 0.5;
    Quat q{0, 0, 0, static_cast<float>(std::cos(radians))};
    q[axis] = static_cast<float>(std::sin(radians));
    return q;
}

Quat multiply(const Quat& a, const Quat& b) {
    const double ax = a[0], ay = a[1], az = a[2], aw = a[3];
    const double bx = b[0], by = b[1], bz = b[2], bw = b[3];
    return {static_cast<float>(ax * bw + aw * bx + ay * bz - az * by), static_cast<float>(ay * bw + aw * by + az * bx - ax * bz),
            static_cast<float>(az * bw + aw * bz + ax * by - ay * bx), static_cast<float>(aw * bw - ax * bx - ay * by - az * bz)};
}

Matrix4 fromQuat(const Quat& q) {
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double x2 = x + x, y2 = y + y, z2 = z + z;
    const double xx = x * x2, yx = y * x2, yy = y * y2, zx = z * x2, zy = z * y2, zz = z * z2;
    const double wx = w * x2, wy = w * y2, wz = w * z2;
    const auto f = [](double v) { return static_cast<float>(v); };
    return {f(1 - yy - zz), f(yx + wz), f(zx - wy), 0, f(yx - wz), f(1 - xx - zz), f(zy + wx), 0,
            f(zx + wy), f(zy - wx), f(1 - xx - yy), 0, 0, 0, 0, 1};
}

// skeleton.ts rotationOf: Euler angles of a bone's inverted rest matrix.
std::array<float, 3> rotationOf(const Matrix4& m) {
    std::array<float, 3> out{static_cast<float>(-std::asin(static_cast<double>(m[6]))), 0, 0};
    if (std::abs(std::cos(static_cast<double>(out[0]))) > 0.005) {
        out[1] = static_cast<float>(std::atan2(static_cast<double>(m[2]), static_cast<double>(m[10])));
        out[2] = static_cast<float>(std::atan2(static_cast<double>(m[4]), static_cast<double>(m[5])));
    } else {
        const double angle = std::atan2(static_cast<double>(m[1]), static_cast<double>(m[0]));
        out[1] = static_cast<float>(m[6] < 0 ? angle : -angle);
    }
    return out;
}

// curve.ts, which keeps the client's names: the interpolation between a
// curve's points, and the polynomial root finding its Bézier form needs.
constexpr double kUlp = 1.1920929e-7;
constexpr double kUlp2 = 2 * kUlp;

struct CurvePoint {
    int x = 0;
    float y = 0, field2 = 0, field3 = 0, field4 = 0, field5 = 0;
};

// A Float32Array of fixed length: writes past its end are dropped and reads
// give NaN, as in JavaScript.
class Floats {
public:
    explicit Floats(std::size_t size) : values_(size, 0.0f) {}
    double get(int i) const { return i >= 0 && i < size() ? values_[i] : std::numeric_limits<double>::quiet_NaN(); }
    void set(int i, double v) {
        if (i >= 0 && i < size()) values_[i] = static_cast<float>(v);
    }
    int size() const { return static_cast<int>(values_.size()); }

private:
    std::vector<float> values_;
};

double method6869(const Floats& values, int lastIndex, double x) {
    double output = values.get(lastIndex);
    for (int i = lastIndex - 1; i >= 0; --i) output = output * x + values.get(i);
    return output;
}

// The roots of a polynomial between two bounds, as the client finds them.
int method5023(const Floats& var0, int var1, double var2, bool var3, double var4, bool var5, Floats& var6) {
    double var7 = 0;
    for (int i = 0; i < var1 + 1; ++i) var7 += std::abs(var0.get(i));
    const double var44 = (std::abs(var2) + std::abs(var4)) * (var1 + 1) * kUlp;
    if (var7 <= var44) return -1;
    Floats var9(static_cast<std::size_t>(var1) + 1);
    for (int i = 0; i < var1 + 1; ++i) var9.set(i, (1 / var7) * var0.get(i));
    while (var1 >= 0 && std::abs(var9.get(var1)) < var44) --var1;
    int status = 0;
    if (var1 == 0) return status;
    if (var1 == 1) {
        var6.set(0, -var9.get(0) / var9.get(1));
        const bool var42 = var3 ? var2 < var6.get(0) + var44 : var2 < var6.get(0) - var44;
        const bool var43 = var5 ? var4 > var6.get(0) - var44 : var4 > var6.get(0) + var44;
        status = var42 && var43 ? 1 : 0;
        if (status > 0) {
            if (var3 && var6.get(0) < var2) {
                var6.set(0, var2);
            } else if (var5 && var6.get(0) > var4) {
                var6.set(0, var4);
            }
        }
        return status;
    }
    const Floats& field4756 = var9;
    const int field4757 = var1;
    Floats var12(static_cast<std::size_t>(var1) + 1);
    for (int i = 1; i <= var1; ++i) var12.set(i - 1, i * var9.get(i));
    Floats var41(static_cast<std::size_t>(var1) + 1);
    const int recursiveStatus = method5023(var12, var1 - 1, var2, false, var4, false, var41);
    if (recursiveStatus == -1) return 0;
    bool var15 = false;
    double var17 = 0, var18 = 0, var19 = 0;
    for (int s = 0; s <= recursiveStatus; ++s) {
        if (status > var1) return status;
        double var16;
        if (s == 0) {
            var16 = var2;
            var18 = method6869(var9, var1, var2);
            if (std::abs(var18) <= var44 && var3) var6.set(status++, var2);
        } else {
            var16 = var19;
            var18 = var17;
        }
        if (recursiveStatus == s) {
            var19 = var4;
            var15 = false;
        } else {
            var19 = var41.get(s);
        }
        var17 = method6869(var9, var1, var19);
        if (var15) {
            var15 = false;
        } else if (std::abs(var17) < var44) {
            if (recursiveStatus != s || var5) {
                var6.set(status++, var19);
                var15 = true;
            }
        } else if ((var18 < 0 && var17 > 0) || (var18 > 0 && var17 < 0)) {
            const int var22 = status++;
            double var24 = var16, var25 = var19;
            double var26 = method6869(field4756, field4757, var16);
            double var23;
            if (std::abs(var26) < kUlp) {
                var23 = var16;
            } else {
                double var27 = method6869(field4756, field4757, var19);
                if (std::abs(var27) < kUlp) {
                    var23 = var19;
                } else {
                    double var28 = 0, var29 = 0, var30 = 0, var35 = 0;
                    bool var36 = true;
                    bool var37;
                    do {
                        var37 = false;
                        if (var36) {
                            var28 = var24;
                            var35 = var26;
                            var29 = var25 - var24;
                            var30 = var29;
                            var36 = false;
                        }
                        if (std::abs(var35) < std::abs(var27)) {
                            var24 = var25;
                            var25 = var28;
                            var28 = var24;
                            var26 = var27;
                            var27 = var35;
                            var35 = var26;
                        }
                        const double var38 = kUlp2 * std::abs(var25) + 0;
                        const double var39 = 0.5 * (var28 - var25);
                        if (std::abs(var39) > var38 && var27 != 0) {
                            if (std::abs(var30) < var38 || std::abs(var26) <= std::abs(var27)) {
                                var29 = var39;
                                var30 = var39;
                            } else {
                                double var34 = var27 / var26;
                                double var31, var32;
                                if (var24 == var28) {
                                    var31 = var39 * 2 * var34;
                                    var32 = 1 - var34;
                                } else {
                                    var32 = var26 / var35;
                                    const double var33 = var27 / var35;
                                    var31 = var34 * (var39 * 2 * var32 * (var32 - var33) - (var33 - 1) * (var25 - var24));
                                    var32 = (var33 - 1) * (var32 - 1) * (var34 - 1);
                                }
                                if (var31 > 0) {
                                    var32 = -var32;
                                } else {
                                    var31 = -var31;
                                }
                                var34 = var30;
                                var30 = var29;
                                if (2 * var31 < 3 * var39 * var32 - std::abs(var32 * var38) && var31 < std::abs(var32 * var34 * 0.5)) {
                                    var29 = var31 / var32;
                                } else {
                                    var29 = var39;
                                    var30 = var39;
                                }
                            }
                            var24 = var25;
                            var26 = var27;
                            if (std::abs(var29) > var38) {
                                var25 += var29;
                            } else if (var39 > 0) {
                                var25 += var38;
                            } else {
                                var25 -= var38;
                            }
                            var27 = method6869(field4756, field4757, var25);
                            if (var27 * (var35 / std::abs(var35)) > 0) var36 = true;
                            var37 = true;
                        }
                    } while (var37);
                    var23 = var25;
                }
            }
            var6.set(var22, var23);
            if (status > 1 && var6.get(status - 2) >= var6.get(status - 1) - var44) {
                var6.set(status - 2, 0.5 * (var6.get(status - 2) + var6.get(status - 1)));
                --status;
            }
        }
    }
    return status;
}

void method3282(Floats& v) {
    v.set(1, 1 - v.get(1));
    if (v.get(0) < 0) v.set(0, 0);
    if (v.get(1) < 0) v.set(1, 0);
    if (v.get(0) > 1 || v.get(1) > 1) {
        const double var1 = 1 + v.get(0) * (v.get(0) - 2 + v.get(1)) + (v.get(1) - 2) * v.get(1);
        if (var1 + kUlp > 0) {
            if (kUlp + v.get(0) < 1.3333334) {
                const double var2 = v.get(0) - 2;
                const double var3 = v.get(0) - 1;
                const double var4 = std::sqrt(var2 * var2 - 4 * var3 * var3);
                const double var5 = 0.5 * (var4 + -var2);
                if (v.get(1) + kUlp > var5) {
                    v.set(1, var5 - kUlp);
                } else {
                    const double var6 = (-var2 - var4) * 0.5;
                    if (v.get(1) < var6 + kUlp) v.set(1, var6 + kUlp);
                }
            } else {
                v.set(0, 1.3333334 - kUlp);
                v.set(1, 0.33333334 - kUlp);
            }
        }
    }
    v.set(1, 1 - v.get(1));
}

// A curve while it is sampled: its points and the interpolation state the
// client caches between the cycles it samples in order.
class CurveSampler {
public:
    CurveSampler(std::vector<CurvePoint> points, bool bezier) : points_(std::move(points)), bezier_(bezier) {}

    double interpolate(int t) {
        const int index = pointIndex(t);
        if (index < 0 || index >= static_cast<int>(points_.size())) return 0;
        const CurvePoint& point = points_[index];
        const CurvePoint* next = index + 1 < static_cast<int>(points_.size()) ? &points_[index + 1] : nullptr;
        bool bool0 = false;
        bool bool1 = false;
        if (point.field4 == 0 && point.field5 == 0) {
            bool0 = true;
        } else if (point.field4 == FLT_MAX && point.field5 == FLT_MAX) {
            bool1 = true;
        } else if (next == nullptr) {
            bool0 = true;
        } else if (pointIndexUpdated_) {
            updateCoefficients(point, *next);
            pointIndexUpdated_ = false;
        }
        if (bool0) return point.y;
        if (bool1) return point.x != t && next != nullptr ? next->y : point.y;
        if (bezier_) return method8290(t);
        const double var6 = t - v_[0];
        return v_[5] + var6 * ((var6 * v_[2] + v_[3]) * var6 + v_[4]);
    }

private:
    int pointIndex(int t) {
        const auto& p = points_;
        const int last = static_cast<int>(p.size()) - 1;
        if (pointIndex_ < 0 || p[pointIndex_].x > t || (pointIndex_ < last && p[pointIndex_ + 1].x <= t)) {
            if (t < p.front().x || t > p.back().x) return -1;
            int found = pointIndex_;
            int lo = 0;
            int hi = last;
            do {
                const int mid = (lo + hi) >> 1;
                if (t < p[mid].x) {
                    if (mid > 0 && t > p[mid - 1].x) {
                        found = mid - 1;
                        break;
                    }
                    hi = mid - 1;
                } else {
                    if (t <= p[mid].x || (mid < last && t < p[mid + 1].x)) {
                        found = mid;
                        break;
                    }
                    lo = mid + 1;
                }
            } while (lo <= hi);
            if (pointIndex_ != found) {
                pointIndex_ = found;
                pointIndexUpdated_ = true;
            }
        }
        return pointIndex_;
    }

    void updateCoefficients(const CurvePoint& point, const CurvePoint& next) {
        const double var5 = point.x;
        const double var9 = point.y;
        const double var6 = point.field4 * 0.33333334 + var5;
        const double var10 = point.field5 * 0.33333334 + var9;
        const double var8 = next.x;
        const double var12 = next.y;
        const double var7 = var8 - next.field2 * 0.33333334;
        const double var11 = var12 - next.field3 * 0.33333334;
        if (bezier_) {
            double var15 = var10;
            double var16 = var11;
            const double var17 = var8 - var5;
            if (var17 != 0) {
                const double var18 = var6 - var5;
                const double var19 = var7 - var5;
                Floats var29(2);
                var29.set(0, var18 / var17);
                var29.set(1, var19 / var17);
                interpBool_ = static_cast<float>(var29.get(0)) == 0.33333334f && static_cast<float>(var29.get(1)) == 0.6666667f;
                const double var21 = var29.get(0);
                const double var22 = var29.get(1);
                if (var29.get(0) < 0) var29.set(0, 0);
                if (var29.get(1) > 1) var29.set(1, 1);
                if (var29.get(0) > 1 || var29.get(1) < -1) method3282(var29);
                if (var29.get(0) != var21 && var21 != 0) var15 = ((var10 - var9) * var29.get(0)) / var21 + var9;
                if (var22 != var29.get(1) && var22 != 1) var16 = var12 - ((1 - var29.get(1)) * (var12 - var11)) / (1 - var22);
                v_[0] = var5;
                v_[1] = var8;
                const double var23 = var29.get(0);
                const double var24 = var29.get(1);
                double var25 = var23 - 0;
                double var26 = var24 - var23;
                double var27 = 1 - var24;
                double var28 = var26 - var25;
                v_[5] = var27 - var26 - var28;
                v_[4] = var28 + var28 + var28;
                v_[3] = var25 + var25 + var25;
                v_[2] = 0;
                var25 = var15 - var9;
                var26 = var16 - var15;
                var27 = var12 - var16;
                var28 = var26 - var25;
                v_[9] = var27 - var26 - var28;
                v_[8] = var28 + var28 + var28;
                v_[7] = var25 + var25 + var25;
                v_[6] = var9;
            }
        } else {
            v_[0] = var5;
            const double var13 = var8 - var5;
            const double var14 = var12 - var9;
            double var15 = var6 - var5;
            double var16 = 0;
            double var17 = 0;
            if (var15 != 0) var16 = (var10 - var9) / var15;
            var15 = var8 - var7;
            if (var15 != 0) var17 = (var12 - var11) / var15;
            const double var18 = 1 / (var13 * var13);
            const double var19 = var16 * var13;
            const double var20 = var17 * var13;
            v_[2] = (var18 * (var19 + var20 - var14 - var14)) / var13;
            v_[3] = var18 * (var14 + var14 + var14 - var19 - var19 - var20);
            v_[4] = var16;
            v_[5] = var9;
        }
    }

    double method8290(int t) const {
        double v0;
        if (v_[0] == t) {
            v0 = 0;
        } else if (t == v_[1]) {
            v0 = 1;
        } else {
            v0 = (t - v_[0]) / (v_[1] - v_[0]);
        }
        double v1;
        if (interpBool_) {
            v1 = v0;
        } else {
            Floats input(4);
            input.set(3, v_[5]);
            input.set(2, v_[4]);
            input.set(1, v_[3]);
            input.set(0, v_[2] - v0);
            Floats output(5);
            v1 = method5023(input, 3, 0, true, 1, true, output) == 1 ? output.get(0) : 0;
        }
        return v1 * (v_[7] + v1 * (v1 * v_[9] + v_[8])) + v_[6];
    }

    std::vector<CurvePoint> points_;
    bool bezier_;
    int pointIndex_ = 0;
    bool pointIndexUpdated_ = true;
    bool interpBool_ = false;
    // interpV0 to interpV9.
    std::array<double, 10> v_{};
};

// curve.ts Curve.decode and load.
Curve readCurve(Buffer& in) {
    const int count = in.u16();
    in.skip(3); // type, then how it continues before its first point and after its last
    const bool bezier = in.u8() != 0;
    std::vector<CurvePoint> points(count);
    for (CurvePoint& point : points) {
        point.x = in.i16();
        point.y = in.f32();
        point.field2 = in.f32();
        point.field3 = in.f32();
        point.field4 = in.f32();
        point.field5 = in.f32();
    }
    Curve curve;
    if (points.empty()) return curve;
    curve.start = points.front().x;
    const int end = points.back().x;
    if (end < curve.start) throw std::runtime_error("curve ends before it starts");
    CurveSampler sampler(std::move(points), bezier);
    curve.values.reserve(static_cast<std::size_t>(end - curve.start) + 1);
    for (int t = curve.start; t <= end; ++t) curve.values.push_back(static_cast<float>(sampler.interpolate(t)));
    return curve;
}

std::optional<std::vector<std::uint8_t>> firstFile(const Store& store, Index index, int group) {
    auto files = store.files(index, group);
    if (!files || files->empty()) return std::nullopt;
    return std::move(files->begin()->second);
}

constexpr int kBoneTransform = 1;
constexpr int kAlphaTransform = 4;
// How many curves each transform type carries, and which slot each curve
// type fills: a bone's rotation x y z, translation x y z, scale x y z.
constexpr int kCurveCounts[] = {0, 9, 3, 6, 1, 3};
constexpr int kCurveSlots[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4, 5, 0};

} // namespace

std::optional<float> Curve::value(int cycle) const {
    if (cycle < start || cycle > start + static_cast<int>(values.size()) - 1) {
        if (values.empty() && cycle == start) return std::nullopt;
        return 0.0f;
    }
    return values[cycle - start];
}

std::optional<Skeleton> loadSkeleton(const Store& store, int id) {
    const auto data = firstFile(store, Index::Skeletons, id);
    if (!data) return std::nullopt;
    try {
        Buffer in(*data);
        Skeleton skeleton;
        const int count = in.u8();
        skeleton.types.resize(count);
        for (std::uint8_t& type : skeleton.types) {
            const int value = in.u8();
            type = static_cast<std::uint8_t>(value == 6 ? 2 : value);
        }
        skeleton.labels.resize(count);
        for (auto& labels : skeleton.labels) labels.resize(in.u8());
        for (auto& labels : skeleton.labels) {
            for (std::uint8_t& label : labels) label = static_cast<std::uint8_t>(in.u8());
        }
        if (in.remaining() == 0) return skeleton;
        const int boneCount = in.u16();
        if (boneCount == 0) return skeleton;
        skeleton.poseCount = in.u8();
        skeleton.bones.resize(boneCount);
        for (Skeleton::Bone& bone : skeleton.bones) {
            bone.parent = static_cast<std::int16_t>(in.i16());
            for (int pose = 0; pose < skeleton.poseCount; ++pose) {
                Matrix4& m = bone.local.emplace_back();
                for (float& value : m) value = in.f32();
                in.skip(12); // three floats nothing reads
            }
            // As skeleton.ts: a singular rest matrix keeps the previous pose's inverse.
            Matrix4 inverted = identity();
            for (const Matrix4& m : bone.local) {
                if (const auto inverse = invert(m)) inverted = *inverse;
                bone.rotation.push_back(rotationOf(inverted));
                bone.translation.push_back({m[12], m[13], m[14]});
                bone.scale.push_back({static_cast<float>(std::hypot(double{m[0]}, double{m[1]}, double{m[2]})),
                                      static_cast<float>(std::hypot(double{m[4]}, double{m[5]}, double{m[6]})),
                                      static_cast<float>(std::hypot(double{m[8]}, double{m[9]}, double{m[10]}))});
            }
        }
        // Each bone's matrix relative to the model, parents first. A parent
        // chain longer than the bones, a loop, ends as if at a root.
        std::vector<std::vector<Matrix4>> model(boneCount);
        const auto modelMatrix = [&](auto& self, int bone, int pose, int depth) -> const Matrix4& {
            auto& known = model[bone];
            if (known.empty()) {
                const Skeleton::Bone& b = skeleton.bones[bone];
                const bool hasParent = b.parent >= 0 && b.parent < boneCount && depth < boneCount;
                for (int p = 0; p < skeleton.poseCount; ++p) {
                    known.push_back(hasParent ? multiply(self(self, b.parent, p, depth + 1), b.local[p]) : b.local[p]);
                }
            }
            return known[pose];
        };
        for (int bone = 0; bone < boneCount; ++bone) {
            for (int pose = 0; pose < skeleton.poseCount; ++pose) {
                // A singular model matrix, where skeleton.ts would fail, leaves the bone unmoved.
                skeleton.bones[bone].inverseModel.push_back(invert(modelMatrix(modelMatrix, bone, pose, 0)).value_or(identity()));
            }
        }
        return skeleton;
    } catch (const std::runtime_error& error) {
        undecodable(store, "skeleton", id, error);
        return std::nullopt;
    }
}

std::optional<SkeletalAnimation> loadSkeletalAnimation(const Store& store, std::int32_t animMayaId) {
    auto files = store.files(Index::SkeletalAnimations, animMayaId >> 16);
    if (!files) return std::nullopt;
    const auto file = files->find(animMayaId & 0xFFFF);
    if (file == files->end()) return std::nullopt;
    try {
        Buffer in(file->second);
        SkeletalAnimation animation;
        in.u8(); // version
        animation.skeleton = in.u16();
        in.skip(4);
        animation.pose = in.u8();
        const int curveCount = in.u16();
        for (int i = 0; i < curveCount; ++i) {
            int transform = in.u8();
            if (transform > 5) transform = 0;
            const int target = in.shortSmart();
            int curveType = in.u8();
            if (curveType > 16) curveType = 0;
            Curve curve = readCurve(in);
            const int slot = kCurveSlots[curveType];
            if (transform == kAlphaTransform) animation.hasAlpha = true;
            // skeleton.ts files a curve under its target's index; a negative
            // target or slot, a JavaScript array property, is never read.
            if (target < 0 || slot < 0) continue;
            if (transform == kBoneTransform) {
                if (animation.boneCurves.size() <= static_cast<std::size_t>(target)) animation.boneCurves.resize(target + 1);
                animation.boneCurves[target][slot] = std::move(curve);
            } else {
                if (animation.groupCurves.size() <= static_cast<std::size_t>(target)) animation.groupCurves.resize(target + 1);
                auto& curves = animation.groupCurves[target];
                if (curves.empty()) curves.resize(kCurveCounts[transform]);
                if (curves.size() <= static_cast<std::size_t>(slot)) curves.resize(slot + 1);
                curves[slot] = std::move(curve);
            }
        }
        return animation;
    } catch (const std::runtime_error& error) {
        undecodable(store, "skeletal animation", animMayaId, error);
        return std::nullopt;
    }
}

std::vector<Matrix4> boneTransforms(const Skeleton& skeleton, const SkeletalAnimation& animation, int cycle) {
    const int boneCount = static_cast<int>(skeleton.bones.size());
    const int pose = animation.pose;
    // skeleton.ts SkeletalAnimation.animate: each bone's rotation, then scale,
    // then translation, from its curves or its rest pose.
    std::vector<Matrix4> anim(boneCount);
    for (int bone = 0; bone < boneCount; ++bone) {
        const Skeleton::Bone& b = skeleton.bones[bone];
        if (pose >= static_cast<int>(b.local.size())) {
            anim[bone] = identity();
            continue;
        }
        const auto* curves = bone < static_cast<int>(animation.boneCurves.size()) ? &animation.boneCurves[bone] : nullptr;
        const auto pick = [&](int slot, float rest) -> double {
            const std::optional<Curve>* curve = curves != nullptr ? &(*curves)[slot] : nullptr;
            return curve != nullptr && *curve ? (*curve)->value(cycle).value_or(rest) : rest;
        };
        const auto& r = b.rotation[pose];
        Quat q{0, 0, 0, 1};
        q = multiply(axisAngle(2, pick(2, r[2])), q);
        q = multiply(axisAngle(0, pick(0, r[0])), q);
        q = multiply(axisAngle(1, pick(1, r[1])), q);
        Matrix4 m = multiply(fromQuat(q), identity());
        const auto& s = b.scale[pose];
        Matrix4 scaling = identity();
        scaling[0] = static_cast<float>(pick(6, s[0]));
        scaling[5] = static_cast<float>(pick(7, s[1]));
        scaling[10] = static_cast<float>(pick(8, s[2]));
        m = multiply(scaling, m);
        const auto& t = b.translation[pose];
        m[12] = static_cast<float>(pick(3, t[0]));
        m[13] = static_cast<float>(pick(4, t[1]));
        m[14] = static_cast<float>(pick(5, t[2]));
        anim[bone] = m;
    }
    // Bone.final: the animated matrix down from the root, after the inverse
    // of the rest pose's.
    std::vector<std::optional<Matrix4>> animModel(boneCount);
    const auto modelOf = [&](auto& self, int bone, int depth) -> const Matrix4& {
        if (!animModel[bone]) {
            const int parent = skeleton.bones[bone].parent;
            const bool hasParent = parent >= 0 && parent < boneCount && depth < boneCount;
            animModel[bone] = hasParent ? multiply(self(self, parent, depth + 1), anim[bone]) : anim[bone];
        }
        return *animModel[bone];
    };
    std::vector<Matrix4> transforms(boneCount);
    for (int bone = 0; bone < boneCount; ++bone) {
        const auto& inverse = skeleton.bones[bone].inverseModel;
        transforms[bone] = pose < static_cast<int>(inverse.size()) ? multiply(modelOf(modelOf, bone, 0), inverse[pose]) : identity();
    }
    return transforms;
}

void skin(std::span<std::int32_t> x, std::span<std::int32_t> y, std::span<std::int32_t> z,
          std::span<const std::vector<std::uint8_t>> bones, std::span<const std::vector<std::uint8_t>> weights,
          std::span<const Matrix4> transforms) {
    // rig.ts poseSkeletal, with JavaScript's Math.round (halves up).
    const auto round = [](double v) { return static_cast<std::int32_t>(std::floor(v + 0.5)); };
    const std::size_t count = std::min({x.size(), y.size(), z.size(), bones.size(), weights.size()});
    for (std::size_t i = 0; i < count; ++i) {
        if (bones[i].empty()) continue;
        std::array<double, 16> m{};
        for (std::size_t k = 0; k < bones[i].size() && k < weights[i].size(); ++k) {
            if (bones[i][k] >= transforms.size()) continue;
            const double scale = weights[i][k] / 255.0;
            const Matrix4& f = transforms[bones[i][k]];
            for (int j = 0; j < 16; ++j) m[j] += scale * f[j];
        }
        const double vx = x[i];
        const double vy = -static_cast<double>(y[i]);
        const double vz = -static_cast<double>(z[i]);
        x[i] = round(m[0] * vx + m[4] * vy + m[8] * vz + m[12]);
        y[i] = -round(m[1] * vx + m[5] * vy + m[9] * vz + m[13]);
        z[i] = -round(m[2] * vx + m[6] * vy + m[10] * vz + m[14]);
    }
}

} // namespace osrs::cache
