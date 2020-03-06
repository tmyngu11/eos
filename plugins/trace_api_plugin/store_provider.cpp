#include <eosio/trace_api_plugin/store_provider.hpp>

#include <fc/variant_object.hpp>
#include <fc/crypto/base64.hpp>

namespace {
      static constexpr uint32_t _current_version = 1;
      static constexpr const char* _trace_prefix = "trace_";
      static constexpr const char* _trace_index_prefix = "trace_index_";
      static constexpr const char* _trace_ext = ".log";
      static constexpr uint _max_filename_size = std::char_traits<char>::length(_trace_index_prefix) + 10 + 1 + 10 + std::char_traits<char>::length(_trace_ext) + 1; // "trace_index_" + 10-digits + '-' + 10-digits + ".log" + null-char
}

namespace eosio::trace_api_plugin {
   namespace bfs = boost::filesystem;
   store_provider::store_provider(const bfs::path& slice_dir, uint32_t width)
   : _slice_provider(slice_dir, width) {
   }

   void store_provider::append(const block_trace_v0& bt) {
      fc::cfile trace;
      fc::cfile index;
      find_slice_pair(bt.number, true, trace, index);
      const uint64_t offset = append_store(bt, trace);
      auto be = metadata_log_entry { block_entry_v0 { .id = bt.id, .number = bt.number, .offset = offset }};
      append_store(be, index);
   }

   void store_provider::append_lib(uint32_t lib) {
      fc::cfile index;
      const uint32_t slice_number = _slice_provider.slice_number(lib);
      _slice_provider.find_index_slice(slice_number, true, index);
      auto le = metadata_log_entry { lib_entry_v0 { .lib = lib }};
      append_store(le, index);
   }

   void store_provider::find_slice_pair(uint32_t block_height, bool append, fc::cfile& trace, fc::cfile& index) {
      const uint32_t slice_number = _slice_provider.slice_number(block_height);
      const bool trace_created = _slice_provider.find_trace_slice(slice_number, append, trace);
      const bool index_created = _slice_provider.find_index_slice(slice_number, append, index);
      if (trace_created != index_created) {
         const std::string trace_status = trace_created ? "new" : "existing";
         const std::string index_status = index_created ? "new" : "existing";
         elog("Trace file is ${ts}, but it's metadata file is ${is}. This means the files are not consistent.", ("ts", trace_status)("is", index_status));
      }
   }

   slice_provider::slice_provider(const bfs::path& slice_dir, uint32_t width)
   : _slice_dir(slice_dir), _width(width) {
      if (!exists(_slice_dir))
         throw path_does_not_exist("The provided path does not exist: " + _slice_dir.generic_string());
   }

   bool slice_provider::find_index_slice(uint32_t slice_number, bool append, fc::cfile& slice_file) const {
      const bool new_slice = find_slice(_trace_index_prefix, slice_number, slice_file);
      if( new_slice ) {
         index_header h { .version = _current_version };
         store_provider::append_store(h, slice_file);
      } else {
         const auto header = store_provider::extract_store<index_header>(slice_file);
         if (header.version != _current_version) {
            throw old_slice_version("Old slice file with version: " + boost::lexical_cast<std::string>(header.version) +
               " is in directory, only supporting version: " + boost::lexical_cast<std::string>(_current_version));
         }
         if( append ) {
            slice_file.seek_end(0);
         }
      }
      return new_slice;
   }

   bool slice_provider::find_trace_slice(uint32_t slice_number, bool append, fc::cfile& slice_file) const {
      const bool new_slice = find_slice(_trace_prefix, slice_number, slice_file);

      if( append ) {
         slice_file.seek_end(0);
      }
      return new_slice;
   }

   bool slice_provider::find_slice(const char* slice_prefix, uint32_t slice_number, fc::cfile& slice_file) const {
      char filename[_max_filename_size] = {};
      const uint32_t slice_start = slice_number * _width;
      const int size_written = snprintf(filename, _max_filename_size, "%s%010d-%010d%s", slice_prefix, slice_start, (slice_start + _width), _trace_ext);
      // assert that _max_filename_size is correct
      if ( size_written >= _max_filename_size ) {
         const std::string max_size_str = std::to_string(_max_filename_size - 1); // dropping null character from size
         const std::string size_written_str = std::to_string(size_written);
         throw std::runtime_error("Could not write the complete filename.  Anticipated the max filename characters to be: " +
            max_size_str + " or less, but wrote: " + size_written_str + " characters.  This is likely because the file "
            "format was changed and the code was not updated accordingly. Filename created: " + filename);
      }
      const path slice_path = _slice_dir / filename;
      slice_file.set_file_path(slice_path);
      const bool file_exists = exists(slice_path);
      slice_file.open(fc::cfile::create_or_update_rw_mode);
      return !file_exists;
   }
}