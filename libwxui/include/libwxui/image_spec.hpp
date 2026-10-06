#pragma once
/// libwxui — nine-slice image specification.
/// Parses the DUILib bkimage / normalimage / … attribute formats.

#include "types.hpp"

namespace wxui {

/// Fully-parsed image descriptor.
/// Raw attribute format:
///   Short:  "bg.png"
///   Full:   "file='bg.png' res='' restype='0' dest='0,0,0,0'
///            source='0,0,0,0' corner='4,4,4,4' mask='#FF0000'
///            fade='255' hole='false' xtiled='false' ytiled='false'"
struct ImageSpec {
    std::string  path;           ///< File path (may be resource name)
    std::string  resName;        ///< Embedded resource name (optional)
    int          resType  = 0;   ///< Resource type identifier
    wxRect       dest;           ///< Destination rect; {0,0,0,0} = fill whole control
    wxRect       source;         ///< Source clip rect; {0,0,0,0} = full image
    wxRect       corner;         ///< Nine-slice border widths (l,t,r,b)
    wxColour     maskColor;      ///< Colour treated as transparent
    uint8_t      fade     = 255; ///< Global alpha (0=transparent, 255=opaque)
    bool         hole     = false; ///< Cut a transparent hole in center
    bool         xTiled   = false; ///< Tile horizontally instead of stretch
    bool         yTiled   = false; ///< Tile vertically instead of stretch

    [[nodiscard]] bool IsEmpty() const noexcept {
        return path.empty() && resName.empty();
    }
};

/// Parse a raw bkimage/normalimage/… attribute value string.
ImageSpec ParseImageSpec(std::string_view value);

} // namespace wxui
