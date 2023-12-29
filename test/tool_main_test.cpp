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

#include <filesystem>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fmt/format.h>

#include "dwarfs/filesystem_v2.h"
#include "dwarfs/util.h"
#include "dwarfs_tool_main.h"

#include "mmap_mock.h"
#include "test_helpers.h"
#include "test_logger.h"

using namespace dwarfs;

namespace fs = std::filesystem;

namespace {

auto test_dir = fs::path(TEST_DATA_DIR).make_preferred();
auto audio_data_dir = test_dir / "pcmaudio";
auto test_data_image = test_dir / "data.dwarfs";

struct locale_setup_helper {
  locale_setup_helper() { setup_default_locale(); }
};

void setup_locale() { static locale_setup_helper helper; }

class tool_main_test : public testing::Test {
 public:
  void SetUp() override {
    setup_locale();
    iol = std::make_unique<test::test_iolayer>();
  }

  void TearDown() override { iol.reset(); }

  std::string out() const { return iol->out(); }
  std::string err() const { return iol->err(); }

  std::unique_ptr<test::test_iolayer> iol;
};

} // namespace

class mkdwarfs_main_test : public tool_main_test {
 public:
  int run(std::vector<std::string> args) {
    args.insert(args.begin(), "mkdwarfs");
    return mkdwarfs_main(args, iol->get());
  }
};

class dwarfsck_main_test : public tool_main_test {
 public:
  int run(std::vector<std::string> args) {
    args.insert(args.begin(), "dwarfsck");
    return dwarfsck_main(args, iol->get());
  }
};

class dwarfsextract_main_test : public tool_main_test {
 public:
  int run(std::vector<std::string> args) {
    args.insert(args.begin(), "dwarfsextract");
    return dwarfsextract_main(args, iol->get());
  }
};

TEST_F(mkdwarfs_main_test, no_cmdline_args) {
  auto exit_code = run({});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: mkdwarfs"));
  EXPECT_THAT(out(), ::testing::HasSubstr("--help"));
}

TEST_F(dwarfsck_main_test, no_cmdline_args) {
  auto exit_code = run({});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: dwarfsck"));
  EXPECT_THAT(out(), ::testing::HasSubstr("--help"));
}

TEST_F(dwarfsextract_main_test, no_cmdline_args) {
  auto exit_code = run({});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: dwarfsextract"));
  EXPECT_THAT(out(), ::testing::HasSubstr("--help"));
}

TEST_F(mkdwarfs_main_test, invalid_cmdline_args) {
  auto exit_code = run({"--some-invalid-option"});
  EXPECT_EQ(exit_code, 1);
  EXPECT_FALSE(err().empty());
  EXPECT_TRUE(out().empty());
  EXPECT_THAT(err(), ::testing::HasSubstr(
                         "unrecognised option '--some-invalid-option'"));
}

TEST_F(dwarfsck_main_test, invalid_cmdline_args) {
  auto exit_code = run({"--some-invalid-option"});
  EXPECT_EQ(exit_code, 1);
  EXPECT_FALSE(err().empty());
  EXPECT_TRUE(out().empty());
  EXPECT_THAT(err(), ::testing::HasSubstr(
                         "unrecognised option '--some-invalid-option'"));
}

TEST_F(dwarfsextract_main_test, invalid_cmdline_args) {
  auto exit_code = run({"--some-invalid-option"});
  EXPECT_EQ(exit_code, 1);
  EXPECT_FALSE(err().empty());
  EXPECT_TRUE(out().empty());
  EXPECT_THAT(err(), ::testing::HasSubstr(
                         "unrecognised option '--some-invalid-option'"));
}

TEST_F(mkdwarfs_main_test, cmdline_help_arg) {
  auto exit_code = run({"--help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: mkdwarfs"));
  EXPECT_THAT(out(), ::testing::HasSubstr("--help"));
  EXPECT_THAT(out(), ::testing::HasSubstr("--long-help"));
  // check that the detailed help is not shown
  EXPECT_THAT(out(), ::testing::Not(::testing::HasSubstr("Advanced options:")));
  EXPECT_THAT(out(),
              ::testing::Not(::testing::HasSubstr("Compression algorithms:")));
}

TEST_F(mkdwarfs_main_test, cmdline_long_help_arg) {
  auto exit_code = run({"--long-help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: mkdwarfs"));
  EXPECT_THAT(out(), ::testing::HasSubstr("Advanced options:"));
  EXPECT_THAT(out(), ::testing::HasSubstr("Compression level defaults:"));
  EXPECT_THAT(out(), ::testing::HasSubstr("Compression algorithms:"));
  EXPECT_THAT(out(), ::testing::HasSubstr("Categories:"));
}

TEST_F(dwarfsck_main_test, cmdline_help_arg) {
  auto exit_code = run({"--help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: dwarfsck"));
}

TEST_F(dwarfsextract_main_test, cmdline_help_arg) {
  auto exit_code = run({"--help"});
  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(err().empty());
  EXPECT_FALSE(out().empty());
  EXPECT_THAT(out(), ::testing::HasSubstr("Usage: dwarfsextract"));
}

#ifdef DWARFS_PERFMON_ENABLED
TEST_F(dwarfsextract_main_test, perfmon) {
  // TODO: passing in test_data_image this way only only works because
  //       dwarfsextract_main does not currently use the os_access abstraction
  auto exit_code = run({"-i", test_data_image.string(), "-f", "mtree",
                        "--perfmon", "filesystem_v2,inode_reader_v2"});
  EXPECT_EQ(exit_code, 0);
  auto outs = out();
  auto errs = err();
  EXPECT_GT(outs.size(), 100);
  EXPECT_FALSE(errs.empty());
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.readv_future]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.getattr]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.open]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.readlink]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[filesystem_v2.statvfs]"));
  EXPECT_THAT(errs, ::testing::HasSubstr("[inode_reader_v2.readv_future]"));
#ifndef _WIN32
  // googletest on Windows does not support fancy regexes
  EXPECT_THAT(errs, ::testing::ContainsRegex(
                        R"(\[filesystem_v2\.getattr\])"
                        R"(\s+samples:\s+[0-9]+)"
                        R"(\s+overall:\s+[0-9]+(\.[0-9]+)?[num]?s)"
                        R"(\s+avg latency:\s+[0-9]+(\.[0-9]+)?[num]?s)"
                        R"(\s+p50 latency:\s+[0-9]+(\.[0-9]+)?[num]?s)"
                        R"(\s+p90 latency:\s+[0-9]+(\.[0-9]+)?[num]?s)"
                        R"(\s+p99 latency:\s+[0-9]+(\.[0-9]+)?[num]?s)"));
#endif
}
#endif

TEST_F(mkdwarfs_main_test, input_list_file_test) {
  auto fa = std::make_shared<test::test_file_access>();
  iol->set_file_access(fa);

  fa->set_file("input_list.txt", "somelink\nfoo.pl\nsomedir/ipsum.py\n");

  auto exit_code = run({"--input-list", "input_list.txt", "-o", "test.dwarfs"});
  EXPECT_EQ(exit_code, 0);

  auto fsimage = fa->get_file("test.dwarfs");
  EXPECT_TRUE(fsimage);

  auto mm = std::make_shared<test::mmap_mock>(std::move(fsimage.value()));
  test::test_logger lgr;
  filesystem_v2 fs(lgr, mm);

  auto link = fs.find("/somelink");
  auto foo = fs.find("/foo.pl");
  auto ipsum = fs.find("/somedir/ipsum.py");

  EXPECT_TRUE(link);
  EXPECT_TRUE(foo);
  EXPECT_TRUE(ipsum);

  EXPECT_FALSE(fs.find("/test.pl"));

  EXPECT_TRUE(link->is_symlink());
  EXPECT_TRUE(foo->is_regular_file());
  EXPECT_TRUE(ipsum->is_regular_file());
}

TEST_F(mkdwarfs_main_test, input_list_stdin_test) {
  auto fa = std::make_shared<test::test_file_access>();
  iol->set_file_access(fa);
  iol->set_in("somelink\nfoo.pl\nsomedir/ipsum.py\n");

  auto exit_code = run({"--input-list", "-", "-o", "test.dwarfs"});
  EXPECT_EQ(exit_code, 0);

  auto fsimage = fa->get_file("test.dwarfs");
  EXPECT_TRUE(fsimage);

  auto mm = std::make_shared<test::mmap_mock>(std::move(fsimage.value()));
  test::test_logger lgr;
  filesystem_v2 fs(lgr, mm);

  auto link = fs.find("/somelink");
  auto foo = fs.find("/foo.pl");
  auto ipsum = fs.find("/somedir/ipsum.py");

  EXPECT_TRUE(link);
  EXPECT_TRUE(foo);
  EXPECT_TRUE(ipsum);

  EXPECT_FALSE(fs.find("/test.pl"));

  EXPECT_TRUE(link->is_symlink());
  EXPECT_TRUE(foo->is_regular_file());
  EXPECT_TRUE(ipsum->is_regular_file());
}

class categorizer_test : public testing::TestWithParam<std::string> {};

TEST_P(categorizer_test, end_to_end) {
  auto level = GetParam();

  auto input = std::make_shared<test::os_access_mock>();

  input->add("", {1, 040755, 1, 0, 0, 10, 42, 0, 0, 0});
  input->add_local_files(audio_data_dir);
  input->add_file("random", 4096, true);

  auto fa = std::make_shared<test::test_file_access>();
  test::test_iolayer iolayer(input, fa);

  setup_locale();

  auto args = test::parse_args(fmt::format(
      "mkdwarfs -i / -o test.dwarfs --chmod=norm --categorize --log-level={}",
      level));
  auto exit_code = mkdwarfs_main(args, iolayer.get());

  EXPECT_EQ(exit_code, 0);

  auto fsimage = fa->get_file("test.dwarfs");

  EXPECT_TRUE(fsimage);

  auto mm = std::make_shared<test::mmap_mock>(std::move(fsimage.value()));

  test::test_logger lgr;
  filesystem_v2 fs(lgr, mm);

  auto iv16 = fs.find("/test8.aiff");
  auto iv32 = fs.find("/test8.caf");

  EXPECT_TRUE(iv16);
  EXPECT_TRUE(iv32);
}

INSTANTIATE_TEST_SUITE_P(dwarfs, categorizer_test,
                         ::testing::Values("error", "warn", "info", "verbose",
                                           "debug", "trace"));
