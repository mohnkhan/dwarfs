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
#include <stdexcept>
#include <string>
#include <vector>

#include "dwarfs/entry.h"
#include "dwarfs/inode.h"
#include "dwarfs/inode_manager.h"
#include "dwarfs/logger.h"
#include "dwarfs/mmif.h"
#include "dwarfs/nilsimsa.h"
#include "dwarfs/os_access.h"
#include "dwarfs/script.h"
#include "dwarfs/similarity.h"

#include "dwarfs/gen-cpp2/metadata_types.h"

namespace dwarfs {

namespace {

class inode_ : public inode {
 public:
  using chunk_type = thrift::metadata::chunk;

  void set_num(uint32_t num) override { num_ = num; }

  uint32_t num() const override { return num_; }

  uint32_t similarity_hash() const override {
    if (files_.empty()) {
      throw std::runtime_error("inode has no file");
    }
    return similarity_hash_;
  }

  std::vector<uint64_t> const& nilsimsa_similarity_hash() const override {
    if (files_.empty()) {
      throw std::runtime_error("inode has no file");
    }
    return nilsimsa_similarity_hash_;
  }

  void set_files(files_vector&& fv) override {
    if (!files_.empty()) {
      throw std::runtime_error("files already set for inode");
    }

    files_ = std::move(fv);
  }

  void scan(os_access& os, inode_options const& opts) override {
    if (opts.needs_scan()) {
      auto file = files_.front();
      auto size = file->size();

      if (size > 0) {
        auto mm = os.map_file(file->path(), size);
        auto data = mm->as<uint8_t>();

        if (opts.with_similarity) {
          similarity_hash_ = get_similarity_hash(data, size);
        }

        if (opts.with_nilsimsa) {
          nilsimsa_similarity_hash_ = nilsimsa_compute_hash(data, size);
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
      throw std::runtime_error("inode has no file");
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

class nilsimsa_cache_entry {
 public:
  nilsimsa_cache_entry(std::shared_ptr<inode> i)
      : size(i->size())
      , hash(i->nilsimsa_similarity_hash().data())
      , path(i->any()->path())
      , ino(std::move(i)) {
    assert(hash);
  }

  int similarity{0};
  uint64_t const size;
  uint64_t const* const hash;
  std::string const path;
  std::shared_ptr<inode> ino;
};

} // namespace

template <typename LoggerPolicy>
class inode_manager_ : public inode_manager::impl {
 public:
  inode_manager_(logger& lgr)
      : log_(lgr) {}

  std::shared_ptr<inode> create_inode() override {
    auto ino = std::make_shared<inode_>();
    inodes_.push_back(ino);
    return ino;
  }

  size_t count() const override { return inodes_.size(); }

  void order_inodes(std::shared_ptr<script> scr, file_order_mode file_order,
                    uint32_t first_inode,
                    inode_manager::inode_cb const& fn) override {
    switch (file_order) {
    case file_order_mode::NONE:
      log_.info() << "keeping inode order";
      break;

    case file_order_mode::PATH: {
      log_.info() << "ordering " << count() << " inodes by path name...";
      auto ti = log_.timed_info();
      order_inodes_by_path();
      ti << count() << " inodes ordered";
      break;
    }

    case file_order_mode::SCRIPT: {
      if (!scr->has_order()) {
        throw std::runtime_error("script cannot order inodes");
      }
      log_.info() << "ordering " << count() << " inodes using script...";
      auto ti = log_.timed_info();
      scr->order(inodes_);
      ti << count() << " inodes ordered";
      break;
    }

    case file_order_mode::SIMILARITY: {
      log_.info() << "ordering " << count() << " inodes by similarity...";
      auto ti = log_.timed_info();
      order_inodes_by_similarity();
      ti << count() << " inodes ordered";
      break;
    }

    case file_order_mode::NILSIMSA: {
      log_.info() << "ordering " << count()
                  << " inodes using nilsimsa similarity...";
      auto ti = log_.timed_info();
      order_inodes_by_nilsimsa(fn, first_inode);
      ti << count() << " inodes ordered";
      break;
    }
    }

    if (file_order != file_order_mode::NILSIMSA) {
      log_.info() << "assigning file inodes...";
      number_inodes(first_inode);
      for_each_inode(fn);
    }
  }

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

  void order_inodes_by_nilsimsa(inode_manager::inode_cb const& fn,
                                uint32_t inode_no) {
    auto finalize_inode = [&](auto& ino) {
      ino->set_num(inode_no++);
      fn(ino);
    };

    auto count = inodes_.size();

    // skip all empty inodes (this is at most one)
    auto beg = std::partition(inodes_.begin(), inodes_.end(),
                              [](auto const& p) { return p->size() == 0; });

    for (auto it = inodes_.begin(); it != beg; ++it) {
      finalize_inode(*it);
    }

    // find the largest inode
    std::nth_element(beg, beg, inodes_.end(), [](auto const& a, auto const& b) {
      return (a->size() > b->size() ||
              (a->size() == b->size() && a->any()->path() < b->any()->path()));
    });

    finalize_inode(*beg);

    // build a cache for the remaining inodes
    std::vector<nilsimsa_cache_entry> cache;
    std::deque<uint32_t> index;
    index.resize(std::distance(beg + 1, inodes_.end()));
    std::iota(index.begin(), index.end(), 0);
    cache.reserve(index.size());

    for (auto it = beg + 1; it != inodes_.end(); ++it) {
      cache.emplace_back(std::move(*it));
    }

    assert(index.size() == cache.size());

    // and temporarily remove from the original array
    inodes_.erase(beg + 1, inodes_.end());

    while (!index.empty()) {
      // compare reference inode with all remaining inodes
      auto* ref_hash = inodes_.back()->nilsimsa_similarity_hash().data();
      for (auto& d : cache) {
        d.similarity = dwarfs::nilsimsa_similarity(ref_hash, d.hash);
      }

      auto cmp = [&cache](uint32_t a, uint32_t b) {
        auto& da = cache[a];
        auto& db = cache[b];
        return da.similarity > db.similarity ||
               (da.similarity == db.similarity &&
                (da.size > db.size ||
                 (da.size == db.size && da.path < db.path)));
      };

      size_t depth = 0;
      size_t depth_thresh;
      const int sim_thresh_depth = 16;
      const int sim_thresh = 0;
      const size_t max_depth = 2000;
      const size_t depth_step = 500;

      if (index.size() > max_depth) {
        while (depth < max_depth && depth + depth_step < index.size()) {
          std::partial_sort(index.begin() + depth,
                            index.begin() + depth + depth_step, index.end(),
                            cmp);
          depth += depth_step;
          if (cache[index[0]].similarity - cache[index[depth - 1]].similarity >
              sim_thresh_depth) {
            do {
              --depth;
            } while (cache[index[0]].similarity -
                         cache[index[depth - 1]].similarity >
                     sim_thresh_depth);
            break;
          }
        }
        depth_thresh = depth / 2;
      } else {
        std::sort(index.begin(), index.end(), cmp);
        depth = index.size();
        depth_thresh = 0;
      }

      auto sim = cache[index.front()].similarity;

      while (!index.empty() && depth > depth_thresh &&
             sim - cache[index.front()].similarity <= sim_thresh) {
        inodes_.push_back(std::move(cache[index.front()].ino));
        finalize_inode(inodes_.back());
        index.pop_front();
        --depth;
      }

      while (depth > depth_thresh) {
        ref_hash = inodes_.back()->nilsimsa_similarity_hash().data();
        for (size_t i = 0; i < depth; ++i) {
          cache[index[i]].similarity =
              dwarfs::nilsimsa_similarity(ref_hash, cache[index[i]].hash);
        }

        std::partial_sort(index.begin(), index.begin() + (depth - depth_thresh),
                          index.begin() + depth, cmp);

        sim = cache[index.front()].similarity;

        while (!index.empty() && depth > depth_thresh &&
               sim - cache[index.front()].similarity <= sim_thresh) {
          inodes_.push_back(std::move(cache[index.front()].ino));
          finalize_inode(inodes_.back());
          index.pop_front();
          --depth;
        }
      }
    }

    if (count != inodes_.size()) {
      throw std::runtime_error("internal error: nilsimsa ordering failed");
    }
  }

  void number_inodes(size_t first_no) {
    for (auto& i : inodes_) {
      i->set_num(first_no++);
    }
  }

  void
  for_each_inode(std::function<void(std::shared_ptr<inode> const&)> const& fn)
      const override {
    for (const auto& ino : inodes_) {
      fn(ino);
    }
  }

 private:
  std::vector<std::shared_ptr<inode>> inodes_;
  log_proxy<LoggerPolicy> log_;
};

inode_manager::inode_manager(logger& lgr)
    : impl_(make_unique_logging_object<impl, inode_manager_, logger_policies>(
          lgr)) {}

} // namespace dwarfs
