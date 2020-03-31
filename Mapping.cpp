#include "Mapping.hpp"

using namespace controldev_websocket;


std::string Mapping::getType(){
    if (type == Type::Button){
        return "button";
    }
    if (type == Type::Axis){
        return "axes";
    }
    return "";
}
