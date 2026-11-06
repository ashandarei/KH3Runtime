#pragma once
#include <algorithm>
#include <optional>

#include "kh3ap/features/ui/map_overlay/map_overlay_math.h"
#include <span>
#include <array>
#include <vector>

namespace kh3ap
{
    inline constexpr auto minimap_singular_determinant_tolerance = 1e-10;

    inline auto minimap_matrix_product(const MapOverlayMatrix& p, const MapOverlayMatrix& q)
        -> MapOverlayMatrix
    {
        return {p.a * q.a + p.c * q.b,          p.b * q.a + p.d * q.b,
                p.a * q.c + p.c * q.d,          p.b * q.c + p.d * q.d,
                p.a * q.tx + p.c * q.ty + p.tx, p.b * q.tx + p.d * q.ty + p.ty};
    }
    inline auto minimap_local_matrix(double x, double y, double sx, double sy, double degrees)
        -> MapOverlayMatrix
    {
        const double r = degrees * map_overlay_settings::degrees_to_radians, c = std::cos(r),
                     s = std::sin(r);
        return {sx * c, sx * s, -sy * s, sy * c, x, y};
    }
    // Scalar-only cache. The caller supplies live parent/layout inputs and clears
    // on weak owner/viewport changes. Canvas movement/rotation never needs AS
    // getBounds or concatenatedMatrix reads after the parent has been calibrated.
    class MinimapProjectionCache
    {
    public:
        void clear()
        {
            projection_.reset();
            signature_.clear();
        }
        template <class Read>
        auto resolve(const MapOverlayMatrix& local, std::span<const double> signature, Read&& read)
            -> std::optional<MinimapIconProjection>
        {
            const double det = local.a * local.d - local.b * local.c;
            for (double v : {local.a, local.b, local.c, local.d, local.tx, local.ty})
            {
                if (!std::isfinite(v))
                {
                    clear();
                    return {};
                }
            }
            for (double v : signature)
            {
                if (!std::isfinite(v))
                {
                    clear();
                    return {};
                }
            }
            if (!std::isfinite(det) || std::abs(det) < minimap_singular_determinant_tolerance)
            {
                clear();
                return {};
            }
            if (!projection_ || signature_.size() != signature.size() ||
                !std::equal(signature.begin(), signature.end(), signature_.begin()))
            {
                const auto projection = read();
                if (!projection)
                {
                    clear();
                    return {};
                }
                for (double v : {projection->canvas.a, projection->canvas.b, projection->canvas.c,
                                 projection->canvas.d, projection->canvas.tx, projection->canvas.ty,
                                 projection->source_width, projection->source_height, projection->x,
                                 projection->y, projection->width, projection->height})
                {
                    if (!std::isfinite(v))
                    {
                        clear();
                        return {};
                    }
                }
                if (projection->width <= 0 || projection->height <= 0 ||
                    projection->source_width <= 0 || projection->source_height <= 0)
                {
                    clear();
                    return {};
                }
                const MapOverlayMatrix inverse{local.d / det,
                                               -local.b / det,
                                               -local.c / det,
                                               local.a / det,
                                               (local.c * local.ty - local.d * local.tx) / det,
                                               (local.b * local.tx - local.a * local.ty) / det};
                parent_ = minimap_matrix_product(projection->canvas, inverse);
                projection_ = projection;
                signature_.assign(signature.begin(), signature.end());
            }
            auto result = *projection_;
            result.canvas = minimap_matrix_product(parent_, local);
            return result;
        }

    private:
        std::optional<MinimapIconProjection> projection_;
        MapOverlayMatrix parent_;
        std::vector<double> signature_;
    };
} // namespace kh3ap
