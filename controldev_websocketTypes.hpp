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
        int errors = 0;
        int received = 0;
        base::Time time;
    };
} // end namespace controldev_websocket

#endif
