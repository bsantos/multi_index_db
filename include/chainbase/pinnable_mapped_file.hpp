#pragma once

#include <boost/interprocess/sync/mutex_family.hpp>
#include <boost/interprocess/indexes/iset_index.hpp>
#include <boost/interprocess/mem_algo/rbtree_best_fit.hpp>
#include <boost/interprocess/segment_manager.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/file_lock.hpp>

#include <filesystem>
#include <system_error>

namespace chainbase {

namespace bip = boost::interprocess;
namespace fs = std::filesystem;

class pinnable_mapped_file {
   public:
      constexpr static unsigned db_size_multiple_requirement = 1024*1024; //1MB

      using segment_manager = bip::segment_manager<char, bip::rbtree_best_fit<bip::null_mutex_family>, bip::iset_index>;

      pinnable_mapped_file(const fs::path& fpath, bool writable, uint64_t db_file_size, bool allow_dirty);
      pinnable_mapped_file(pinnable_mapped_file&& o);
      pinnable_mapped_file& operator=(pinnable_mapped_file&&);
      pinnable_mapped_file(const pinnable_mapped_file&) = delete;
      pinnable_mapped_file& operator=(const pinnable_mapped_file&) = delete;
      ~pinnable_mapped_file();

      void flush();
      void dirty();
      bool dirty() const;

      segment_manager* get_segment_manager() const { return _segment_manager;}

   private:
      bip::file_lock                                _mapped_file_lock;
      std::string                                   _database_name;
      bool                                          _writable;

      bip::file_mapping                             _file_mapping;
      bip::mapped_region                            _file_mapped_region;

      segment_manager*                              _segment_manager = nullptr;
};

}
