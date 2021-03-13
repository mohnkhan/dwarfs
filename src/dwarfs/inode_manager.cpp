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

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <deque>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include "dwarfs/compiler.h"
#include "dwarfs/entry.h"
#include "dwarfs/error.h"
#include "dwarfs/inode.h"
#include "dwarfs/inode_manager.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmif.h"
#include "dwarfs/nilsimsa.h"
#include "dwarfs/options.h"
#include "dwarfs/os_access.h"
#include "dwarfs/progress.h"
#include "dwarfs/script.h"
#include "dwarfs/similarity.h"

#include "dwarfs/gen-cpp2/metadata_types.h"

namespace dwarfs {

#define DWARFS_FIND_SIMILAR_INODE_IMPL                                         \
  std::pair<int_fast32_t, int_fast32_t> find_similar_inode(                    \
      uint64_t const* ref_hash,                                                \
      std::vector<std::shared_ptr<inode>> const& inodes,                       \
      std::vector<uint32_t> const& index, int_fast32_t const limit,            \
      int_fast32_t const end) {                                                \
    int_fast32_t max_sim = 0;                                                  \
    int_fast32_t max_sim_ix = 0;                                               \
                                                                               \
    for (int_fast32_t i = index.size() - 1; i >= end; --i) {                   \
      auto const* test_hash =                                                  \
          inodes[index[i]]->nilsimsa_similarity_hash().data();                 \
      int sim;                                                                 \
      DWARFS_NILSIMSA_SIMILARITY(sim =, ref_hash, test_hash);                  \
                                                                               \
      if (DWARFS_UNLIKELY(sim > max_sim)) {                                    \
        max_sim = sim;                                                         \
        max_sim_ix = i;                                                        \
                                                                               \
        if (DWARFS_UNLIKELY(max_sim >= limit)) {                               \
          break;                                                               \
        }                                                                      \
      }                                                                        \
    }                                                                          \
                                                                               \
    return {max_sim_ix, max_sim};                                              \
  }                                                                            \
  static_assert(true, "")

#ifdef DWARFS_MULTIVERSIONING
#ifdef __clang__
__attribute__((target("arch=skylake"))) DWARFS_FIND_SIMILAR_INODE_IMPL;
#endif
__attribute__((target("popcnt"))) DWARFS_FIND_SIMILAR_INODE_IMPL;
__attribute__((target("default")))
#endif
DWARFS_FIND_SIMILAR_INODE_IMPL;

namespace {

class inode_ : public inode {
 public:
  using chunk_type = thrift::metadata::chunk;

  void set_num(uint32_t num) override { num_ = num; }

  uint32_t num() const override { return num_; }

  uint32_t similarity_hash() const override {
    if (files_.empty()) {
      DWARFS_THROW(runtime_error, "inode has no file");
    }
    return similarity_hash_;
  }

  std::vector<uint64_t> const& nilsimsa_similarity_hash() const override {
    if (files_.empty()) {
      DWARFS_THROW(runtime_error, "inode has no file");
    }
    return nilsimsa_similarity_hash_;
  }

  void set_files(files_vector&& fv) override {
    if (!files_.empty()) {
      DWARFS_THROW(runtime_error, "files already set for inode");
    }

    files_ = std::move(fv);
  }

  void scan(os_access& os, inode_options const& opts) override {
    if (opts.needs_scan()) {
      auto file = files_.front();
      auto size = file->size();

      if (size > 0) {
        similarity sc;
        nilsimsa nc;

        auto update_hashes = [&](uint8_t const* data, size_t size) {
          if (opts.with_similarity) {
            sc.update(data, size);
          }

          if (opts.with_nilsimsa) {
            nc.update(data, size);
          }
        };

        constexpr size_t chunk_size = 16 << 20;
        auto mm = os.map_file(file->path(), size);
        size_t offset = 0;

        while (size >= chunk_size) {
          update_hashes(mm->as<uint8_t>(offset), chunk_size);
          mm->release_until(offset);
          offset += chunk_size;
          size -= chunk_size;
        }

        update_hashes(mm->as<uint8_t>(offset), size);

        if (opts.with_similarity) {
          similarity_hash_ = sc.finalize();
        }

        if (opts.with_nilsimsa) {
          nilsimsa_similarity_hash_ = nc.finalize();
        }
      }
    }
  }

  void add_chunk(size_t block, size_t offset, size_t size) override {
    chunk_type c;
    c.block = block;
    c.offset = offset;
    c.size = size;
    chunks_.push_back(c);
  }

  size_t size() const override { return any()->size(); }

  files_vector const& files() const override { return files_; }

  file const* any() const override {
    if (files_.empty()) {
      DWARFS_THROW(runtime_error, "inode has no file");
    }
    return files_.front();
  }

  void append_chunks_to(std::vector<chunk_type>& vec) const override {
    vec.insert(vec.end(), chunks_.begin(), chunks_.end());
  }

 private:
  uint32_t num_{std::numeric_limits<uint32_t>::max()};
  uint32_t similarity_hash_{0};
  files_vector files_;
  std::vector<chunk_type> chunks_;
  std::vector<uint64_t> nilsimsa_similarity_hash_;
};

} // namespace

template <typename LoggerPolicy>
class inode_manager_ final : public inode_manager::impl {
 public:
  inode_manager_(logger& lgr, progress& prog)
      : LOG_PROXY_INIT(lgr)
      , prog_(prog) {}

  std::shared_ptr<inode> create_inode() override {
    auto ino = std::make_shared<inode_>();
    inodes_.push_back(ino);
    return ino;
  }

  size_t count() const override { return inodes_.size(); }

  void order_inodes(std::shared_ptr<script> scr,
                    file_order_options const& file_order, uint32_t first_inode,
                    inode_manager::order_cb const& fn) override;

  void
  for_each_inode(std::function<void(std::shared_ptr<inode> const&)> const& fn)
      const override {
    for (const auto& ino : inodes_) {
      fn(ino);
    }
  }

 private:
  void order_inodes_by_path() {
    std::vector<std::string> paths;
    std::vector<size_t> index(inodes_.size());

    paths.reserve(inodes_.size());

    for (auto const& ino : inodes_) {
      paths.emplace_back(ino->any()->path());
    }

    std::iota(index.begin(), index.end(), size_t(0));

    std::sort(index.begin(), index.end(),
              [&](size_t a, size_t b) { return paths[a] < paths[b]; });

    std::vector<std::shared_ptr<inode>> tmp;
    tmp.reserve(inodes_.size());

    for (size_t ix : index) {
      tmp.emplace_back(inodes_[ix]);
    }

    inodes_.swap(tmp);
  }

  void order_inodes_by_similarity() {
    std::sort(
        inodes_.begin(), inodes_.end(),
        [](const std::shared_ptr<inode>& a, const std::shared_ptr<inode>& b) {
          auto ash = a->similarity_hash();
          auto bsh = b->similarity_hash();
          return ash < bsh ||
                 (ash == bsh && (a->size() > b->size() ||
                                 (a->size() == b->size() &&
                                  a->any()->path() < b->any()->path())));
        });
  }

  void presort_index(std::vector<std::shared_ptr<inode>>& inodes,
                     std::vector<uint32_t>& index);

  void
  order_inodes_by_nilsimsa(inode_manager::order_cb const& fn, uint32_t inode_no,
                           file_order_options const& file_order);

  void number_inodes(size_t first_no) {
    for (auto& i : inodes_) {
      i->set_num(first_no++);
    }
  }

  std::vector<std::shared_ptr<inode>> inodes_;
  LOG_PROXY_DECL(LoggerPolicy);
  progress& prog_;
};

template <typename LoggerPolicy>
void inode_manager_<LoggerPolicy>::order_inodes(
    std::shared_ptr<script> scr, file_order_options const& file_order,
    uint32_t first_inode, inode_manager::order_cb const& fn) {
  switch (file_order.mode) {
  case file_order_mode::NONE:
    LOG_INFO << "keeping inode order";
    break;

  case file_order_mode::PATH: {
    LOG_INFO << "ordering " << count() << " inodes by path name...";
    auto ti = LOG_TIMED_INFO;
    order_inodes_by_path();
    ti << count() << " inodes ordered";
    break;
  }

  case file_order_mode::SCRIPT: {
    if (!scr->has_order()) {
      DWARFS_THROW(runtime_error, "script cannot order inodes");
    }
    LOG_INFO << "ordering " << count() << " inodes using script...";
    auto ti = LOG_TIMED_INFO;
    scr->order(inodes_);
    ti << count() << " inodes ordered";
    break;
  }

  case file_order_mode::SIMILARITY: {
    LOG_INFO << "ordering " << count() << " inodes by similarity...";
    auto ti = LOG_TIMED_INFO;
    order_inodes_by_similarity();
    ti << count() << " inodes ordered";
    break;
  }

  case file_order_mode::NILSIMSA: {
    LOG_INFO << "ordering " << count()
             << " inodes using nilsimsa similarity...";
    auto ti = LOG_TIMED_INFO;
    order_inodes_by_nilsimsa(fn, first_inode, file_order);
    ti << count() << " inodes ordered";
    return;
  }
  }

  LOG_INFO << "assigning file inodes...";
  number_inodes(first_inode);
  for (const auto& ino : inodes_) {
    fn(ino);
  }
}

template <typename LoggerPolicy>
void inode_manager_<LoggerPolicy>::presort_index(
    std::vector<std::shared_ptr<inode>>& inodes, std::vector<uint32_t>& index) {
  auto ti = LOG_TIMED_INFO;
  size_t num_name = 0;
  size_t num_path = 0;

  std::sort(index.begin(), index.end(), [&](auto a, auto b) {
    auto const& ia = *inodes[a];
    auto const& ib = *inodes[b];
    auto sa = ia.size();
    auto sb = ib.size();

    if (sa < sb) {
      return true;
    } else if (sa > sb) {
      return false;
    }

    ++num_name;

    auto fa = ia.any();
    auto fb = ib.any();
    auto& na = fa->name();
    auto& nb = fb->name();

    if (na > nb) {
      return true;
    } else if (na < nb) {
      return false;
    }

    ++num_path;

    return fa->path() > fb->path();
  });

  ti << "pre-sorted index (" << num_name << " name, " << num_path
     << " path lookups)";
}

template <typename LoggerPolicy>
void inode_manager_<LoggerPolicy>::order_inodes_by_nilsimsa(
    inode_manager::order_cb const& fn, uint32_t inode_no,
    file_order_options const& file_order) {
  auto count = inodes_.size();

  std::vector<std::shared_ptr<inode>> inodes;
  inodes.swap(inodes_);
  inodes_.reserve(count);
  std::vector<uint32_t> index;
  index.resize(count);
  std::iota(index.begin(), index.end(), 0);

  auto empty = std::partition(index.begin(), index.end(),
                              [&](auto i) { return inodes[i]->size() > 0; });

  auto finalize_inode = [&]() {
    inodes_.push_back(std::move(inodes[index.back()]));
    index.pop_back();
    inodes_.back()->set_num(inode_no++);
    return fn(inodes_.back());
  };

  if (empty != index.end()) {
    assert(empty + 1 == index.end());
    finalize_inode();
  }

  if (!index.empty()) {
    const int_fast32_t max_depth = file_order.nilsimsa_depth;
    const int_fast32_t min_depth =
        std::min<int32_t>(file_order.nilsimsa_min_depth, max_depth);
    const int_fast32_t limit = file_order.nilsimsa_limit;
    int_fast32_t depth = max_depth;
    int64_t processed = 0;

    LOG_INFO << "nilsimsa: depth=" << depth << " (" << min_depth
             << "), limit=" << limit;

    presort_index(inodes, index);

    finalize_inode();

    while (!index.empty()) {
      auto [max_sim_ix, max_sim] = find_similar_inode(
          inodes_.back()->nilsimsa_similarity_hash().data(), inodes, index,
          limit, int(index.size()) > depth ? index.size() - depth : 0);

      LOG_TRACE << max_sim << " @ " << max_sim_ix << "/" << index.size();

      std::rotate(index.begin() + max_sim_ix, index.begin() + max_sim_ix + 1,
                  index.end());

      auto fill = finalize_inode();

      if (++processed >= 4096 && processed % 32 == 0) {
        constexpr int64_t smooth = 512;
        auto target_depth = fill * max_depth / 2048;

        depth = ((smooth - 1) * depth + target_depth) / smooth;

        if (depth > max_depth) {
          depth = max_depth;
        } else if (depth < min_depth) {
          depth = min_depth;
        }
      }

      prog_.nilsimsa_depth = depth;
    }
  }

  if (count != inodes_.size()) {
    DWARFS_THROW(runtime_error, "internal error: nilsimsa ordering failed");
  }
}

inode_manager::inode_manager(logger& lgr, progress& prog)
    : impl_(make_unique_logging_object<impl, inode_manager_, logger_policies>(
          lgr, prog)) {}

} // namespace dwarfs
