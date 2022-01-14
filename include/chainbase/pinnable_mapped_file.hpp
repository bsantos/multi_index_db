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

enum db_error_code {
   ok = 0,
   dirty,
   incompatible,
   incorrect_db_version,
   not_found,
   bad_size,
   bad_header,
   no_access,
};

const std::error_category& chainbase_error_category();

inline std::error_code make_error_code(db_error_code e) noexcept {
   return std::error_code(static_cast<int>(e), chainbase_error_category());
}

class chainbase_error_category : public std::error_category {
public:
   const char* name() const noexcept override;
   std::string message(int ev) const override;
};

class pinnable_mapped_file {
   public:
      using segment_manager = bip::segment_manager<char, bip::rbtree_best_fit<bip::null_mutex_family>, bip::iset_index>;

      pinnable_mapped_file(const fs::path& dir, bool writable, uint64_t shared_file_size, bool allow_dirty);
      pinnable_mapped_file(pinnable_mapped_file&& o);
      pinnable_mapped_file& operator=(pinnable_mapped_file&&);
      pinnable_mapped_file(const pinnable_mapped_file&) = delete;
      pinnable_mapped_file& operator=(const pinnable_mapped_file&) = delete;
      ~pinnable_mapped_file();

      segment_manager* get_segment_manager() const { return _segment_manager;}

   private:
      void                                          set_mapped_file_db_dirty(bool);

      bip::file_lock                                _mapped_file_lock;
      fs::path                                      _data_file_path;
      std::string                                   _database_name;
      bool                                          _writable;

      bip::file_mapping                             _file_mapping;
      bip::mapped_region                            _file_mapped_region;

#ifdef _WIN32
      bip::permissions                              _db_permissions;
#else
      bip::permissions                              _db_permissions{S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH};
#endif

      segment_manager*                              _segment_manager = nullptr;

      constexpr static unsigned                     _db_size_multiple_requirement = 1024*1024; //1MB
};

}
