/**
 * @file route_validate.cpp
 * route_validate <routingDir> <pairsPerBand> <seed>
 * Sweeps the A* heuristic weight on random road-to-road pairs of one region
 * (short 3-10 km, medium 20-60 km, long 100-250 km) and prints, against the
 * weight the router used before (0.8): iterations, give-ups and the change in
 * route length. See docs/router-refactor.md.
 */
#include "maptile/router.h"
#include "maptile/routing_graph.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <random>
#include <string>
#include <vector>
using namespace MapTile;
static double metres(double lon1, double lat1, double lon2, double lat2)
{
    double dy = (lat2 - lat1) * 110574.0;
    double dx = (lon2 - lon1) * 111320.0 * cos((lat1 + lat2) * 0.5 * M_PI / 180.0);
    return sqrt(dx * dx + dy * dy);
}
static double pathLen(const RouteResult& r)
{
    double s = 0;
    for (size_t i = 1; i < r.path.size(); i++)
        s += metres(r.path[i - 1].lon, r.path[i - 1].lat, r.path[i].lon, r.path[i].lat);
    return s;
}
int main(int argc, char** argv)
{
    std::string dir = argv[1];
    int per = atoi(argv[2]);
    int seed = atoi(argv[3]);
    std::string fmt = dir + "/%d/%d/%d.rng";
    std::vector<std::pair<int, int>> tiles;
    std::string z = dir + "/10";
    DIR* d = opendir(z.c_str());
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.')
            continue;
        int x = atoi(e->d_name);
        DIR* d2 = opendir((z + "/" + e->d_name).c_str());
        while (dirent* f = readdir(d2)) {
            if (f->d_name[0] == '.')
                continue;
            tiles.push_back({ x, atoi(f->d_name) });
        }
        closedir(d2);
    }
    closedir(d);
    std::sort(tiles.begin(), tiles.end());
    RoutingGraph g;
    g.setDataPath(fmt.c_str());
    g.setCacheSize(64);
    g.setCacheBytes(16 << 20);
    std::mt19937 rng(seed);
    const double Hs[] = { 0.8, 1.0, 1.15, 1.3 };
    const int NH = 4;
    Router r[NH];
    for (int i = 0; i < NH; i++) {
        r[i].setDataPath(fmt.c_str());
        r[i].setCacheSize(64);
        r[i].setCacheBytes(16 << 20);
        r[i].setMaxIterations(600000);
        r[i].setHeuristicWeight(Hs[i]);
        r[i].setSnapLimit(300);
    }
    struct Band {
        const char* n;
        double lo, hi;
    } bands[] = { { "short", 3, 10 }, { "medium", 20, 60 }, { "long", 100, 250 } };
    for (auto& b : bands) {
        std::vector<double> dd[NH];
        long it[NH] = { 0 };
        int found[NH] = { 0 }, giveup[NH] = { 0 }, worse2[NH] = { 0 }, worse8[NH] = { 0 }, n = 0, nf0 = 0;
        double itBase = 0;
        double itSum[NH] = { 0 };
        int both[NH] = { 0 };
        for (int tries = 0; n < per && tries < per * 40; tries++) {
            auto t = tiles[rng() % tiles.size()];
            TileBlock* bl = g.getTile(t.first, t.second);
            if (!bl || bl->nodes.empty())
                continue;
            const RngNode& nd = bl->nodes[rng() % bl->nodes.size()];
            double lon = nd.lon * 1e-7, lat = nd.lat * 1e-7;
            std::uniform_real_distribution<double> ud(b.lo, b.hi), ua(0, 2 * M_PI);
            double dist = ud(rng) * 1000, a = ua(rng);
            double lon2 = lon + dist * sin(a) / (111320.0 * cos(lat * M_PI / 180)), lat2 = lat + dist * cos(a) / 110574.0;
            RouteResult base = r[0].findRoute({ lon, lat }, { lon2, lat2 });
            if (base.status == RouteStatus::NoData || base.status == RouteStatus::NoNodeNearStart || base.status == RouteStatus::NoNodeNearEnd || base.status == RouteStatus::NotConnected)
                continue;
            n++;
            double L0 = base.found ? pathLen(base) : 0;
            for (int i = 0; i < NH; i++) {
                RouteResult x = i == 0 ? base : r[i].findRoute({ lon, lat }, { lon2, lat2 });
                itSum[i] += x.iterations;
                if (x.found) {
                    found[i]++;
                    if (base.found) {
                        both[i]++;
                        double pct = (pathLen(x) / L0 - 1) * 100;
                        dd[i].push_back(pct);
                        if (pct > 2)
                            worse2[i]++;
                        if (pct > 8)
                            worse8[i]++;
                    }
                } else
                    giveup[i]++;
            }
        }
        printf("%s: %d pairs\n", b.n, n);
        for (int i = 0; i < NH; i++) {
            std::sort(dd[i].begin(), dd[i].end());
            auto q = [&](double p) { return dd[i].empty() ? 0 : dd[i][std::min(dd[i].size() - 1, (size_t)(p * dd[i].size()))]; };
            printf("  H=%.2f found %d giveup %d iters %.0f%% dist%% p50 %+.2f p95 %+.2f max %+.2f  >2%%:%d >8%%:%d (of %d)\n", Hs[i], found[i], giveup[i], 100 * itSum[i] / std::max(1.0, itSum[0]), q(0.5), q(0.95), dd[i].empty() ? 0 : dd[i].back(), worse2[i], worse8[i], both[i]);
        }
    }
}
