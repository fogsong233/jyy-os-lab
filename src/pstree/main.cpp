#include "printer.h"
#include "sys.h"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <expected>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <print>
#include <ranges>
#include <sched.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>
using namespace std::literals;

struct Process {
  std::string name;
  pid_t pid;
  pid_t parent_pid;
  bool is_thread;
  bool print_pid = false;

  friend std::ostream &operator<<(std::ostream &o, const Process &p) {
    if (p.is_thread) {
      o << '{';
    }
    if (p.print_pid) {
      o << p.name << '(' << p.pid << ')';
    } else {
      o << p.name;
    }
    if (p.is_thread) {
      o << '}';
    }
    return o;
  }

  friend bool operator==(const Process &p1, const Process &p2) {
    return p1.name == p2.name;
  }
};

size_t w_first_n_bytes(std::string_view sv, size_t max_width) {
  if (sv.empty() || max_width == 0)
    return 0;

  static bool locale_initialized = []() {
    std::setlocale(LC_ALL, "");
    return true;
  }();

  std::mbstate_t state = std::mbstate_t();
  const char *ptr = sv.data();
  const char *end = ptr + sv.size();
  size_t current_width = 0;
  size_t last_safe_byte_pos = 0;

  while (ptr < end) {
    wchar_t wc;
    size_t len = std::mbrtowc(&wc, ptr, end - ptr, &state);

    if (len == 0)
      break; // 遇到 \0
    if (len == (size_t)-1 || len == (size_t)-2) {
      ptr++;
      std::mbsinit(&state);
      continue;
    }

    int w = wcwidth(wc);
    if (w < 0)
      w = 0;

    if (current_width + w > max_width)
      break;

    current_width += w;
    ptr += len;
    last_safe_byte_pos = ptr - sv.data();
  }
  return last_safe_byte_pos;
}

int main(int argc, const char **argv) {
  bool with_p = false;
  bool rank_by_pid = false;
  bool tunc_l = true;
  for (int i = 0; i < argc; i++) {
    if (argv[i] == "-v"sv) {
      std::println(std::cerr,
                   R"(Meow! This is a tiny ps-tree demo!
Author: fogsong233
See more in github:))");
      return 0;
    }
    with_p = with_p || argv[i] == "-p"sv || argv[i] == "--show-pids"sv;
    rank_by_pid =
        rank_by_pid || argv[i] == "-n"sv || argv[i] == "--numeric-sort"sv;
    tunc_l = tunc_l && argv[i] != "-l"sv && argv[i] != "--long"sv;
  }

  auto res = load_p(
      [&](std::filesystem::path p_status)
          -> std::expected<Process, std::error_code> {
        auto st = std::ifstream(p_status);
        if (!st.is_open()) {
          return std::unexpected(
              std::error_code(errno, std::generic_category()));
        }

        Process p;
        p.print_pid = with_p;
        std::string line;
        pid_t tgid = -1;
        p.pid = -1;

        while (std::getline(st, line)) {
          if (line.starts_with("Name:")) {
            size_t pos = line.find_first_not_of(" \t", 5);
            if (pos != std::string::npos)
              p.name = line.substr(pos);
          } else if (line.starts_with("Tgid:")) {
            size_t pos = line.find_first_not_of(" \t", 5);
            if (pos != std::string::npos) {
              std::from_chars(line.data() + pos, line.data() + line.size(),
                              tgid);
            }
          } else if (line.starts_with("Pid:")) {
            size_t pos = line.find_first_not_of(" \t", 4);
            if (pos != std::string::npos) {
              std::from_chars(line.data() + pos, line.data() + line.size(),
                              p.pid);
            }
          } else if (line.starts_with("PPid:")) {
            size_t pos = line.find_first_not_of(" \t", 5);
            if (pos != std::string::npos) {
              std::from_chars(line.data() + pos, line.data() + line.size(),
                              p.parent_pid);
            }
          }
        }

        p.is_thread = (p.pid != tgid);
        if (p.is_thread) {
          p.parent_pid = tgid;
        }

        if (p.pid == -1) {
          return std::unexpected(std::make_error_code(std::errc::bad_message));
        }

        return p;
      },
      [rank_by_pid](auto *a, auto *b) {
        if (rank_by_pid) {
          if (a->content.pid == b->content.pid) {
            return (a->children.size() < b->children.size());
          } else {
            return a->content.pid < b->content.pid;
          }
        }
        if (a->content.name == b->content.name) {
          return (a->children.size() < b->children.size());
        } else {
          return a->content.name < b->content.name;
        }
      });
  if (!res.has_value()) {
    std::println(std::cerr, "Found error: {}", res.error().message());
    return 1;
  }
  // all thread print the name of father
  if (!with_p) {
    for (auto &[id, node] : res.value()) {
      if (node.content.is_thread) {
        node.children.clear();
        node.content.name = res.value()[node.content.parent_pid].content.name;
      }
    }
  }
  if (tunc_l) {
    // get terminal width
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    uint64_t term_width = w.ws_col;
    std::stringstream ss;
    res.value()[1].print_self(ss);
    std::println(std::cerr, "term width: {}", term_width);
    ss.str() | std::views::lazy_split('\n') |
        std::views::transform([&](auto line) {
          auto sv =
              std::string_view(&*line.begin(), std::ranges::distance(line));
          return 1;
        });
    for (const auto line : ss.view() | std::views::lazy_split('\n')) {
      auto sv = std::string_view(&*line.begin(), std::ranges::distance(line));
      if (auto len = w_first_n_bytes(sv, term_width - 1); len == sv.size()) {
        std::println(std::cerr, "{}", sv);
      } else {
        if (auto part = sv.substr(0, len); part.back() == ' ') {
          std::println(std::cerr, "{}", part);
        } else {
          std::println(std::cerr, "{}+", part);
        }
      }
    };
  } else {
    res.value()[1].print_self(std::cerr);
  }
  return 0;
}