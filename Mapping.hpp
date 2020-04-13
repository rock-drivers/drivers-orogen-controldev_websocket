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
         * @note don't use this value directly, is useless. Use getFieldName() to
         * access the JSON Field Name for this type.
         */
        Type type;

        std::string getFieldName(){
            return mapFieldName(type);
        };

        static std::string mapFieldName(Type type){
            if (type == Type::Button){
                return "buttons";
            }
            if (type == Type::Axis){
                return "axes";
            }
            throw "Failed to get Field Name. The type set is invalid";
        }

        int index;
    };

    struct ButtonMapping: public Mapping
    {
        double threshold;
    };
} // end namespace controldev_websocket

#endif
