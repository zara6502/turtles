#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t INF = std::numeric_limits<uint32_t>::max();
constexpr std::array<int, 4> DX{0, 1, 0, -1};
constexpr std::array<int, 4> DY{-1, 0, 1, 0};
constexpr std::array<char, 4> MOVE{'U', 'R', 'D', 'L'};

using Clock = std::chrono::steady_clock;
using Bits = std::vector<uint64_t>;

struct Maze {
    int width = 0;
    int height = 0;
    int exitX = -1;
    int exitY = -1;
    std::vector<std::string> rows;
    std::vector<int32_t> cellAt; // -1 for wall/exit; otherwise compact cell id
    std::vector<int32_t> cellX;
    std::vector<int32_t> cellY;
    std::vector<std::array<uint32_t, 4>> next;

    uint32_t cellCount() const { return static_cast<uint32_t>(next.size()); }

    int index(int x, int y) const { return y * width + x; }

    static Maze load(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("Cannot open maze file: " + path);

        Maze m;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            m.rows.push_back(line);
        }
        if (m.rows.empty()) throw std::runtime_error("Maze file is empty");

        m.height = static_cast<int>(m.rows.size());
        m.width = static_cast<int>(m.rows.front().size());
        if (m.width == 0) throw std::runtime_error("Maze width is zero");
        for (int y = 0; y < m.height; ++y) {
            if (static_cast<int>(m.rows[y].size()) != m.width) {
                throw std::runtime_error("Maze rows have different widths (line " + std::to_string(y + 1) + ")");
            }
        }

        int exits = 0;
        for (int y = 0; y < m.height; ++y) {
            for (int x = 0; x < m.width; ++x) {
                if (m.rows[y][x] == 'E' || m.rows[y][x] == 'e') {
                    ++exits;
                    m.exitX = x;
                    m.exitY = y;
                }
            }
        }
        if (exits != 1) throw std::runtime_error("Maze must contain exactly one exit marked 'E'");

        m.cellAt.assign(static_cast<size_t>(m.width) * m.height, -1);
        for (int y = 0; y < m.height; ++y) {
            for (int x = 0; x < m.width; ++x) {
                const char c = m.rows[y][x];
                if (c != '#' && c != 'E' && c != 'e') {
                    const int32_t id = static_cast<int32_t>(m.next.size());
                    m.cellAt[m.index(x, y)] = id;
                    m.cellX.push_back(x);
                    m.cellY.push_back(y);
                    m.next.emplace_back();
                }
            }
        }
        if (m.next.empty()) throw std::runtime_error("Maze has no walkable cells");

        const uint32_t sink = m.cellCount();
        for (int y = 0; y < m.height; ++y) {
            for (int x = 0; x < m.width; ++x) {
                const int32_t id = m.cellAt[m.index(x, y)];
                if (id < 0) continue;
                for (int d = 0; d < 4; ++d) {
                    const int nx = x + DX[d];
                    const int ny = y + DY[d];
                    if (nx < 0 || nx >= m.width || ny < 0 || ny >= m.height) {
                        m.next[id][d] = static_cast<uint32_t>(id); // boundary acts as a wall
                    } else {
                        const char c2 = m.rows[ny][nx];
                        if (c2 == 'E' || c2 == 'e') m.next[id][d] = sink;
                        else {
                            const int32_t target = m.cellAt[m.index(nx, ny)];
                            m.next[id][d] = target >= 0 ? static_cast<uint32_t>(target) : static_cast<uint32_t>(id);
                        }
                    }
                }
            }
        }
        return m;
    }
};

Bits makeInitial(const Maze& m) {
    Bits b((m.cellCount() + 63u) / 64u, ~uint64_t{0});
    const uint32_t rem = m.cellCount() & 63u;
    if (rem != 0) b.back() = (uint64_t{1} << rem) - 1;
    return b;
}

bool emptyBits(const Bits& b) {
    for (uint64_t w : b) if (w) return false;
    return true;
}

uint32_t bitCount(const Bits& b) {
    uint32_t n = 0;
    for (uint64_t w : b) n += static_cast<uint32_t>(__builtin_popcountll(w));
    return n;
}

bool sameBits(const Bits& a, const Bits& b) { return a == b; }

struct BitsHash {
    size_t operator()(const Bits& b) const noexcept {
        uint64_t h = 0x9e3779b97f4a7c15ULL;
        for (uint64_t x : b) {
            x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
            x ^= x >> 27; x *= 0x94d049bb133111ebULL;
            x ^= x >> 31;
            h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        }
        return static_cast<size_t>(h);
    }
};

Bits applyMove(const Maze& m, const Bits& state, int direction) {
    Bits out(state.size(), 0);
    for (size_t wi = 0; wi < state.size(); ++wi) {
        uint64_t word = state[wi];
        while (word) {
            const unsigned bit = static_cast<unsigned>(__builtin_ctzll(word));
            const uint32_t from = static_cast<uint32_t>(wi * 64 + bit);
            word &= word - 1;
            if (from >= m.cellCount()) continue;
            const uint32_t to = m.next[from][direction];
            if (to < m.cellCount()) out[to >> 6] |= uint64_t{1} << (to & 63u);
            // to == cellCount() means the turtle exited and is removed.
        }
    }
    return out;
}

struct Distances {
    std::vector<uint32_t> distance;
    std::vector<uint8_t> routeDirection;
    bool allReachExit = false;
};

Distances buildCellDistances(const Maze& m) {
    const uint32_t n = m.cellCount();
    Distances result;
    result.distance.assign(n, INF);
    result.routeDirection.assign(n, 255);
    std::queue<uint32_t> q;

    // Cells directly adjacent to E have distance 1.
    for (uint32_t p = 0; p < n; ++p) {
        for (int d = 0; d < 4; ++d) {
            if (m.next[p][d] == n) {
                result.distance[p] = 1;
                result.routeDirection[p] = static_cast<uint8_t>(d);
                q.push(p);
                break;
            }
        }
    }

    while (!q.empty()) {
        const uint32_t current = q.front();
        q.pop();
        // Find cells that can move into current. Grid movement is symmetric,
        // but verify the actual directed transition before accepting a predecessor.
        for (int move = 0; move < 4; ++move) {
            // The predecessor lies one cell opposite the move from current.
            const int x = m.cellX[current] - DX[move];
            const int y = m.cellY[current] - DY[move];
            if (x < 0 || x >= m.width || y < 0 || y >= m.height) continue;
            const int32_t pred = m.cellAt[m.index(x, y)];
            if (pred < 0 || result.distance[pred] != INF) continue;
            if (m.next[pred][move] != current) continue;
            result.distance[pred] = result.distance[current] + 1;
            result.routeDirection[pred] = static_cast<uint8_t>(move);
            q.push(static_cast<uint32_t>(pred));
        }
    }

    result.allReachExit = std::all_of(result.distance.begin(), result.distance.end(), [](uint32_t d) { return d != INF; });
    return result;
}

struct SolveResult {
    bool solved = false;
    bool provedOptimal = false;
    bool limitReached = false;
    uint64_t commands = 0;
    uint64_t expanded = 0;
    uint64_t discovered = 0;
    double seconds = 0.0;
    std::string path;
    std::string note;
};

uint64_t sumDistances(const Bits& state, const Distances& d) {
    uint64_t sum = 0;
    for (size_t wi = 0; wi < state.size(); ++wi) {
        uint64_t word = state[wi];
        while (word) {
            const unsigned bit = static_cast<unsigned>(__builtin_ctzll(word));
            const uint32_t p = static_cast<uint32_t>(wi * 64 + bit);
            word &= word - 1;
            if (p < d.distance.size() && d.distance[p] != INF) sum += d.distance[p];
        }
    }
    return sum;
}

struct DAFdsResult {
    bool solved = false;
    uint64_t commands = 0;
    uint32_t fallbacks = 0;
    uint32_t adaptive = 0;
    std::string path;
    std::string error;
};

DAFdsResult runDAFds(const Maze& m, const Distances& distances, uint32_t stagnationLimit) {
    DAFdsResult result;
    if (!distances.allReachExit) {
        result.error = "some cells cannot reach E";
        return result;
    }

    Bits state = makeInitial(m);
    uint64_t cycleMinSum = sumDistances(state, distances);
    uint32_t stagnation = 0;
    const uint32_t n = m.cellCount();

    auto applyOne = [&](int dir) {
        state = applyMove(m, state, dir);
        ++result.commands;
        result.path.push_back(MOVE[dir]);
    };

    while (!emptyBits(state)) {
        Bits bestState;
        uint64_t bestSum = std::numeric_limits<uint64_t>::max();
        uint32_t bestCount = INF;
        int bestDirection = -1;

        for (int dir = 0; dir < 4; ++dir) {
            Bits candidate = applyMove(m, state, dir);
            const uint32_t count = bitCount(candidate);
            const uint64_t sum = sumDistances(candidate, distances);
            if (sum < bestSum || (sum == bestSum && count < bestCount)) {
                bestSum = sum;
                bestCount = count;
                bestDirection = dir;
                bestState = std::move(candidate);
            }
        }

        if (bestDirection < 0) {
            result.error = "no adaptive candidate";
            return result;
        }

        const uint32_t oldCount = bitCount(state);
        const bool identity = sameBits(bestState, state);
        const bool progress = (bestSum < cycleMinSum) || (bestCount < oldCount);

        if (identity) {
            // The selected projection changes nothing: immediately use Farthest.
            ++result.fallbacks;
        } else {
            state = std::move(bestState);
            ++result.commands;
            ++result.adaptive;
            result.path.push_back(MOVE[bestDirection]);
            if (emptyBits(state)) break;
            if (progress) {
                if (bestSum < cycleMinSum) cycleMinSum = bestSum;
                stagnation = 0;
            } else {
                ++stagnation;
            }
            if (stagnation < stagnationLimit) continue;
            ++result.fallbacks;
        }

        if (!emptyBits(state)) {
            // Farthest fallback: follow a shortest route for the farthest active cell.
            uint32_t farthest = 0;
            uint32_t farthestDistance = 0;
            bool found = false;
            for (size_t wi = 0; wi < state.size(); ++wi) {
                uint64_t word = state[wi];
                while (word) {
                    const unsigned bit = static_cast<unsigned>(__builtin_ctzll(word));
                    const uint32_t p = static_cast<uint32_t>(wi * 64 + bit);
                    word &= word - 1;
                    if (p < n && (!found || distances.distance[p] > farthestDistance)) {
                        found = true;
                        farthest = p;
                        farthestDistance = distances.distance[p];
                    }
                }
            }
            if (!found || farthestDistance == INF) {
                result.error = "Farthest could not select a reachable active cell";
                return result;
            }
            uint32_t selected = farthest;
            while (selected < n) {
                const uint8_t dir = distances.routeDirection[selected];
                if (dir > 3) {
                    result.error = "missing shortest-route direction";
                    return result;
                }
                applyOne(dir);
                selected = m.next[selected][dir];
                if (selected == n) break;
            }
            cycleMinSum = sumDistances(state, distances);
            stagnation = 0;
        }
    }

    result.solved = true;
    return result;
}

struct PairAutomaton {
    uint32_t locations = 0; // walkable cells + sink
    uint64_t pairStates = 0;
    std::vector<uint32_t> distance;
    uint32_t initialLowerBound = 0;
    uint64_t unreachablePairs = 0;
    size_t pairIndex(uint32_t a, uint32_t b) const {
        if (a > b) std::swap(a, b);
        return static_cast<size_t>(a) * locations - (static_cast<size_t>(a) * (a - 1)) / 2 + (b - a);
    }
};

bool buildPairAutomaton(const Maze& m, uint64_t maxPairStates, PairAutomaton& pa, std::string& error) {
    pa.locations = m.cellCount() + 1; // final location is EXIT/sink
    const uint64_t k = pa.locations;
    pa.pairStates = k * (k + 1) / 2;
    if (pa.pairStates > maxPairStates) {
        error = "pair automaton would contain " + std::to_string(pa.pairStates) +
                " pair states (limit " + std::to_string(maxPairStates) + ")";
        return false;
    }
    if (pa.pairStates > static_cast<uint64_t>(std::numeric_limits<int32_t>::max() / 4)) {
        error = "pair automaton is too large for 32-bit reverse-edge indices";
        return false;
    }

    const size_t count = static_cast<size_t>(pa.pairStates);
    const size_t edgeCapacity = count * 4;
    std::vector<int32_t> head(count, -1);
    std::vector<uint32_t> reverseFrom(edgeCapacity);
    std::vector<int32_t> reverseNext(edgeCapacity);
    size_t edgeCount = 0;

    auto transition = [&](uint32_t p, int d) -> uint32_t {
        if (p == m.cellCount()) return p;
        return m.next[p][d];
    };

    for (uint32_t a = 0; a < pa.locations; ++a) {
        for (uint32_t b = a; b < pa.locations; ++b) {
            const size_t source = pa.pairIndex(a, b);
            for (int d = 0; d < 4; ++d) {
                uint32_t na = transition(a, d);
                uint32_t nb = transition(b, d);
                if (na > nb) std::swap(na, nb);
                const size_t target = pa.pairIndex(na, nb);
                reverseFrom[edgeCount] = static_cast<uint32_t>(source);
                reverseNext[edgeCount] = head[target];
                head[target] = static_cast<int32_t>(edgeCount);
                ++edgeCount;
            }
        }
    }

    pa.distance.assign(count, INF);
    std::vector<uint32_t> queue(count);
    size_t qHead = 0, qTail = 0;
    const uint32_t sink = m.cellCount();
    const size_t goal = pa.pairIndex(sink, sink);
    pa.distance[goal] = 0;
    queue[qTail++] = static_cast<uint32_t>(goal);

    while (qHead < qTail) {
        const uint32_t current = queue[qHead++];
        const uint32_t nextDistance = pa.distance[current] + 1;
        for (int32_t e = head[current]; e != -1; e = reverseNext[static_cast<size_t>(e)]) {
            const uint32_t predecessor = reverseFrom[static_cast<size_t>(e)];
            if (pa.distance[predecessor] == INF) {
                pa.distance[predecessor] = nextDistance;
                queue[qTail++] = predecessor;
            }
        }
    }

    for (uint32_t a = 0; a < m.cellCount(); ++a) {
        for (uint32_t b = a; b < m.cellCount(); ++b) {
            const uint32_t d = pa.distance[pa.pairIndex(a, b)];
            if (d == INF) ++pa.unreachablePairs;
            else pa.initialLowerBound = std::max(pa.initialLowerBound, d);
        }
    }
    if (pa.unreachablePairs != 0) pa.initialLowerBound = INF;
    return true;
}

uint32_t pairHeuristic(const Bits& state, const PairAutomaton& pa, uint32_t cellCount) {
    std::vector<uint32_t> active;
    active.reserve(bitCount(state));
    for (size_t wi = 0; wi < state.size(); ++wi) {
        uint64_t word = state[wi];
        while (word) {
            const unsigned bit = static_cast<unsigned>(__builtin_ctzll(word));
            const uint32_t p = static_cast<uint32_t>(wi * 64 + bit);
            word &= word - 1;
            if (p < cellCount) active.push_back(p);
        }
    }

    uint32_t h = 0;
    for (size_t i = 0; i < active.size(); ++i) {
        for (size_t j = i; j < active.size(); ++j) {
            const uint32_t d = pa.distance[pa.pairIndex(active[i], active[j])];
            if (d == INF) return INF;
            h = std::max(h, d);
        }
    }
    return h;
}

struct SearchNode {
    Bits state;
    uint32_t g = 0;
    uint32_t parent = INF;
    uint8_t move = 255;
};

std::string reconstructPath(const std::vector<SearchNode>& nodes, uint32_t id) {
    std::string path;
    while (id != INF && nodes[id].parent != INF) {
        path.push_back(MOVE[nodes[id].move]);
        id = nodes[id].parent;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

struct SearchOptions {
    uint64_t maxStates = 1000000;
    double timeLimitSeconds = 30.0;
    uint64_t upperBound = INF;
};

SolveResult runBFS(const Maze& m, const SearchOptions& options) {
    SolveResult result;
    const auto started = Clock::now();
    std::vector<SearchNode> nodes;
    nodes.reserve(static_cast<size_t>(std::min<uint64_t>(options.maxStates, 250000)));
    std::unordered_map<Bits, uint32_t, BitsHash> seen;
    seen.reserve(static_cast<size_t>(std::min<uint64_t>(options.maxStates, 250000)));

    SearchNode root;
    root.state = makeInitial(m);
    nodes.push_back(root);
    seen.emplace(nodes[0].state, 0);
    std::queue<uint32_t> q;
    q.push(0);

    if (emptyBits(root.state)) {
        result.solved = result.provedOptimal = true;
        result.path.clear();
        result.seconds = 0;
        return result;
    }

    while (!q.empty()) {
        if (Clock::now() - started > std::chrono::duration<double>(options.timeLimitSeconds)) {
            result.limitReached = true;
            result.note = "time limit reached";
            break;
        }
        const uint32_t id = q.front();
        q.pop();
        // Copy the state: nodes may reallocate while successors are appended.
        const Bits currentState = nodes[id].state;
        const uint32_t currentG = nodes[id].g;
        ++result.expanded;
        for (int d = 0; d < 4; ++d) {
            const uint32_t ng = currentG + 1;
            if (ng >= options.upperBound) continue; // DAFds already gives a valid path of this length.
            Bits candidate = applyMove(m, currentState, d);
            if (seen.find(candidate) != seen.end()) continue;
            if (nodes.size() >= options.maxStates) {
                result.limitReached = true;
                result.note = "state limit reached";
                break;
            }
            SearchNode node;
            node.state = std::move(candidate);
            node.g = ng;
            node.parent = id;
            node.move = static_cast<uint8_t>(d);
            const uint32_t newId = static_cast<uint32_t>(nodes.size());
            const bool goal = emptyBits(node.state);
            nodes.push_back(std::move(node));
            seen.emplace(nodes.back().state, newId);
            if (goal) {
                result.solved = true;
                result.provedOptimal = true;
                result.path = reconstructPath(nodes, newId);
                result.commands = result.path.size();
                q = {};
                break;
            }
            q.push(newId);
        }
        if (result.limitReached || result.solved) break;
    }

    result.discovered = nodes.size();
    if (!result.solved && !result.limitReached && q.empty()) {
        result.provedOptimal = true;
        result.solved = true;
        result.commands = options.upperBound;
        result.note = "no shorter path exists; DAFds upper bound is optimal";
    }
    result.seconds = std::chrono::duration<double>(Clock::now() - started).count();
    return result;
}

struct OpenNode {
    uint64_t f;
    uint32_t g;
    uint32_t id;
};
struct OpenCompare {
    bool operator()(const OpenNode& a, const OpenNode& b) const {
        if (a.f != b.f) return a.f > b.f;
        return a.g < b.g; // prefer deeper nodes on equal f
    }
};

SolveResult runAStar(const Maze& m, const PairAutomaton& pa, const SearchOptions& options) {
    SolveResult result;
    const auto started = Clock::now();
    std::vector<SearchNode> nodes;
    nodes.reserve(static_cast<size_t>(std::min<uint64_t>(options.maxStates, 250000)));
    std::unordered_map<Bits, uint32_t, BitsHash> best;
    best.reserve(static_cast<size_t>(std::min<uint64_t>(options.maxStates, 250000)));
    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenCompare> open;

    SearchNode root;
    root.state = makeInitial(m);
    const uint32_t h0 = pairHeuristic(root.state, pa, m.cellCount());
    if (h0 == INF) {
        result.note = "at least one initial pair cannot be driven to EXIT by any common word";
        result.seconds = std::chrono::duration<double>(Clock::now() - started).count();
        return result;
    }
    if (h0 >= options.upperBound) {
        result.solved = true;
        result.provedOptimal = true;
        result.commands = options.upperBound;
        result.note = "pair-automaton lower bound matches the DAFds upper bound";
        result.seconds = std::chrono::duration<double>(Clock::now() - started).count();
        return result;
    }

    nodes.push_back(root);
    best.emplace(nodes[0].state, 0);
    open.push({h0, 0, 0});

    while (!open.empty()) {
        if (Clock::now() - started > std::chrono::duration<double>(options.timeLimitSeconds)) {
            result.limitReached = true;
            result.note = "time limit reached";
            break;
        }
        const OpenNode top = open.top();
        open.pop();
        // Copy the state: appending successors can reallocate the node vector.
        const Bits currentState = nodes[top.id].state;
        const uint32_t currentG = nodes[top.id].g;
        const auto itCurrent = best.find(currentState);
        if (itCurrent == best.end() || itCurrent->second != top.id) continue; // stale heap item
        if (top.f >= options.upperBound) break; // no remaining route can beat DAFds

        if (emptyBits(currentState)) {
            result.solved = true;
            result.provedOptimal = true;
            result.path = reconstructPath(nodes, top.id);
            result.commands = result.path.size();
            break;
        }

        ++result.expanded;
        for (int d = 0; d < 4; ++d) {
            const uint32_t ng = currentG + 1;
            if (ng >= options.upperBound) continue;
            Bits candidate = applyMove(m, currentState, d);
            const auto existing = best.find(candidate);
            if (existing != best.end() && nodes[existing->second].g <= ng) continue;

            uint32_t h = 0;
            if (!emptyBits(candidate)) {
                h = pairHeuristic(candidate, pa, m.cellCount());
                if (h == INF || static_cast<uint64_t>(ng) + h >= options.upperBound) continue;
            }
            if (nodes.size() >= options.maxStates) {
                result.limitReached = true;
                result.note = "state limit reached";
                break;
            }

            SearchNode node;
            node.state = std::move(candidate);
            node.g = ng;
            node.parent = top.id;
            node.move = static_cast<uint8_t>(d);
            const uint32_t newId = static_cast<uint32_t>(nodes.size());
            nodes.push_back(std::move(node));
            best[nodes.back().state] = newId;
            open.push({static_cast<uint64_t>(ng) + h, ng, newId});
        }
        if (result.limitReached) break;
    }

    result.discovered = nodes.size();
    if (!result.solved && !result.limitReached) {
        result.solved = true;
        result.provedOptimal = true;
        result.commands = options.upperBound;
        result.note = "search exhausted all candidates shorter than DAFds; its result is optimal";
    }
    result.seconds = std::chrono::duration<double>(Clock::now() - started).count();
    return result;
}

bool verifyPath(const Maze& m, const std::string& path) {
    Bits state = makeInitial(m);
    for (char c : path) {
        const auto it = std::find(MOVE.begin(), MOVE.end(), c);
        if (it == MOVE.end()) return false;
        state = applyMove(m, state, static_cast<int>(it - MOVE.begin()));
    }
    return emptyBits(state);
}

void printUsage(const char* exe) {
    std::cout << "Usage: " << exe << " --maze FILE [options]\n"
              << "Options:\n"
              << "  --algo all|dafds|bfs|astar   Which solvers to run (default: all)\n"
              << "  --limit N                   DAFds stagnation limit (default: 5)\n"
              << "  --max-states N              Search state cap per exact solver (default: 1000000)\n"
              << "  --time-limit SEC            Time cap per exact solver (default: 30)\n"
              << "  --max-pair-states N         Pair-automaton cap (default: 2000000)\n"
              << "  --print-path                Print found command sequences\n"
              << "\nMaze format: one rectangular ASCII grid, '#' = wall, 'E' = exit, any other character = free cell.\n"
              << "All free cells are starting positions; blocked moves leave a turtle in place.\n";
}

uint64_t parseU64(const std::string& s, const std::string& option) {
    size_t pos = 0;
    const unsigned long long v = std::stoull(s, &pos);
    if (pos != s.size()) throw std::runtime_error("Invalid value for " + option + ": " + s);
    return static_cast<uint64_t>(v);
}

double parseDouble(const std::string& s, const std::string& option) {
    size_t pos = 0;
    const double v = std::stod(s, &pos);
    if (pos != s.size() || !(v > 0.0)) throw std::runtime_error("Invalid value for " + option + ": " + s);
    return v;
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string mazePath;
        std::string algo = "all";
        uint32_t stagnationLimit = 5;
        uint64_t maxStates = 1000000;
        uint64_t maxPairStates = 2000000;
        double timeLimit = 30.0;
        bool printPath = false;

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&](const std::string& name) -> std::string {
                if (i + 1 >= argc) throw std::runtime_error("Missing value after " + name);
                return argv[++i];
            };
            if (arg == "--maze") mazePath = value(arg);
            else if (arg == "--algo") algo = value(arg);
            else if (arg == "--limit") stagnationLimit = static_cast<uint32_t>(parseU64(value(arg), arg));
            else if (arg == "--max-states") maxStates = parseU64(value(arg), arg);
            else if (arg == "--max-pair-states") maxPairStates = parseU64(value(arg), arg);
            else if (arg == "--time-limit") timeLimit = parseDouble(value(arg), arg);
            else if (arg == "--print-path") printPath = true;
            else if (arg == "--help" || arg == "-h") { printUsage(argv[0]); return 0; }
            else throw std::runtime_error("Unknown option: " + arg);
        }
        if (mazePath.empty()) { printUsage(argv[0]); return 2; }
        if (stagnationLimit == 0) throw std::runtime_error("--limit must be >= 1");
        if (maxStates == 0 || maxPairStates == 0) throw std::runtime_error("state limits must be >= 1");
        if (algo != "all" && algo != "dafds" && algo != "bfs" && algo != "astar")
            throw std::runtime_error("--algo must be all, dafds, bfs, or astar");

        const Maze maze = Maze::load(mazePath);
        const Distances distances = buildCellDistances(maze);
        std::cout << "Maze: " << maze.width << 'x' << maze.height
                  << ", free cells: " << maze.cellCount()
                  << ", exit: (" << maze.exitX << ',' << maze.exitY << ")\n";
        if (!distances.allReachExit) {
            const auto bad = std::count(distances.distance.begin(), distances.distance.end(), INF);
            std::cout << "WARNING: " << bad << " free cells cannot reach E.\n";
        }

        DAFdsResult dafds;
        const bool needDafds = (algo == "all" || algo == "dafds" || algo == "bfs" || algo == "astar");
        if (needDafds) {
            dafds = runDAFds(maze, distances, stagnationLimit);
            if (!dafds.solved) {
                std::cout << "DAFds: FAILED: " << dafds.error << "\n";
            } else {
                std::cout << "DAFds(limit=" << stagnationLimit << "): commands=" << dafds.commands
                          << ", adaptive=" << dafds.adaptive
                          << ", fallbacks=" << dafds.fallbacks
                          << ", verify=" << (verifyPath(maze, dafds.path) ? "YES" : "NO") << "\n";
                if (printPath) std::cout << "DAFds path: " << dafds.path << "\n";
            }
        }
        if ((algo == "dafds") || !dafds.solved) return dafds.solved ? 0 : 1;

        SearchOptions searchOptions;
        searchOptions.maxStates = maxStates;
        searchOptions.timeLimitSeconds = timeLimit;
        searchOptions.upperBound = dafds.commands;

        PairAutomaton pairAutomaton;
        bool pairReady = false;
        if (algo == "all" || algo == "astar") {
            std::string pairError;
            const auto t0 = Clock::now();
            pairReady = buildPairAutomaton(maze, maxPairStates, pairAutomaton, pairError);
            const double elapsed = std::chrono::duration<double>(Clock::now() - t0).count();
            if (pairReady) {
                std::cout << "Pair automaton: states=" << pairAutomaton.pairStates
                          << ", unreachable pairs=" << pairAutomaton.unreachablePairs
                          << ", initial lower bound=";
                if (pairAutomaton.initialLowerBound == INF) std::cout << "INF";
                else std::cout << pairAutomaton.initialLowerBound;
                std::cout << ", build=" << std::fixed << std::setprecision(3) << elapsed << " s\n";
            } else {
                std::cout << "Pair automaton: skipped (" << pairError << ")\n";
            }
        }

        if (algo == "all" || algo == "astar") {
            if (!pairReady) {
                std::cout << "A*: not run (pair automaton unavailable)\n";
            } else {
                const SolveResult a = runAStar(maze, pairAutomaton, searchOptions);
                std::cout << "A* + pair heuristic: ";
                if (a.provedOptimal) std::cout << "OPTIMAL";
                else if (a.limitReached) std::cout << "LIMIT";
                else std::cout << "NO SOLUTION";
                if (a.solved) std::cout << ", commands=" << a.commands;
                std::cout << ", expanded=" << a.expanded << ", discovered=" << a.discovered
                          << ", time=" << std::fixed << std::setprecision(3) << a.seconds << " s";
                if (!a.note.empty()) std::cout << " (" << a.note << ')';
                std::cout << "\n";
                if (a.provedOptimal && !a.path.empty() && !verifyPath(maze, a.path))
                    std::cout << "ERROR: A* path verification failed\n";
                if (printPath && !a.path.empty()) std::cout << "A* path: " << a.path << "\n";
                if (a.solved && a.commands <= dafds.commands)
                    std::cout << "A* vs DAFds: " << (dafds.commands - a.commands) << " fewer commands\n";
            }
        }

        if (algo == "all" || algo == "bfs") {
            const SolveResult b = runBFS(maze, searchOptions);
            std::cout << "Subset BFS: ";
            if (b.provedOptimal) std::cout << "OPTIMAL";
            else if (b.limitReached) std::cout << "LIMIT";
            else std::cout << "NO SOLUTION";
            if (b.solved) std::cout << ", commands=" << b.commands;
            std::cout << ", expanded=" << b.expanded << ", discovered=" << b.discovered
                      << ", time=" << std::fixed << std::setprecision(3) << b.seconds << " s";
            if (!b.note.empty()) std::cout << " (" << b.note << ')';
            std::cout << "\n";
            if (b.provedOptimal && !b.path.empty() && !verifyPath(maze, b.path))
                std::cout << "ERROR: BFS path verification failed\n";
            if (printPath && !b.path.empty()) std::cout << "BFS path: " << b.path << "\n";
            if (b.solved && b.commands <= dafds.commands)
                std::cout << "BFS vs DAFds: " << (dafds.commands - b.commands) << " fewer commands\n";
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
