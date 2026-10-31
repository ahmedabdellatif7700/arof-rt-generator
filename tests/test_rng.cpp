#include "check.h"
#include "core/rng.h"

using namespace arof;

TEST(same_seed_same_stream) {
    Rng a(2017, Stream::pd_noise), b(2017, Stream::pd_noise);
    for (int i = 0; i < 1000; ++i) CHECK(a.next() == b.next());
}

TEST(streams_differ) {
    Rng a(2017, Stream::pd_noise), b(2017, Stream::ue_noise), c(2018, Stream::pd_noise);
    int same_ab = 0, same_ac = 0;
    for (int i = 0; i < 1000; ++i) {
        const auto x = a.next();
        same_ab += x == b.next();
        same_ac += x == c.next();
    }
    CHECK(same_ab == 0);
    CHECK(same_ac == 0);
}

TEST(normal_moments) {
    Rng r(1, Stream::pd_noise);
    const int n = 2000000;
    double s1 = 0, s2 = 0, s3 = 0, s4 = 0;
    int tail3 = 0, tail4 = 0;
    for (int i = 0; i < n; ++i) {
        const double x = r.normal();
        s1 += x;
        s2 += x * x;
        s3 += x * x * x;
        s4 += x * x * x * x;
        tail3 += std::fabs(x) > 3;
        tail4 += std::fabs(x) > 3.442619855899;   // ziggurat tail region
    }
    CHECK_NEAR(s1 / n, 0, 0.005);
    CHECK_NEAR(s2 / n, 1, 0.005);
    CHECK_NEAR(s3 / n, 0, 0.02);
    CHECK_NEAR(s4 / n, 3, 0.03);
    CHECK_NEAR(static_cast<double>(tail3) / n, std::erfc(3 / std::sqrt(2.0)), 2e-4);
    CHECK_NEAR(static_cast<double>(tail4) / n, std::erfc(3.442619855899 / std::sqrt(2.0)), 1e-4);
}

TEST(symbols_uniform) {
    Rng r(2017, Stream::symbols);
    int count[16] = {};
    const int n = 1600000;
    for (int i = 0; i < n; ++i) ++count[r.below_pow2(4)];
    for (int c : count) CHECK_NEAR(c, n / 16.0, 5 * std::sqrt(n / 16.0));
}

TEST_MAIN()
