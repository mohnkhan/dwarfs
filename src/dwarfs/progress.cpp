/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "dwarfs/progress.h"

#include <folly/system/ThreadName.h>

namespace dwarfs {

progress::progress(folly::Function<void(const progress&, bool)>&& func)
    : running_(true)
    , thread_([this, func = std::move(func)]() mutable {
      folly::setThreadName("progress");
      std::unique_lock<std::mutex> lock(mx_);
      while (running_) {
        func(*this, false);
        cond_.wait_for(lock, std::chrono::milliseconds(200));
      }
      func(*this, true);
    }) {}

progress::~progress() noexcept {
  try {
    running_ = false;
    cond_.notify_all();
    thread_.join();
  } catch (...) {
  }
}
} // namespace dwarfs
