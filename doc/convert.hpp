#pragma once
#include "doc/writer.hpp"
#include "pic/writer.hpp"
#include <string_view>
#include <stack>
#include <set>
#include <cassert>
#include <brick-min>

namespace umd::doc
{
    static inline int stoi( std::u32string_view n )
    {
        std::wstring_convert< std::codecvt_utf8< char32_t >, char32_t > conv;
        return std::stoi( conv.to_bytes( n.begin(), n.end() ) );
    }

    enum class span
    {
        tt, em, bf, lref, gref
    };

    inline auto &operator<<( brq::string_builder &b, span s )
    {
        switch ( s )
        {
            case span::tt: return b << "monospace ‹tt›";
            case span::em: return b << "emphasis «em»";
            case span::bf: return b << "boldface ❮bf❯";
            case span::lref: return b << "local reference ▷";
            case span::gref: return b << "global reference ▶";
        }
    }

    struct convert : pic::writer
    {
        using sv = std::u32string_view;

        doc::writer &w;
        std::stack< sv > todo;

        std::stack< span > _spans;
        std::set< const char32_t * > _footnotes;
        sv _last_footnote;

        convert( sv t, doc::writer &w )
            : w( w ), _last_footnote( t.substr( 0, 0 ) )
        {
            todo.emplace( t );
        }

        struct list
        {
            enum type { bullets, numbered, lettered } t;
            int indent;
            list( type t, int i ) : t( t ), indent( i ) {}
        };

        std::stack< list > _list;

        int rec_list_depth = 0;

        bool in_picture = false;
        bool in_code = false;
        bool in_quote = false;
        int in_math = 0;

        std::u32string default_typing;

        void emit_mpost( std::string_view s ) { w.mpost_write( s ); }
        void emit_tex( std::u32string_view s ) { emit_text( s ); }

        char32_t nonwhite();
        void skip_white( std::u32string_view &l );

        int white_count( std::u32string_view v );
        int white_count() { return white_count( todo.top() ); }

        void skip_white() { skip_white( todo.top() ); }
        bool skip( char32_t c );

        void skip_to_addr( const char32_t *a )
        {
            if ( a > todo.top().data() )
                shift( a - todo.top().data() );
        }

        std::pair< int, int > skip_item_lead( list::type );
        int skip_bullet_lead();
        std::pair< int, int > skip_enum_lead();

        template< typename F >
        std::u32string_view fetch( std::u32string_view &v, F pred )
        {
            auto l = v;
            while ( !v.empty() && !pred( v ) )
                v.remove_prefix( 1 );
            if ( !v.empty() )
                v.remove_prefix( pred( v ) );
            return l.substr( 0, l.size() - v.size() - 1 );
        }

        static int count( sv s, char32_t ch )
        {
            int count = 0;
            while ( !s.empty() && s[ 0 ] == ch )
                s.remove_prefix( 1 ), ++ count;
            return count;
        }

        static int newline( sv s )  { return s[ 0 ] == U'\n'; }
        static int space( sv s )    { return s[ 0 ] == U' ' || s[ 0 ] == U'\n'; }
        static int parbreak( sv s ) { int c = count( s, U'\n' ); return c >= 2 ? c : 0; }

        std::u32string_view fetch_line() { return fetch( todo.top(), newline ); }
        std::u32string_view fetch_word() { return fetch( todo.top(), space ); }
        std::u32string_view fetch_par()  { return fetch( todo.top(), parbreak ); }

        bool starts_with( char32_t c ) const { return !todo.top().empty() && todo.top()[ 0 ] == c; }
        bool starts_with( sv s ) const { return todo.top().substr( 0, s.size() ) == s; }
        sv peek( int c ) const { return todo.top().substr( 0, c ); }
        char32_t peek() const { assert( !todo.top().empty() ); return todo.top()[ 0 ]; }
        sv shift( int c = 1 ) { sv res = peek( c ); todo.top().remove_prefix( c ); return res; }
        bool have_chars( int c = 1 ) { return todo.top().size() >= c; }

        void checkpoint() { todo.emplace( todo.top() ); }
        void restore() { todo.pop(); }

        void emit_text( std::u32string_view v );

        template< typename flush_t > void process_footnote( flush_t, sv par );
        template< typename flush_t > void emit_footnote( flush_t, char32_t head );

        void heading();
        void start_list( list::type l, int indent, int first );
        bool end_list( int count = 1 );
        void ensure_list( int l, list::type t );
        bool try_enum();

        template< typename flush_t > void span_start( flush_t, span );
        template< typename flush_t > void span_stop( flush_t, span );

        void emit_quote();
        void end_quote();

        auto code_types();
        void emit_code();
        void end_code();

        void try_picture();
        void try_table();
        bool try_dispmath();
        void try_nested();
        bool try_directive();
        void try_quote();

        void recurse( const std::u32string &data );

        void header();
        void body();
        void run();
    };

}
