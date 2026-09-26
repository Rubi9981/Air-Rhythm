#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
struct Row { int raw, mv; };
static std::vector<Row> read_csv(const char *path) {
    std::vector<Row> out; FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
    char line[512]; bool first = true;
    while (fgets(line, sizeof line, f)) {
        if (first) { first = false; continue; }          // 헤더
        // elapsed_s,timestamp,raw,mv,...
        char *p = line; int col = 0; double raw = 0, mv = 0;
        for (char *tok = strsep(&p, ","); tok; tok = strsep(&p, ",")) {
            if (col == 2) raw = atof(tok);
            if (col == 3) { mv = atof(tok); break; }
            col++;
        }
        out.push_back({(int)raw, (int)mv});
    }
    fclose(f); return out;
}
