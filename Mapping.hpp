#ifndef CONTROLDEV_WEBSOCKET_TYPES_HPP
#define CONTROLDEV_WEBSOCKET_TYPES_HPP

#include <string>

namespace controldev_websocket
{
    struct Mapping
    {
        enum Type {
            Axis = 0,
            Button = 1
        };

        /**
         * Enum to represent an axis or a button.
         *
         *
         * @note don't use this value directly. It's only public to allow this class
         * to be used as an interface type.
         */
        Type type;

        std::string getType(){
            if (type == Type::Button){
                return "button";
            }
            if (type == Type::Axis){
                return "axes";
            }
            return "";
        };

        int index;
    };

} // end namespace controldev_websocket

#endif
