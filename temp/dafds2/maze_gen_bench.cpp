#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <limits>
#include <queue>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using Bits = std::vector<uint64_t>;

constexpr uint32_t INF = std::numeric_limits<uint32_t>::max();
constexpr std::array<int, 4> DX{0, 1, 0, -1};
constexpr std::array<int, 4> DY{-1, 0, 1, 0};
constexpr std::array<char, 4> MOVE{'U', 'R', 'D', 'L'};
constexpr uint64_t MAX_COMMANDS = 100000000ULL;

struct Maze {
    int width = 0;
    int height = 0;
    std::vector<std::string> rows;
    std::vector<int32_t> cellAt;
    std::vector<int32_t> cellX;
    std::vector<int32_t> cellY;
    std::vector<std::array<uint32_t, 4>> next;

    uint32_t cellCount() const { return static_cast<uint32_t>(next.size()); }
    int index(int x, int y) const { return y * width + x; }

    static Maze load(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open maze: " + path);

        Maze m;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) m.rows.push_back(line);
        }
        if (m.rows.empty()) throw std::runtime_error("maze is empty: " + path);

        m.height = static_cast<int>(m.rows.size());
        m.width = static_cast<int>(m.rows.front().size());
        if (m.width == 0) throw std::runtime_error("maze width is zero");

        int exits = 0;
        for (int y = 0; y < m.height; ++y) {
            if (static_cast<int>(m.rows[y].size()) != m.width)
                throw std::runtime_error("maze rows have different widths");
            for (int x = 0; x < m.width; ++x)
                if (m.rows[y][x] == 'E' || m.rows[y][x] == 'e') ++exits;
        }
        if (exits != 1) throw std::runtime_error("maze must contain exactly one E cell");

        m.cellAt.assign(static_cast<size_t>(m.width) * m.height, -1);
        for (int y = 0; y < m.height; ++y) {
            for (int x = 0; x < m.width; ++x) {
                const char c = m.rows[y][x];
                if (c == '#' || c == 'E' || c == 'e') continue;
                const int32_t id = static_cast<int32_t>(m.next.size());
                m.cellAt[m.index(x, y)] = id;
                m.cellX.push_back(x);
                m.cellY.push_back(y);
                m.next.emplace_back();
            }
        }
        if (m.next.empty()) throw std::runtime_error("maze has no walkable cells");

        const uint32_t sink = m.cellCount();
        for (uint32_t id = 0; id < m.cellCount(); ++id) {
            const int x = m.cellX[id];
            const int y = m.cellY[id];
            for (int d = 0; d < 4; ++d) {
                const int nx = x + DX[d];
                const int ny = y + DY[d];
                if (nx < 0 || nx >= m.width || ny < 0 || ny >= m.height) {
                    m.next[id][d] = id;
                    continue;
                }
                const char target = m.rows[ny][nx];
                if (target == 'E' || target == 'e') m.next[id][d] = sink;
                else if (target == '#') m.next[id][d] = id;
                else {
                    const int32_t targetId = m.cellAt[m.index(nx, ny)];
                    if (targetId < 0) throw std::runtime_error("invalid maze cell mapping");
                    m.next[id][d] = static_cast<uint32_t>(targetId);
                }
            }
        }
        return m;
    }
};

struct Distances {
    std::vector<uint32_t> distance;
    std::vector<uint8_t> routeDirection;
    bool allReachExit = false;
};

Distances buildDistances(const Maze& m) {
    const uint32_t n = m.cellCount();
    Distances result;
    result.distance.assign(n, INF);
    result.routeDirection.assign(n, 255);
    std::queue<uint32_t> q;

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
        for (int move = 0; move < 4; ++move) {
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
    result.allReachExit = std::all_of(result.distance.begin(), result.distance.end(),
                                      [](uint32_t d) { return d != INF; });
    return result;
}

Bits makeInitial(const Maze& m) {
    Bits state((m.cellCount() + 63u) / 64u, ~uint64_t{0});
    const uint32_t rem = m.cellCount() & 63u;
    if (rem != 0) state.back() = (uint64_t{1} << rem) - 1;
    return state;
}

bool emptyBits(const Bits& state) {
    for (uint64_t word : state) if (word != 0) return false;
    return true;
}

uint32_t bitCount(const Bits& state) {
    uint32_t count = 0;
    for (uint64_t word : state) count += static_cast<uint32_t>(__builtin_popcountll(word));
    return count;
}

Bits applyMove(const Maze& m, const Bits& state, int direction) {
    Bits out(state.size(), 0);
    const uint32_t n = m.cellCount();
    for (size_t wi = 0; wi < state.size(); ++wi) {
        uint64_t word = state[wi];
        while (word) {
            const unsigned bit = static_cast<unsigned>(__builtin_ctzll(word));
            const uint32_t from = static_cast<uint32_t>(wi * 64 + bit);
            word &= word - 1;
            if (from >= n) continue;
            const uint32_t to = m.next[from][direction];
            if (to < n) out[to >> 6] |= uint64_t{1} << (to & 63u);
        }
    }
    return out;
}

uint64_t sumDistances(const Bits& state, const Distances& distances) {
    uint64_t sum = 0;
    for (size_t wi = 0; wi < state.size(); ++wi) {
        uint64_t word = state[wi];
        while (word) {
            const unsigned bit = static_cast<unsigned>(__builtin_ctzll(word));
            const uint32_t p = static_cast<uint32_t>(wi * 64 + bit);
            word &= word - 1;
            if (p < distances.distance.size() && distances.distance[p] != INF)
                sum += distances.distance[p];
        }
    }
    return sum;
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

struct SolveResult {
    bool solved = false;
    uint64_t commands = 0;
    uint64_t adaptive = 0;
    uint64_t fallbacks = 0;
    double milliseconds = 0.0;
    std::string path;
    std::string error;
};

uint32_t selectFarthest(const Bits& state, const Distances& distances) {
    uint32_t best = 0;
    uint32_t bestDistance = 0;
    bool found = false;
    for (size_t wi = 0; wi < state.size(); ++wi) {
        uint64_t word = state[wi];
        while (word) {
            const unsigned bit = static_cast<unsigned>(__builtin_ctzll(word));
            const uint32_t p = static_cast<uint32_t>(wi * 64 + bit);
            word &= word - 1;
            if (p < distances.distance.size() &&
                (!found || distances.distance[p] > bestDistance)) {
                found = true;
                best = p;
                bestDistance = distances.distance[p];
            }
        }
    }
    if (!found || bestDistance == INF) return INF;
    return best;
}

SolveResult runDAFds(const Maze& m, const Distances& distances, uint32_t stagnationLimit) {
    SolveResult result;
    if (!distances.allReachExit) {
        result.error = "some free cells cannot reach E";
        return result;
    }
    const auto started = Clock::now();
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
        if (result.commands >= MAX_COMMANDS) {
            result.error = "command safety limit reached";
            return result;
        }
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
        const bool identity = (bestState == state);
        const bool progress = (bestSum < cycleMinSum) || (bestCount < oldCount);
        bool doFallback = identity;

        if (!identity) {
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
            if (stagnation >= stagnationLimit) doFallback = true;
        }

        if (doFallback && !emptyBits(state)) {
            ++result.fallbacks;
            const uint32_t farthest = selectFarthest(state, distances);
            if (farthest == INF) {
                result.error = "Farthest could not select a reachable active cell";
                return result;
            }
            uint32_t selected = farthest;
            while (selected < n) {
                if (result.commands >= MAX_COMMANDS) {
                    result.error = "command safety limit reached in Farthest";
                    return result;
                }
                const uint8_t dir = distances.routeDirection[selected];
                if (dir > 3) {
                    result.error = "missing shortest-route direction";
                    return result;
                }
                applyOne(dir);
                selected = m.next[selected][dir];
            }
            cycleMinSum = sumDistances(state, distances);
            stagnation = 0;
        }
    }

    result.solved = true;
    result.milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    return result;
}

struct FallbackEvent {
    uint64_t episode = 0;
    uint32_t steps = 0;
    std::string type;
};

struct DAFds2Result : SolveResult {
    std::vector<FallbackEvent> events;
};

DAFds2Result runDAFds2(const Maze& m, const Distances& distances, uint32_t stagnationLimit) {
    DAFds2Result result;
    if (!distances.allReachExit) {
        result.error = "some free cells cannot reach E";
        return result;
    }
    const auto started = Clock::now();
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
        if (result.commands >= MAX_COMMANDS) {
            result.error = "command safety limit reached";
            return result;
        }
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
        const bool identity = (bestState == state);
        const bool progress = (bestSum < cycleMinSum) || (bestCount < oldCount);
        bool doFallback = identity;

        if (!identity) {
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
            if (stagnation >= stagnationLimit) doFallback = true;
        }

        if (!doFallback || emptyBits(state)) continue;
        ++result.fallbacks;
        const uint32_t farthest = selectFarthest(state, distances);
        if (farthest == INF) {
            result.error = "Farthest could not select a reachable active cell";
            return result;
        }

        uint32_t selected = farthest;
        uint32_t episodeSteps = 0;
        bool eventFound = false;
        while (selected < n) {
            if (result.commands >= MAX_COMMANDS) {
                result.error = "command safety limit reached in DAFds2 Farthest";
                return result;
            }
            const uint8_t dir = distances.routeDirection[selected];
            if (dir > 3) {
                result.error = "missing shortest-route direction";
                return result;
            }

            const uint32_t beforeCount = bitCount(state);
            uint32_t exitCount = 0;
            for (size_t wi = 0; wi < state.size(); ++wi) {
                uint64_t word = state[wi];
                while (word) {
                    const unsigned bit = static_cast<unsigned>(__builtin_ctzll(word));
                    const uint32_t p = static_cast<uint32_t>(wi * 64 + bit);
                    word &= word - 1;
                    if (p < n && m.next[p][dir] == n) ++exitCount;
                }
            }

            applyOne(dir);
            ++episodeSteps;
            selected = m.next[selected][dir];
            const uint32_t afterCount = bitCount(state);
            const bool hadExit = exitCount != 0;
            const bool hadMerge = afterCount < beforeCount - exitCount;
            if (hadExit || hadMerge) {
                std::string type;
                if (hadExit && hadMerge) type = "merge+exit";
                else if (hadMerge) type = "merge";
                else type = "exit";
                result.events.push_back({result.fallbacks, episodeSteps, type});
                eventFound = true;
                break;
            }
        }

        if (!eventFound) {
            result.error = "Farthest reached E without detecting an exit or merge event";
            return result;
        }
        cycleMinSum = sumDistances(state, distances);
        stagnation = 0;
    }

    result.solved = true;
    result.milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    return result;
}

std::string readJsonText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open metadata JSON: " + path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string jsonStringField(const std::string& json, const std::string& key) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (!std::regex_search(json, match, pattern))
        throw std::runtime_error("metadata JSON is missing string field: " + key);
    return match[1].str();
}

uint64_t jsonU64Field(const std::string& json, const std::string& key) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (!std::regex_search(json, match, pattern))
        throw std::runtime_error("metadata JSON is missing integer field: " + key);
    size_t used = 0;
    const auto value = std::stoull(match[1].str(), &used);
    if (used != match[1].str().size())
        throw std::runtime_error("invalid integer metadata field: " + key);
    return static_cast<uint64_t>(value);
}

std::string mapPathFromMetadata(const std::string& metadataPath) {
    const std::string json = readJsonText(metadataPath);
    const std::string filename = jsonStringField(json, "map_file");
    if (filename.empty())
        throw std::runtime_error("metadata JSON has no ASCII map_file; generate with --format ascii or both");
    const std::filesystem::path metadata = std::filesystem::absolute(metadataPath);
    const std::filesystem::path mapPath = metadata.parent_path() / filename;
    const uint64_t expectedWidth = jsonU64Field(json, "map_width");
    const uint64_t expectedHeight = jsonU64Field(json, "map_height");
    const Maze map = Maze::load(mapPath.string());
    if (static_cast<uint64_t>(map.width) != expectedWidth || static_cast<uint64_t>(map.height) != expectedHeight)
        throw std::runtime_error("map dimensions do not match metadata JSON: " + mapPath.string());
    return mapPath.string();
}

uint64_t parseU64(const std::string& value, const std::string& option) {
    size_t used = 0;
    const unsigned long long parsed = std::stoull(value, &used);
    if (used != value.size()) throw std::runtime_error("invalid value for " + option + ": " + value);
    return static_cast<uint64_t>(parsed);
}

void usage(const char* exe) {
    std::cout << "Usage: " << exe << " (--maze FILE | --maze-json METADATA.json) [--limit N]\n"
              << "Runs DAFds and DAFds2 on the same maze and verifies both paths.\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::string mazePath;
        std::string metadataPath;
        uint32_t stagnationLimit = 1;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (i + 1 >= argc) throw std::runtime_error("missing value after " + arg);
                return argv[++i];
            };
            if (arg == "--maze") mazePath = value();
            else if (arg == "--maze-json") metadataPath = value();
            else if (arg == "--limit") {
                const uint64_t v = parseU64(value(), arg);
                if (v == 0 || v > std::numeric_limits<uint32_t>::max())
                    throw std::runtime_error("--limit must be in 1..4294967295");
                stagnationLimit = static_cast<uint32_t>(v);
            } else if (arg == "--help" || arg == "-h") {
                usage(argv[0]);
                return 0;
            } else {
                throw std::runtime_error("unknown option: " + arg);
            }
        }
        if (mazePath.empty() == metadataPath.empty()) {
            usage(argv[0]);
            return 2;
        }
        if (!metadataPath.empty()) mazePath = mapPathFromMetadata(metadataPath);

        const Maze maze = Maze::load(mazePath);
        const Distances distances = buildDistances(maze);
        if (!distances.allReachExit) {
            const auto unreachable = std::count(distances.distance.begin(), distances.distance.end(), INF);
            throw std::runtime_error(std::to_string(unreachable) + " free cells cannot reach E");
        }

        const SolveResult dafds = runDAFds(maze, distances, stagnationLimit);
        if (!dafds.solved) throw std::runtime_error("DAFds failed: " + dafds.error);
        const DAFds2Result dafds2 = runDAFds2(maze, distances, stagnationLimit);
        if (!dafds2.solved) throw std::runtime_error("DAFds2 failed: " + dafds2.error);

        const bool verify1 = verifyPath(maze, dafds.path);
        const bool verify2 = verifyPath(maze, dafds2.path);
        std::cout << "META\t" << maze.cellCount() << "\n";
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "DAFDS\t" << dafds.commands << '\t' << dafds.adaptive << '\t'
                  << dafds.fallbacks << '\t' << dafds.milliseconds << '\t'
                  << (verify1 ? 1 : 0) << "\n";
        std::cout << "DAFDS2\t" << dafds2.commands << '\t' << dafds2.adaptive << '\t'
                  << dafds2.fallbacks << '\t' << dafds2.milliseconds << '\t'
                  << (verify2 ? 1 : 0) << "\n";
        for (const auto& event : dafds2.events)
            std::cout << "EVENT\t" << event.episode << '\t' << event.steps << '\t' << event.type << "\n";
        if (!verify1 || !verify2) return 3;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
