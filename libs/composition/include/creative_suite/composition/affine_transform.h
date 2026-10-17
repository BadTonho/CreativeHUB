#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace creative_suite::composition {

// Column vectors: (x',y') = (a*x+c*y+tx, b*x+d*y+ty). No Qt or media ownership.
// Render inputs map source pixel coordinates directly to canvas pixels; callers
// include fit and the source-center translation exactly once.
struct AffineTransform2D {
    double a=1, b=0, c=0, d=1, tx=0, ty=0;
    friend bool operator==(const AffineTransform2D&,const AffineTransform2D&)=default;
    [[nodiscard]] std::array<double,2> map(double x,double y) const noexcept {
        return {a*x+c*y+tx,b*x+d*y+ty};
    }
};
inline bool finiteAffine(const AffineTransform2D& m) noexcept {
    return std::isfinite(m.a)&&std::isfinite(m.b)&&std::isfinite(m.c)&&
        std::isfinite(m.d)&&std::isfinite(m.tx)&&std::isfinite(m.ty);
}
inline std::optional<AffineTransform2D> inverseAffine(const AffineTransform2D& m) noexcept {
    if(!finiteAffine(m))return {};
    const double size=std::max({std::abs(m.a),std::abs(m.b),std::abs(m.c),std::abs(m.d)});
    if(size==0)return {};
    const double a=m.a/size,b=m.b/size,c=m.c/size,d=m.d/size,det=a*d-b*c;
    if(!std::isfinite(det)||det==0)return {};
    AffineTransform2D result{(d/det)/size,(-b/det)/size,(-c/det)/size,(a/det)/size,0,0};
    result.tx=-(result.a*m.tx+result.c*m.ty);result.ty=-(result.b*m.tx+result.d*m.ty);
    if(!finiteAffine(result))return {};
    return result;
}
inline bool validAffine(const AffineTransform2D& m) noexcept {return inverseAffine(m).has_value();}
inline AffineTransform2D composeAffine(const AffineTransform2D& l,const AffineTransform2D& r) {
    if(!validAffine(l)||!validAffine(r))throw std::runtime_error("Affine operands require finite inverses.");
    AffineTransform2D m{l.a*r.a+l.c*r.b,l.b*r.a+l.d*r.b,l.a*r.c+l.c*r.d,l.b*r.c+l.d*r.d,
        l.a*r.tx+l.c*r.ty+l.tx,l.b*r.tx+l.d*r.ty+l.ty};
    if(!validAffine(m))throw std::runtime_error("Affine geometry exceeds finite invertible limits.");
    return m;
}
inline AffineTransform2D requireInverseAffine(const AffineTransform2D& m) {
    const auto inverse=inverseAffine(m);
    if(!inverse)throw std::runtime_error("Affine geometry has no finite inverse.");
    return *inverse;
}
// Empty/off-canvas/overflowing bounds produce no pixels. Clamp before integer
// conversion, including when a valid off-canvas translation is extremely large.
inline std::optional<std::array<int,4>> affinePixelBounds(const AffineTransform2D& m,
    int source_width,int source_height,int width,int height) noexcept {
    const std::array points{m.map(0,0),m.map(source_width,0),m.map(0,source_height),m.map(source_width,source_height)};
    double x0=points[0][0],x1=x0,y0=points[0][1],y1=y0;
    for(const auto& p:points){if(!std::isfinite(p[0])||!std::isfinite(p[1]))return {};
        x0=std::min(x0,p[0]);x1=std::max(x1,p[0]);y0=std::min(y0,p[1]);y1=std::max(y1,p[1]);}
    if(width<=0||height<=0||x1<0||y1<0||x0>=width||y0>=height)return {};
    return std::array{int(std::floor(std::clamp(x0,0.,double(width-1)))),
        int(std::ceil(std::clamp(x1,0.,double(width-1)))),
        int(std::floor(std::clamp(y0,0.,double(height-1)))),int(std::ceil(std::clamp(y1,0.,double(height-1))))};
}
} // namespace creative_suite::composition
