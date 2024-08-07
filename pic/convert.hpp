// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4 -*-

#include "scene.hpp"
#include "reader.hpp"
#include "util.hpp"

#include <array>
#include <set>

namespace umd::pic::convert
{
    static inline reader::point diff( dir_t dir )
    {
        switch ( dir )
        {
            case north: return { 0, -1 };
            case south: return { 0, +1 };
            case east:  return { +1, 0 };
            case west:  return { -1, 0 };

            case north_east: return { +1, -1 };
            case north_west: return { -1, -1 };
            case south_east: return { +1, +1 };
            case south_west: return { -1, +1 };
        }
    }

    struct object_map : std::map< reader::point, std::shared_ptr< pic::object > >
    {
        // object_ptr at( int x, int y ) const { return at( point( x, y ) ); }

        std::shared_ptr< pic::object > at( reader::point p ) const
        {
            if ( auto i = find( p ) ; i != end() )
                return i->second;
            else
                return nullptr;
        }
    };

    struct state
    {
        object_map objects;
        pic::group group;
        const reader::grid &grid;
        std::set< reader::point > processed;

        static constexpr double xpitch = 4.5, ypitch = 9;

        state( const reader::grid &g ) : grid( g ) {}

        void arrow( int x, int y ) { arrow( reader::point( x, y ) ); }
        void arrow( reader::point p )
        {
            arrow( p, grid[ p ].arrow_dir() );
        }

        void arrow( reader::point p, dir_t to_dir )
        {
            auto conv = []( reader::point p )
            {
                return pic::point( xpitch * p.x(), -ypitch * p.y() );
            };

            reader::point next;

            auto head   = grid[ p ].head();
            auto at_dir = grid[ p ].attach_dir( to_dir );
            auto to     = p + diff( to_dir );
            auto to_obj = objects.at( to );
            std::vector< pic::point > points;
            bool dashed = false, curved = false;
            int shade = 0;

            auto to_port = port( conv( to ), to_dir );

            if ( to_obj && ( !grid[ to ].attach( opposite( to_dir ) ) || grid[ to ].node() ) )
                to_port = to_obj->port( opposite( to_dir ) );

            std::shared_ptr< pic::object > from_obj = nullptr;

            while ( true )
            {
                next = p + diff( at_dir );
                from_obj = objects.at( next );

                if ( grid[ p ].dashed() )
                    dashed = true;
                if ( grid[ p ].rounded() )
                    curved = true;

                processed.insert( p );

                if ( grid[ p ].arrow() )
                    shade = grid[ p ].shade();

                auto cell = grid[ next ];
                auto ndir = at_dir;

                if ( from_obj || cell.arrow() )
                    break;

                p = next;

                if ( !cell.attach_all() && !cell.arrow() )
                    ndir = cell.attach_dir( opposite( at_dir ) );
                if ( at_dir != ndir )
                    points.emplace_back( xpitch * next.x(), -ypitch * next.y() );

                at_dir = ndir;
            }

            auto from = grid[ next ];
            auto from_port = port( conv( next ), at_dir );

            if ( from_obj && ( !from.attach( opposite( at_dir ) ) || from.node() ) )
                from_port = from_obj->port( opposite( at_dir ) );

            auto &arrow = *group.add< pic::arrow >( from_port, to_port );
            arrow._dashed = dashed;
            arrow._curved = curved;
            arrow._head   = head;
            arrow._shade  = shade;
            std::copy( points.rbegin(), points.rend(), std::back_inserter( arrow._controls ) );
        }

        std::pair< reader::point, int > boundary( reader::point p, dir_t dir, int joins = 1,
                                                  bool jcw = true, bool *dashed = nullptr )
        {
            int joined = 0;
            bool first = true;

            for ( ; grid[ p ].attach( dir ); p = p + diff( dir ) )
            {
                if ( dashed && grid[ p ].dashed() )
                    *dashed = true;

                if ( first )
                {
                    first = false;
                    continue;
                }

                if ( ( !jcw && grid[ p ].attach( cw( dir ) ) ) ||
                     ( jcw && grid[ p ].attach( ccw( dir ) ) ) )
                    if ( ++joined == joins )
                        break;

                if ( grid[ p + diff( dir ) ].node() )
                    break;
            }

            return { p, joined };
        }

        using joins = std::array< int, 4 >;
        using box_ptr = std::shared_ptr< pic::box >;

        box_ptr box( reader::point p, joins mj = { 1, 1, 1, 1 } )
        {
            std::array< reader::point, 4 > c;
            joins j;
            bool dashed[ 4 ] = { false };

            if ( processed.count( p ) )
                return nullptr;
            else
                processed.insert( p );

            if ( grid[ p ].node() )
                return nullptr;

            auto nw = p;
            auto [ ne, jn ] = boundary( p, east,   mj[ 0 ], false, dashed + 0 );
            auto [ sw, je ] = boundary( p, south,  mj[ 1 ], true,  dashed + 3 );
            auto [ se, jw ] = boundary( ne, south, mj[ 2 ], false, dashed + 1 );
            auto [ sx, js ] = boundary( sw, east,  mj[ 3 ], true,  dashed + 2 );

            c[ corner_ne ] = ne; j[ 0 ] = jn;
            c[ corner_nw ] = nw; j[ 1 ] = je;
            c[ corner_se ] = se; j[ 2 ] = jw;
            c[ corner_sw ] = sw; j[ 3 ] = js;

            if ( se != sx )
            {
                auto idx = index_sort( mj );
                for ( auto i : idx )
                    if ( mj[ i ] == j[ i ] )
                    {
                        mj[ i ] ++;
                        return box( p, mj );
                        mj[ i ] --;
                    }

                return nullptr; /* not a box */
            }

            double w = ne.x() - nw.x();
            double h = sw.y() - nw.y();

            auto obj = group.add< pic::box >( xpitch * (   p.x() + w / 2 ),
                                              ypitch * ( - p.y() - h / 2 ),
                                              xpitch * w, ypitch * h );
            for ( int i = 0; i < int( c.size() ); ++i )
                obj->set_rounded( i, grid[ c[ i ] ].rounded() );
            for ( int i = 0; i < int( c.size() ); ++i )
                obj->set_dashed( i, dashed[ i ] );

            std::vector< std::pair< int, std::u32string > > txt;
            int last_x = p.x(), last_y = 0;

            for ( auto p = ne; p != se; p = p + reader::point( 0, 1 ) )
                if ( grid[ p ].attach( east ) )
                    if ( auto joined = box( p ) )
                    {
                        if ( obj->height() > joined->height() )
                            joined->set_visible( west, false );
                        else
                            obj->set_visible( east, false );
                    }

            for ( auto p = sw; p != se; p = p + reader::point( 1, 0 ) )
                if ( grid[ p ].attach( south ) )
                    if ( auto joined = box( p ) )
                    {
                        if ( obj->width() > joined->width() )
                            joined->set_visible( north, false );
                        else
                            obj->set_visible( south, false );
                    }

            for ( int y = p.y(); y <= p.y() + h; ++y )
                for ( int x = p.x(); x <= p.x() + w; ++x )
                {
                    reader::point p( x, y );

                    if ( grid[ p ].shade() )
                        obj->set_shaded( grid[ p ].shade() );
                    else if ( grid[ p ].text() )
                    {
                        if ( txt.empty() )
                            txt.emplace_back( txt.size(), U"" );

                        if ( y != last_y && !txt.empty() && !txt.back().second.empty() )
                            txt.emplace_back( txt.size(), U"" );

                        auto &t = txt.back().second;

                        if ( x != last_x + 1 && !t.empty() )
                            t += ' ';

                        t += grid[ p ].character();
                        last_x = x, last_y = y;
                    }

                    objects[ reader::point( x, y ) ] = obj;
                    processed.emplace( x, y );
                }

            for ( auto [ y, t ] : txt )
            {
                double baseline_shift = txt.size() > 1 ? 0 : 0.08;
                double center_offset = - baseline_shift - h / 2;
                double line_offset = ( txt.size() - 1 ) / 2.0 - y;
                group.add< pic::label >( xpitch * (   p.x() + w / 2 ),
                                         ypitch * ( - p.y() + center_offset + line_offset ), t );
            }

            return obj;
        }

        void object( int x, int y ) { object( reader::point( x, y ) ); }
        void object( reader::point p )
        {
            auto c = grid.at( p );
            if ( objects.at( p ) ) return; /* already taken up by an object */

            if ( c.node() )
            {
                auto node = group.add< pic::node >( xpitch * p.x(), -ypitch * p.y(), 2 );
                node->_shade = c.shade();
                objects[ p ] = node;
            }

            if ( c.attach( south ) && c.attach( east ) )
                box( p );
        }

        void line( int x, int y ) { line( reader::point( x, y ) ); }
        void line( reader::point p )
        {
            auto c = grid.at( p );

            for ( auto dir : all_dirs )
                if ( c.attach( dir ) && objects.count( p + diff( dir ) ) )
                {
                    arrow( p, dir );
                    break;
                }
        }

        void label( int x, int y ) { label( reader::point( x, y ) ); }
        void label( reader::point p )
        {
            if ( objects.at( p ) )
                return;

            auto origin = p;
            std::u32string txt;

            /* non-breaking spaces are required within labels */

            for ( int i = 0; ; ++i, p = p + reader::point( 1, 0 ) )
            {
                if ( grid.at( p ).character() == ' ' || grid.at( p ).attach() )
                    break;
                else
                    txt += grid.at( p ).character();
            }

            auto w = p.x() - origin.x() - 1;
            auto obj = group.add< pic::text >( xpitch * ( origin.x() + w / 2.0 ),
                                               ypitch * ( - p.y() ), txt );
            while ( p != origin ) /* fixme off by one */
            {
                objects[ p ] = obj;
                processed.insert( p );
                p = p + reader::point( -1, 0 );
            }
        }
    };

    static inline group scene( const reader::grid &grid )
    {
        state s( grid );
        auto seen = [&]( int x, int y ) { return s.processed.count( reader::point( x, y ) ); };

        try
        {
            for ( auto [ x, y, c ] : grid )
                if ( c.attach() )
                    s.object( x, y );

            for ( auto [ x, y, c ] : grid )
                if ( c.arrow() && !seen( x, y ) )
                    s.arrow( x, y );

            for ( auto [ x, y, c ] : grid )
                if ( c.attach() && !seen( x, y ) )
                    s.line( x, y );

            for ( auto [ x, y, c ] : grid )
                if ( c.text() && !seen( x, y ) )
                    s.label( x, y );
        }
        catch ( bad_picture &bp )
        {
            bp.picture = grid._raw;
            throw;
        }

        return s.group;
    }
}
