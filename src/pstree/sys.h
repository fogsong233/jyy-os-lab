#pragma once

#include "printer.h"
#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <functional>
#include <print>
#include <sched.h>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <vector>

template <class Fn, class Comp>
  requires requires(Fn fn, std::filesystem::path p) {
    {
      fn(p)->parent_pid
    } -> std::convertible_to<std::expected<pid_t, std::error_code>>;
  }

auto load_p(Fn fn, Comp comp_fn = std::less<pid_t>()) -> std::expected<
    std::unordered_map<pid_t, Node<typename std::invoke_result_t<
                                  Fn, std::filesystem::path>::value_type>>,
    std::error_code> {
  using Cont = std::invoke_result_t<Fn, std::filesystem::path>::value_type;
  std::error_code ec;
  std::unordered_map<pid_t, Node<Cont>> pid2node;
  std::vector<uint64_t> pids;
  auto ls = std::filesystem::directory_iterator("/proc", ec);
  if (ec) {
    return std::unexpected{ec};
  }
  for (const auto file : ls) {
    auto fname = file.path().filename().string();
    pid_t pid;
    auto [ptr, cec] =
        std::from_chars(fname.data(), fname.data() + fname.size(), pid);
    if (cec == std::errc()) {
      auto res = fn(file.path() / "status");
      if (!res.has_value()) {
        return std::unexpected{res.error()};
      }
      pid2node.emplace(pid, res.value());
      auto ls2 = std::filesystem::directory_iterator(file.path() / "task", ec);
      if (ec) {
        return std::unexpected{ec};
      }
      for (const auto file : ls2) {
        auto fname = file.path().filename().string();
        pid_t pid;
        auto [ptr, cec] =
            std::from_chars(fname.data(), fname.data() + fname.size(), pid);
        if (cec == std::errc()) {
          auto res = fn(file.path() / "status");
          if (!res.has_value()) {
            return std::unexpected{res.error()};
          }
          pid2node.emplace(pid, res.value());
        }
      }
    }
  }
  for (auto &[pid, node] : pid2node) {
    pid2node[node.content.parent_pid].children.push_back(&node);
  }
  for (auto &[pid, node] : pid2node) {
    std::sort(node.children.begin(), node.children.end(), comp_fn);
  }
  return pid2node;
}