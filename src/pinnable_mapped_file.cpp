#include <chainbase/pinnable_mapped_file.hpp>
#include <chainbase/environment.hpp>

#include <fstream>

namespace chainbase {

const char* chainbase_error_category::name() const noexcept {
   return "chainbase";
}

std::string chainbase_error_category::message(int ev) const {
   switch(ev) {
      case db_error_code::ok: 
         return "Ok";
      case db_error_code::dirty:
         return "Database dirty flag set";
      case db_error_code::incompatible:
         return "Database incompatible; All environment parameters must match";
      case db_error_code::incorrect_db_version:
         return "Database format not compatible with this version of chainbase";
      case db_error_code::not_found:
         return "Database file not found";
      case db_error_code::bad_size:
         return "Bad size";
      case db_error_code::bad_header:
         return "Failed to read DB header";
      case db_error_code::no_access:
         return "Could not gain write access to the shared memory file";
      default:
         return "Unrecognized error code";
   }
}

const std::error_category& chainbase_error_category() {
   static class chainbase_error_category the_category;
   return the_category;
}

pinnable_mapped_file::pinnable_mapped_file(const fs::path& dir, bool writable, uint64_t shared_file_size, bool allow_dirty) :
   _data_file_path(fs::absolute(dir/"shared_memory.bin")),
   _database_name(dir.filename().string()),
   _writable(writable)
{
   if(shared_file_size % _db_size_multiple_requirement) {
      std::string what_str("database must be multiple of " + std::to_string(_db_size_multiple_requirement) + " bytes");
      BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::bad_size), what_str));
   }

   if(!_writable && !fs::exists(_data_file_path)){
      std::string what_str("database file not found at " + _data_file_path.string());
      BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::not_found), what_str));
   }

   fs::create_directories(dir);

   if(fs::exists(_data_file_path)) {
      char header[header_size];
      std::ifstream hs(_data_file_path.generic_string(), std::ifstream::binary);
      hs.read(header, header_size);
      if(hs.fail())
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::bad_header)));

      db_header* dbheader = reinterpret_cast<db_header*>(header);
      if(dbheader->id != header_id || dbheader->size != header_size) {
         std::string what_str("\"" + _database_name + "\" database format not compatible with this version of chainbase");
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::incorrect_db_version), what_str));
      }
      if(!allow_dirty && dbheader->dirty) {
         std::string what_str("\"" + _database_name + "\" database dirty flag set");
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::dirty)));
      }
      if(dbheader->dbenviron != environment()) {
         std::string what_str("\"" + _database_name + "\" database was created with a chainbase from a different environment:\n" + dbheader->dbenviron.str());
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::incompatible), what_str));
      }
   }

   segment_manager* file_mapped_segment_manager = nullptr;
   if(!fs::exists(_data_file_path)) {
      std::ofstream ofs(_data_file_path.generic_string(), std::ofstream::trunc);
      //win32 impl of fs::resize_file() doesn't like the file being open
      ofs.close();
      fs::resize_file(_data_file_path, shared_file_size);
      _file_mapping = bip::file_mapping(_data_file_path.generic_string().c_str(), bip::read_write);
      _file_mapped_region = bip::mapped_region(_file_mapping, bip::read_write);
      file_mapped_segment_manager = new ((char*)_file_mapped_region.get_address()+header_size) segment_manager(shared_file_size-header_size);
      new (_file_mapped_region.get_address()) db_header;
   }
   else if(_writable) {
         auto existing_file_size = fs::file_size(_data_file_path);
         size_t grow = 0;
         if(shared_file_size > existing_file_size) {
            grow = shared_file_size - existing_file_size;
            fs::resize_file(_data_file_path, shared_file_size);
         }

         _file_mapping = bip::file_mapping(_data_file_path.generic_string().c_str(), bip::read_write);
         _file_mapped_region = bip::mapped_region(_file_mapping, bip::read_write);
         file_mapped_segment_manager = reinterpret_cast<segment_manager*>((char*)_file_mapped_region.get_address()+header_size);
         if(grow)
            file_mapped_segment_manager->grow(grow);
   }
   else {
         _file_mapping = bip::file_mapping(_data_file_path.generic_string().c_str(), bip::read_only);
         _file_mapped_region = bip::mapped_region(_file_mapping, bip::read_only);
         file_mapped_segment_manager = reinterpret_cast<segment_manager*>((char*)_file_mapped_region.get_address()+header_size);
   }

   if(_writable) {
      _mapped_file_lock = bip::file_lock(_data_file_path.generic_string().c_str());
      if(!_mapped_file_lock.try_lock())
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::no_access)));

      dirty();
   }

   _segment_manager = file_mapped_segment_manager;
}

pinnable_mapped_file::pinnable_mapped_file(pinnable_mapped_file&& o) :
   _mapped_file_lock(std::move(o._mapped_file_lock)),
   _data_file_path(std::move(o._data_file_path)),
   _database_name(std::move(o._database_name)),
   _file_mapped_region(std::move(o._file_mapped_region))
{
   _segment_manager = o._segment_manager;
   _writable = o._writable;
   o._writable = false; //prevent dtor from doing anything interesting
}

pinnable_mapped_file& pinnable_mapped_file::operator=(pinnable_mapped_file&& o) {
   _mapped_file_lock = std::move(o._mapped_file_lock);
   _data_file_path = std::move(o._data_file_path);
   _database_name = std::move(o._database_name);
   _file_mapped_region = std::move(o._file_mapped_region);
   _segment_manager = o._segment_manager;
   _writable = o._writable;
   o._writable = false; //prevent dtor from doing anything interesting
   return *this;
}

pinnable_mapped_file::~pinnable_mapped_file() {
   if(_writable) {
      flush();
   }
}

void pinnable_mapped_file::flush()
{
   *((char*)_file_mapped_region.get_address()+header_dirty_bit_offset) = false;
   _file_mapped_region.flush(0, 0, false);
}

void pinnable_mapped_file::dirty()
{
   *((char*)_file_mapped_region.get_address()+header_dirty_bit_offset) = true;
   _file_mapped_region.flush(0, 0, false);
}

bool pinnable_mapped_file::dirty() const
{
   return *((char*)_file_mapped_region.get_address()+header_dirty_bit_offset);
}

static std::string_view print_os(environment::os_t os) {
   switch(os) {
      case environment::OS_LINUX: return "Linux";
      case environment::OS_MACOS: return "macOS";
      case environment::OS_WINDOWS: return "Windows";
   }
   return {};
}

static std::string_view print_arch(environment::arch_t arch) {
   switch(arch) {
      case environment::ARCH_X86_64: return "x86_64";
      case environment::ARCH_ARM: return "ARM";
   }
   return {};
}

std::string chainbase::environment::str() const
{
   std::stringstream ss;

   ss << *this;
   return ss.str();
}

std::ostream& operator<<(std::ostream& os, const chainbase::environment& dt) {
   os << std::right << std::setw(17) << "Compiler: " << dt.compiler << std::endl;
   os << std::right << std::setw(17) << "Debug: " << (dt.debug ? "Yes" : "No") << std::endl;
   os << std::right << std::setw(17) << "OS: " << print_os(dt.os) << std::endl;
   os << std::right << std::setw(17) << "Arch: " << print_arch(dt.arch) << std::endl;
   os << std::right << std::setw(17) << "Boost: " << dt.boost_version/100000 << "."
                                                  << dt.boost_version/100%1000 << "."
                                                  << dt.boost_version%100 << std::endl;
   return os;
}

}
