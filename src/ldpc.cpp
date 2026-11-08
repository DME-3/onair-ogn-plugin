// SPDX-License-Identifier: GPL-3.0-or-later
// Original bounded decoder for the public OGN1 (208,160) parity-check graph.
#include "ogn.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace ogn {
namespace {
#include "ogn1_checks.inc"
struct Graph {
    std::array<std::vector<int>, 48> rows;
    std::array<uint64_t, 208> columns{};
    Graph() {
        for (int row = 0; row < 48; ++row) for (int bit = 0; bit < 208; ++bit)
            if ((checks[row][bit / 32] >> (bit % 32)) & 1u) {
                rows[row].push_back(bit);
                columns[bit] |= uint64_t{1} << row;
            }
    }
};
const Graph& graph() { static const Graph g; return g; }
bool bit(const Codeword& w, int b) { return (w[b / 8] >> (b % 8)) & 1; }
void flip(Codeword& w, int b) { w[b / 8] ^= uint8_t(1u << (b % 8)); }
uint64_t syndrome(const Codeword& w) {
    uint64_t out = 0;
    for (int b = 0; b < 208; ++b) if (bit(w, b)) out ^= graph().columns[b];
    return out;
}
int distance(const Codeword& a, const Codeword& b) {
    int n = 0; for (size_t i = 0; i < a.size(); ++i) n += __builtin_popcount(unsigned(a[i] ^ b[i]));
    return n;
}
}
int parityFailures(const Codeword& word) { return __builtin_popcountll(syndrome(word)); }

void encodeParity(Codeword& word) {
    // Solve H_parity * p = H_data * d over GF(2); avoid a second protocol matrix.
    std::fill(word.begin() + 20, word.end(), 0);
    const uint64_t s = syndrome(word);
    std::array<uint64_t, 48> rows{};
    for (int r = 0; r < 48; ++r) {
        for (int c = 0; c < 48; ++c) if ((graph().columns[160 + c] >> r) & 1) rows[r] |= uint64_t{1} << c;
        rows[r] |= ((s >> r) & 1) << 48;
    }
    for (int c = 0; c < 48; ++c) {
        int pivot = c; while (pivot < 48 && !((rows[pivot] >> c) & 1)) ++pivot;
        if (pivot == 48) throw std::runtime_error("OGN parity matrix is singular");
        std::swap(rows[c], rows[pivot]);
        for (int r = 0; r < 48; ++r) if (r != c && ((rows[r] >> c) & 1)) rows[r] ^= rows[c];
    }
    for (int c = 0; c < 48; ++c) if ((rows[c] >> 48) & 1) flip(word, 160 + c);
}

LdpcResult correct(Codeword& word, int maxChanges) {
    const Codeword original = word;
    const uint64_t s = syndrome(word);
    LdpcResult result; result.failedBefore = __builtin_popcountll(s);
    if (!s) { result.valid = true; return result; }
    maxChanges = std::clamp(maxChanges, 0, 6);
    if (!maxChanges) return result;
    // Exact syndrome matching gives deterministic correction for isolated errors.
    int matchA = -1, matchB = -1, matches = 0;
    for (int a = 0; a < 208; ++a) {
        if (graph().columns[a] == s) { matchA = a; matchB = -1; ++matches; }
        if (maxChanges >= 2) for (int b = a + 1; b < 208; ++b)
            if ((graph().columns[a] ^ graph().columns[b]) == s) { matchA = a; matchB = b; ++matches; }
    }
    if (matches == 1) {
        flip(word, matchA); if (matchB >= 0) flip(word, matchB);
        result.valid = syndrome(word) == 0; result.corrected = matchB < 0 ? 1 : 2;
        return result;
    }
    // Normalised min-sum belief propagation, hard-input LLRs. No soft information
    // is invented: the bundled receiver exposes decided bits only.
    if (maxChanges < 3) return result;
    std::array<double, 208> prior{}, posterior{};
    std::array<std::vector<double>, 48> q, messages;
    for (int b = 0; b < 208; ++b) prior[b] = bit(original, b) ? -4.0 : 4.0;
    for (int r = 0; r < 48; ++r) {
        for (int b : graph().rows[r]) q[r].push_back(prior[b]);
        messages[r].resize(q[r].size());
    }
    for (int iter = 1; iter <= 48; ++iter) {
        posterior = prior;
        for (int r = 0; r < 48; ++r) {
            double lo = 1e100, next = 1e100; int minIndex = -1, sign = 1;
            for (size_t e = 0; e < q[r].size(); ++e) {
                const double v = q[r][e]; if (v < 0) sign = -sign;
                const double a = std::abs(v);
                if (a < lo) { next = lo; lo = a; minIndex = int(e); } else next = std::min(next, a);
            }
            for (size_t e = 0; e < q[r].size(); ++e) {
                messages[r][e] = 0.8 * (int(e) == minIndex ? next : lo) * sign * (q[r][e] < 0 ? -1 : 1);
                posterior[graph().rows[r][e]] += messages[r][e];
            }
        }
        Codeword candidate{};
        for (int b = 0; b < 208; ++b) if (posterior[b] < 0) flip(candidate, b);
        result.iterations = iter;
        if (!syndrome(candidate)) {
            const int changed = distance(original, candidate);
            if (changed <= maxChanges) { word = candidate; result.valid = true; result.corrected = changed; }
            return result;
        }
        for (int r = 0; r < 48; ++r) for (size_t e = 0; e < q[r].size(); ++e)
            q[r][e] = std::clamp(posterior[graph().rows[r][e]] - messages[r][e], -40.0, 40.0);
    }
    return result;
}
} // namespace ogn
