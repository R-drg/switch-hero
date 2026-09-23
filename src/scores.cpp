#include "scores.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fret {
int starsFor(double accuracy, bool failed) {
    if (failed)
        return 0;
    return accuracy >= .97 ? 5 : accuracy >= .9 ? 4 : accuracy >= .8 ? 3 : accuracy >= .65 ? 2 : 1;
}
char rankFor(double accuracy, bool failed) {
    if (failed)
        return 'F';
    return accuracy >= .97 ? 'S' : accuracy >= .9 ? 'A' : accuracy >= .8 ? 'B' : accuracy >= .65 ? 'C' : 'D';
}
std::string Scores::key(const std::string &folder, const std::string &part, int difficulty) {
    return folder + '\t' + part + '\t' + std::to_string(difficulty);
}
void Scores::load(const fs::path &path) {
    records.clear();
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        // folder, part, difficulty, score, stars, accuracy, full combo
        std::vector<std::string> fields;
        std::istringstream in(line);
        for (std::string field; std::getline(in, field, '\t');)
            fields.push_back(field);
        if (fields.size() != 7 || fields[0].empty())
            continue;
        try {
            Record r;
            const int difficulty = std::stoi(fields[2]);
            r.score = std::stoi(fields[3]), r.stars = std::stoi(fields[4]), r.accuracy = std::stoi(fields[5]);
            r.fullCombo = fields[6] == "1";
            if (difficulty < 0 || difficulty > 3 || r.score < 0 || r.stars < 0 || r.stars > 5)
                continue;
            records[key(fields[0], fields[1], difficulty)] = r;
        } catch (const std::exception &) {
            // A damaged line costs one record, not the whole file.
        }
    }
}
void Scores::save(const fs::path &path) const {
    std::ofstream f(path);
    if (!f)
        throw std::runtime_error("Cannot save scores: " + path.string());
    for (const auto &[k, r] : records)
        f << k << '\t' << r.score << '\t' << r.stars << '\t' << r.accuracy << '\t' << (r.fullCombo ? 1 : 0) << '\n';
}
const Record *Scores::find(const std::string &folder, const std::string &part, int difficulty) const {
    auto it = records.find(key(folder, part, difficulty));
    return it == records.end() ? nullptr : &it->second;
}
int Scores::bestStars(const std::string &folder) const {
    int best = -1;
    const std::string prefix = folder + '\t';
    for (auto it = records.lower_bound(prefix); it != records.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
        best = std::max(best, it->second.stars);
    return best;
}
void Scores::forget(const std::string &folder) {
    const std::string prefix = folder + '\t';
    auto it = records.lower_bound(prefix);
    while (it != records.end() && it->first.compare(0, prefix.size(), prefix) == 0)
        it = records.erase(it);
}
bool Scores::submit(const std::string &folder, const std::string &part, int difficulty, const Record &run) {
    auto &stored = records[key(folder, part, difficulty)];
    const bool first = stored.score == 0 && stored.stars == 0 && !stored.fullCombo;
    if (!first && run.score <= stored.score) {
        // A full combo is worth keeping even without a higher score.
        stored.fullCombo = stored.fullCombo || run.fullCombo;
        return false;
    }
    const bool fc = stored.fullCombo || run.fullCombo;
    stored = run;
    stored.fullCombo = fc;
    return true;
}
} // namespace fret
