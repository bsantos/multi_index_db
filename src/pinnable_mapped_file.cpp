#include <chainbase/error.hpp>
#include <chainbase/pinnable_mapped_file.hpp>
#include <chainbase/environment.hpp>

#include <fstream>

namespace chainbase {

pinnable_mapped_file::pinnable_mapped_file(const fs::path& fpath, bool writable, uint64_t db_file_size, bool allow_dirty)
   : _database_name(fpath.filename().string())
   , _writable(writable)
{
   if(db_file_size % db_size_multiple_requirement) {
      std::string what_str("database must be multiple of " + std::to_string(db_size_multiple_requirement) + " bytes");
      BOOST_THROW_EXCEPTION(std::system_error(make_error_code(errc::bad_size), what_str));
   }

   if(!_writable && !fs::exists(fpath)){
      std::string what_str("database file not found at " + fpath.string());
      BOOST_THROW_EXCEPTION(std::system_error(make_error_code(errc::not_found), what_str));
   }

   if (auto dir = fpath.parent_path(); !dir.empty())
      fs::create_directories(dir);

   if(fs::exists(fpath)) {
      char header[header_size];
      std::ifstream hs(fpath.generic_string(), std::ifstream::binary);
      hs.read(header, header_size);
      if(hs.fail())
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(errc::bad_header)));

      db_header* dbheader = reinterpret_cast<db_header*>(header);
      if(dbheader->id != header_id || dbheader->size != header_size) {
         std::string what_str("\"" + _database_name + "\" database format not compatible with this version of chainbase");
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(errc::incorrect_db_version), what_str));
      }
      if(!allow_dirty && dbheader->dirty) {
         std::string what_str("\"" + _database_name + "\" database dirty flag set");
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(errc::dirty)));
      }
      if(dbheader->dbenviron != environment()) {
         std::string what_str("\"" + _database_name + "\" database was created with a chainbase from a different environment:\n" + dbheader->dbenviron.str());
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(errc::incompatible), what_str));
      }
   }

   segment_manager* file_mapped_segment_manager = nullptr;
   if(!fs::exists(fpath)) {
      std::ofstream ofs(fpath.generic_string(), std::ofstream::trunc);
      //win32 impl of fs::resize_file() doesn't like the file being open
      ofs.close();
      fs::resize_file(fpath, db_file_size);
      _file_mapping = bip::file_mapping(fpath.generic_string().c_str(), bip::read_write);
      _file_mapped_region = bip::mapped_region(_file_mapping, bip::read_write);
      file_mapped_segment_manager = new ((char*)_file_mapped_region.get_address()+header_size) segment_manager(db_file_size-header_size);
      new (_file_mapped_region.get_address()) db_header;
   }
   else if(_writable) {
         auto existing_file_size = fs::file_size(fpath);
         size_t grow = 0;
         if(db_file_size > existing_file_size) {
            grow = db_file_size - existing_file_size;
            fs::resize_file(fpath, db_file_size);
         }

         _file_mapping = bip::file_mapping(fpath.generic_string().c_str(), bip::read_write);
         _file_mapped_region = bip::mapped_region(_file_mapping, bip::read_write);
         file_mapped_segment_manager = reinterpret_cast<segment_manager*>((char*)_file_mapped_region.get_address()+header_size);
         if(grow)
            file_mapped_segment_manager->grow(grow);
   }
   else {
         _file_mapping = bip::file_mapping(fpath.generic_string().c_str(), bip::read_only);
         _file_mapped_region = bip::mapped_region(_file_mapping, bip::read_only);
         file_mapped_segment_manager = reinterpret_cast<segment_manager*>((char*)_file_mapped_region.get_address()+header_size);
   }

   if(_writable) {
      _mapped_file_lock = bip::file_lock(fpath.generic_string().c_str());
      if(!_mapped_file_lock.try_lock())
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(errc::no_access)));

      dirty();
   }

   _segment_manager = file_mapped_segment_manager;
}

pinnable_mapped_file::pinnable_mapped_file(pinnable_mapped_file&& o) :
   _mapped_file_lock(std::move(o._mapped_file_lock)),
   _database_name(std::move(o._database_name)),
   _file_mapped_region(std::move(o._file_mapped_region))
{
   _segment_manager = o._segment_manager;
   _writable = o._writable;
   o._writable = false; //prevent dtor from doing anything interesting
}

pinnable_mapped_file& pinnable_mapped_file::operator=(pinnable_mapped_file&& o) {
   _mapped_file_lock = std::move(o._mapped_file_lock);
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

}
