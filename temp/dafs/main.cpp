#include <bits/stdc++.h>
using namespace std;

int H, W;
vector<string> grid;
vector<vector<int>> cell_id;
vector<int> free_r, free_c;
vector<int> bfs_dist, parent_dir;
int exit_id = -1;

const int DR[4] = {-1, 1, 0, 0};
const int DC[4] = {0, 0, -1, 1};
const char CMDS[4] = {'U', 'D', 'L', 'R'};

int move_cell(int id, int d) {
    int r = free_r[id], c = free_c[id];
    int nr = r + DR[d], nc = c + DC[d];
    if (nr < 0 || nr >= H || nc < 0 || nc >= W) return id;
    if (grid[nr][nc] == '#') return id;
    return cell_id[nr][nc];
}

void apply_cmd(const vector<int>& S, int d, vector<int>& T) {
    T.clear();
    T.reserve(S.size());
    for (int id : S) {
        if (id == exit_id) { T.push_back(id); continue; }
        T.push_back(move_cell(id, d));
    }
    sort(T.begin(), T.end());
    T.erase(unique(T.begin(), T.end()), T.end());
}

void apply_to_set(vector<int>& S, int d) {
    vector<int> T;
    apply_cmd(S, d, T);
    S = move(T);
}

void bfs_from_exit() {
    bfs_dist.assign(free_r.size(), -1);
    parent_dir.assign(free_r.size(), -1);
    queue<int> q;
    bfs_dist[exit_id] = 0;
    q.push(exit_id);
    while (!q.empty()) {
        int id = q.front(); q.pop();
        for (int d = 0; d < 4; d++) {
            int nid = move_cell(id, d);
            if (nid == id) continue;
            if (bfs_dist[nid] == -1) {
                bfs_dist[nid] = bfs_dist[id] + 1;
                parent_dir[nid] = d ^ 1;
                q.push(nid);
            }
        }
    }
}

// Variant 1: farthest first — каждый раунд берём самую дальнюю черепашку
// и проигрываем весь её путь к E, двигая всех.
vector<char> solve_farthest() {
    vector<int> S;
    for (int i = 0; i < (int)free_r.size(); i++)
        if (bfs_dist[i] >= 0) S.push_back(i);

    vector<char> cmds;
    int safety = 0;
    while (!(S.size() == 1 && S[0] == exit_id)) {
        if (++safety > 10000000) { cerr << "farthest: safety limit\n"; break; }
        int best = -1, bd = -1;
        for (int id : S) {
            if (id == exit_id) continue;
            if (bfs_dist[id] > bd) { bd = bfs_dist[id]; best = id; }
        }
        if (best == -1) break;
        int cur = best;
        while (cur != exit_id) {
            int d = parent_dir[cur];
            cmds.push_back(CMDS[d]);
            apply_to_set(S, d);
            cur = move_cell(cur, d);
        }
    }
    return cmds;
}

// Variant 2: DAF — на каждом шаге выбираем направление, минимизирующее
// суммарное расстояние; если 5 шагов подряд нет улучшения на 10% —
// откат к farthest-ходу (самая дальняя черепашка, один шаг к E).
// Variant 2 (fixed): DAF — адаптивная эвристика по сумме расстояний
// с полным farthest-раундом (а не одношаговым) в качестве выхода из стагнации.
vector<char> solve_daf() {
    vector<int> S;
    for (int i = 0; i < (int)free_r.size(); i++)
        if (bfs_dist[i] >= 0) S.push_back(i);

    auto sum_of = [&](const vector<int>& s) -> long long {
        long long t = 0;
        for (int id : s) if (id != exit_id) t += bfs_dist[id];
        return t;
    };

    vector<char> cmds;
    long long best_sum_ever = sum_of(S);
    int stagnation = 0;
    int farthest_rounds = 0, adaptive_steps = 0;
    int safety = 0;

    while (!(S.size() == 1 && S[0] == exit_id)) {
        if (++safety > 200000) {
            cerr << "DAF safety at step " << safety << ", |S|=" << S.size() << "\n";
            break;
        }

        // 4 кандидата: минимум суммы, при равенстве — минимум |T|
        int best_d = 0;
        long long best_sum = LLONG_MAX;
        int best_size = INT_MAX;
        vector<int> best_T;
        for (int d = 0; d < 4; d++) {
            vector<int> T;
            apply_cmd(S, d, T);
            long long ts = sum_of(T);
            if (ts < best_sum || (ts == best_sum && (int)T.size() < best_size)) {
                best_sum = ts;
                best_size = (int)T.size();
                best_d = d;
                best_T = move(T);
            }
        }

        // Улучшение = побили лучшую сумму за всё время ИЛИ уменьшили |S|
        bool improved = (best_sum < best_sum_ever) || (best_size < (int)S.size());
        if (improved) {
            stagnation = 0;
            if (best_sum < best_sum_ever) best_sum_ever = best_sum;
        } else {
            stagnation++;
        }

        // Решаем: farthest-раунд или адаптивный шаг
        bool do_farthest = (best_T == S) || (stagnation >= 5);
//                bool do_farthest = (stagnation >= 5);
        if (do_farthest) {
            // Найти самую дальнюю не-E черепашку и провести её до E
            int f = -1, fd = -1;
            for (int id : S) {
                if (id == exit_id) continue;
                if (bfs_dist[id] > fd) { fd = bfs_dist[id]; f = id; }
            }
            if (f == -1) break;
            int cur = f;
            while (cur != exit_id) {
                int d = parent_dir[cur];
                cmds.push_back(CMDS[d]);
                apply_to_set(S, d);
                cur = move_cell(cur, d);
            }
            best_sum_ever = sum_of(S);   // свежий baseline после farthest
            stagnation = 0;
            farthest_rounds++;
        } else {
            S = move(best_T);
            cmds.push_back(CMDS[best_d]);
            adaptive_steps++;
        }
    }

    cerr << "DAF: adaptive=" << adaptive_steps
         << ", farthest_rounds=" << farthest_rounds
         << ", total_cmds=" << cmds.size() << "\n";
    return cmds;
}
bool verify(const vector<char>& cmds) {
    vector<int> S;
    for (int i = 0; i < (int)free_r.size(); i++)
        if (bfs_dist[i] >= 0) S.push_back(i);
    for (char ch : cmds) {
        int d = -1;
        for (int k = 0; k < 4; k++) if (CMDS[k] == ch) { d = k; break; }
        if (d < 0) return false;
        apply_to_set(S, d);
        if (S.size() == 1 && S[0] == exit_id) return true;
    }
    return S.size() == 1 && S[0] == exit_id;
}

int main(int argc, char** argv) {
    string fname = (argc > 1) ? argv[1] : "maze.txt";
    ifstream f(fname);
    if (!f) { cerr << "cannot open " << fname << "\n"; return 1; }
    string line;
    while (getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        grid.push_back(line);
    }
    H = grid.size();
    W = 0;
    for (auto& s : grid) W = max(W, (int)s.size());
    bool padded = false;
    for (auto& s : grid) { if ((int)s.size() < W) padded = true; s.resize(W, ' '); }

    cell_id.assign(H, vector<int>(W, -1));
    int er = -1, ec = -1;
    for (int r = 0; r < H; r++) for (int c = 0; c < W; c++) {
        if (grid[r][c] == '#') continue;
        cell_id[r][c] = free_r.size();
        free_r.push_back(r);
        free_c.push_back(c);
        if (grid[r][c] == 'E') { er = r; ec = c; }
    }
    if (er == -1) {
        cerr << "No 'E' found in maze. Add 'E' to mark the exit.\n";
        cerr << "Parsed grid (" << H << "x" << W << "):\n";
        for (auto& s : grid) cerr << "|" << s << "|\n";
        return 1;
    }
    if (padded) cerr << "WARNING: some lines were padded with spaces\n";

    exit_id = cell_id[er][ec];
    bfs_from_exit();

    int unreachable = 0;
    for (int i = 0; i < (int)free_r.size(); i++)
        if (bfs_dist[i] == -1) unreachable++;
    if (unreachable) cerr << "WARNING: " << unreachable << " cells unreachable from E\n";

    cerr << "Grid " << H << "x" << W << ", free cells: " << free_r.size()
         << ", exit at (" << er << "," << ec << ")\n";

    auto t0 = chrono::steady_clock::now();
    auto c1 = solve_farthest();
    auto t1 = chrono::steady_clock::now();
    auto c2 = solve_daf();
    auto t2 = chrono::steady_clock::now();

    double ms1 = chrono::duration_cast<chrono::microseconds>(t1-t0).count()/1000.0;
    double ms2 = chrono::duration_cast<chrono::microseconds>(t2-t1).count()/1000.0;

    cout << "Farthest: commands=" << c1.size() << ", time=" << ms1 << "ms, verify=" << (verify(c1)?"YES":"NO") << "\n";
    cout << "DAF:      commands=" << c2.size() << ", time=" << ms2 << "ms, verify=" << (verify(c2)?"YES":"NO") << "\n";

    if (argc > 2 && string(argv[2]) == "-v") {
        cout << "\nFarthest:\n" << string(c1.begin(), c1.end()) << "\n";
        cout << "\nDAF:\n" << string(c2.begin(), c2.end()) << "\n";
    }
    return 0;
}