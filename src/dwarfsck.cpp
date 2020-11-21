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

#include <iostream>
#include <vector>

#include "dwarfs/filesystem.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"

int main(int argc, char** argv) {
  if (argc == 2 || argc == 3) {
    try {
      dwarfs::stream_logger lgr(std::cerr, dwarfs::logger::INFO);
      dwarfs::filesystem fs(lgr, std::make_shared<dwarfs::mmap>(argv[1]),
                            dwarfs::block_cache_options());

      if (argc == 3) {
        auto de = fs.find(argv[2]);

        if (de) {
          struct ::stat stbuf;
          fs.getattr(de, &stbuf);
          std::vector<char> data(stbuf.st_size);
          fs.read(stbuf.st_ino, &data[0], data.size(), 0);
          std::cout.write(&data[0], data.size());
        }
      } else {
        // TODO: add more usage options...
        dwarfs::filesystem::identify(
            lgr, std::make_shared<dwarfs::mmap>(argv[1]), std::cout);
        // TODO:
        // fs.dump(std::cout);
      }
    } catch (const std::exception& e) {
      std::cerr << "Error: " << e.what() << std::endl;
      return 1;
    }
  } else {
    std::cerr << "Usage: " << argv[0] << " file" << std::endl;
  }

  return 0;
}
