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
        /** Time of the last received message, that is the time it was generated */
        base::Time last_received_message_time;
        /** Numerical ID that identifies the connection that currently is controlling */
        uint16_t controlling_connection_id = 0;
        /** Numerical ID that identifies a connection that was received but is not yet
         * controlling */
        uint16_t pending_connection_id = 0;
        /** Count of messages received */
        uint64_t received = 0;
        /** Count of messages received that were not accepted for control */
        uint32_t errors = 0;
        /** Count of messages received that were rejected because too old
         *
         * This count is also included in errors
         */
        uint32_t too_old = 0;
    };
} // end namespace controldev_websocket

#endif
