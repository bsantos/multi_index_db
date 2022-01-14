#pragma once

#include <boost/multi_index_container.hpp>
#include <chainbase/chainbase_node_allocator.hpp>
#include <chainbase/undo_index.hpp>

#include <boost/config.hpp>
#include <boost/core/demangle.hpp>
#include <boost/throw_exception.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

#include <vector>
#include <typeinfo>
#include <stdexcept>
#include <filesystem>

namespace chainbase {

   namespace bip = boost::interprocess;
   namespace fs = std::filesystem;

   template<typename T>
   using allocator = bip::allocator<T, pinnable_mapped_file::segment_manager>;

   template<typename T>
   using node_allocator = chainbase_node_allocator<T, pinnable_mapped_file::segment_manager>;

   /**
    *  Object ID type that includes the type of the object it references
    */
   template<typename T>
   class oid {
      public:
         oid( int64_t i = 0 ):_id(i){}

         oid& operator++() { ++_id; return *this; }

         friend bool operator < ( const oid& a, const oid& b ) { return a._id < b._id; }
         friend bool operator > ( const oid& a, const oid& b ) { return a._id > b._id; }
         friend bool operator <= ( const oid& a, const oid& b ) { return a._id <= b._id; }
         friend bool operator >= ( const oid& a, const oid& b ) { return a._id >= b._id; }
         friend bool operator == ( const oid& a, const oid& b ) { return a._id == b._id; }
         friend bool operator != ( const oid& a, const oid& b ) { return a._id != b._id; }

         int64_t _id = 0;
   };

   /**
    *  Object base class that must be inherited when implementing database objects
    */
   template<uint16_t TypeNumber, typename Derived>
   struct object
   {
      typedef oid<Derived> id_type;
      static const uint16_t type_id = TypeNumber;
   };

   /**
    * This class is ment to be specified to enable lookup of index type by object type using
    * the SET_INDEX_TYPE macro.
    **/
   template<typename T>
   struct get_index_type {};

   /**
    *  This macro must be used at global scope and OBJECT_TYPE and INDEX_TYPE must be fully qualified
    */
   #define CHAINBASE_SET_INDEX_TYPE( OBJECT_TYPE, INDEX_TYPE )  \
   namespace chainbase { template<> struct get_index_type<OBJECT_TYPE> { typedef INDEX_TYPE type; }; }

   #define CHAINBASE_DEFAULT_CONSTRUCTOR( OBJECT_TYPE ) \
   template<typename Constructor, typename Allocator> \
   OBJECT_TYPE( Constructor&& c, Allocator&&  ) { c(*this); }

   template<typename MultiIndexType>
   using generic_index = multi_index_to_undo_index<MultiIndexType>;

   class abstract_session {
      public:
         virtual ~abstract_session(){};
         virtual void push()             = 0;
         virtual void squash()           = 0;
         virtual void undo()             = 0;
   };

   template<typename SessionType>
   class session_impl : public abstract_session
   {
      public:
         session_impl( SessionType&& s ):_session( std::move( s ) ){}

         virtual void push() override  { _session.push();  }
         virtual void squash() override{ _session.squash(); }
         virtual void undo() override  { _session.undo();  }
      private:
         SessionType _session;
   };

   class abstract_index
   {
      public:
         abstract_index( void* i ):_idx_ptr(i){}
         virtual ~abstract_index(){}
         virtual void     set_revision( uint64_t revision ) = 0;
         virtual std::unique_ptr<abstract_session> start_undo_session() = 0;

         virtual int64_t revision()const = 0;
         virtual void    undo()const = 0;
         virtual void    squash()const = 0;
         virtual void    commit( int64_t revision )const = 0;
         virtual void    undo_all()const = 0;
         virtual std::pair<int64_t, int64_t> undo_stack_revision_range()const = 0;

         void* get()const { return _idx_ptr; }

      private:
         void* _idx_ptr;
   };

   template<typename BaseIndex>
   class index_impl : public abstract_index {
      public:
         index_impl( BaseIndex& base ):abstract_index( &base ),_base(base){}

         virtual std::unique_ptr<abstract_session> start_undo_session() override {
            return std::unique_ptr<abstract_session>(new session_impl<typename BaseIndex::session>( _base.start_undo_session() ) );
         }

         virtual void     set_revision( uint64_t revision ) override { _base.set_revision( revision ); }
         virtual int64_t  revision()const  override { return _base.revision(); }
         virtual void     undo()const  override { _base.undo(); }
         virtual void     squash()const  override { _base.squash(); }
         virtual void     commit( int64_t revision )const  override { _base.commit(revision); }
         virtual void     undo_all() const override {_base.undo_all(); }
         virtual std::pair<int64_t, int64_t> undo_stack_revision_range()const override { return _base.undo_stack_revision_range(); }

      private:
         BaseIndex& _base;
   };

   template<typename IndexType>
   class index : public index_impl<IndexType> {
      public:
         index( IndexType& i ):index_impl<IndexType>( i ){}
   };

   /**
    *  This class
    */
   class database
   {
      public:
         enum open_flags {
            read_only     = 0,
            read_write    = 1
         };

         database(const fs::path& dir, open_flags write = read_only, uint64_t shared_file_size = 0, bool allow_dirty = false);
         ~database();
         database(database&&) = default;
         database& operator=(database&&) = default;

         void dirty()
         {
            _db_file.dirty();
            _read_only = false;
         }

         void flush()
         {
            _db_file.flush();
            _read_only = true;
         }

         bool is_read_only() const { return _read_only; }


         struct session {
            public:
               session( session&& s ):_index_sessions( std::move(s._index_sessions) ){}
               session( std::vector<std::unique_ptr<abstract_session>>&& s ):_index_sessions( std::move(s) )
               {
               }

               ~session() {
                  undo();
               }

               void push()
               {
                  for( auto& i : _index_sessions ) i->push();
                  _index_sessions.clear();
               }

               void squash()
               {
                  for( auto& i : _index_sessions ) i->squash();
                  _index_sessions.clear();
               }

               void undo()
               {
                  for( auto& i : _index_sessions ) i->undo();
                  _index_sessions.clear();
               }

            private:
               friend class database;
               session(){}

               std::vector< std::unique_ptr<abstract_session> > _index_sessions;
         };

         session start_undo_session();

         int64_t revision()const {
             if( _index_list.size() == 0 ) return -1;
             return _index_list[0]->revision();
         }

         void undo();
         void squash();
         void commit( int64_t revision );
         void undo_all();


         void set_revision( uint64_t revision )
         {
             for( auto i : _index_list ) i->set_revision( revision );
         }


         template<typename MultiIndexType>
         void add_index() {
            const uint16_t type_id = generic_index<MultiIndexType>::value_type::type_id;
            typedef generic_index<MultiIndexType>          index_type;
            typedef typename index_type::allocator_type    index_alloc;

            std::string type_name = boost::core::demangle( typeid( typename index_type::value_type ).name() );

            if( !( _index_map.size() <= type_id || _index_map[ type_id ] == nullptr ) ) {
               BOOST_THROW_EXCEPTION( std::logic_error( type_name + "::type_id is already in use" ) );
            }

            index_type* idx_ptr = nullptr;
            if( _read_only )
               idx_ptr = _db_file.get_segment_manager()->find_no_lock< index_type >( type_name.c_str() ).first;
            else
               idx_ptr = _db_file.get_segment_manager()->find< index_type >( type_name.c_str() ).first;
            bool first_time_adding = false;
            if( !idx_ptr ) {
               if( _read_only ) {
                  BOOST_THROW_EXCEPTION( std::runtime_error( "unable to find index for " + type_name + " in read only database" ) );
               }
               first_time_adding = true;
               idx_ptr = _db_file.get_segment_manager()->construct< index_type >( type_name.c_str() )( index_alloc( _db_file.get_segment_manager() ) );
             }

            idx_ptr->validate();

            // Ensure the undo stack of added index is consistent with the other indices in the database
            if( _index_list.size() > 0 ) {
               auto expected_revision_range = _index_list.front()->undo_stack_revision_range();
               auto added_index_revision_range = idx_ptr->undo_stack_revision_range();

               if( added_index_revision_range.first != expected_revision_range.first ||
                   added_index_revision_range.second != expected_revision_range.second ) {

                  if( !first_time_adding ) {
                     BOOST_THROW_EXCEPTION( std::logic_error(
                        "existing index for " + type_name + " has an undo stack (revision range [" +
                        std::to_string(added_index_revision_range.first) + ", " + std::to_string(added_index_revision_range.second) +
                        "]) that is inconsistent with other indices in the database (revision range [" +
                        std::to_string(expected_revision_range.first) + ", " + std::to_string(expected_revision_range.second) +
                        "]); corrupted database?"
                     ) );
                  }

                  if( _read_only ) {
                     BOOST_THROW_EXCEPTION( std::logic_error(
                        "new index for " + type_name +
                        " requires an undo stack that is consistent with other indices in the database; cannot fix in read-only mode"
                     ) );
                  }

                  idx_ptr->set_revision( static_cast<uint64_t>(expected_revision_range.first) );
                  while( idx_ptr->revision() < expected_revision_range.second ) {
                     idx_ptr->start_undo_session().push();
                  }
               }
            }

            if( type_id >= _index_map.size() )
               _index_map.resize( type_id + 1 );

            auto new_index = new index<index_type>( *idx_ptr );
            _index_map[ type_id ].reset( new_index );
            _index_list.push_back( new_index );
         }

         auto get_segment_manager() -> decltype( ((pinnable_mapped_file*)nullptr)->get_segment_manager()) {
            return _db_file.get_segment_manager();
         }

         auto get_segment_manager()const -> std::add_const_t< decltype( ((pinnable_mapped_file*)nullptr)->get_segment_manager() ) > {
            return _db_file.get_segment_manager();
         }

         size_t get_free_memory()const
         {
            return _db_file.get_segment_manager()->get_free_memory();
         }

         template<typename MultiIndexType>
         const generic_index<MultiIndexType>& get_index()const
         {
            typedef generic_index<MultiIndexType> index_type;
            typedef index_type*                   index_type_ptr;
            assert( _index_map.size() > index_type::value_type::type_id );
            assert( _index_map[index_type::value_type::type_id] );
            return *index_type_ptr( _index_map[index_type::value_type::type_id]->get() );
         }

         template<typename MultiIndexType, typename ByIndex>
         auto get_index()const -> decltype( ((generic_index<MultiIndexType>*)( nullptr ))->indices().template get<ByIndex>() )
         {
            typedef generic_index<MultiIndexType> index_type;
            typedef index_type*                   index_type_ptr;
            assert( _index_map.size() > index_type::value_type::type_id );
            assert( _index_map[index_type::value_type::type_id] );
            return index_type_ptr( _index_map[index_type::value_type::type_id]->get() )->indices().template get<ByIndex>();
         }

         template<typename MultiIndexType>
         generic_index<MultiIndexType>& get_mutable_index()
         {
            typedef generic_index<MultiIndexType> index_type;
            typedef index_type*                   index_type_ptr;
            assert( _index_map.size() > index_type::value_type::type_id );
            assert( _index_map[index_type::value_type::type_id] );
            return *index_type_ptr( _index_map[index_type::value_type::type_id]->get() );
         }

         template< typename ObjectType, typename IndexedByType, typename CompatibleKey >
         const ObjectType* find( CompatibleKey&& key )const
         {
             typedef typename get_index_type< ObjectType >::type index_type;
             const auto& idx = get_index< index_type >().indices().template get< IndexedByType >();
             auto itr = idx.find( std::forward< CompatibleKey >( key ) );
             if( itr == idx.end() ) return nullptr;
             return &*itr;
         }

         template< typename ObjectType >
         const ObjectType* find( oid< ObjectType > key = oid< ObjectType >() ) const
         {
             typedef typename get_index_type< ObjectType >::type index_type;
             return get_index< index_type >().find( key );
         }

         template<typename ObjectType, typename Modifier>
         void modify( const ObjectType& obj, Modifier&& m )
         {
             typedef typename get_index_type<ObjectType>::type index_type;
             get_mutable_index<index_type>().modify( obj, m );
         }

         template<typename ObjectType>
         void remove( const ObjectType& obj )
         {
             typedef typename get_index_type<ObjectType>::type index_type;
             return get_mutable_index<index_type>().remove( obj );
         }

         template<typename ObjectType, typename Constructor>
         const ObjectType& create( Constructor&& con )
         {
             typedef typename get_index_type<ObjectType>::type index_type;
             return get_mutable_index<index_type>().emplace( std::forward<Constructor>(con) );
         }

      private:
         pinnable_mapped_file                                        _db_file;
         bool                                                        _read_only = false;

         /**
          * This is a sparse list of known indices kept to accelerate creation of undo sessions
          */
         std::vector<abstract_index*>                                _index_list;

         /**
          * This is a full map (size 2^16) of all possible index designed for constant time lookup
          */
         std::vector<std::unique_ptr<abstract_index>>                _index_map;
   };

   template<typename Object, typename... Args>
   using shared_multi_index_container = boost::multi_index_container<Object,Args..., chainbase::node_allocator<Object> >;
}  // namepsace chainbase
