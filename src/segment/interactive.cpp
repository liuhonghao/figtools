// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Interactive segmentation: magic wand and GrabCut, implemented from scratch.

#include "figtools/segment/interactive.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <queue>
#include <utility>
#include <vector>

namespace figtools::segment {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct Rgb {
  double r = 0.0, g = 0.0, b = 0.0;
};

Rgb pixel_rgb(const ImageView& image, int x, int y) {
  const Rgba c = image.at(x, y);
  return {static_cast<double>(c.r), static_cast<double>(c.g), static_cast<double>(c.b)};
}

double dist2(const Rgb& a, const Rgb& b) {
  const double dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
  return dr * dr + dg * dg + db * db;
}

// ---------------------------------------------------------------------------
// Dinic maximum flow on an integer-capacity graph (for GrabCut's min-cut).
// ---------------------------------------------------------------------------
class MaxFlow {
 public:
  explicit MaxFlow(std::size_t n) : graph_(n) {}

  void add_edge(std::size_t u, std::size_t v, int capacity) {
    Edge forward{v, static_cast<std::size_t>(graph_[v].size()), capacity};
    Edge backward{u, static_cast<std::size_t>(graph_[u].size()), 0};
    graph_[u].push_back(forward);
    graph_[v].push_back(backward);
  }

  int maxflow(std::size_t source, std::size_t sink) {
    int flow = 0;
    constexpr int kInf = std::numeric_limits<int>::max() / 4;
    while (bfs(source, sink)) {
      std::fill(work_.begin(), work_.end(), 0);
      if (work_.size() != graph_.size()) work_.assign(graph_.size(), 0);
      while (true) {
        const int pushed = dfs(source, sink, kInf);
        if (pushed == 0) break;
        flow += pushed;
      }
    }
    return flow;
  }

  // Returns source-side membership after the flow (nodes reachable in the
  // residual graph from source).
  [[nodiscard]] std::vector<char> source_side(std::size_t source) const {
    std::vector<char> visited(graph_.size(), 0);
    std::queue<std::size_t> q;
    visited[source] = 1;
    q.push(source);
    while (!q.empty()) {
      const std::size_t u = q.front();
      q.pop();
      for (const Edge& e : graph_[u]) {
        if (e.capacity > 0 && !visited[e.to]) {
          visited[e.to] = 1;
          q.push(e.to);
        }
      }
    }
    return visited;
  }

 private:
  struct Edge {
    std::size_t to = 0;
    std::size_t rev = 0;
    int capacity = 0;
  };
  std::vector<std::vector<Edge>> graph_;
  std::vector<int> level_;
  std::vector<std::size_t> work_;

  bool bfs(std::size_t source, std::size_t sink) {
    level_.assign(graph_.size(), -1);
    std::queue<std::size_t> q;
    level_[source] = 0;
    q.push(source);
    while (!q.empty()) {
      const std::size_t u = q.front();
      q.pop();
      for (const Edge& e : graph_[u]) {
        if (e.capacity > 0 && level_[e.to] < 0) {
          level_[e.to] = level_[u] + 1;
          q.push(e.to);
        }
      }
    }
    return level_[sink] >= 0;
  }

  int dfs(std::size_t u, std::size_t sink, int pushed) {
    if (pushed == 0) return 0;
    if (u == sink) return pushed;
    for (std::size_t& cid = work_[u]; cid < graph_[u].size(); ++cid) {
      Edge& e = graph_[u][cid];
      if (e.capacity <= 0 || level_[e.to] != level_[u] + 1) continue;
      const int tr = dfs(e.to, sink, std::min(pushed, e.capacity));
      if (tr == 0) continue;
      e.capacity -= tr;
      graph_[e.to][e.rev].capacity += tr;
      return tr;
    }
    return 0;
  }
};

// A small diagonal-covariance Gaussian in RGB.
struct Gaussian {
  Rgb mean{};
  double inv_var[3] = {1.0, 1.0, 1.0};
  double weight = 0.0;
  double log_weight = 0.0;
  double det_term = 0.0;

  void prepare() {
    log_weight = weight > 1e-6 ? std::log(weight) : -20.0;
    det_term = 0.0;
    for (int i = 0; i < 3; ++i) {
      const double variance = std::max(25.0, inv_var[i] > 0 ? 1.0 / inv_var[i] : 25.0);
      inv_var[i] = 1.0 / variance;
      det_term += std::log(1.0 / inv_var[i]);
    }
    det_term = 0.5 * det_term;
  }

  [[nodiscard]] double neg_log_prob(const Rgb& c) const {
    double sum = 0.0;
    const double v[3] = {c.r - mean.r, c.g - mean.g, c.b - mean.b};
    const double inv[3] = {inv_var[0], inv_var[1], inv_var[2]};
    const double* mv[3] = {&v[0], &v[1], &v[2]};
    for (int i = 0; i < 3; ++i) sum += (*mv[i]) * (*mv[i]) * inv[i];
    return det_term - log_weight + 0.5 * sum;
  }
};

constexpr int kGaussians = 3;
struct Gmm {
  std::array<Gaussian, kGaussians> g;

  void fit(const std::vector<Rgb>& samples, int iterations = 8) {
    if (samples.empty()) return;
    // k-means++-ish seeding: spread initial means by sample index.
    for (int k = 0; k < kGaussians; ++k) {
      const std::size_t index = static_cast<std::size_t>(samples.size()) *
                                static_cast<std::size_t>(k) / kGaussians;
      g[static_cast<std::size_t>(k)].mean = samples[index];
    }
    std::vector<int> assignment(samples.size(), 0);
    for (int iter = 0; iter < iterations; ++iter) {
      // Assign.
      for (std::size_t i = 0; i < samples.size(); ++i) {
        int best = 0;
        double best_d = std::numeric_limits<double>::max();
        for (int k = 0; k < kGaussians; ++k) {
          const double d = dist2(samples[i], g[static_cast<std::size_t>(k)].mean);
          if (d < best_d) { best_d = d; best = k; }
        }
        assignment[i] = best;
      }
      // Update.
      std::array<Rgb, kGaussians> sums{};
      std::array<int, kGaussians> counts{};
      for (std::size_t i = 0; i < samples.size(); ++i) {
        const int k = assignment[i];
        sums[static_cast<std::size_t>(k)].r += samples[i].r;
        sums[static_cast<std::size_t>(k)].g += samples[i].g;
        sums[static_cast<std::size_t>(k)].b += samples[i].b;
        ++counts[static_cast<std::size_t>(k)];
      }
      for (int k = 0; k < kGaussians; ++k) {
        const int count = std::max(1, counts[static_cast<std::size_t>(k)]);
        g[static_cast<std::size_t>(k)].mean = {
            sums[static_cast<std::size_t>(k)].r / count,
            sums[static_cast<std::size_t>(k)].g / count,
            sums[static_cast<std::size_t>(k)].b / count};
      }
    }
    // Covariances and mixture weights.
    std::array<double, kGaussians> var_sum{};
    std::array<int, kGaussians> counts{};
    for (std::size_t i = 0; i < samples.size(); ++i) {
      const int k = assignment[i];
      const Gaussian& comp = g[static_cast<std::size_t>(k)];
      const double dr = samples[i].r - comp.mean.r;
      const double dg = samples[i].g - comp.mean.g;
      const double db = samples[i].b - comp.mean.b;
      var_sum[static_cast<std::size_t>(k)] += (dr * dr + dg * dg + db * db) / 3.0;
      ++counts[static_cast<std::size_t>(k)];
    }
    for (int k = 0; k < kGaussians; ++k) {
      Gaussian& comp = g[static_cast<std::size_t>(k)];
      const double variance = std::max(25.0, var_sum[static_cast<std::size_t>(k)] /
                                              std::max(1, counts[static_cast<std::size_t>(k)]));
      comp.inv_var[0] = comp.inv_var[1] = comp.inv_var[2] = 1.0 / variance;
      comp.weight = static_cast<double>(counts[static_cast<std::size_t>(k)]) /
                    static_cast<double>(std::max<std::size_t>(1, samples.size()));
      comp.prepare();
    }
  }

  [[nodiscard]] double neg_log_prob(const Rgb& c) const {
    double best = std::numeric_limits<double>::max();
    for (int k = 0; k < kGaussians; ++k) {
      best = std::min(best, g[static_cast<std::size_t>(k)].neg_log_prob(c));
    }
    return best;
  }
};

inline std::size_t index_of(int x, int y, int w) {
  return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
         static_cast<std::size_t>(x);
}

// Moore-neighbour contour tracing: walk the outer boundary of a connected
// component, returning its boundary pixels in order.
std::vector<Point2f> trace_contour(const Mask& mask, int start_x, int start_y) {
  // 8-neighbourhood clockwise from East.
  static const int dx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
  static const int dy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  std::vector<Point2f> points;
  int x = start_x, y = start_y;
  int direction = 6;  // start looking West (came from left)
  const std::size_t max_steps = static_cast<std::size_t>(mask.width) *
                                static_cast<std::size_t>(mask.height) * 8;
  for (std::size_t step = 0; step < max_steps; ++step) {
    points.push_back({static_cast<float>(x), static_cast<float>(y)});
    bool moved = false;
    for (int i = 0; i < 8; ++i) {
      const int dir = (direction + i) % 8;
      const int nx = x + dx[dir], ny = y + dy[dir];
      if (mask.at(nx, ny)) {
        // Move to the first foreground neighbour, turning back one step.
        direction = (dir + 5) % 8;
        x = nx;
        y = ny;
        moved = true;
        break;
      }
    }
    if (!moved) break;
    if (x == start_x && y == start_y) break;
  }
  return points;
}

// Ramer-Douglas-Peucker simplification on a closed polygon.
std::vector<Point2f> simplify(const std::vector<Point2f>& points, double epsilon) {
  if (epsilon <= 0.0 || points.size() < 4) return points;
  const std::size_t n = points.size();
  auto perp_distance = [&](const Point2f& p, const Point2f& a, const Point2f& b) -> double {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::hypot(dx, dy);
    if (len < 1e-9) return std::hypot(p.x - a.x, p.y - a.y);
    return std::abs(dy * static_cast<double>(p.x) - dx * static_cast<double>(p.y) + static_cast<double>(b.x) * a.y - static_cast<double>(b.y) * a.x) / len;
  };
  std::vector<char> keep(n, 0);
  std::vector<std::pair<std::size_t, std::size_t>> stack;
  stack.emplace_back(0, n - 1);
  keep[0] = keep[n - 1] = 1;
  while (!stack.empty()) {
    const auto [a, b] = stack.back();
    stack.pop_back();
    if (b <= a + 1) continue;
    double max_d = 0.0;
    std::size_t max_i = a;
    for (std::size_t i = a + 1; i < b; ++i) {
      const double d = perp_distance(points[i], points[a], points[b]);
      if (d > max_d) { max_d = d; max_i = i; }
    }
    if (max_d > epsilon) {
      keep[max_i] = 1;
      stack.emplace_back(a, max_i);
      stack.emplace_back(max_i, b);
    }
  }
  std::vector<Point2f> result;
  for (std::size_t i = 0; i < n; ++i) {
    if (keep[i]) result.push_back(points[i]);
  }
  return result;
}

}  // namespace

std::size_t Mask::count() const {
  return static_cast<std::size_t>(std::count(bits.begin(), bits.end(), std::uint8_t{1}));
}

Mask magic_wand(const ImageView& image, int seed_x, int seed_y, double tolerance) {
  Mask mask;
  if (image.empty() || !image.contains(seed_x, seed_y)) return mask;
  mask.width = image.width();
  mask.height = image.height();
  mask.bits.assign(static_cast<std::size_t>(mask.width) * static_cast<std::size_t>(mask.height),
                   0);

  const Rgb seed = pixel_rgb(image, seed_x, seed_y);
  const double threshold = std::max(1.0, tolerance) * std::max(1.0, tolerance);
  std::queue<std::pair<int, int>> q;
  q.emplace(seed_x, seed_y);
  mask.bits[index_of(seed_x, seed_y, mask.width)] = 1;
  static const int dx[4] = {1, -1, 0, 0};
  static const int dy[4] = {0, 0, 1, -1};
  while (!q.empty()) {
    const auto [x, y] = q.front();
    q.pop();
    for (int i = 0; i < 4; ++i) {
      const int nx = x + dx[i], ny = y + dy[i];
      if (!image.contains(nx, ny)) continue;
      const std::size_t ni = index_of(nx, ny, mask.width);
      if (mask.bits[ni] != 0) continue;
      if (dist2(pixel_rgb(image, nx, ny), seed) > threshold) continue;
      mask.bits[ni] = 1;
      q.emplace(nx, ny);
    }
  }
  return mask;
}

Mask largest_component(const Mask& mask) {
  Mask result;
  if (mask.empty()) return result;
  result.width = mask.width;
  result.height = mask.height;
  result.bits.assign(mask.bits.size(), 0);
  std::vector<char> visited(mask.bits.size(), 0);
  std::vector<int> best;
  static const int dx[4] = {1, -1, 0, 0};
  static const int dy[4] = {0, 0, 1, -1};
  auto flood = [&](int sx, int sy, std::vector<int>& component) {
    std::queue<std::pair<int, int>> q;
    q.emplace(sx, sy);
    visited[index_of(sx, sy, mask.width)] = 1;
    while (!q.empty()) {
      const auto [x, y] = q.front();
      q.pop();
      component.push_back(y * mask.width + x);
      for (int i = 0; i < 4; ++i) {
        const int nx = x + dx[i], ny = y + dy[i];
        if (!mask.at(nx, ny)) continue;
        const std::size_t ni = index_of(nx, ny, mask.width);
        if (visited[ni]) continue;
        visited[ni] = 1;
        q.emplace(nx, ny);
      }
    }
  };
  for (int y = 0; y < mask.height; ++y) {
    for (int x = 0; x < mask.width; ++x) {
      if (!mask.at(x, y) || visited[index_of(x, y, mask.width)]) continue;
      std::vector<int> component;
      flood(x, y, component);
      if (component.size() > best.size()) best = std::move(component);
    }
  }
  for (int index : best) result.bits[static_cast<std::size_t>(index)] = 1;
  return result;
}

std::vector<Point2f> mask_to_polygon(const Mask& input, double epsilon) {
  const Mask mask = largest_component(input);
  if (mask.empty()) return {};
  int start_x = -1, start_y = -1;
  for (int y = 0; y < mask.height && start_x < 0; ++y) {
    for (int x = 0; x < mask.width; ++x) {
      if (mask.at(x, y)) { start_x = x; start_y = y; break; }
    }
  }
  if (start_x < 0) return {};
  auto contour = trace_contour(mask, start_x, start_y);
  if (contour.size() < 3) return {};
  return simplify(contour, epsilon);
}

Mask grabcut(const ImageView& image, RectF rect, int iterations) {
  Mask result;
  if (image.empty()) return result;
  // Clamp to image.
  const int x0 = std::max(0, static_cast<int>(std::floor(rect.x)));
  const int y0 = std::max(0, static_cast<int>(std::floor(rect.y)));
  const int x1 = std::min(image.width(), static_cast<int>(std::ceil(rect.x + rect.width)));
  const int y1 = std::min(image.height(), static_cast<int>(std::ceil(rect.y + rect.height)));
  if (x1 - x0 < 4 || y1 - y0 < 4) return result;

  result.width = image.width();
  result.height = image.height();
  result.bits.assign(static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height),
                     0);

  // Work on a bounded working copy: graph cuts cost O(pixels), so cap the
  // working resolution at 256px on the long side. The resulting mask is mapped
  // back to full resolution below.
  const int full_w = x1 - x0, full_h = y1 - y0;
  const double scale = std::min(1.0, 256.0 / std::max(full_w, full_h));
  const int w = std::max(4, static_cast<int>(full_w * scale));
  const int h = std::max(4, static_cast<int>(full_h * scale));
  const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
  const std::size_t source = n, sink = n + 1;

  // Downsample the rectangle into the working buffer (box average).
  std::vector<Rgb> colours(n);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int sx0 = x0 + static_cast<int>(x / scale);
      const int sy0 = y0 + static_cast<int>(y / scale);
      const int sx1 = std::min(x1, sx0 + std::max(1, static_cast<int>(1.0 / scale)));
      const int sy1 = std::min(y1, sy0 + std::max(1, static_cast<int>(1.0 / scale)));
      double r = 0, g = 0, b = 0;
      int count = 0;
      for (int sy = sy0; sy < sy1; ++sy) {
        for (int sx = sx0; sx < sx1; ++sx) {
          const Rgb c = pixel_rgb(image, sx, sy);
          r += c.r; g += c.g; b += c.b;
          ++count;
        }
      }
      const double inv = count > 0 ? 1.0 / count : 0.0;
      colours[index_of(x, y, w)] = {r * inv, g * inv, b * inv};
    }
  }

  // Labels: 0 = background, 1 = foreground.
  std::vector<char> labels(n, 1);
  // Border pixels of the rectangle are known background.
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (x == 0 || y == 0 || x == w - 1 || y == h - 1) labels[index_of(x, y, w)] = 0;
    }
  }
  // Conservative inner foreground seed: centre 50% of the rectangle.
  const int cx0 = w / 4, cx1 = w * 3 / 4;
  const int cy0 = h / 4, cy1 = h * 3 / 4;

  Gmm fg_gmm, bg_gmm;
  constexpr int kInf = 100000000;
  const int passes = std::clamp(iterations, 1, 8);

  for (int pass = 0; pass < passes; ++pass) {
    // Fit GMMs from current labels.
    std::vector<Rgb> fg_samples, bg_samples;
    fg_samples.reserve(n);
    bg_samples.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      (labels[i] ? fg_samples : bg_samples).push_back(colours[i]);
    }
    if (fg_samples.empty()) labels[index_of(w / 2, h / 2, w)] = 1;
    if (bg_samples.empty()) labels[index_of(0, 0, w)] = 0;
    fg_gmm.fit(fg_samples);
    bg_gmm.fit(bg_samples);

    // Build graph: data terms + pairwise smoothness.
    MaxFlow flow(n + 2);
    // Pairwise beta from average colour contrast.
    double beta_sum = 0.0;
    int beta_count = 0;
    for (int y = 0; y < h; ++y) {
      for (int x = 1; x < w; ++x) {
        beta_sum += dist2(colours[index_of(x, y, w)], colours[index_of(x - 1, y, w)]);
        ++beta_count;
      }
    }
    const double beta = beta_count > 0 ? beta_sum / static_cast<double>(beta_count) : 1000.0;
    const double gamma = 50.0;
    const auto pairwise = [&](const Rgb& a, const Rgb& b) {
      return static_cast<int>(std::clamp(gamma * std::exp(-dist2(a, b) /
                                              std::max(1.0, 2.0 * beta)), 1.0, 60.0));
    };

    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const std::size_t i = index_of(x, y, w);
        const double fg_cost = fg_gmm.neg_log_prob(colours[i]);
        const double bg_cost = bg_gmm.neg_log_prob(colours[i]);
        const int to_sink = static_cast<int>(std::clamp(fg_cost, 0.0, 10000.0));
        const int from_source = static_cast<int>(std::clamp(bg_cost, 0.0, 10000.0));
        const bool border = (x == 0 || y == 0 || x == w - 1 || y == h - 1);
        const bool centre = (x >= cx0 && x < cx1 && y >= cy0 && y < cy1);
        if (border) {
          flow.add_edge(source, i, 0);
          flow.add_edge(i, sink, kInf);
        } else if (centre) {
          flow.add_edge(source, i, kInf);
          flow.add_edge(i, sink, 0);
        } else {
          flow.add_edge(source, i, from_source);
          flow.add_edge(i, sink, to_sink);
        }
        if (x + 1 < w) flow.add_edge(i, i + 1, pairwise(colours[i], colours[i + 1]));
        if (y + 1 < h) flow.add_edge(i, i + static_cast<std::size_t>(w),
                                     pairwise(colours[i], colours[index_of(x, y + 1, w)]));
      }
    }
    flow.maxflow(source, sink);
    const std::vector<char> side = flow.source_side(source);
    for (std::size_t i = 0; i < n; ++i) labels[i] = side[i] ? 1 : 0;
  }

  // Map back to full-image coordinates.
  for (int y = 0; y < full_h; ++y) {
    for (int x = 0; x < full_w; ++x) {
      const int wx = std::clamp(static_cast<int>(x * scale), 0, w - 1);
      const int wy = std::clamp(static_cast<int>(y * scale), 0, h - 1);
      if (labels[index_of(wx, wy, w)]) {
        result.bits[index_of(x0 + x, y0 + y, result.width)] = 1;
      }
    }
  }
  return largest_component(result);
}

}  // namespace figtools::segment
