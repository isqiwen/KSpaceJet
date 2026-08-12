#include "common.hpp"

namespace {

using namespace ksj::research::cpp_numerics_performance;

template <typename T>
void cg_update_split(Vector<T>& x, Vector<T>& r, Vector<T>& p, const Vector<T>& ap, T alpha, T beta, T& rr) {
  for (std::size_t i = 0; i < x.size(); ++i) {
    x(i) += alpha * p(i);
  }
  for (std::size_t i = 0; i < x.size(); ++i) {
    r(i) -= alpha * ap(i);
  }
  rr = T{};
  for (std::size_t i = 0; i < x.size(); ++i) {
    rr += r(i) * r(i);
  }
  for (std::size_t i = 0; i < x.size(); ++i) {
    p(i) = r(i) + beta * p(i);
  }
}

template <typename T>
void cg_update_fused(Vector<T>& x, Vector<T>& r, Vector<T>& p, const Vector<T>& ap, T alpha, T beta, T& rr) {
  auto rr_value = T{};
  for (std::size_t i = 0; i < x.size(); ++i) {
    const auto p_old = p(i);
    const auto r_new = r(i) - alpha * ap(i);
    x(i) += alpha * p_old;
    r(i) = r_new;
    rr_value += r_new * r_new;
    p(i) = r_new + beta * p_old;
  }
  rr = rr_value;
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    auto x = make_vector<T>(size);
    auto r = make_vector<T>(size);
    auto p = make_vector<T>(size);
    auto ap = make_vector<T>(size);
    ksj::research::cpp_numerics_performance::fill_vector(x);
    ksj::research::cpp_numerics_performance::fill_vector(r);
    ksj::research::cpp_numerics_performance::fill_vector(p);
    ksj::research::cpp_numerics_performance::fill_vector(ap);

    auto rr = T{};
    const auto alpha = static_cast<T>(0.01F);
    const auto beta = static_cast<T>(0.25F);
    const auto split_measurement = ksj::research::cpp_numerics_performance::measure(config, [&] {
      cg_update_split(x, r, p, ap, alpha, beta, rr);
      ksj::research::cpp_numerics_performance::do_not_optimize(rr);
    });
    ksj::research::cpp_numerics_performance::print_row("cg_update", "split_axpy_reduction", type_name, size, 0, config,
                                                       split_measurement, static_cast<double>(rr));

    ksj::research::cpp_numerics_performance::fill_vector(x);
    ksj::research::cpp_numerics_performance::fill_vector(r);
    ksj::research::cpp_numerics_performance::fill_vector(p);
    ksj::research::cpp_numerics_performance::fill_vector(ap);
    const auto fused_measurement = ksj::research::cpp_numerics_performance::measure(config, [&] {
      cg_update_fused(x, r, p, ap, alpha, beta, rr);
      ksj::research::cpp_numerics_performance::do_not_optimize(rr);
    });
    ksj::research::cpp_numerics_performance::print_row("cg_update", "single_fused_pass", type_name, size, 0, config,
                                                       fused_measurement, static_cast<double>(rr));
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config, "usage: ksj_numerics_perf_cg_update [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
