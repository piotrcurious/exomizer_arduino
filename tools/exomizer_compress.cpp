#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <deque>
#include <cmath>
#include <cstring>
#include <stdint.h>
#include <map>

using namespace std;

class BitWriter {
public:
    vector<uint8_t> data;
    uint8_t curr = 0;
    int bits = 0;

    void write_bit(int b) {
        if (b) curr |= (1 << bits);
        bits++;
        if (bits == 8) {
            data.push_back(curr);
            curr = 0;
            bits = 0;
        }
    }

    void write_bits(uint32_t v, int n) {
        for (int i = 0; i < n; ++i) {
            write_bit((v >> i) & 1);
        }
    }

    void write_unary(int n) {
        for (int i = 0; i < n; ++i) write_bit(0);
        write_bit(1);
    }

    void flush() {
        if (bits > 0) data.push_back(curr);
    }
};

void get_base(const uint8_t* bits, uint32_t* base, int count) {
    uint32_t curr = 0;
    for (int i = 0; i < count; ++i) {
        base[i] = curr;
        curr += (bits[i] == 0 ? 1 : (1U << bits[i]));
    }
}

int get_idx_and_extra(uint32_t val, const uint8_t* bits, const uint32_t* base, int count, uint32_t& extra) {
    for (int i = 0; i < count; ++i) {
        uint32_t limit = base[i] + (bits[i] == 0 ? 1 : (1U << bits[i]));
        if (val >= base[i] && val < limit) {
            extra = val - base[i];
            return i;
        }
    }
    return -1;
}

struct Node {
    size_t prev;
    char type; // 'l' lit, 'r' run, 'm' match
    uint32_t len;
    uint32_t off;
    int l_idx;
    int o_idx;
    float total_cost;
};

class Compressor {
public:
    vector<uint8_t> data;
    uint8_t l_bits[16], o_bits3[16], o_bits2[16], o_bits1[4];
    uint32_t l_base[16], o_base3[16], o_base2[16], o_base1[4];
    int hash_depth;
    uint32_t window_size;

    Compressor(const vector<uint8_t>& d, int depth, uint32_t window)
        : data(d), hash_depth(depth), window_size(window) {
        // Reasonable defaults
        uint8_t dl[] = {0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10};
        uint8_t do3[] = {4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 10, 11, 12, 13, 14};
        uint8_t do2[] = {3, 3, 4, 4, 5, 5, 6, 6, 7, 8, 9, 10, 11, 12, 13, 14};
        uint8_t do1[] = {2, 3, 4, 5};
        memcpy(l_bits, dl, 16);
        memcpy(o_bits3, do3, 16);
        memcpy(o_bits2, do2, 16);
        memcpy(o_bits1, do1, 4);
        update_bases();
    }

    void update_bases() {
        get_base(l_bits, l_base, 16);
        get_base(o_bits3, o_base3, 16);
        get_base(o_bits2, o_base2, 16);
        get_base(o_bits1, o_base1, 4);
    }

    void optimize_tables(const vector<Node>& path) {
        vector<uint32_t> l_vals;
        vector<uint32_t> o3_vals, o2_vals, o1_vals;
        uint32_t last_o = 0;
        for (auto& p : path) {
            if (p.type == 'm') {
                l_vals.push_back(p.len);
                uint32_t ov = (p.off == last_o) ? 0 : p.off;
                if (p.len == 1) o1_vals.push_back(ov);
                else if (p.len == 2) o2_vals.push_back(ov);
                else o3_vals.push_back(ov);
                last_o = p.off;
            }
        }

        auto fit_complex = [&](const vector<uint32_t>& vals, uint8_t* bits, uint32_t* base, int count) {
            if (vals.empty()) return;
            map<uint32_t, int> freq;
            for (uint32_t v : vals) freq[v]++;

            uint32_t curr_base = 0;
            for (int i = 0; i < count; ++i) {
                int best_b = 0; float min_added_cost = 1e18f;
                for (int b = 0; b <= 15; ++b) {
                    uint32_t limit = curr_base + (b == 0 ? 1 : (1U << b));
                    float cost = 0;
                    int items = 0;
                    for (auto const& [v, f] : freq) {
                        if (v >= curr_base && v < limit) {
                            cost += f * (i + 1 + b);
                            items += f;
                        }
                    }
                    if (items > 0) {
                        cost /= items;
                        float score = cost - 0.1f * items;
                        if (score < min_added_cost) {
                            min_added_cost = score;
                            best_b = b;
                        }
                    }
                }
                bits[i] = (uint8_t)best_b;
                base[i] = curr_base;
                curr_base += (best_b == 0 ? 1 : (1U << best_b));
                if (freq.lower_bound(curr_base) == freq.end()) break;
            }
        };

        fit_complex(l_vals, l_bits, l_base, 16);
        fit_complex(o3_vals, o_bits3, o_base3, 16);
        fit_complex(o2_vals, o_bits2, o_base2, 16);
        fit_complex(o1_vals, o_bits1, o_base1, 4);
        update_bases();
    }

    vector<Node> solve_dp() {
        size_t n = data.size();
        vector<Node> nodes(n + 1);
        for (auto& node : nodes) node.total_cost = 1e18f;
        nodes[0].total_cost = 0;
        vector<uint32_t> last_o_at(n + 1, 0);
        vector<int> head(65536, -1);
        vector<int> prev_link(n);

        for (size_t i = 0; i < n; ++i) {
            float ci = nodes[i].total_cost;
            uint32_t loi = last_o_at[i];

            if (ci + 9 < nodes[i + 1].total_cost) {
                nodes[i + 1] = { i, 'l', 0, 0, 0, 0, ci + 9 };
                last_o_at[i + 1] = loi;
            }

            if (i + 35 <= n) {
                for (uint32_t rl : {64, 512, 4096, 32768, 65535}) {
                    uint32_t actual_rl = min((uint32_t)(n - i), rl);
                    float cr = ci + 1 + 18 + 16 + actual_rl * 8;
                    if (cr < nodes[i + actual_rl].total_cost) {
                        nodes[i + actual_rl] = { i, 'r', actual_rl, 0, 0, 0, cr };
                        last_o_at[i + actual_rl] = loi;
                    }
                }
            }

            auto try_match = [&](uint32_t off, uint32_t max_l) {
                uint32_t ov = (off == loi) ? 0 : off;
                // Try each possible length bucket
                for (int li = 0; li < 16; ++li) {
                    if (l_base[li] > max_l) break;
                    uint32_t l_limit = l_base[li] + (l_bits[li] == 0 ? 1 : (1U << l_bits[li]));
                    uint32_t use_l = min(max_l, l_limit - 1);
                    if (use_l < l_base[li]) continue;

                    uint8_t* ob_table; uint32_t* oba_table; int o_count;
                    if (use_l == 1) { ob_table = o_bits1; oba_table = o_base1; o_count = 4; }
                    else if (use_l == 2) { ob_table = o_bits2; oba_table = o_base2; o_count = 16; }
                    else { ob_table = o_bits3; oba_table = o_base3; o_count = 16; }

                    uint32_t dummy;
                    int oi = get_idx_and_extra(ov, ob_table, oba_table, o_count, dummy);
                    if (oi != -1) {
                        float cm = ci + 1 + (li + 1) + l_bits[li] + (oi + 1) + ob_table[oi];
                        if (cm < nodes[i + use_l].total_cost) {
                            nodes[i + use_l] = { i, 'm', use_l, off, li, oi, cm };
                            last_o_at[i + use_l] = off;
                        }
                    }
                }
            };

            if (loi > 0 && i >= loi) {
                uint32_t l = 0;
                while (i + l < n && data[i + l] == data[i - loi + l]) {
                    l++; if (l >= 32767) break;
                }
                if (l >= 1) try_match(loi, l);
            }

            if (i + 2 <= n) {
                uint16_t h = (data[i] << 8) | data[i + 1];
                int p = head[h]; int count = 0;
                while (p != -1 && count < hash_depth) {
                    uint32_t off = i - p;
                    if (off > window_size) break;
                    if (off != loi) {
                        uint32_t l = 2;
                        while (i + l < n && data[i + l] == data[p + l]) {
                            l++; if (l >= 32767) break;
                        }
                        try_match(off, l);
                    }
                    p = prev_link[p]; count++;
                }
                prev_link[i] = head[h]; head[h] = i;
            }
        }
        vector<Node> path; size_t curr = n;
        while (curr > 0) {
            if (nodes[curr].total_cost >= 1e17f) break;
            path.push_back(nodes[curr]);
            curr = path.back().prev;
        }
        reverse(path.begin(), path.end());
        return path;
    }

    vector<uint8_t> compress() {
        auto path = solve_dp();
        for (int i=0; i<2; ++i) {
            optimize_tables(path);
            path = solve_dp();
        }
        BitWriter bw;
        for (int i = 0; i < 16; ++i) bw.write_bits(l_bits[i], 4);
        for (int i = 0; i < 16; ++i) bw.write_bits(o_bits3[i], 4);
        for (int i = 0; i < 16; ++i) bw.write_bits(o_bits2[i], 4);
        for (int i = 0; i < 4; ++i) bw.write_bits(o_bits1[i], 4);

        uint32_t last_o = 0;
        for (auto& p : path) {
            if (p.type == 'l') {
                bw.write_bit(1);
                bw.write_bits(data[p.prev], 8);
            } else if (p.type == 'r') {
                bw.write_bit(0);
                bw.write_unary(17);
                bw.write_bits(p.len, 16);
                for (uint32_t j = 0; j < p.len; ++j) bw.write_bits(data[p.prev + j], 8);
            } else {
                bw.write_bit(0);
                bw.write_unary(p.l_idx);
                uint32_t l_extra;
                get_idx_and_extra(p.len, l_bits, l_base, 16, l_extra);
                bw.write_bits(l_extra, l_bits[p.l_idx]);

                bw.write_unary(p.o_idx);
                uint32_t ov = (p.off == last_o) ? 0 : p.off;
                uint8_t* ob_table; uint32_t* oba_table;
                if (p.len == 1) { ob_table = o_bits1; oba_table = o_base1; }
                else if (p.len == 2) { ob_table = o_bits2; oba_table = o_base2; }
                else { ob_table = o_bits3; oba_table = o_base3; }

                uint32_t o_extra;
                o_extra = ov - oba_table[p.o_idx];
                bw.write_bits(o_extra, ob_table[p.o_idx]);
                last_o = p.off;
            }
        }
        bw.write_bit(0); bw.write_unary(16); bw.flush();
        return bw.data;
    }
};

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s <input> <output> [preset: speed|balanced|ratio]\n", argv[0]);
        return 1;
    }
    ifstream ifs(argv[1], ios::binary);
    if (!ifs) return 1;
    vector<uint8_t> data((istreambuf_iterator<char>(ifs)), istreambuf_iterator<char>());
    int depth = 64; uint32_t window = 32767;
    if (argc > 3) {
        string p = argv[3];
        if (p == "speed") { depth = 16; window = 4096; }
        else if (p == "ratio") { depth = 512; window = 65535; }
    }
    Compressor comp(data, depth, window);
    auto res = comp.compress();
    ofstream ofs(argv[2], ios::binary);
    ofs.write((char*)res.data(), res.size());
    printf("Compressed %zu -> %zu bytes (%.2f%%)\n", data.size(), res.size(), (double)res.size() / (data.empty() ? 1 : data.size()) * 100);
    return 0;
}
