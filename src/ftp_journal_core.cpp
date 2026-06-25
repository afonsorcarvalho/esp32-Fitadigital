#include "ftp_journal_core.h"

#include <cstdlib>
#include <sstream>

namespace ftpj {

JournalMap parse_journal(const std::string &text) {
    JournalMap m;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t bar = line.find('|');
        if (bar == std::string::npos || bar == 0) {
            continue;
        }
        std::string relpath = line.substr(0, bar);
        std::string num = line.substr(bar + 1);
        // tamanho: precisa de pelo menos um digito e so digitos
        if (num.empty()) {
            continue;
        }
        bool ok = true;
        for (char c : num) {
            if (c < '0' || c > '9') {
                ok = false;
                break;
            }
        }
        if (!ok) {
            continue;
        }
        m[relpath] = std::strtol(num.c_str(), nullptr, 10);
    }
    return m;
}

std::string serialize_journal(const JournalMap &m) {
    std::string out;
    for (const auto &kv : m) {
        out += kv.first;
        out += '|';
        out += std::to_string(kv.second);
        out += '\n';
    }
    return out;
}

bool needs_upload(const JournalMap &j, const std::string &relpath, long cur_size) {
    auto it = j.find(relpath);
    if (it == j.end()) {
        return true;
    }
    return it->second != cur_size;
}

static std::string join_slash(const std::string &a, const std::string &b) {
    if (a.empty()) {
        return b;
    }
    bool a_slash = !a.empty() && a.back() == '/';
    bool b_slash = !b.empty() && b.front() == '/';
    if (a_slash && b_slash) {
        return a + b.substr(1);
    }
    if (!a_slash && !b_slash) {
        return a + "/" + b;
    }
    return a + b;
}

std::string remote_path(const std::string &base, const std::string &relpath) {
    std::string r = join_slash(base, relpath);
    if (r.size() > 1 && r.back() == '/') {
        r.pop_back();
    }
    return r;
}

std::vector<std::string> remote_dir_components(const std::string &base, const std::string &relpath) {
    std::vector<std::string> out;
    std::string b = base.empty() ? "/" : base;
    if (b.size() > 1 && b.back() == '/') {
        b.pop_back();
    }
    out.push_back(b);
    std::string acc = b;
    // percorre os segmentos de directorio do relpath (exclui o ultimo = nome do ficheiro)
    std::istringstream in(relpath);
    std::string seg;
    std::vector<std::string> segs;
    while (std::getline(in, seg, '/')) {
        if (!seg.empty()) {
            segs.push_back(seg);
        }
    }
    if (segs.empty()) {
        return out;
    }
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
        acc = join_slash(acc, segs[i]);
        out.push_back(acc);
    }
    return out;
}

}  // namespace ftpj
