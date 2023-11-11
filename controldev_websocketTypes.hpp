#ifndef controldev_websocket_TYPES_HPP
#define controldev_websocket_TYPES_HPP

#include <base/Time.hpp>
#include <string>

namespace controldev_websocket {
    struct Mapping {
        enum Type {
            Axis = 0,
            Button = 1
        };

        /**
         * Enum to represent an axis or a button.
         */
        Type type;

        int index;

        bool optional = false;
    };

    struct ButtonMapping : public Mapping {
        double threshold = 0.5;
    };

    struct Statistics {
        /** Time of generation of this statistics message */
        base::Time time;
        /** Count of messages received */
        uint64_t received = 0;
        /** Count of messages received that were malformed */
        uint32_t errors = 0;
    };
} // end namespace controldev_websocket

#endif
